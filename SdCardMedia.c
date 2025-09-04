#include "SdCardBlockIo.h"
#include "SdCardDxe.h"
#include "DriverLib.h"
#include "HostIo.h"
#include "SpiIo.h"
#include "SdCardMode.h"
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/TimerLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

// Forward declarations for internal functions
STATIC EFI_STATUS GetCardIdentificationData (IN SD_CARD_PRIVATE_DATA *Private);
STATIC EFI_STATUS ParseCsdRegister (IN SD_CARD_PRIVATE_DATA *Private);
STATIC EFI_STATUS CheckCardStatus (IN SD_CARD_PRIVATE_DATA *Private);
STATIC BOOLEAN DetectCardPresence (IN SD_CARD_PRIVATE_DATA *Private);
STATIC VOID UpdateMediaParameters (IN SD_CARD_PRIVATE_DATA *Private);

/**
  Resets the block device.
**/
EFI_STATUS
EFIAPI
SdCardMediaReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                ExtendedVerification
  )
{
  SD_CARD_PRIVATE_DATA *Private = SD_CARD_PRIVATE_DATA_FROM_BLOCK_IO(This);
  
  DEBUG((DEBUG_INFO, "SdCardMedia: Reset requested (ExtendedVerification: %d)\n", ExtendedVerification));
  
  if (!Private->BlockMedia.MediaPresent) {
    return EFI_NO_MEDIA;
  }
  
  // For extended verification, reinitialize the card
  if (ExtendedVerification) {
    EFI_STATUS Status = SdCardInitialize(Private);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_WARN, "SdCardMedia: Extended verification failed: %r\n", Status));
      return Status;
    }
  }
  
  return EFI_SUCCESS;
}

/**
  Reads blocks from the SD card.
**/
EFI_STATUS
EFIAPI
SdCardMediaReadBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  OUT VOID                  *Buffer
  )
{
  SD_CARD_PRIVATE_DATA *Private = SD_CARD_PRIVATE_DATA_FROM_BLOCK_IO(This);
  EFI_STATUS Status;
  VOID *BounceBuffer = NULL;
  
  // Parameter validation
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  if (!Private->BlockMedia.MediaPresent) {
    return EFI_NO_MEDIA;
  }
  
  if (MediaId != Private->BlockMedia.MediaId) {
    return EFI_MEDIA_CHANGED;
  }
  
  if (Lba > Private->BlockMedia.LastBlock) {
    return EFI_INVALID_PARAMETER;
  }
  
  if ((BufferSize % Private->BlockMedia.BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }
  
  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }
  
  DEBUG((DEBUG_VERBOSE, "SdCardMedia: Reading %u blocks from LBA %lu\n", 
         BufferSize / Private->BlockMedia.BlockSize, Lba));

  // Handle unaligned buffers
  if (!SdCardIsBufferAligned(Buffer, Private->BlockMedia.IoAlign)) {
    Status = SdCardCreateBounceBuffer(Buffer, BufferSize, Private->BlockMedia.IoAlign, &BounceBuffer);
    if (EFI_ERROR(Status)) {
      return Status;
    }
  }
  
  // Route to mode-specific implementation
  if (Private->Mode == SD_CARD_MODE_HOST) {
    Status = SdCardExecuteReadWriteHost(Private, Lba, BufferSize, BounceBuffer ? BounceBuffer : Buffer, FALSE);
  } else if (Private->Mode == SD_CARD_MODE_SPI) {
    Status = SdCardExecuteReadWriteSpi(Private, Lba, BufferSize, BounceBuffer ? BounceBuffer : Buffer, FALSE);
  } else {
    Status = EFI_UNSUPPORTED;
  }

  if (BounceBuffer) {
    if (!EFI_ERROR(Status)) {
      SdCardHandleBounceBuffer(FALSE, Buffer, BounceBuffer, BufferSize);
    }
    SdCardFreeBounceBuffer(BounceBuffer);
  }
  
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardMedia: Read failed: %r\n", Status));
    
    // Check if card was removed during operation
    if (!DetectCardPresence(Private)) {
      Private->BlockMedia.MediaPresent = FALSE;
      Private->BlockMedia.MediaId++;
      return EFI_NO_MEDIA;
    }
  }
  
  return Status;
}

