#include "SdCardBlockIo.h"
#include "SdCardDxe.h"
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

/**
  Implementation of EFI_BLOCK_IO_PROTOCOL.ReadBlocks()
**/
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
  DEBUG((DEBUG_VERBOSE, "SdCardBlockIo: ReadBlocks LBA:%lu Size:%u\n", Lba, BufferSize));
  
  return SdCardMediaReadBlocks(This, MediaId, Lba, BufferSize, Buffer);
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