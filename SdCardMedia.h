#ifndef __SD_CARD_MEDIA_H__
#define __SD_CARD_MEDIA_H__

#include "SdCardDxe.h"

//
// Media and Command Function Prototypes
//

EFI_STATUS EFIAPI SdCardMediaReset(IN EFI_BLOCK_IO_PROTOCOL *This, IN BOOLEAN ExtendedVerification);
EFI_STATUS EFIAPI SdCardMediaReadBlocks(IN EFI_BLOCK_IO_PROTOCOL *This, IN UINT32 MediaId, IN EFI_LBA Lba, IN UINTN BufferSize, OUT VOID *Buffer);
EFI_STATUS EFIAPI SdCardMediaWriteBlocks(IN EFI_BLOCK_IO_PROTOCOL *This, IN UINT32 MediaId, IN EFI_LBA Lba, IN UINTN BufferSize, IN VOID *Buffer);
EFI_STATUS EFIAPI SdCardMediaFlushBlocks(IN EFI_BLOCK_IO_PROTOCOL *This);

#endif // __SD_CARD_MEDIA_H__