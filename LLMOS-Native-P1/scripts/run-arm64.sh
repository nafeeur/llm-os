#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QEMU="${QEMU_AARCH64:-qemu-system-aarch64}"
MODEL="$ROOT/build/models/nativelm2-test.lmof"
DRIVE_ARGS=()
if [[ -f "$MODEL" ]]; then
  DRIVE_ARGS=(-drive "file=$MODEL,if=none,format=raw,id=hd1,readonly=on" \
              -device virtio-blk-device,drive=hd1 \
              -global virtio-mmio.force-legacy=true)
fi
exec "$QEMU" -machine virt -cpu cortex-a72 -m 256M -smp 1 \
  -kernel "$ROOT/build/aarch64-qemu/llmos-aarch64-qemu.bin" \
  "${DRIVE_ARGS[@]}" \
  -nographic -monitor none -no-reboot
