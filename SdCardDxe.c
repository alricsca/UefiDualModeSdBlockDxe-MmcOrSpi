#include "SdCardBlockIo.h"
#include "SdCardDxe.h"
#include "SdCardMedia.h"
#include "HostIo.h"
#include "SpiIo.h"
#include "SdCardMode.h"
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h> // For SwapBytes32
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/PrintLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SdMmcPassThru.h>
#include <Protocol/SpiHc.h>
#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/ShellParameters.h>
#include <Library/PcdLib.h>
#include "SdCardHelp.h"


// Add Component Name Protocol functions
EFI_STATUS
EFIAPI
GetDriverName (
  IN  EFI_COMPONENT_NAME2_PROTOCOL  *This,
  IN  CHAR8                         *Language,
  OUT CHAR16                        **DriverName
  )
{
  *DriverName = L"SD Card Driver";
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
GetControllerName (
  IN  EFI_COMPONENT_NAME2_PROTOCOL  *This,
  IN  EFI_HANDLE                    ControllerHandle,
  IN  EFI_HANDLE                    ChildHandle OPTIONAL,
  IN  CHAR8                         *Language,
  OUT CHAR16                        **ControllerName
  )
{
  *ControllerName = L"SD Card Controller";
  return EFI_SUCCESS;
}

// Define SD Card Device Path GUID
#define SD_CARD_DEVICE_PATH_GUID {0x8f0d5b9c, 0x1c13, 0x49a5, {0x93, 0x82, 0x6d, 0x84, 0x3e, 0x80, 0x55, 0x25}}

EFI_GUID gSdCardDevicePathGuid = SD_CARD_DEVICE_PATH_GUID;

// Define SD Card Device Path structure
#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH        Vendor;
  EFI_DEVICE_PATH_PROTOCOL  End;
} SD_CARD_DEVICE_PATH;
#pragma pack()

// Add global variables:
EFI_COMPONENT_NAME2_PROTOCOL gSdCardComponentName2 = {
  (EFI_COMPONENT_NAME2_GET_DRIVER_NAME) GetDriverName,
  (EFI_COMPONENT_NAME2_GET_CONTROLLER_NAME) GetControllerName,
  "en"
};

/**
  Creates a complete device path for the SD card by appending an SD-specific node
  to the parent controller's device path.
**/
EFI_DEVICE_PATH_PROTOCOL *
CreateSdCardDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *ParentDevicePath
  )
{
  SD_CARD_DEVICE_PATH  *SdCardNode;
  EFI_DEVICE_PATH_PROTOCOL *FullDevicePath;
  UINTN ParentSize, NewSize;

  if (ParentDevicePath == NULL) {
    return NULL;
  }

  // Calculate sizes
  ParentSize = GetDevicePathSize (ParentDevicePath) - sizeof(EFI_DEVICE_PATH_PROTOCOL);
  NewSize = ParentSize + sizeof(SD_CARD_DEVICE_PATH);

  // Allocate memory for the new device path
  FullDevicePath = AllocatePool (NewSize);
  if (FullDevicePath == NULL) {
    return NULL;
  }

  // Copy the parent device path (excluding the end node)
  CopyMem (FullDevicePath, ParentDevicePath, ParentSize);

  // Create the SD card vendor-specific node
  SdCardNode = (SD_CARD_DEVICE_PATH *)((UINT8 *)FullDevicePath + ParentSize);
  ZeroMem (SdCardNode, sizeof(SD_CARD_DEVICE_PATH));
  
  // Set up the vendor device path node
  SdCardNode->Vendor.Header.Type = HARDWARE_DEVICE_PATH;
  SdCardNode->Vendor.Header.SubType = HW_VENDOR_DP;
  SetDevicePathNodeLength (&SdCardNode->Vendor.Header, sizeof(VENDOR_DEVICE_PATH));
  CopyGuid (&SdCardNode->Vendor.Guid, &gSdCardDevicePathGuid);

  // Set the end node
  SdCardNode->End.Type = END_DEVICE_PATH_TYPE;
  SdCardNode->End.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  SetDevicePathNodeLength (&SdCardNode->End, sizeof(EFI_DEVICE_PATH_PROTOCOL));

  return FullDevicePath;
}

