#include "SdCardBlockIo.h"
#include "SpiIo.h"
#include "DriverLib.h"
#include "SpiLib.h"
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h> // For SwapBytes32 and CRC16

//
// Forward declarations for internal SPI functions
//
EFI_STATUS EFIAPI SdCardSendCommandSpi (IN SD_CARD_PRIVATE_DATA *Private, IN UINT8 Command, IN UINT32 Argument, OUT UINT8 *Response);
EFI_STATUS EFIAPI SdCardWaitNotBusySpi (IN SD_CARD_PRIVATE_DATA *Private);
EFI_STATUS EFIAPI SdCardReadDataBlockSpi (IN SD_CARD_PRIVATE_DATA *Private, IN UINTN Length, OUT UINT8 *Buffer);
EFI_STATUS EFIAPI SdCardWriteDataBlockSpi (IN SD_CARD_PRIVATE_DATA *Private, IN UINT8 Token, IN UINTN Length, IN CONST UINT8 *Buffer);

// =============================================================================
// SPI I/O Functions
// =============================================================================
/**
  SPI mode read/write function.
**/
EFI_STATUS
EFIAPI
SdCardExecuteReadWriteSpi (
  IN  SD_CARD_PRIVATE_DATA  *Private,
  IN  EFI_LBA               Lba,
  IN  UINTN                 BufferSize,
  IN OUT  VOID                  *Buffer,
  IN  BOOLEAN               IsWrite
  )
{
  EFI_STATUS Status = EFI_SUCCESS;
  UINTN BlockCount = BufferSize / SD_BLOCK_SIZE;
  UINT8 *CurrentBuffer = (UINT8*)Buffer;
  UINT8 Response;
  UINT32 Address;

  Address = (Private->CardType == CARD_TYPE_SD_V2_HC) ? (UINT32)Lba : (UINT32)(Lba * SD_BLOCK_SIZE);

  if (BlockCount > 1) {
    // Multi-block
    UINT8 Command = IsWrite ? CMD25 : CMD18;
    Status = SdCardSendCommandSpi(Private, Command, Address, &Response);
    if (EFI_ERROR(Status) || (Response & 0x80) != 0) {
      return EFI_DEVICE_ERROR;
    }

    for (UINTN i = 0; i < BlockCount; i++) {
      if (IsWrite) {
        Status = SdCardWriteDataBlockSpi(Private, DATA_TOKEN_WRITE_MULTI, SD_BLOCK_SIZE, CurrentBuffer);
      } else {
        Status = SdCardReadDataBlockSpi(Private, SD_BLOCK_SIZE, CurrentBuffer);
      }
      if (EFI_ERROR(Status)) {
        break;
      }
      CurrentBuffer += SD_BLOCK_SIZE;
    }

    if (IsWrite) {
      // stop transmission token for multi-write
      UINT8 StopToken = DATA_TOKEN_WRITE_MULTI_STOP;
      SpiTransferBuffer(Private, &StopToken, NULL, 1);
      Status = SdCardWaitNotBusySpi(Private);
    } else {
      if (EFI_ERROR(Status)) {
        // Attempt to stop transmission on card
        SdCardSendCommandSpi(Private, CMD12, 0, &Response);
      }
    }
  } else {
    // Single block
    UINT8 Command = IsWrite ? CMD24 : CMD17;
    Status = SdCardSendCommandSpi(Private, Command, Address, &Response);
    if (EFI_ERROR(Status) || (Response & 0x80) != 0) {
      return EFI_DEVICE_ERROR;
    }

    if (IsWrite) {
      Status = SdCardWriteDataBlockSpi(Private, DATA_TOKEN_WRITE_SINGLE, SD_BLOCK_SIZE, CurrentBuffer);
    } else {
      Status = SdCardReadDataBlockSpi(Private, SD_BLOCK_SIZE, CurrentBuffer);
    }
  }

  return Status;
}
/**
  Initializes the SD card in SPI mode.
**/
EFI_STATUS
EFIAPI
SdCardInitializeSpi (
  IN SD_CARD_PRIVATE_DATA   *Private
  )
{
  EFI_STATUS Status;
  UINT8      Response;
  UINT8      Ocr[4];
  UINTN      Retry;
  UINT8      Csd[CSD_REGISTER_SIZE];

  Private->IsInitialized = FALSE;
  Private->BlockMedia.MediaPresent = FALSE;

// Add to SdCardInitializeSpi() before CMD0
// Send 80+ dummy clocks with CS deasserted and DI/MOSI high
UINT8 dummyClocks[10];
SetMem(dummyClocks, sizeof(dummyClocks), 0xFF);
SpiDeassertCs(Private);
SpiTransferBuffer(Private, dummyClocks, NULL, sizeof(dummyClocks));
SpiAssertCs(Private);

  // CMD0: Reset card to SPI mode
  Status = SdCardSendCommandSpi(Private, CMD0, 0, &Response);
  if (EFI_ERROR(Status) || Response != R1_IDLE_STATE) {
    DEBUG((DEBUG_ERROR, "SDCard: CMD0 failed. Response: 0x%x, Status: %r\n", Response, Status));
    return EFI_DEVICE_ERROR;
  }

  // CMD8: Check voltage range and pattern
  Status = SdCardSendCommandSpi(Private, CMD8, CMD8_ARG_V2, &Response);
  if (Status == EFI_SUCCESS && (Response & R1_IDLE_STATE)) {
    Private->CardType = CARD_TYPE_SD_V2_SC;
    SpiTransferBuffer(Private, NULL, Ocr, 4);
    if (Ocr[3] != CMD8_CHECK_PATTERN) {
      DEBUG((DEBUG_ERROR, "SDCard: CMD8 check pattern mismatch\n"));
      return EFI_DEVICE_ERROR;
    }
  } else {
    // For v1 cards or non-responsive cards, set to v1
    Private->CardType = CARD_TYPE_SD_V1;
  }

  // ACMD41 initialize
  Retry = 100;
  do {
    // send CMD55
    Status = SdCardSendCommandSpi(Private, CMD55, 0, &Response);
    if (EFI_ERROR(Status) || (Response & R1_ILLEGAL_COMMAND)) {
      // If CMD55 fails or is illegal, try direct ACMD41 (some cards might not support CMD55)
      Status = SdCardSendCommandSpi(Private, ACMD41, 0, &Response);
    } else {
      // ACMD41 with HCS if v2
      UINT32 Arg = (Private->CardType == CARD_TYPE_SD_V2_SC) ? ACMD41_ARG_HCS : 0;
      Status = SdCardSendCommandSpi(Private, ACMD41, Arg, &Response);
    }
    
    if (EFI_ERROR(Status)) break;

    if ((Response & R1_IDLE_STATE) == 0) {
      break; // initialization complete
    }

    gBS->Stall(10000);
    Retry--;
  } while (Retry > 0);

  if (Retry == 0 || EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SDCard: Initialization timeout or error. Response: 0x%x\n", Response));
    return EFI_TIMEOUT;
  }

  // CMD58: Read OCR and detect CCS for HC
  if (Private->CardType == CARD_TYPE_SD_V2_SC) {
    Status = SdCardSendCommandSpi(Private, CMD58, 0, &Response);
    if (EFI_ERROR(Status) || (Response != 0 && Response != R1_IDLE_STATE)) {
      DEBUG((DEBUG_ERROR, "SDCard: CMD58 failed\n"));
      return EFI_DEVICE_ERROR;
    }
    SpiTransferBuffer(Private, NULL, Ocr, 4);
    if (Ocr[0] & OCR_CCS_BIT) {
      Private->CardType = CARD_TYPE_SD_V2_HC;
    }
  }

  // CMD16: set block length for standard capacity
  if (Private->CardType != CARD_TYPE_SD_V2_HC) {
    Status = SdCardSendCommandSpi(Private, CMD16, SD_BLOCK_SIZE, &Response);
    if (EFI_ERROR(Status) || (Response != 0)) {
      DEBUG((DEBUG_ERROR, "SDCard: CMD16 failed\n"));
      return EFI_DEVICE_ERROR;
    }
  }

  // CMD9: read CSD
  Status = SdCardSendCommandSpi(Private, CMD9, 0, &Response);
  if (EFI_ERROR(Status) || (Response != 0 && Response != R1_IDLE_STATE)) {
    DEBUG((DEBUG_ERROR, "SDCard: CMD9 failed\n"));
    return EFI_DEVICE_ERROR;
  }

  Status = SdCardReadDataBlockSpi(Private, CSD_REGISTER_SIZE, Csd);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SDCard: Failed to read CSD\n"));
    return Status;
  }

  // Use the SPI-specific CSD parser instead of the generic one
  Status = SdCardParseCsdSpi(Private, Csd);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "Failed to parse CSD register: %r\n", Status));
    return Status;
  }

  DEBUG((DEBUG_INFO, "SDCard: Initialized successfully. CardType: %d, LastBlock: %llu\n",
         Private->CardType, Private->LastBlock));
  return EFI_SUCCESS;
}



