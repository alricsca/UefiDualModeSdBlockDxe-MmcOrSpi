#!/usr/bin/env bash
#
# Test script for SdCardDxe EFI driver using QEMU.
#
# This script automates the process of testing the SD card EFI driver by:
# 1. Creating a FAT32-formatted image to serve as the virtual SD card.
# 2. Populating the image with a test file for read verification.
# 3. Launching QEMU with an emulated SDHCI controller and the virtual SD card.
# 4. Mounting the current directory as a separate FAT drive for easy access to the EFI driver.
# 5. Providing instructions for hotplug simulation via the QEMU monitor.
#

set -euo pipefail

# --- Configuration ---
# All paths are relative to the script's location to ensure portability.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OVMF_CODE="${SCRIPT_DIR}/ovmf-x86_64-opensuse-code.bin"
OVMF_VARS_TEMPLATE="${SCRIPT_DIR}/ovmf-x86_64-opensuse-4m-vars.bin"
OVMF_VARS="${SCRIPT_DIR}/my_ovmf_vars.fd"
SD_IMAGE="${SCRIPT_DIR}/sdcard.img"
SD_IMAGE_SIZE_MB=64
TEST_FILE_NAME="test.txt"
TEST_FILE_CONTENT="Hello from the emulated SD card!"

# --- Dependency Check ---
# Ensures required tools are available in the environment.
for cmd in qemu-system-x86_64 dd mkfs.fat mcopy; do
    if ! command -v "$cmd" &> /dev/null; then
        echo "Error: Required command '$cmd' not found." >&2
        echo "Please install it and ensure it is in your PATH." >&2
        exit 1
    fi
done

# --- SD Card Image Creation ---
# Creates and formats a raw disk image to be used as the virtual SD card.
# This function avoids needing sudo by creating a simple, partition-less FAT image.
create_sd_image() {
    echo "Creating ${SD_IMAGE_SIZE_MB}MB SD card image at: ${SD_IMAGE}"
    dd if=/dev/zero of="${SD_IMAGE}" bs=1M count=${SD_IMAGE_SIZE_MB}
    mkfs.fat -F 32 -n "EFI_SD_CARD" "${SD_IMAGE}"

    echo "Copying test file ('${TEST_FILE_NAME}') to image..."
    # Use mtools to copy a test file into the root of the FAT image.
    echo -n "${TEST_FILE_CONTENT}" > "${SCRIPT_DIR}/${TEST_FILE_NAME}"
    mcopy -i "${SD_IMAGE}" "${SCRIPT_DIR}/${TEST_FILE_NAME}" ::
    rm "${SCRIPT_DIR}/${TEST_FILE_NAME}"
    echo "SD card image created and populated successfully."
}

# --- QEMU Launch ---
# Starts the QEMU VM with the appropriate OVMF firmware and emulated hardware.
start_qemu() {
    # Create a writable copy of the OVMF variables file for this session.
    cp "${OVMF_VARS_TEMPLATE}" "${OVMF_VARS}"

    echo
    echo "###########################################################################"
    echo "# Starting QEMU for SdCardDxe Test                                        #"
    echo "###########################################################################"
    echo
    echo "Your EFI driver (e.g., SdCardDxe.efi) should be in this directory."
    echo
    echo "--- Inside the EFI Shell ---"
    echo "1. Map devices: 'map -r'"
    echo "2. Switch to the workspace drive: 'fs0:'"
    echo "3. Load your driver: 'load SdCardDxe.efi'"
    echo "4. The driver should initialize and create a new block device (e.g., blk0)."
    echo "5. You can map again to see the new filesystem (e.g., fs1:)."
    echo "6. Verify by listing files: 'ls fs1:' (should show '${TEST_FILE_NAME}')."
    echo
    echo "--- Hotplug Simulation (in this terminal) ---"
    echo "The QEMU monitor is connected to this terminal."
    echo "To EJECT card: Type 'eject sdcard0' and press Enter."
    echo "To INSERT card: Type 'device_add sd-card,drive=sdcard0' and press Enter."
    echo
    echo "To exit QEMU, type 'quit' in the monitor or close the window."
    echo "###########################################################################"
    echo

    qemu-system-x86_64 \
        -m 2048 \
        -pflash "${OVMF_CODE}" \
        -pflash "${OVMF_VARS}" \
        -drive "file=fat:rw:${SCRIPT_DIR},format=raw,if=virtio" \
        -device "sdhci-pci,id=sdhci0" \
        -drive "if=none,id=sdcard0,file=${SD_IMAGE},format=raw" \
        -device "sd-card,drive=sdcard0" \
        -monitor stdio \
        -net none
}

# --- Main Logic ---
# If the SD card image doesn't exist, create it. Otherwise, use the existing one.
if [ ! -f "${SD_IMAGE}" ]; then
    create_sd_image
else
    echo "Using existing SD card image: ${SD_IMAGE}"
    read -p "Do you want to recreate it? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        create_sd_image
    fi
fi

start_qemu