#!/bin/bash

# Stop on first error
set -e

# Paths
TEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$TEST_DIR/.."
OVMF_CODE="ovmf-x86_64-4m-code.bin"
OVMF_VARS="ovmf-x86_64-4m-vars.bin"
BOOT_IMG="$TEST_DIR/boot.img"
SD_CARD_IMG="$TEST_DIR/sdcard.img"
SHELL_EFI="$TEST_DIR/shellx64.efi"
SD_CARD_DXE_EFI="$TEST_DIR/SdCardDxe.efi"

# Create a bootable image with the UEFI shell and the driver
echo "Creating bootable image at: $BOOT_IMG"
dd if=/dev/zero of="$BOOT_IMG" bs=1M count=64
mkfs.fat -F 32 "$BOOT_IMG"
mmd -i "$BOOT_IMG" ::/EFI
mmd -i "$BOOT_IMG" ::/EFI/BOOT
mcopy -i "$BOOT_IMG" "$SHELL_EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$BOOT_IMG" "$SD_CARD_DXE_EFI" ::/

# Create a test SD card image
echo "Creating test SD card image at: $SD_CARD_IMG"
dd if=/dev/zero of="$SD_CARD_IMG" bs=1M count=32
mkfs.fat -F 32 "$SD_CARD_IMG"
mcopy -i "$SD_CARD_IMG" "$PROJECT_ROOT/README.md" ::/

# Run QEMU
echo "Starting QEMU..."
qemu-system-x86_64 \
  -machine q35,accel=kvm \
  -cpu host \
  -m 4G \
  -drive if=pflash,format=raw,file="$OVMF_CODE",readonly=on \
  -drive if=pflash,format=raw,file="$OVMF_VARS" \
  -device intel-iommu \
  -device ssi-bus,id=spi0 \
  -device sd-card-spi,spi=spi0,id=sdcard0 \
  -drive if=none,id=sd0,file="$SD_CARD_IMG",format=raw \
  -device sd-card,drive=sd0 \
  -drive if=none,id=bootdisk,file="$BOOT_IMG",format=raw \
  -device virtio-blk-pci,drive=bootdisk \
  -nographic
