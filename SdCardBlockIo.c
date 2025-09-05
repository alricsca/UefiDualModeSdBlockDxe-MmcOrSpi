#include "HostIo.h"
#include "SpiIo.h"
#include "SdCardBlockIo.h"
#include "SdCardDxe.h"
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
// Replace the current stubs with proper implementations

EFI_STATUS
EFIAPI
SdCardBlockIoReadBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  OUT VOID                  *Buffer
  )
{
  SD_CARD_PRIVATE_DATA *Private = SD_CARD_PRIVATE_DATA_FROM_BLOCK_IO(This);
  
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
  
  DEBUG((DEBUG_VERBOSE, "SdCardBlockIo: Reading %u blocks from LBA %lu\n", 
         BufferSize / Private->BlockMedia.BlockSize, Lba));
  
  // Route to mode-specific implementation
  if (Private->Mode == SD_CARD_MODE_HOST) {
    return SdCardExecuteReadWriteHost(Private, Lba, BufferSize, Buffer, FALSE);
  } else if (Private->Mode == SD_CARD_MODE_SPI) {
    return SdCardExecuteReadWriteSpi(Private, Lba, BufferSize, Buffer, FALSE);
  }
  
  return EFI_UNSUPPORTED;
}

/**
  Implementation of EFI_BLOCK_IO_PROTOCOL.WriteBlocks()
**/
EFI_STATUS
EFIAPI
SdCardBlockIoWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                  *Buffer
  )
{
    SD_CARD_PRIVATE_DATA *Private = SD_CARD_PRIVATE_DATA_FROM_BLOCK_IO(This);

  // Parameter validation (same as in SdCardMediaWriteBlocks)
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!Private->BlockMedia.MediaPresent) {
    return EFI_NO_MEDIA;
  }

  if (Private->BlockMedia.ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }

  DEBUG((DEBUG_VERBOSE, "SdCardBlockIo: WriteBlocks LBA:%lu Size:%u\n", Lba, BufferSize));

  // Route to mode-specific implementation
  DEBUG((DEBUG_VERBOSE, "SdCardBlockIo: WriteBlocks LBA:%lu Size:%u\n", Lba, BufferSize));
  
  return SdCardMediaWriteBlocks(This, MediaId, Lba, BufferSize, Buffer);
}

/**
  Implementation of EFI_BLOCK_IO_PROTOCOL.FlushBlocks()
**/
EFI_STATUS
EFIAPI
SdCardBlockIoFlushBlocks (
 IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  DEBUG((DEBUG_VERBOSE, "SdCardBlockIo: FlushBlocks\n"));
  
  return SdCardMediaFlushBlocks(This);
}