//
// Driver Binding Protocol Implementation
//
EFI_STATUS
EFIAPI
SdCardDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  SD_CARD_MODE Mode;
  BOOLEAN ForceSpi;
  
  DEBUG((DEBUG_INFO, "SdCardDxe: Checking support for controller %p\n", ControllerHandle));
  
   // Check if SPI mode is forced via PCD
  ForceSpi = PcdGetBool(PcdSdCardSpiOnlyMode);
  
  // Probe for available communication modes
  Mode = SdCardProbeMode(ControllerHandle, ForceSpi);
  
  if (Mode == SD_CARD_MODE_UNKNOWN) {
    DEBUG((DEBUG_VERBOSE, "SdCardDxe: No supported protocols found on controller %p\n", ControllerHandle));
    return EFI_UNSUPPORTED;
  }
  
  DEBUG((DEBUG_INFO, "SdCardDxe: Controller %p supported in %a mode\n", 
         ControllerHandle, GetModeName(Mode)));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SdCardDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_STATUS Status;
  SD_CARD_PRIVATE_DATA *Private;
  EFI_DEVICE_PATH_PROTOCOL *ParentDevicePath;
  BOOLEAN ProtocolOpened = FALSE;
  SD_CARD_MODE Mode;
  BOOLEAN ForceSpi;
  
  DEBUG((DEBUG_INFO, "SdCardDxe: Starting driver on handle %p\n", ControllerHandle));

  Private = NULL;
  
  //
  // Allocate and initialize the private device structure
  //
  Private = AllocateZeroPool(sizeof(SD_CARD_PRIVATE_DATA));
  if (Private == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  Private->Signature = SD_CARD_PRIVATE_DATA_SIGNATURE;
  Private->DriverBinding = This;
  Private->Handle = NULL;

  //
  // Determine operation mode
  //
  ForceSpi = PcdGetBool(PcdSdCardSpiOnlyMode);
  Mode = SdCardProbeMode(ControllerHandle, ForceSpi);
  
  if (Mode == SD_CARD_MODE_UNKNOWN) {
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }
  
  Private->Mode = Mode;
  
  //
  // Open the appropriate protocol based on mode
  //
  if (Mode == SD_CARD_MODE_HOST) {
    // Open MMC host protocol
    Status = gBS->OpenProtocol(
                    ControllerHandle,
                    &gEfiSdMmcPassThruProtocolGuid,
                    (VOID **)&Private->SdMmcPassThru,
                    This->DriverBindingHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_BY_DRIVER
                    );
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "SdCardDxe: Failed to open MMC host protocol: %r\n", Status));
      goto Exit;
    }
    ProtocolOpened = TRUE;
    DEBUG((DEBUG_INFO, "SdCardDxe: Operating in MMC host mode\n"));
  } else {
    // Open SPI protocol
    Status = gBS->OpenProtocol(
                    ControllerHandle,
                    &gEfiSpiHcProtocolGuid,
                    (VOID **)&Private->SpiHcProtocol,
                    This->DriverBindingHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_BY_DRIVER
                    );
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "SdCardDxe: Failed to open SPI protocol: %r\n", Status));
      goto Exit;
    }
    ProtocolOpened = TRUE;
    
    // Set up SPI peripheral
    Private->SpiPeripheral = AllocateZeroPool(sizeof(EFI_SPI_PERIPHERAL));
    if (Private->SpiPeripheral == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Exit;
    }
    
    // Configure SPI peripheral with common defaults
    Private->SpiPeripheral->SpiBus = 0;
    Private->SpiPeripheral->MaxClockHz = 25000000; // SD Card max in SPI mode
    
    DEBUG((DEBUG_INFO, "SdCardDxe: Operating in SPI mode\n"));
  }

  //
  // Initialize the SD card
  //
  Status = SdCardInitialize(Private);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardDxe: Failed to initialize SD card: %r\n", Status));
    
    // Attempt mode fallback if initialization failed
    Status = SdCardHandleModeFallback(Private, Status);
    if (EFI_ERROR(Status)) {
      goto Exit;
    }
  }

  //
  // Set up Block I/O Protocol
  //
  Private->BlockIo.Revision = EFI_BLOCK_IO_PROTOCOL_REVISION3;
  Private->BlockIo.Media = &Private->BlockMedia;
  Private->BlockIo.Reset = SdCardMediaReset;
  Private->BlockIo.ReadBlocks = SdCardMediaReadBlocks;
  Private->BlockIo.WriteBlocks = SdCardMediaWriteBlocks;
  Private->BlockIo.FlushBlocks = SdCardMediaFlushBlocks;
  
  //
  // Set up Block I/O Media information
  //
  Private->BlockMedia.MediaPresent = TRUE;
  Private->BlockMedia.LogicalPartition = FALSE;
  Private->BlockMedia.ReadOnly = FALSE; // Will be set based on write protect detection
  Private->BlockMedia.WriteCaching = FALSE;
  Private->BlockMedia.BlockSize = Private->BlockSize;
  Private->BlockMedia.LastBlock = Private->LastBlock;
  
  // Set alignment based on mode
  if (Private->Mode == SD_CARD_MODE_HOST) {
    Private->BlockMedia.IoAlign = 4; // 4-byte alignment for DMA
  } else {
    Private->BlockMedia.IoAlign = 1; // 1-byte alignment for SPI
  }

  //
  // Get the parent's device path and create a complete device path for the SD card
  //
  Status = gBS->OpenProtocol(
                  ControllerHandle,
                  &gEfiDevicePathProtocolGuid,
                  (VOID**)&ParentDevicePath,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardDxe: Failed to get parent device path: %r\n", Status));
    goto Exit;
  }

  Private->DevicePath = CreateSdCardDevicePath(ParentDevicePath);
  if (Private->DevicePath == NULL) {
    DEBUG((DEBUG_ERROR, "SdCardDxe: Failed to create SD card device path\n"));
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  //
  // Install protocols on a new child handle
  //
  Status = gBS->InstallMultipleProtocolInterfaces(
                  &Private->Handle,
                  &gEfiBlockIoProtocolGuid,   &Private->BlockIo,
                  &gEfiDevicePathProtocolGuid, Private->DevicePath,
                  &gEfiComponentName2ProtocolGuid, &gSdCardComponentName2,
                  NULL
                  );
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardDxe: Failed to install protocols: %r\n", Status));
    goto Exit;
  }

  //
  // Link the child handle to the controller using BY_CHILD_CONTROLLER
  //
  if (Private->Mode == SD_CARD_MODE_HOST) {
    Status = gBS->OpenProtocol(
                    ControllerHandle,
                    &gEfiSdMmcPassThruProtocolGuid,
                    (VOID **)&Private->SdMmcPassThru,
                    This->DriverBindingHandle,
                    Private->Handle,
                    EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                    );
  } else {
    Status = gBS->OpenProtocol(
                    ControllerHandle,
                    &gEfiSpiHcProtocolGuid,
                    (VOID **)&Private->SpiHcProtocol,
                    This->DriverBindingHandle,
                    Private->Handle,
                    EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                    );
  }

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardDxe: Failed to open protocol by child controller: %r\n", Status));
    
    // Clean up the protocols we just installed
    gBS->UninstallMultipleProtocolInterfaces(
            Private->Handle,
            &gEfiBlockIoProtocolGuid,   &Private->BlockIo,
            &gEfiDevicePathProtocolGuid, Private->DevicePath,
            &gEfiComponentName2ProtocolGuid, &gSdCardComponentName2,
            NULL
            );
    goto Exit;
  }

  DEBUG((DEBUG_INFO, "SdCardDxe: Driver started successfully. Child Handle: %p\n", Private->Handle));
  return EFI_SUCCESS;

