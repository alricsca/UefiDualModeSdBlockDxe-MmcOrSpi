#include "SdCardBlockIo.h"
#include "SdCardDxe.h"
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/MemoryAllocationLib.h>

/**
  Probe for available communication modes with the SD card controller
  @param[in] ControllerHandle  Handle to the controller
  @param[in] ForceSpi          If TRUE, force SPI mode even if MMC host is available
  @return Detected mode (SD_CARD_MODE_HOST, SD_CARD_MODE_SPI, or SD_CARD_MODE_UNKNOWN)
**/
SD_CARD_MODE
EFIAPI
SdCardProbeMode (
  IN EFI_HANDLE  ControllerHandle,
  IN BOOLEAN     ForceSpi
  )
{
  EFI_STATUS Status;
  VOID       *Protocol;
  
  DEBUG((DEBUG_INFO, "SdCardMode: Probing modes for controller %p\n", ControllerHandle));
  
  // Check if SPI mode is forced via PCD
  if (PcdGetBool(PcdSdCardSpiOnlyMode) || ForceSpi) {
    DEBUG((DEBUG_INFO, "SdCardMode: SPI mode forced via PCD or parameter\n"));
    return SD_CARD_MODE_SPI;
  }
  
  // First check for preferred MMC host protocol (EFI_SD_MMC_PASS_THRU_PROTOCOL)
  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiSdMmcPassThruProtocolGuid,
                  &Protocol,
                  gSdCardDriverBinding.DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_TEST_PROTOCOL
                  );
  if (!EFI_ERROR(Status)) {
    DEBUG((DEBUG_INFO, "SdCardMode: MMC host protocol found - using host mode\n"));
    return SD_CARD_MODE_HOST;
  } else {
    DEBUG((DEBUG_VERBOSE, "SdCardMode: MMC host protocol not available: %r\n", Status));
  }
  
  // Fall back to SPI protocol (EFI_SPI_HC_PROTOCOL)
  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiSpiHcProtocolGuid,
                  &Protocol,
                  gSdCardDriverBinding.DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_TEST_PROTOCOL
                  );
  if (!EFI_ERROR(Status)) {
    DEBUG((DEBUG_INFO, "SdCardMode: SPI host protocol found - using SPI mode\n"));
    return SD_CARD_MODE_SPI;
  } else {
    DEBUG((DEBUG_VERBOSE, "SdCardMode: SPI host protocol not available: %r\n", Status));
  }
  
  DEBUG((DEBUG_ERROR, "SdCardMode: No supported protocols found on controller %p\n", ControllerHandle));
  return SD_CARD_MODE_UNKNOWN;
}