// =============================================================================
// Internal SPI Helper Functions
// =============================================================================

/**
  Sends a 6-byte command frame to the SD card over SPI with proper CRC calculation.
**/
EFI_STATUS
EFIAPI
SdCardSendCommandSpi (
  IN  SD_CARD_PRIVATE_DATA  *Private,
  IN  UINT8                 Command,
  IN  UINT32                Argument,
  OUT UINT8                 *Response
  )
{
  UINT8 CommandFrame[6];
  UINT8 WriteBuffer[14]; // 6 for command, 8 for response polling
  UINT8 ReadBuffer[14];
  UINT8 Crc;
  UINTN Index;
  EFI_STATUS Status;

  // Wait for card to be ready (except for CMD0)
  if (Command != CMD0) {
    (void)SdCardWaitNotBusySpi(Private);
  }

  // Construct command frame
  CommandFrame[0] = (UINT8)(0x40 | (Command & 0x3F));
  CommandFrame[1] = (UINT8)(Argument >> 24);
  CommandFrame[2] = (UINT8)(Argument >> 16);
  CommandFrame[3] = (UINT8)(Argument >> 8);
  CommandFrame[4] = (UINT8)(Argument & 0xFF);

  // Calculate proper CRC7 for all commands
  Crc = SdCardCalculateCrc7(CommandFrame, 5);
  CommandFrame[5] = Crc;

  // Construct a single transaction buffer: 6-byte command + 8 dummy bytes for response
  CopyMem(WriteBuffer, CommandFrame, sizeof(CommandFrame));
  SetMem(WriteBuffer + sizeof(CommandFrame), 8, 0xFF);

  // Perform a single, full-duplex transaction to send the command and poll for the response.
  // This ensures Chip Select remains asserted for the entire operation, as required by the spec.
  Status = SpiTransferBuffer(Private, WriteBuffer, ReadBuffer, sizeof(WriteBuffer));
  if (EFI_ERROR(Status)) {
    return Status;
  }

  // Find the response in the read buffer. It's the first byte after the command
  // that is not 0xFF.
  for (Index = sizeof(CommandFrame); Index < sizeof(ReadBuffer); Index++) {
    if (Index >= sizeof(ReadBuffer)) { 
      DEBUG((DEBUG_ERROR, "Response buffer overflow in CMD%d\n", Command)); 
      return EFI_BUFFER_TOO_SMALL; 
    }
    if ((ReadBuffer[Index] & 0x80) == 0) {
      *Response = ReadBuffer[Index];

      if ((*Response & R1_COM_CRC_ERROR) != 0) {
        DEBUG((DEBUG_WARN, "SdCardSendCommandSpi: CRC indicated in response for CMD%d\n", Command));
        return EFI_CRC_ERROR;
      }
      if ((*Response & R1_ILLEGAL_COMMAND) != 0) {
        DEBUG((DEBUG_WARN, "SdCardSendCommandSpi: Illegal command error for CMD%d\n", Command));
        return EFI_UNSUPPORTED;
      }

      return EFI_SUCCESS;
    }
  }

  DEBUG((DEBUG_ERROR, "SdCardSendCommandSpi: Timeout waiting for response to CMD%d\n", Command));
  return EFI_TIMEOUT;
}

