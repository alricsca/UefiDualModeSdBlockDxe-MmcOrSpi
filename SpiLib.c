// Replace SpiLib.c with this enhanced version
#include "SpiLib.h"
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/SpiHc.h>
#include <Library/DebugLib.h>

/**
  Asserts the SPI chip select line.
**/
EFI_STATUS
EFIAPI
SpiAssertCs (
  IN SD_CARD_PRIVATE_DATA *Private
  )
{
  EFI_STATUS Status;
  
  if (Private == NULL || Private->SpiHcProtocol == NULL || Private->SpiPeripheral == NULL) {
    DEBUG((DEBUG_ERROR, "SpiAssertCs: Invalid parameters\n"));
    return EFI_INVALID_PARAMETER;
  }
  
  Status = Private->SpiHcProtocol->ChipSelect(Private->SpiHcProtocol, Private->SpiPeripheral, TRUE);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SpiAssertCs: ChipSelect failed - %r\n", Status));
  }
  
  return Status;
}

/**
  Deasserts the SPI chip select line.
**/
EFI_STATUS
EFIAPI
SpiDeassertCs (
  IN SD_CARD_PRIVATE_DATA *Private
  )
{
  EFI_STATUS Status;
  
  if (Private == NULL || Private->SpiHcProtocol == NULL || Private->SpiPeripheral == NULL) {
    DEBUG((DEBUG_ERROR, "SpiDeassertCs: Invalid parameters\n"));
    return EFI_INVALID_PARAMETER;
  }
  
  Status = Private->SpiHcProtocol->ChipSelect(Private->SpiHcProtocol, Private->SpiPeripheral, FALSE);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SpiDeassertCs: ChipSelect failed - %r\n", Status));
  }
  
  return Status;
}

/**
  Transfers a single byte to and from the SPI device.
**/
EFI_STATUS
EFIAPI
SpiTransferByte (
  IN     SD_CARD_PRIVATE_DATA *Private,
  IN     UINT8                WriteByte,
  OUT    UINT8                *ReadByte
  )
{
  EFI_STATUS Status;
  UINT8 WriteBuffer[1];
  UINT8 ReadBuffer[1];

  WriteBuffer[0] = WriteByte;

  Status = SpiTransferBuffer(Private, WriteBuffer, ReadBuffer, 1);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SpiTransferByte: Transfer failed - %r\n", Status));
    return Status;
  }

  if (ReadByte != NULL) {
    *ReadByte = ReadBuffer[0];
  }

  return EFI_SUCCESS;
}

/**
  Transfers a buffer of data to and from the SPI device.
**/
EFI_STATUS
EFIAPI
SpiTransferBuffer (
  IN     SD_CARD_PRIVATE_DATA *Private,
  IN     CONST UINT8          *WriteBuffer,
  OUT    UINT8                *ReadBuffer,
  IN     UINTN                TransferLength
  )
{
  EFI_SPI_BUS_TRANSACTION Transaction;
  UINT8                   *LocalWriteBuffer;
  BOOLEAN                 FreeBuffer;
  EFI_STATUS              Status;

  if (Private == NULL || Private->SpiHcProtocol == NULL) {
    DEBUG((DEBUG_ERROR, "SpiTransferBuffer: Invalid parameters\n"));
    return EFI_INVALID_PARAMETER;
  }

  if (TransferLength == 0 || (WriteBuffer == NULL && ReadBuffer == NULL)) {
    DEBUG((DEBUG_ERROR, "SpiTransferBuffer: Invalid transfer parameters\n"));
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&Transaction, sizeof (Transaction));
  FreeBuffer = FALSE;

  if (WriteBuffer == NULL) {
    LocalWriteBuffer = AllocatePool (TransferLength);
    if (LocalWriteBuffer == NULL) {
      DEBUG((DEBUG_ERROR, "SpiTransferBuffer: Failed to allocate write buffer\n"));
      return EFI_OUT_OF_RESOURCES;
    }
    SetMem (LocalWriteBuffer, TransferLength, 0xFF);
    FreeBuffer = TRUE;
  } else {
    LocalWriteBuffer = (UINT8 *)WriteBuffer;
  }

  Transaction.SpiPeripheral     = Private->SpiPeripheral;
  Transaction.TransactionType   = SPI_TRANSACTION_FULL_DUPLEX;
  Transaction.DebugTransaction  = FALSE;
  Transaction.BusWidth          = 1;
  Transaction.FrameSize         = 8;
  Transaction.WriteBytes        = TransferLength;
  Transaction.WriteBuffer       = LocalWriteBuffer;
  Transaction.ReadBytes         = TransferLength;
  Transaction.ReadBuffer        = ReadBuffer;

  Status = Private->SpiHcProtocol->Transaction (Private->SpiHcProtocol, &Transaction);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SpiTransferBuffer: Transaction failed - %r\n", Status));
  }

  if (FreeBuffer) {
    FreePool (LocalWriteBuffer);
  }

  return Status;
}