/**
  Writes blocks to the SD card.
**/
EFI_STATUS
EFIAPI
SdCardMediaWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                  *Buffer
  )
{
  SD_CARD_PRIVATE_DATA *Private = SD_CARD_PRIVATE_DATA_FROM_BLOCK_IO(This);
  EFI_STATUS Status;
  VOID *BounceBuffer = NULL;
  
  // Parameter validation
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  if (!Private->BlockMedia.MediaPresent) {
    return EFI_NO_MEDIA;
  }
  
  if (Private->BlockMedia.ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }
  
  if (MediaId != Private->BlockMedia.MediaId) {
    return EFI_MEDIA_CHANGED;
  }
  
  if (Lba > Private->BlockMedia.LastBlock) {
    return EFI_INVALID_PARAMETER;
  }
  
  if ((BufferSize % Private->BlockMedia.BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }
  
  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }
  
  DEBUG((DEBUG_VERBOSE, "SdCardMedia: Writing %u blocks to LBA %lu\n", 
         BufferSize / Private->BlockMedia.BlockSize, Lba));

  // Handle unaligned buffers
  if (!SdCardIsBufferAligned(Buffer, Private->BlockMedia.IoAlign)) {
    Status = SdCardCreateBounceBuffer(Buffer, BufferSize, Private->BlockMedia.IoAlign, &BounceBuffer);
    if (EFI_ERROR(Status)) {
      return Status;
    }
    SdCardHandleBounceBuffer(TRUE, Buffer, BounceBuffer, BufferSize);
  }
  
  // Route to mode-specific implementation
  if (Private->Mode == SD_CARD_MODE_HOST) {
    Status = SdCardExecuteReadWriteHost(Private, Lba, BufferSize, BounceBuffer ? BounceBuffer : Buffer, TRUE);
  } else if (Private->Mode == SD_CARD_MODE_SPI) {
    Status = SdCardExecuteReadWriteSpi(Private, Lba, BufferSize, BounceBuffer ? BounceBuffer : Buffer, TRUE);
  } else {
    Status = EFI_UNSUPPORTED;
  }

  if (BounceBuffer) {
    SdCardFreeBounceBuffer(BounceBuffer);
  }
  
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardMedia: Write failed: %r\n", Status));
    
    // Check if card was removed during operation
    if (!DetectCardPresence(Private)) {
      Private->BlockMedia.MediaPresent = FALSE;
      Private->BlockMedia.MediaId++;
      return EFI_NO_MEDIA;
    }
  }
  
  return Status;
}

/**
  Flushes any cached data to the SD card.
**/
EFI_STATUS
EFIAPI
SdCardMediaFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  SD_CARD_PRIVATE_DATA *Private = SD_CARD_PRIVATE_DATA_FROM_BLOCK_IO(This);
  
  // For SD cards, there's no controller cache to flush. The card itself
  // handles write completion internally. We just need to ensure any
  // pending write operations are complete.
  
  if (!Private->BlockMedia.MediaPresent) {
    return EFI_NO_MEDIA;
  }
  
  DEBUG((DEBUG_VERBOSE, "SdCardMedia: Flush completed\n"));
  return EFI_SUCCESS;
}