Exit:
  // Centralized cleanup logic
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardDxe: Start failed: %r\n", Status));
    
    if (Private != NULL) {
      // If we opened a protocol on the controller, we must close it
      if (ProtocolOpened) {
        if (Private->Mode == SD_CARD_MODE_HOST) {
          gBS->CloseProtocol(
                  ControllerHandle,
                  &gEfiSdMmcPassThruProtocolGuid,
                  This->DriverBindingHandle,
                  ControllerHandle
                  );
        } else {
          gBS->CloseProtocol(
                  ControllerHandle,
                  &gEfiSpiHcProtocolGuid,
                  This->DriverBindingHandle,
                  ControllerHandle
                  );
        }
      }
      
      if (Private->SpiPeripheral != NULL) {
        FreePool(Private->SpiPeripheral);
      }
      
      if (Private->DevicePath != NULL) {
        FreePool(Private->DevicePath);
      }
      
      FreePool(Private);
    }
  }
  
  return Status;
}


EFI_STATUS
EFIAPI
SdCardDriverBindingStop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer OPTIONAL
  )
{
  EFI_STATUS Status;
  UINTN Index;
  BOOLEAN AllChildrenStopped = TRUE;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  SD_CARD_PRIVATE_DATA *Private;

  DEBUG((DEBUG_INFO, "SdCardDxe: Stopping driver on handle %p\n", ControllerHandle));

  if (NumberOfChildren == 0) {
    // If no children, just close the protocol we opened in Supported/Start
    gBS->CloseProtocol(
            ControllerHandle,
            &gEfiSdMmcPassThruProtocolGuid,
            This->DriverBindingHandle,
            ControllerHandle
            );
    gBS->CloseProtocol(
            ControllerHandle,
            &gEfiSpiHcProtocolGuid,
            This->DriverBindingHandle,
            ControllerHandle
            );
    return EFI_SUCCESS;
  }

  AllChildrenStopped = TRUE;

  for (Index = 0; Index < NumberOfChildren; Index++) {
    Status = gBS->OpenProtocol(
                    ChildHandleBuffer[Index],
                    &gEfiBlockIoProtocolGuid,
                    (VOID **)&BlockIo,
                    This->DriverBindingHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_WARN, "SdCardDxe: Failed to get BlockIo protocol for child %p: %r\n", 
             ChildHandleBuffer[Index], Status));
      AllChildrenStopped = FALSE;
      continue;
    }

    Private = SD_CARD_PRIVATE_DATA_FROM_BLOCK_IO(BlockIo);

    //
    // Disconnect the child controller by closing BY_CHILD_CONTROLLER
    //
    if (Private->Mode == SD_CARD_MODE_HOST) {
      Status = gBS->CloseProtocol(
                      ControllerHandle,
                      &gEfiSdMmcPassThruProtocolGuid,
                      This->DriverBindingHandle,
                      ChildHandleBuffer[Index]
                      );
    } else {
      Status = gBS->CloseProtocol(
                      ControllerHandle,
                      &gEfiSpiHcProtocolGuid,
                      This->DriverBindingHandle,
                      ChildHandleBuffer[Index]
                      );
    }

    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "SdCardDxe: Failed to close child protocol for handle %p: %r\n", 
             ChildHandleBuffer[Index], Status));
      AllChildrenStopped = FALSE;
      continue;
    }

    //
    // Uninstall protocols from the child handle
    //
    Status = gBS->UninstallMultipleProtocolInterfaces(
                    ChildHandleBuffer[Index],
                    &gEfiBlockIoProtocolGuid, &Private->BlockIo,
                    &gEfiDevicePathProtocolGuid, Private->DevicePath,
                    &gEfiComponentName2ProtocolGuid, &gSdCardComponentName2,
                    NULL
                    );

    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "SdCardDxe: Failed to uninstall protocols for handle %p: %r\n", 
             ChildHandleBuffer[Index], Status));
      AllChildrenStopped = FALSE;
      
      // Attempt to reopen the child protocol to leave system in a consistent state
      if (Private->Mode == SD_CARD_MODE_HOST) {
        gBS->OpenProtocol(
                ControllerHandle,
                &gEfiSdMmcPassThruProtocolGuid,
                (VOID **)NULL,
                This->DriverBindingHandle,
                ChildHandleBuffer[Index],
                EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                );
      } else {
        gBS->OpenProtocol(
                ControllerHandle,
                &gEfiSpiHcProtocolGuid,
                (VOID **)NULL,
                This->DriverBindingHandle,
                ChildHandleBuffer[Index],
                EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                );
      }
    } else {
      // Successfully uninstalled, free resources
      if (Private->SpiPeripheral != NULL) {
        FreePool(Private->SpiPeripheral);
      }
      
      if (Private->DevicePath != NULL) {
        FreePool(Private->DevicePath);
      }
      
      FreePool(Private);
    }
  }

  //
  // Close the main protocol opened by the driver on the controller handle
  //
  gBS->CloseProtocol(
          ControllerHandle,
          &gEfiSdMmcPassThruProtocolGuid,
          This->DriverBindingHandle,
          ControllerHandle
          );
  gBS->CloseProtocol(
          ControllerHandle,
          &gEfiSpiHcProtocolGuid,
          This->DriverBindingHandle,
          ControllerHandle
          );

  if (!AllChildrenStopped) {
    DEBUG((DEBUG_WARN, "SdCardDxe: Not all children were stopped cleanly\n"));
    return EFI_DEVICE_ERROR;
  }

  DEBUG((DEBUG_INFO, "SdCardDxe: Driver stopped successfully\n"));
  return EFI_SUCCESS;
}