/**
  Waits for the card to be not busy (DO/MISO line is high) in SPI mode.
**/
EFI_STATUS
EFIAPI
SdCardWaitNotBusySpi (
  IN  SD_CARD_PRIVATE_DATA  *Private
  )
{
  UINTN Retry = 5000; // ~500ms depending on stall granularity
  UINT8 BusyByte;

  do {
    SpiTransferBuffer (Private, NULL, &BusyByte, 1);
    if (BusyByte == 0xFF) {
      return EFI_SUCCESS;
    }
    gBS->Stall (1);
    Retry--;
  } while (Retry > 0);

  return EFI_TIMEOUT;
}

/**
  Parses the CSD register to determine card capacity in SPI mode.
**/
EFI_STATUS
EFIAPI
SdCardParseCsdSpi(
  IN SD_CARD_PRIVATE_DATA *Private,
  IN UINT8                *Csd
  )
{
  UINT64 Capacity;
  UINT32 CSize;
  UINT8 ReadBlLen;
  UINT16 CSizeMult;

  if ((Csd[0] & 0xC0) == 0x40) {
    // CSD Version 2.0 (for High Capacity cards)
    CSize = ((Csd[7] & 0x3F) << 16) | (Csd[8] << 8) | Csd[9];
    Capacity = (UINT64)(CSize + 1) * 512ULL * 1024ULL; // bytes
  } else {
    // CSD Version 1.0 (for Standard Capacity cards)
    ReadBlLen = Csd[5] & 0x0F;
    CSize = ((Csd[6] & 0x03) << 10) | (Csd[7] << 2) | ((Csd[8] & 0xC0) >> 6);
    CSizeMult = (UINT16)(((Csd[9] & 0x03) << 1) | ((Csd[10] & 0x80) >> 7));
    Capacity = (UINT64)(CSize + 1) * (1ULL << (CSizeMult + 2)) * (1ULL << ReadBlLen);
  }

  if (Capacity == 0) {
    return EFI_DEVICE_ERROR;
  }

  Private->CapacityInBytes = Capacity;
  Private->BlockSize = SD_BLOCK_SIZE;
  Private->LastBlock = (Capacity / SD_BLOCK_SIZE) - 1;
  Private->BlockMedia.BlockSize = Private->BlockSize;
  Private->BlockMedia.LastBlock = Private->LastBlock;

  return EFI_SUCCESS;
}