/**
  Handles mode fallback when initial mode initialization fails
  @param[in] Private               SD card private data
  @param[in] InitializationStatus  Status from the initial mode initialization attempt
  @return EFI_STATUS indicating whether fallback was successful
**/
EFI_STATUS
EFIAPI
SdCardHandleModeFallback (
  IN SD_CARD_PRIVATE_DATA  *Private,
  IN EFI_STATUS            InitializationStatus
  )
{
  EFI_STATUS Status;
  EFI_HANDLE ControllerHandle;
  
  DEBUG((DEBUG_INFO, "SdCardMode: Considering mode fallback, initial status: %r\n", InitializationStatus));
  
  // Only consider fallback under specific conditions
  if (InitializationStatus != EFI_CRC_ERROR && 
      InitializationStatus != EFI_DEVICE_ERROR &&
      InitializationStatus != EFI_TIMEOUT) {
    DEBUG((DEBUG_VERBOSE, "SdCardMode: No fallback needed for status %r\n", InitializationStatus));
    return InitializationStatus;
  }
  
  // Get the controller handle from private data
  ControllerHandle = Private->DriverBinding->DriverBindingHandle;
  if (ControllerHandle == NULL) {
    DEBUG((DEBUG_ERROR, "SdCardMode: Cannot fallback - no controller handle\n"));
    return InitializationStatus;
  }
  
  // Check if we're currently in host mode and SPI is available as fallback
  if (Private->Mode == SD_CARD_MODE_HOST) {
    DEBUG((DEBUG_INFO, "SdCardMode: Attempting fallback from host to SPI mode\n"));
    
    // Close the MMC host protocol
    gBS->CloseProtocol (
            ControllerHandle,
            &gEfiSdMmcPassThruProtocolGuid,
            gSdCardDriverBinding.DriverBindingHandle,
            ControllerHandle
            );
    
    // Try to open SPI protocol
    Status = gBS->OpenProtocol (
                    ControllerHandle,
                    &gEfiSpiHcProtocolGuid,
                    (VOID **)&Private->SpiHcProtocol,
                    gSdCardDriverBinding.DriverBindingHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_BY_DRIVER
                    );
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "SdCardMode: SPI fallback failed - protocol open error: %r\n", Status));
      // Try to reopen the original protocol
      gBS->OpenProtocol (
              ControllerHandle,
              &gEfiSdMmcPassThruProtocolGuid,
              (VOID **)&Private->SdMmcPassThru,
              gSdCardDriverBinding.DriverBindingHandle,
              ControllerHandle,
              EFI_OPEN_PROTOCOL_BY_DRIVER
              );
      return InitializationStatus;
    }
    
    // Set up SPI peripheral
    Private->SpiPeripheral = AllocateZeroPool(sizeof(EFI_SPI_PERIPHERAL));
    if (Private->SpiPeripheral == NULL) {
      DEBUG((DEBUG_ERROR, "SdCardMode: SPI fallback failed - memory allocation error\n"));
      gBS->CloseProtocol (
              ControllerHandle,
              &gEfiSpiHcProtocolGuid,
              gSdCardDriverBinding.DriverBindingHandle,
              ControllerHandle
              );
      return EFI_OUT_OF_RESOURCES;
    }
    
    // Configure SPI peripheral with common defaults
    Private->SpiPeripheral->SpiBus = 0;
    Private->SpiPeripheral->MaxClockHz = 25000000; // SD Card max in SPI mode
    
    // Switch mode to SPI
    Private->Mode = SD_CARD_MODE_SPI;
    
    DEBUG((DEBUG_INFO, "SdCardMode: Successfully switched to SPI mode for fallback\n"));
    
    // Retry initialization in SPI mode
    Status = SdCardInitialize(Private);
    if (!EFI_ERROR(Status)) {
      DEBUG((DEBUG_INFO, "SdCardMode: Fallback to SPI mode successful\n"));
      return EFI_SUCCESS;
    } else {
      DEBUG((DEBUG_ERROR, "SdCardMode: Fallback to SPI mode failed: %r\n", Status));
      return Status;
    }
  }
  
  // Check if we're in SPI mode and MMC host is available (less common fallback)
  else if (Private->Mode == SD_CARD_MODE_SPI) {
    DEBUG((DEBUG_INFO, "SdCardMode: Considering fallback from SPI to host mode\n"));
    
    // Check if MMC host protocol is actually available
    VOID *TestProtocol;
    Status = gBS->OpenProtocol (
                    ControllerHandle,
                    &gEfiSdMmcPassThruProtocolGuid,
                    &TestProtocol,
                    gSdCardDriverBinding.DriverBindingHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_TEST_PROTOCOL
                    );
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_VERBOSE, "SdCardMode: No MMC host available for SPI fallback\n"));
      return InitializationStatus;
    }
    
    DEBUG((DEBUG_INFO, "SdCardMode: Attempting fallback from SPI to host mode\n"));
    
    // Close SPI protocol
    gBS->CloseProtocol (
            ControllerHandle,
            &gEfiSpiHcProtocolGuid,
            gSdCardDriverBinding.DriverBindingHandle,
            ControllerHandle
            );
    
    // Free SPI peripheral
    if (Private->SpiPeripheral != NULL) {
      FreePool(Private->SpiPeripheral);
      Private->SpiPeripheral = NULL;
    }
    
    // Try to open MMC host protocol
    Status = gBS->OpenProtocol (
                    ControllerHandle,
                    &gEfiSdMmcPassThruProtocolGuid,
                    (VOID **)&Private->SdMmcPassThru,
                    gSdCardDriverBinding.DriverBindingHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_BY_DRIVER
                    );
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "SdCardMode: Host mode fallback failed - protocol open error: %r\n", Status));
      return InitializationStatus;
    }
    
    // Switch mode to host
    Private->Mode = SD_CARD_MODE_HOST;
    
    DEBUG((DEBUG_INFO, "SdCardMode: Successfully switched to host mode for fallback\n"));
    
    // Retry initialization in host mode
    Status = SdCardInitialize(Private);
    if (!EFI_ERROR(Status)) {
      DEBUG((DEBUG_INFO, "SdCardMode: Fallback to host mode successful\n"));
      return EFI_SUCCESS;
    } else {
      DEBUG((DEBUG_ERROR, "SdCardMode: Fallback to host mode failed: %r\n", Status));
      return Status;
    }
  }
  
  DEBUG((DEBUG_VERBOSE, "SdCardMode: No fallback options available for current mode\n"));
  return InitializationStatus;
}

/**
  Validates that the selected mode is properly configured and available
  @param[in] ControllerHandle  Handle to the controller
  @param[in] Mode              Mode to validate
  @return TRUE if mode is valid and available, FALSE otherwise
**/
BOOLEAN
ValidateMode (
  IN EFI_HANDLE    ControllerHandle,
  IN SD_CARD_MODE  Mode
  )
{
  EFI_STATUS Status;
  VOID       *Protocol;
  
  switch (Mode) {
    case SD_CARD_MODE_HOST:
      Status = gBS->OpenProtocol (
                      ControllerHandle,
                      &gEfiSdMmcPassThruProtocolGuid,
                      &Protocol,
                      gSdCardDriverBinding.DriverBindingHandle,
                      ControllerHandle,
                      EFI_OPEN_PROTOCOL_TEST_PROTOCOL
                      );
      return !EFI_ERROR(Status);
      
    case SD_CARD_MODE_SPI:
      Status = gBS->OpenProtocol (
                      ControllerHandle,
                      &gEfiSpiHcProtocolGuid,
                      &Protocol,
                      gSdCardDriverBinding.DriverBindingHandle,
                      ControllerHandle,
                      EFI_OPEN_PROTOCOL_TEST_PROTOCOL
                      );
      return !EFI_ERROR(Status);
      
    default:
      return FALSE;
  }
}

/**
  Gets the human-readable name for a mode
  @param[in] Mode  Mode to get name for
  @return String representation of the mode
**/
CONST CHAR8* EFIAPI GetModeName (
  IN SD_CARD_MODE  Mode
  )
{
  switch (Mode) {
    case SD_CARD_MODE_HOST:    return "MMC Host";
    case SD_CARD_MODE_SPI:     return "SPI";
    case SD_CARD_MODE_UNKNOWN: return "Unknown";
    default:                   return "Invalid";
  }
}