//
// Driver Binding Protocol Variable
//
EFI_DRIVER_BINDING_PROTOCOL gSdCardDriverBinding = {
  SdCardDriverBindingSupported,
  SdCardDriverBindingStart,
  SdCardDriverBindingStop,
  0xa,    // Version
  NULL,   // ImageHandle
  NULL    // DriverBindingHandle
};

//
// Driver Entry and Unload points
//
EFI_STATUS
EFIAPI
SdCardDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;

  // Initialize the driver binding handle
  gSdCardDriverBinding.ImageHandle = ImageHandle;
  gSdCardDriverBinding.DriverBindingHandle = ImageHandle;

Status = EfiLibInstallDriverBindingComponentName2(
           ImageHandle,
           SystemTable,
           &gSdCardDriverBinding,
           ImageHandle,
           NULL,  // ComponentName (protocol version 1) - optional
           &gSdCardComponentName2  // ComponentName2 (protocol version 2)
           );
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardDxe: Failed to install DriverBinding: %r\n", Status));
  } else {
    DEBUG((DEBUG_INFO, "SdCardDxe: Driver installed successfully\n"));
  }
  
  return Status;
}

EFI_STATUS
EFIAPI
SdCardDxeUnload (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS Status;
  
  // First, uninstall the Driver Binding protocol
  Status = gBS->UninstallProtocolInterface(
                  ImageHandle,
                  &gEfiDriverBindingProtocolGuid,
                  &gSdCardDriverBinding
                  );
  
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardDxe: Failed to uninstall DriverBinding protocol: %r\n", Status));
    return Status;
  }
  
  DEBUG((DEBUG_INFO, "SdCardDxe: Driver unloaded successfully\n"));
  return EFI_SUCCESS;
}

