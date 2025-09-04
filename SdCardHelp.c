/**
  @file
  SD Card Driver Help System - Provides command-line help for the SD card driver.

  Copyright (c) 2023, Your Company. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/ShellLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Protocol/ShellParameters.h>
#include "SdCardDxe.h"
#include "SdCardHelp.h"

/**
  Displays comprehensive help information for the SD card driver.
  @param[in] Private  SD card private data (optional)
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
DisplaySdCardHelp (
  IN SD_CARD_PRIVATE_DATA  *Private OPTIONAL
  )
{
  SHELL_STATUS ShellStatus;
  CHAR16       *HelpText;
  UINTN        HelpSize;
  
  // Allocate buffer for help text
  HelpSize = 2048; // Sufficient for help text
  HelpText = AllocatePool(HelpSize * sizeof(CHAR16));
  if (HelpText == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  
  // Format help text
  UnicodeSPrint(
    HelpText,
    HelpSize * sizeof(CHAR16),
    L"SD Card Driver Help\n\n"
    L"Supported Features:\n"
    L"  - Dual-mode operation (MMC Host + SPI Fallback)\n"
    L"  - UHS-I support (SDR12, SDR25, SDR50, SDR104, DDR50)\n"
    L"  - Advanced power management\n"
    L"  - Enhanced hotplug detection\n"
    L"  - Boot partition access\n"
    L"  - Error recovery and CRC checking\n\n"
    L"Driver Parameters:\n"
    L"  - Mode: %s\n",
    GetModeName(Private ? Private->Mode : SD_CARD_MODE_UNKNOWN)
  );
  
  if (Private) {
    UnicodeSPrint(
      HelpText + StrLen(HelpText),
      (HelpSize - StrLen(HelpText)) * sizeof(CHAR16),
      L"  - Card Type: %s\n"
      L"  - Capacity: %lu MB\n"
      L"  - Block Size: %u bytes\n"
      L"  - Current Clock: %u Hz\n",
      Private->CardType == CARD_TYPE_SD_V2_HC ? L"SDHC/SDXC" : 
      Private->CardType == CARD_TYPE_SD_V2_SC ? L"SDSC v2" : 
      Private->CardType == CARD_TYPE_SD_V1 ? L"SDSC v1" : L"Unknown",
      (UINT32)(Private->CapacityInBytes / (1024 * 1024)),
      Private->BlockSize,
      Private->CurrentClockHz
    );
  }
  
  UnicodeSPrint(
    HelpText + StrLen(HelpText),
    (HelpSize - StrLen(HelpText)) * sizeof(CHAR16),
    L"\nUsage:\n"
    L"  The driver automatically detects and initializes SD cards.\n"
    L"  For advanced configuration, use the following protocols:\n"
    L"  - EFI_BLOCK_IO_PROTOCOL: For block-level access\n"
    L"  - EFI_SD_MMC_PASS_THRU_PROTOCOL: For host mode control\n"
    L"  - EFI_SPI_HC_PROTOCOL: For SPI mode control\n\n"
    L"Command-line Options:\n"
    L"  -? or --help: Display this help message\n"
    L"  --mode [host|spi]: Force specific mode\n"
    L"  --uhs-mode [sdr12|sdr25|sdr50|sdr104|ddr50]: Set UHS-I mode\n"
    L"  --power-management [on|off|low|suspend]: Set power state\n"
  );
  
  // Print help text
  ShellStatus = ShellPrintEx(-1, -1, L"%s", HelpText);
  
  // Free buffer
  FreePool(HelpText);
  
  return (ShellStatus == SHELL_SUCCESS) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

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
  )
{
  EFI_STATUS               Status;
  EFI_SHELL_PARAMETERS_PROTOCOL *ShellParams;
  CHAR16                   **Argv;
  UINTN                    Argc;
  SD_CARD_PRIVATE_DATA     *Private = NULL;
  
  // Get command-line parameters
  Status = gBS->HandleProtocol(
                  ImageHandle,
                  &gEfiShellParametersProtocolGuid,
                  (VOID **)&ShellParams
                  );
  
  if (EFI_ERROR(Status)) {
    return DisplaySdCardHelp(NULL);
  }
  
  Argv = ShellParams->Argv;
  Argc = ShellParams->Argc;
  
  // Check for help request
  if (Argc > 1 && 
      (StrCmp(Argv[1], L"-?") == 0 || StrCmp(Argv[1], L"--help") == 0)) {
    // Try to get private data from existing driver instance
    // This would typically involve locating the protocol on a handle
    // For simplicity, we'll just display generic help
    return DisplaySdCardHelp(NULL);
  }
  
  // Handle other command-line options here
  // ...
  
  return EFI_SUCCESS;
}