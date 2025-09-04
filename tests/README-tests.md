- ci/qemu_test.sh : run QEMU + OVMF with current directory as FAT drive.
- To test driver:
  1) place sdcard.img in the FAT root (will be created automatically by the driver if missing)
  2) boot QEMU with ci/qemu_test.sh
  3) from the EFI shell, load the .efi produced by your build or add it to the boot order.
