#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QEMU="${QEMU_X86_64:-qemu-system-x86_64}"
MODEL="$ROOT/build/models/nativelm2-test.lmof"
DRIVE_ARGS=()
if [[ -f "$MODEL" ]]; then
  DRIVE_ARGS=(-drive "file=$MODEL,if=none,format=raw,id=hd1,readonly=on" \
              -device virtio-blk-pci,drive=hd1)
fi
exec "$QEMU" -machine pc -m 128M -smp 1 \
  -drive format=raw,file="$ROOT/build/x86_64/llmos-x86_64.img" \
  "${DRIVE_ARGS[@]}" \
  -display none -serial stdio -monitor none -no-reboot
