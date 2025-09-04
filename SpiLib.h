#ifndef __SPI_LIB_H__
#define __SPI_LIB_H__

#include <Uefi.h>
#include <Protocol/SpiHc.h>
#include "SdCardDxe.h"

EFI_STATUS
EFIAPI
SpiAssertCs (
  IN SD_CARD_PRIVATE_DATA *Private
  );

EFI_STATUS
EFIAPI
SpiDeassertCs (
  IN SD_CARD_PRIVATE_DATA *Private
  );

EFI_STATUS
EFIAPI
SpiTransferByte (
  IN     SD_CARD_PRIVATE_DATA *Private,
  IN     UINT8                WriteByte,
  OUT    UINT8                *ReadByte
  );

EFI_STATUS
EFIAPI
SpiTransferBuffer (
  IN     SD_CARD_PRIVATE_DATA *Private,
  IN     CONST UINT8          *WriteBuffer,
  OUT    UINT8                *ReadBuffer,
  IN     UINTN                TransferLength
  );

#endif // __SPI_LIB_H__