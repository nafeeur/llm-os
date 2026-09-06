#!/usr/bin/env python3
"""Boot the x86-64 LLMOS image in QEMU and verify its serial self-test."""
from __future__ import annotations

import argparse
import os
import selectors
import shutil
import subprocess
import sys
import time
from pathlib import Path


def command_for(root: Path) -> list[str]:
    qemu = os.environ.get("QEMU_X86_64") or shutil.which("qemu-system-x86_64")
    if not qemu:
        raise FileNotFoundError("qemu-system-x86_64 not found")
    return [
        qemu, "-machine", "pc", "-m", "128M", "-smp", "1",
        "-drive", f"format=raw,file={root / 'build/x86_64/llmos-x86_64.img'}",
        "-display", "none", "-serial", "stdio", "-monitor", "none",
        "-no-reboot", "-no-shutdown",
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("target", choices=("x86",))
    parser.add_argument("--timeout", type=float, default=12.0)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    try:
        cmd = command_for(root)
    except FileNotFoundError as exc:
        print(f"SKIP: {exc}", file=sys.stderr)
        return 2

    proc = subprocess.Popen(
        cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, bufsize=0,
    )
    assert proc.stdin is not None and proc.stdout is not None
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    output = bytearray()
    sent = False
    deadline = time.monotonic() + args.timeout
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(timeout=0.1):
                chunk = os.read(key.fd, 4096)
                if not chunk:
                    break
                output.extend(chunk)
                text = output.decode("utf-8", errors="replace")
                if "llmos> " in text and not sent:
                    proc.stdin.write(b"selftest\nhalt\n")
                    proc.stdin.flush()
                    sent = True
                if "selftest: 13/13 passed" in text:
                    print(text)
                    print(f"{args.target} VM smoke: PASS")
                    return 0
            if proc.poll() is not None:
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                proc.kill()
        selector.close()

    print(output.decode("utf-8", errors="replace"), file=sys.stderr)
    print(f"{args.target} VM smoke: FAIL", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