/**
  Initializes the SD card (dispatcher function).
**/
EFI_STATUS
EFIAPI
SdCardInitialize (
  IN SD_CARD_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS Status;
  
  DEBUG((DEBUG_INFO, "SdCardMedia: Initializing SD card in %a mode\n", 
         GetModeName(Private->Mode)));
  
  // Check card presence first
  if (!DetectCardPresence(Private)) {
    DEBUG((DEBUG_INFO, "SdCardMedia: No card present\n"));
    Private->BlockMedia.MediaPresent = FALSE;
    return EFI_NO_MEDIA;
  }
  
  // Initialize the card using mode-specific implementation
  if (Private->Mode == SD_CARD_MODE_HOST) {
    Status = SdCardInitializeHost(Private);
  } else if (Private->Mode == SD_CARD_MODE_SPI) {
    Status = SdCardInitializeSpi(Private);
  } else {
    Status = EFI_UNSUPPORTED;
  }
  
  // Handle initialization failure with mode fallback if appropriate
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "SdCardMedia: Initialization failed: %r\n", Status));
    
    // Attempt mode fallback if enabled and appropriate
    Status = SdCardHandleModeFallback(Private, Status);
    if (EFI_ERROR(Status)) {
      Private->BlockMedia.MediaPresent = FALSE;
      return Status;
    }
  }
  
  // Get card identification data
  Status = GetCardIdentificationData(Private);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardMedia: Failed to get identification data: %r\n", Status));
    Private->BlockMedia.MediaPresent = FALSE;
    return Status;
  }
  
  // Update media parameters
  UpdateMediaParameters(Private);
  
  Private->IsInitialized = TRUE;
  Private->BlockMedia.MediaPresent = TRUE;
  Private->BlockMedia.MediaId++;
  
  DEBUG((DEBUG_INFO, "SdCardMedia: Initialization successful. Capacity: %llu MB\n", 
         Private->CapacityInBytes / (1024 * 1024)));
  
  return EFI_SUCCESS;
}

// =============================================================================
// Internal Helper Functions
// =============================================================================

/**
  Retrieves card identification data (CID and CSD).
**/
STATIC
EFI_STATUS
GetCardIdentificationData (
  IN SD_CARD_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS Status;
  
  DEBUG((DEBUG_INFO, "SdCardMedia: Reading card identification data\n"));
  
  // CSD and CID reading is handled by mode-specific initialization
  // This function primarily validates that we have valid data
  
  if (Private->Mode == SD_CARD_MODE_HOST) {
    // For host mode, CSD/CID should already be read during initialization
    if (Private->Csd[0] == 0 && Private->Csd[15] == 0) {
      DEBUG((DEBUG_ERROR, "SdCardMedia: No CSD data available\n"));
      return EFI_DEVICE_ERROR;
    }
  } else if (Private->Mode == SD_CARD_MODE_SPI) {
    // For SPI mode, CSD should be read during initialization
    if (Private->Csd[0] == 0 && Private->Csd[15] == 0) {
      DEBUG((DEBUG_ERROR, "SdCardMedia: No CSD data available\n"));
      return EFI_DEVICE_ERROR;
    }
  }
  
  // Parse CSD to extract capacity and other parameters
  Status = ParseCsdRegister(Private);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardMedia: Failed to parse CSD register: %r\n", Status));
    return Status;
  }
  
  return EFI_SUCCESS;
}

/**
  Parses the CSD register to extract card parameters.
**/
STATIC
EFI_STATUS
ParseCsdRegister (
  IN SD_CARD_PRIVATE_DATA  *Private
  )
{
  UINT8 *Csd = Private->Csd;
  UINT64 Capacity;
  UINT32 BlockSize;
  BOOLEAN IsHighCapacity;
  
  // Use the helper function from DriverLib
  EFI_STATUS Status = SdCardParseCsdRegister(Csd, &Capacity, &BlockSize, &IsHighCapacity);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  
  Private->CapacityInBytes = Capacity;
  Private->BlockSize = BlockSize;
  
  if (IsHighCapacity) {
    Private->CardType = CARD_TYPE_SD_V2_HC;
  } else if (Private->CardType == CARD_TYPE_SD_V2_SC) {
    // Already set during initialization
  } else {
    Private->CardType = CARD_TYPE_SD_V1;
  }
  
  DEBUG((DEBUG_INFO, "SdCardMedia: Card type: %d, Capacity: %llu bytes, Block size: %u\n",
         Private->CardType, Private->CapacityInBytes, Private->BlockSize));
  
  return EFI_SUCCESS;
}

