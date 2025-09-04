#!/usr/bin/env bash
set -euo pipefail
OVMF_CODE="/usr/share/OVMF/OVMF_CODE.fd"
OVMF_VARS="/usr/share/OVMF/OVMF_VARS.fd"
if [ ! -f "$OVMF_CODE" ] || [ ! -f "$OVMF_VARS" ]; then
  echo "OVMF not found in /usr/share/OVMF. Adjust paths in this script."
  exit 1
fi
qemu-system-x86_64 -m 2048 \
  -drive if=pflash,format=raw,readonly,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,file="$OVMF_VARS" \
  -drive file=fat:rw:.,format=raw,if=virtio \
  -net none \
  -nographic