/**
  Reads a data block from the card in SPI mode with CRC verification.
**/
EFI_STATUS
EFIAPI
SdCardReadDataBlockSpi (
  IN  SD_CARD_PRIVATE_DATA  *Private,
  IN  UINTN                 Length,
  OUT UINT8                 *Buffer
  )
{
  UINTN Retry = 200000; // loop count — tuned by caller
  UINT8 Token;
  UINT16 ReceivedCrc, CalculatedCrc;

  do {
    SpiTransferBuffer(Private, NULL, &Token, 1);
    if (Token == DATA_TOKEN_READ_START) {
      // Read data payload
      SpiTransferBuffer(Private, NULL, Buffer, Length);

      // Read received CRC (big-endian on bus)
      UINT8 CrcBytes[2];
      SpiTransferBuffer(Private, NULL, CrcBytes, 2);
      ReceivedCrc = (UINT16)((CrcBytes[0] << 8) | CrcBytes[1]);

      // Calculate CRC and compare
      CalculatedCrc = SdCardCalculateCrc16(Buffer, Length);;
      if (ReceivedCrc != CalculatedCrc) {
        DEBUG((DEBUG_ERROR, "SdCardReadDataBlockSpi: CRC mismatch! Received: 0x%04X, Calculated: 0x%04X\n",
               ReceivedCrc, CalculatedCrc));
        return EFI_CRC_ERROR;
      }

      return EFI_SUCCESS;
    }
    gBS->Stall(1); // small delay
    Retry--;
  } while (Retry > 0);

  DEBUG((DEBUG_ERROR, "SdCardReadDataBlockSpi: Timeout waiting for data token\n"));
  return EFI_TIMEOUT;
}

/**
  Writes a data block to the card in SPI mode with proper CRC generation.
**/
EFI_STATUS
EFIAPI
SdCardWriteDataBlockSpi (
  IN  SD_CARD_PRIVATE_DATA  *Private,
  IN  UINT8                 Token,
  IN  UINTN                 Length,
  IN  CONST UINT8           *Buffer
  )
{
  EFI_STATUS Status;
  UINT8 Response;
  UINT16 Crc;

  // Calculate CRC for the data block
  Crc = SdCardCalculateCrc16(Buffer, Length);

  // Send data token
  SpiTransferBuffer(Private, &Token, NULL, 1);

  // Send data payload
  SpiTransferBuffer(Private, (UINT8*)Buffer, NULL, Length);

  // Send CRC (big-endian on the bus)
  UINT8 CrcBytes[2] = { (UINT8)(Crc >> 8), (UINT8)(Crc & 0xFF) };
  SpiTransferBuffer(Private, CrcBytes, NULL, 2);

  // Read data response token
  SpiTransferBuffer(Private, NULL, &Response, 1);
  if ((Response & DATA_RESP_MASK) != DATA_RESP_ACCEPTED) {
    DEBUG((DEBUG_ERROR, "SdCardWriteDataBlockSpi: Data response error: 0x%02X\n", Response));
    return EFI_DEVICE_ERROR;
  }

  // Wait for write complete (card not busy)
  Status = SdCardWaitNotBusySpi(Private);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardWriteDataBlockSpi: Write completion timeout\n"));
  }

  return Status;
}

/**
  Receives a response from the SD card in SPI mode.
**/
EFI_STATUS
SdCardReceiveResponseSpi(
  IN UINT8 *ResponseBuffer,
  OUT UINT8 *Response
  )
{
  if (Response && ResponseBuffer) {
    *Response = *ResponseBuffer;
    return EFI_SUCCESS;
  }
  return EFI_INVALID_PARAMETER;
}
