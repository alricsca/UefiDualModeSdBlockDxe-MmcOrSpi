# QEMU SPI Test for SdCardDxe

This document describes how to use the `spi_test.sh` script to test the `SdCardDxe.efi` driver in a QEMU environment that emulates an SPI-attached SD card.

This setup is designed for testing the driver's ability to work on systems that lack a standard SDHCI controller, relying instead on a more basic SPI interface.

## Requirements

Make sure the following tools are installed and available in your `PATH`:
- `qemu-system-x86_64`
- `mtools` (which provides `mcopy` and `mmd`)
- `dd`
- `mkfs.fat`

Additionally, the following files must be present in the `tests` directory:
- `ovmf-x86_64-opensuse-code.bin`
- `ovmf-x86_64-opensuse-4m-vars.bin`
- `shellx64.efi`
- `Fat.efi` (the UEFI FAT filesystem driver)
- `SdCardDxe.efi` (you must build this and place it in the `tests` directory)

## How to Run

1.  **Make the script executable:**
    ```sh
    chmod +x spi_test.sh
    ```

2.  **Execute the script (you may need sudo):**
    ```sh
    sudo ./spi_test.sh
    ```

The script will first check for dependencies. It will then create two image files if they don't already exist:
- `boot_hd.img`: A 32MB bootable FAT32 hard drive image containing the UEFI Shell, `Fat.efi`, and `SdCardDxe.efi`.
- `sdcard_spi.img`: A 64MB virtual SD card image with a test file (`test_spi.txt`) in its root.

QEMU will then launch, booting automatically from the virtual hard drive into the UEFI Shell.

## Testing Steps in the EFI Shell

Once you are in the EFI Shell, follow these steps:

1.  **Confirm you are on the boot filesystem.** The prompt should be `fs0:>`. You can type `ls` to see `SdCardDxe.efi` and `Fat.efi`.

2.  **Load the necessary drivers:**
    First, load the FAT filesystem driver, then your SD card driver.
    ```
    load Fat.efi
    load SdCardDxe.efi
    ```

3.  **Map devices:**
    ```
    map -r
    ```
    This command refreshes the list of devices and filesystems. You should see a new block device (e.g., `blk1`) and a new filesystem mapping (e.g., `fs1:`).

4.  **Verify the SD card contents:**
    Switch to the new filesystem and list its contents.
    ```
    fs1:
    ls
    ```
    You should see the `test_spi.txt` file.

## Simulating Hotplug

The QEMU monitor is connected to the terminal where you launched the script. You can simulate removing and re-inserting the SD card:

-   **To Eject:** Type `device_del ssi-sd-card` and press Enter.
-   **To Insert:** Type `device_add ssi-sd,id=ssi-sd-card,spi=spi0,sd=sdcard0` and press Enter.

After re-inserting, you may need to run `reconnect -r` in the EFI Shell to make the driver re-initialize the device.