/**
  Sets power management state for the SD card.
  @param[in] Private  SD card private data
  @param[in] State    Power state to set
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
SetPowerState (
  IN SD_CARD_PRIVATE_DATA  *Private,
  IN POWER_STATE           State
  )
{
  EFI_STATUS Status = EFI_SUCCESS; 
  
  DEBUG((DEBUG_INFO, "SdCard: Setting power state %d\n", State));
  
  switch (State) {
    case POWER_OFF:
      // Power off the card
      if (Private->Mode == SD_CARD_MODE_HOST) {
        // Send reset command
        UINT32 Response;
        Status = SdCardSendCommandHost(Private, CMD0, 0, &Response);
      }
      break;
      
    case POWER_ON:
      // Power on and reinitialize
      Status = SdCardInitialize(Private);
      break;
      
    case POWER_LOW:
      // Reduce clock speed for lower power
      if (Private->Mode == SD_CARD_MODE_HOST) {
        Status = SetBusSpeedHost(Private, 1000000); // 1 MHz
      }
      break;
      
    case POWER_SUSPEND:
      // Suspend operations but maintain power
      // This would typically involve reducing clock speed and voltage
      if (Private->Mode == SD_CARD_MODE_HOST) {
        Status = SetBusSpeedHost(Private, 400000); // 400 kHz
      }
      break;
      
    default:
      return EFI_INVALID_PARAMETER;
  }
  
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "SdCard: Failed to set power state %d: %r\n", State, Status));
  } else {
  }
  
  return Status;
}


/**
  Switches to boot partition for boot operation.
  @param[in] Private       SD card private data
  @param[in] BootPartition TRUE to switch to boot partition, FALSE for main
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
SwitchToBootPartition (
  IN SD_CARD_PRIVATE_DATA  *Private,
  IN BOOLEAN               BootPartition
  )
{
  EFI_STATUS Status;
  UINT32 Response;
  
  if (Private == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  DEBUG((DEBUG_INFO, "SdCard: Switching to %s partition\n", 
         BootPartition ? "boot" : "main"));
  
  // Send CMD6 to switch partition
  UINT32 PartitionArg = BootPartition ? 0x03B70200 : 0x03B70100;
  
  if (Private->Mode == SD_CARD_MODE_HOST) {
    Status = SdCardSendCommandHost(Private, CMD6, PartitionArg, &Response);
  } else {
    UINT8 Resp;
    Status = SdCardSendCommandSpi(Private, CMD6, PartitionArg, &Resp);
    Response = Resp;
  }
  
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCard: Failed to switch partition: %r\n", Status));
    return Status;
  }
  
  // Check if switch was successful
  if ((Response & 0x0000000F) != 0) {
    DEBUG((DEBUG_ERROR, "SdCard: Partition switch failed, response: 0x%08X\n", Response));
    return EFI_DEVICE_ERROR;
  }
  
  DEBUG((DEBUG_INFO, "SdCard: Successfully switched to %s partition\n", 
         BootPartition ? "boot" : "main"));
  return EFI_SUCCESS;
}