/**
  Checks card status and handles errors.
**/
STATIC
EFI_STATUS
CheckCardStatus (
  IN SD_CARD_PRIVATE_DATA  *Private
  )
{
  // This would implement periodic status checking
  // For now, just verify card is still present
  if (!DetectCardPresence(Private)) {
    Private->BlockMedia.MediaPresent = FALSE;
    Private->BlockMedia.MediaId++;
    return EFI_NO_MEDIA;
  }
  
  return EFI_SUCCESS;
}

/**
  Detects card presence.
**/
STATIC
BOOLEAN
DetectCardPresence (
  IN SD_CARD_PRIVATE_DATA  *Private
  )
{
  // TODO: Implement proper card detection based on hardware capabilities
  // For now, assume card is present
  return TRUE;
}

/**
  Updates media parameters in the block I/O media structure.
**/
STATIC
VOID
UpdateMediaParameters (
  IN SD_CARD_PRIVATE_DATA  *Private
  )
{
  Private->BlockMedia.BlockSize = Private->BlockSize;
  Private->BlockMedia.LastBlock = (Private->CapacityInBytes / Private->BlockSize) - 1;
  
  // Set alignment requirements based on mode
  if (Private->Mode == SD_CARD_MODE_HOST) {
    // Host mode typically has stricter alignment requirements
    Private->BlockMedia.IoAlign = 4; // 4-byte alignment for DMA
  } else {
    // SPI mode is more flexible
    Private->BlockMedia.IoAlign = 1; // 1-byte alignment (no DMA requirements)
  }
  
  DEBUG((DEBUG_INFO, "SdCardMedia: Media parameters - BlockSize: %u, LastBlock: %llu, IoAlign: %u\n",
         Private->BlockMedia.BlockSize, Private->BlockMedia.LastBlock, Private->BlockMedia.IoAlign));
}

/**
  Hotplug notification callback.
**/
VOID
EFIAPI
SdCardHotplugNotification (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  SD_CARD_PRIVATE_DATA *Private = (SD_CARD_PRIVATE_DATA *)Context;
  
  DEBUG((DEBUG_INFO, "SdCardMedia: Hotplug event received\n"));
  
  // Debounce logic - check multiple times to avoid false triggers
  BOOLEAN CardPresent = FALSE;
  for (UINTN i = 0; i < 3; i++) {
    CardPresent = DetectCardPresence(Private);
    gBS->Stall(10000); // 10ms delay
  }
  
  if (CardPresent && !Private->BlockMedia.MediaPresent) {
    // Card inserted
    DEBUG((DEBUG_INFO, "SdCardMedia: Card inserted\n"));
    EFI_STATUS Status = SdCardInitialize(Private);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_WARN, "SdCardMedia: Failed to initialize inserted card: %r\n", Status));
    }
  } else if (!CardPresent && Private->BlockMedia.MediaPresent) {
    // Card removed
    DEBUG((DEBUG_INFO, "SdCardMedia: Card removed\n"));
    Private->BlockMedia.MediaPresent = FALSE;
    Private->BlockMedia.MediaId++;
    Private->IsInitialized = FALSE;
  }
}

/**
  Timer callback for periodic status checking.
**/
VOID
EFIAPI
SdCardTimerCallback (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  SD_CARD_PRIVATE_DATA *Private = (SD_CARD_PRIVATE_DATA *)Context;
  
  // Perform periodic status check
  if (Private->BlockMedia.MediaPresent) {
    EFI_STATUS Status = CheckCardStatus(Private);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_WARN, "SdCardMedia: Periodic status check failed: %r\n", Status));
    }
  }
}