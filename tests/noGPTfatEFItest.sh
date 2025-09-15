#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# --- Configuration ---
EFI_APP_PATH="/home/daddy/projects/edk2/Build/MdeModule/DEBUG_GCC/X64/MdeModulePkg/Application/HelloWorld/HelloWorld/OUTPUT/HelloWorld.efi"
IMAGE_FILE="/home/daddy/src/fatNoGPT.img"
BIOS_FILE="/home/daddy/bios/OVMF_VARS.4m.fd"
READONLY_BIOS_FILE="/usr/share/edk2/x64/OVMF_CODE.4m.fd"

# --- User & Group Check ---
USER="daddy"
if ! id -nG "$USER" | grep -qw "kvm"; then
    echo "User '$USER' is not in the 'kvm' group."
    echo "Please run 'sudo usermod -aG kvm $USER' and then log out and back in."
    exit 1
fi

# --- Script Logic ---
echo "Starting process to create FAT32 image and copy EFI application..."

# Create a 20MB disk image and format it as FAT32.
dd if=/dev/zero of="$IMAGE_FILE" bs=1M count=20
sudo mkfs.vfat "$IMAGE_FILE"

# The file now needs to be owned by your user for QEMU to read it.
sudo chown "$USER:$USER" "$IMAGE_FILE"

# Use mtools to create directories and copy the EFI file directly to the image.
# No mounting or cleanup is required for this step.
echo "Copying EFI application to image using mtools..."
mmd -i "$IMAGE_FILE" ::/EFI
mmd -i "$IMAGE_FILE" ::/EFI/BOOT
mcopy -i "$IMAGE_FILE" "$EFI_APP_PATH" "::/EFI/BOOT/BOOTX64.EFI"

echo "FAT32 image '$IMAGE_FILE' created and EFI application copied successfully."
echo ""

# Ask the user if they want to launch QEMU.
read -p "Do you want to launch QEMU now? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Launching QEMU..."
    qemu-system-x86_64 \
        -enable-kvm \
        -drive if=pflash,format=raw,readonly=on,file="$READONLY_BIOS_FILE" \
        -drive if=pflash,format=raw,file="$BIOS_FILE" \
        -drive file="$IMAGE_FILE",if=ide,format=raw
fi
