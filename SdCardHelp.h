#ifndef SD_CARD_HELP_H_
#define SD_CARD_HELP_H_

#include <Uefi.h>
#include "SdCardDxe.h"

/**
  Displays comprehensive help information for the SD card driver.
  @param[in] Private  SD card private data (optional)
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
DisplaySdCardHelp (
  IN SD_CARD_PRIVATE_DATA  *Private OPTIONAL
  );

/**
  Command-line entry point for the SD card driver help system.
  @param[in] ImageHandle  The image handle of the process.
  @param[in] SystemTable  The pointer to the system table.
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
SdCardHelpMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  );

#endif // SD_CARD_HELP_H_