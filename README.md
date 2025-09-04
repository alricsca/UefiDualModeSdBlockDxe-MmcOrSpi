# UefiDualModeSdBlockDxe-MmcOrSpi
This is a dual-mode UEFI DXE driver for SD cards. It will use an MMC host controller if available, otherwise it falls back to direct SPI mode, exposing the card via EFI_BLOCK_IO_PROTOCOL so fat.efi can mount EFI partitions.

## License

This project is dual-licensed:

- [GNU GPL v3 (or later)](LICENSE)  
- [BSD 2-Clause License](BSD-2-Clause.txt)  

You may choose which license you prefer to use.  

Note: This project depends on EDK2, which is itself licensed under the BSD
2-Clause license (with some components under MIT or similar permissive
licenses). Any portions of EDK2 code incorporated into this project remain under
their original licenses.
