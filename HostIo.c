#include "HostIo.h"
#include "SdCardBlockIo.h"
#include "SdCardDxe.h"
#include "SdCardMedia.h"
#include "DriverLib.h"
#include <Library/DebugLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>  // For PCD access
#include <Protocol/SdMmcPassThru.h> // For SD_MMC protocol definitions
#include <Library/BaseLib.h> // For SwapBytes32

STATIC
UINT32
GetUhsModeFrequency (
  IN UHS_MODE Mode
  );

/**
  Sets UHS-I mode for higher performance.
  @param[in] Private  SD card private data
  @param[in] Mode     UHS-I mode to set (SDR12, SDR25, SDR50, SDR104, DDR50)
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
SetUhsMode (
  IN SD_CARD_PRIVATE_DATA  *Private,
  IN UHS_MODE              Mode
  )
{
  EFI_STATUS Status;
  UINT32 Response;
  
  if (Private == NULL || Private->SdMmcPassThru == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  DEBUG((DEBUG_INFO, "SdCardHost: Setting UHS-I mode %d\n", Mode));
  
  // Send CMD6 to switch function
  UINT32 SwitchArg = 0;
  
  switch (Mode) {
    case SDR12:
      // Default mode, no switching needed
      return EFI_SUCCESS;
      
    case SDR25:
      SwitchArg = (0x1 << 24) | (0xF << 16); // Group 1, Function 1 (SDR25)
      break;
      
    case SDR50:
      SwitchArg = (0x1 << 24) | (0x2 << 16); // Group 1, Function 2 (SDR50)
      break;
      
    case SDR104:
      SwitchArg = (0x1 << 24) | (0x3 << 16); // Group 1, Function 3 (SDR104)
      break;
      
    case DDR50:
      SwitchArg = (0x1 << 24) | (0x4 << 16); // Group 1, Function 4 (DDR50)
      break;
      
    default:
      return EFI_INVALID_PARAMETER;
  }
  
  // Set access mode
  SwitchArg |= (0xF << 8); // Access mode bits
  
  Status = SdCardSendCommandHost(Private, CMD6, SwitchArg, &Response);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardHost: Failed to set UHS mode %d: %r\n", Mode, Status));
    return Status;
  }
  
  // Check if switch was successful
  if ((Response & 0xF0000000) != 0) {
    DEBUG((DEBUG_ERROR, "SdCardHost: UHS mode switch failed, response: 0x%08X\n", Response));
    return EFI_DEVICE_ERROR;
  }
  
  // Update controller timing based on new mode
  Status = SetBusSpeedHost(Private, GetUhsModeFrequency(Mode));
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "SdCardHost: Failed to set bus speed for UHS mode %d: %r\n", Mode, Status));
  }
  
  DEBUG((DEBUG_INFO, "SdCardHost: UHS-I mode %d set successfully\n", Mode));
  return EFI_SUCCESS;
}

/**
  Gets the frequency for a UHS-I mode.
  @param[in] Mode  UHS-I mode
  @return Frequency in Hz
**/
UINT32
GetUhsModeFrequency (
  IN UHS_MODE Mode
  )
{
  switch (Mode) {
    case SDR12:    return 25000000;  // 25 MHz max
    case SDR25:    return 50000000;  // 50 MHz max
    case SDR50:    return 100000000; // 100 MHz max
    case SDR104:   return 208000000; // 208 MHz max
    case DDR50:    return 50000000;  // 50 MHz DDR (100 MT/s)
    default:       return 25000000;  // Default to 25 MHz
  }
}

/**
  Maps SD card specific error codes to EFI_STATUS values.
  @param[in] SdError  SD card error code from R1 response
  @return Corresponding EFI_STATUS value
**/

EFI_STATUS // Becomes public via HostIo.h
EFIAPI
SdCardMapSdErrorToEfiStatus (
  IN UINT32  SdError
  )
{
  // R1 response errors are typically in the lower bits
  UINT8 R1Status = (UINT8)(SdError & 0xFF);
  
  if (R1Status == 0x00) {
    return EFI_SUCCESS;
  }
  
  if (R1Status & R1_COM_CRC_ERROR) {
    return EFI_CRC_ERROR;
  }
  
  if (R1Status & R1_ILLEGAL_COMMAND) {
    return EFI_UNSUPPORTED;
  }
  
  if (R1Status & R1_ADDRESS_ERROR) {
    return EFI_INVALID_PARAMETER;
  }
  
  if (R1Status & R1_PARAMETER_ERROR) {
    return EFI_INVALID_PARAMETER;
  }
  
  if (R1Status & R1_ERASE_SEQUENCE_ERROR) {
    return EFI_DEVICE_ERROR;
  }
  
  if (R1Status & R1_ERASE_RESET) {
    return EFI_DEVICE_ERROR;
  }
  
  // Check if card is still in idle state (should not be after initialization)
  if (R1Status & R1_IDLE_STATE) {
    return EFI_NOT_READY;
  }

  // Handle additional error conditions that might be present in other response types
  if (SdError & 0xFFFF0000) {
    // This might be an R2 response or other extended error
    DEBUG((DEBUG_WARN, "SdCardMapSdErrorToEfiStatus: Extended error bits set: 0x%08X\n", SdError));
  }
  
  return EFI_DEVICE_ERROR;
}

/**
  Checks for SD card specific errors in the response and maps them to EFI_STATUS.
  @param[in] Response  SD card response value
  @param[in] Command   SD command that was executed
  @return EFI_SUCCESS if no errors, appropriate error code otherwise
**/
STATIC
EFI_STATUS
CheckSdErrorResponse (
  IN UINT32  Response,
  IN UINT8   Command
  )
{
  // Check if any error bits are set in the R1 response
  if (Response & 0xFF8000) {
    EFI_STATUS SdStatus = SdCardMapSdErrorToEfiStatus(Response);
    DEBUG((DEBUG_ERROR, "SdCardHost: CMD%d SD error: 0x%08X -> %r\n", Command, Response, SdStatus));
    return SdStatus;
  }
  
  return EFI_SUCCESS;
}

/**
  Sends a command to the SD card in MMC Host mode.
  @param[in] Private   SD card private data
  @param[in] Command   Command index
  @param[in] Argument  Command argument
  @param[out] Response Pointer to store response
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
SdCardSendCommandHost (
  IN  SD_CARD_PRIVATE_DATA  *Private,
  IN  UINT8                 Command,
  IN  UINT32                Argument,
  OUT UINT32                *Response
  )
{
  EFI_STATUS Status;
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  
  if (Private->SdMmcPassThru == NULL || Response == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  ZeroMem(&SdMmcCmdBlk, sizeof(SdMmcCmdBlk));
  ZeroMem(&SdMmcStatusBlk, sizeof(SdMmcStatusBlk));
  ZeroMem(&Packet, sizeof(Packet));
  
  // Set up command block based on command type
  SdMmcCmdBlk.CommandIndex = Command;
  SdMmcCmdBlk.CommandArgument = Argument;
  
  // Set response type based on command
  switch (Command) {
    case SD_CMD0_GO_IDLE_STATE:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR1;
      break;
    case SD_CMD8_SEND_IF_COND:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR7;
      break;
    case SD_CMD2_ALL_SEND_CID:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR2;
      break;
    case SD_CMD3_SEND_RELATIVE_ADDR:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR6;
      break;
    case SD_CMD9_SEND_CSD:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR2;
      break;
    case SD_CMD7_SELECT_DESELECT_CARD:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR1b;
      break;
    case SD_CMD16_SET_BLOCKLEN:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR1;
      break;
    case SD_CMD17_READ_SINGLE_BLOCK:
    case SD_CMD18_READ_MULTIPLE_BLOCK:
    case SD_CMD24_WRITE_BLOCK:
    case SD_CMD25_WRITE_MULTIPLE_BLOCK:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR1;
      break;
    case SD_CMD55_APP_CMD:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR1;
      break;
    case SD_ACMD41_SD_SEND_OP_COND:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR3;
      break;
    case SD_CMD58_READ_OCR:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR3;
      break;
    default:
      SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR1;
      break;
  }
  
  Packet.SdMmcCmdBlk = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;
  Packet.Timeout = 1000000; // 1 second timeout
  
  Status = Private->SdMmcPassThru->PassThru(
             Private->SdMmcPassThru,
             0, // Slot (typically 0 for single slot controllers)
             0, // Flags
             &Packet
             );
  
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardHost: CMD%d failed - %r\n", Command, Status));
    return Status;
  }
  
  // Store the response
  *Response = SdMmcStatusBlk.Resp0;
  
  // Check for SD card specific errors
  return CheckSdErrorResponse(*Response, Command);
}

/**
  Reads the full CSD or CID register (128 bits).
  @param[in] Private   SD card private data
  @param[in] Command   Command to send (CMD9 for CSD, CMD2 for CID)
  @param[in] Argument  Command argument
  @param[out] Data     Buffer to store the register data (16 bytes)
  @return EFI_STATUS
**/
STATIC
EFI_STATUS
SdCardReadRegister (
  IN  SD_CARD_PRIVATE_DATA  *Private,
  IN  UINT8                 Command,
  IN  UINT32                Argument,
  OUT UINT8                 *Data
  )
{
  EFI_STATUS Status;
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  
  if (Private->SdMmcPassThru == NULL || Data == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  ZeroMem(&SdMmcCmdBlk, sizeof(SdMmcCmdBlk));
  ZeroMem(&SdMmcStatusBlk, sizeof(SdMmcStatusBlk));
  ZeroMem(&Packet, sizeof(Packet));
  
  // Set up command block
  SdMmcCmdBlk.CommandIndex = Command;
  SdMmcCmdBlk.CommandArgument = Argument;
  
  // CSD and CID commands use R2 response (136 bits)
  SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR2;
  
  Packet.SdMmcCmdBlk = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;
  Packet.Timeout = 1000000; // 1 second timeout
  
  Status = Private->SdMmcPassThru->PassThru(
             Private->SdMmcPassThru,
             0, // Slot
             0, // Flags
             &Packet
             );
  
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardHost: Read register CMD%d failed - %r\n", Command, Status));
    return Status;
  }
  
  // Copy the response data (128 bits, ignoring CRC)
  // R2 response is stored in Resp0, Resp1, Resp2, Resp3
  UINT32 *Data32 = (UINT32 *)Data;
  Data32[0] = SdMmcStatusBlk.Resp0;
  Data32[1] = SdMmcStatusBlk.Resp1;
  Data32[2] = SdMmcStatusBlk.Resp2;
  Data32[3] = SdMmcStatusBlk.Resp3;
  
  return EFI_SUCCESS;
}

/**
  Parses the CID register and extracts card information.
  @param[in] Private   SD card private data
  @param[in] CidData   CID register data (128-bit)
  @return EFI_STATUS
**/
STATIC EFI_STATUS
ParseCidRegister (
  IN SD_CARD_PRIVATE_DATA  *Private,
  IN UINT8                 *CidData
  )
{
  SD_CID Cid;
  CHAR8 ProductName[6];
  
  // Convert the CID data to a more manageable format
  UINT32 *CidPtr = (UINT32*)CidData;
  UINT32 CidWord0 = SwapBytes32(CidPtr[0]); // bits 31:0
  UINT32 CidWord1 = SwapBytes32(CidPtr[1]); // bits 63:32
  UINT32 CidWord2 = SwapBytes32(CidPtr[2]); // bits 95:64
  UINT32 CidWord3 = SwapBytes32(CidPtr[3]); // bits 127:96

  // Manually extract CID fields based on SD spec
  Cid.ManufacturerID = (CidWord3 >> 24) & 0xFF;
  
  Cid.OEM_AppID[0] = (CidWord3 >> 16) & 0xFF;
  Cid.OEM_AppID[1] = (CidWord3 >> 8) & 0xFF;

  ProductName[0] = (CidWord3) & 0xFF;
  ProductName[1] = (CidWord2 >> 24) & 0xFF;
  ProductName[2] = (CidWord2 >> 16) & 0xFF;
  ProductName[3] = (CidWord2 >> 8) & 0xFF;
  ProductName[4] = (CidWord2) & 0xFF;
  ProductName[5] = '\0';

  Cid.ProductRevision = (CidWord1 >> 24) & 0xFF;
  
  Cid.ProductSerialNumber = ((CidWord1 & 0x00FFFFFF) << 8) | (CidWord0 >> 24);

  UINT16 ManufDate = (CidWord0 >> 8) & 0xFFF;
  Cid.ManufacturingYear = ((ManufDate >> 4) & 0xFF) + 2000;
  Cid.ManufacturingMonth = ManufDate & 0x0F;

  // Log CID information
  DEBUG((DEBUG_INFO, "SdCardHost: Manufacturer ID: 0x%02X\n", Cid.ManufacturerID));
  DEBUG((DEBUG_INFO, "SdCardHost: OEM/App ID: %c%c\n", Cid.OEM_AppID[0], Cid.OEM_AppID[1]));
  DEBUG((DEBUG_INFO, "SdCardHost: Product Name: %a\n", ProductName));
  DEBUG((DEBUG_INFO, "SdCardHost: Product Revision: %d.%d\n", 
         (Cid.ProductRevision >> 4) & 0xF, Cid.ProductRevision & 0xF));
  DEBUG((DEBUG_INFO, "SdCardHost: Serial Number: 0x%08X\n", Cid.ProductSerialNumber));
  DEBUG((DEBUG_INFO, "SdCardHost: Manufacturing Date: %d/%d\n", Cid.ManufacturingMonth, Cid.ManufacturingYear));
  
  return EFI_SUCCESS;
}


/**
  Parses the CSD register and extracts card information.
  @param[in] Private   SD card private data
  @param[in] CsdData   CSD register data (128-bit)
  @return EFI_STATUS
**/
STATIC
EFI_STATUS
ParseCsdRegister (
  IN SD_CARD_PRIVATE_DATA  *Private,
  IN UINT8                 *CsdData
  )
{
  UINT64 Capacity;
  UINT32 BlockCount;
  
  // Convert the CSD data to a more manageable format
  UINT32 *CsdPtr = (UINT32*)CsdData;
  UINT32 CsdWord1 = SwapBytes32(CsdPtr[1]);
  UINT32 CsdWord2 = SwapBytes32(CsdPtr[2]);
  UINT32 CsdWord3 = SwapBytes32(CsdPtr[3]);
  
  // Determine CSD structure version
  UINT8 CsStructure = (CsdWord3 >> 18) & 0x3;
  
  if (CsStructure == 0) {
    // CSD version 1.0 (Standard Capacity)
    // Extract fields using proper bit manipulation
    UINT32 CSizeLow = CsdWord3 & 0x3FF;
    UINT32 CSizeHigh = (CsdWord3 >> 12) & 0x3;
    UINT32 ReadBlLen = (CsdWord2 >> 16) & 0xF;  // Bits [66:63] in word2
    UINT32 CSizeMul = (CsdWord2 >> 7) & 0x7;    // Bits [88:86] in word2
    
    // Calculate capacity
    UINT32 Mult = 1 << (CSizeMul + 2);
    UINT32 BlockLen = 1 << ReadBlLen;
    BlockCount = ((CSizeHigh << 10) | CSizeLow) + 1;
    BlockCount = BlockCount * Mult;
    Capacity = (UINT64)BlockCount * BlockLen;
    
    // For standard capacity cards, we need to set block length
    Private->BlockSize = SD_BLOCK_SIZE;
    Private->LastBlock = (Capacity / SD_BLOCK_SIZE) - 1;
    Private->CapacityInBytes = Capacity;
    
    DEBUG((DEBUG_INFO, "SdCardHost: Standard Capacity Card: %Lu bytes\n", Capacity));
    DEBUG((DEBUG_INFO, "SdCardHost: CSizeHigh: %u, CSizeLow: %u, CSizeMul: %u, ReadBlLen: %u\n", 
           CSizeHigh, CSizeLow, CSizeMul, ReadBlLen));
  } else if (CsStructure == 1) {
    // CSD version 2.0 (High Capacity)
    UINT32 CSize = (CsdWord3 >> 6) & 0x3FFFFF;
    
    // Calculate capacity (fixed block size of 512 bytes for HC cards)
    BlockCount = (CSize + 1) * 1024; // CSize is in units of 512KB
    Capacity = (UINT64)BlockCount * SD_BLOCK_SIZE;
    
    Private->BlockSize = SD_BLOCK_SIZE;
    Private->LastBlock = BlockCount - 1;
    Private->CapacityInBytes = Capacity;
    
    DEBUG((DEBUG_INFO, "SdCardHost: High Capacity Card: %Lu bytes\n", Capacity));
    DEBUG((DEBUG_INFO, "SdCardHost: CSize: %u\n", CSize));
  } else {
    DEBUG((DEBUG_ERROR, "SdCardHost: Unknown CSD structure: %d\n", CsStructure));
    return EFI_UNSUPPORTED;
  }
  
  // Extract other useful information from CSD
  // TranSpeed is in bits [103:96] which is in CsdWord1[7:0]
  UINT8 TranSpeed = CsdWord1 & 0xFF;
  UINT8 TimeValue = TranSpeed & 0x7;
  UINT8 TimeUnit = (TranSpeed >> 3) & 0x7;
  
  // Calculate maximum transfer speed in MHz
  // Time units: 0=100ns, 1=1us, 2=10us, 3=100us, 4=1ms, 5=10ms, 6=100ms, 7=1s
  // Time values: 0=reserved, 1=1.0, 2=1.2, 3=1.3, 4=1.5, 5=2.0, 6=2.5, 7=3.0, 8=3.5, 9=4.0, 10=4.5, 11=5.0, 12=5.5, 13=6.0, 14=7.0, 15=8.0
  static const UINT32 TimeUnitMultiplier[8] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000};
  static const UINT16 TimeValueMultiplier[16] = {0, 10, 12, 13, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80};
  
  if (TimeUnit < 8 && TimeValue < 16) {
    UINT32 MaxDataRate = (TimeValueMultiplier[TimeValue] * TimeUnitMultiplier[TimeUnit]) / 10;
    DEBUG((DEBUG_INFO, "SdCardHost: Max data rate: %d Kb/s\n", MaxDataRate));
  }
  
  return EFI_SUCCESS;
}
/**
  Initializes the SD card in Host mode.
**/
EFI_STATUS
EFIAPI
SdCardInitializeHost (
  IN SD_CARD_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS Status;
  UINT32 Response;
  UINT32 Ocr;
  UINTN Retry;
  UINT16 Rca = 0;
  UINT8 RegisterData[16]; // 128 bits for CSD/CID
  
  DEBUG((DEBUG_INFO, "SdCardHost: Starting host mode initialization\n"));
  
  if (Private->SdMmcPassThru == NULL) {
    return EFI_UNSUPPORTED;
  }
  
  // CMD0: Reset the card to idle state
  Status = SdCardSendCommandHost(Private, SD_CMD0_GO_IDLE_STATE, 0, &Response);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardHost: CMD0 failed - %r\n", Status));
    return Status;
  }
  
  // CMD8: Check voltage range and pattern (for SDv2+ cards)
  Status = SdCardSendCommandHost(Private, SD_CMD8_SEND_IF_COND, SD_CHECK_VOLTAGE_PATTERN, &Response);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_INFO, "SdCardHost: CMD8 failed, assuming SDv1 or MMC card - %r\n", Status));
    Private->CardType = CARD_TYPE_SD_V1;
  } else {
    // Check if the response matches our voltage pattern
    if ((Response & 0xFFF) == SD_CHECK_VOLTAGE_PATTERN) {
      DEBUG((DEBUG_INFO, "SdCardHost: SDv2+ card detected\n"));
      Private->CardType = CARD_TYPE_SD_V2_SC;
    } else {
      DEBUG((DEBUG_ERROR, "SdCardHost: Voltage mismatch in CMD8 response: 0x%08X\n", Response));
      return EFI_UNSUPPORTED;
    }
  }
  
  // ACMD41: Initialize the card (with HCS bit for SDv2+)
  Retry = 100; // Retry up to 100 times (about 1 second)
  do {
    // First send CMD55 (APP_CMD) to indicate next command is application-specific
    Status = SdCardSendCommandHost(Private, SD_CMD55_APP_CMD, 0, &Response);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "SdCardHost: CMD55 failed - %r\n", Status));
      return Status;
    }
    
    // Send ACMD41 with appropriate arguments
    UINT32 Acmd41Arg = 0;
    if (Private->CardType == CARD_TYPE_SD_V2_SC) {
      Acmd41Arg = SD_HCS; // Set Host Capacity Support bit for SDv2+
    }
    
    Status = SdCardSendCommandHost(Private, SD_ACMD41_SD_SEND_OP_COND, Acmd41Arg, &Response);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "SdCardHost: ACMD41 failed - %r\n", Status));
      return Status;
    }
    
    // Check if initialization is complete (power up bit set)
    if (Response & OCR_POWERUP_BIT) {
      Ocr = Response;
      break;
    }
    
    // Wait before retrying
    gBS->Stall(10000); // 10ms delay
    Retry--;
  } while (Retry > 0);
  
  if (Retry == 0) {
    DEBUG((DEBUG_ERROR, "SdCardHost: ACMD41 timeout\n"));
    return EFI_TIMEOUT;
  }
  
  // Check if card is high capacity
  if (Ocr & OCR_CCS_BIT) {
    Private->CardType = CARD_TYPE_SD_V2_HC;
    DEBUG((DEBUG_INFO, "SdCardHost: High capacity card detected\n"));
  }
  
  // CMD2: Get CID
  Status = SdCardReadRegister(Private, SD_CMD2_ALL_SEND_CID, 0, RegisterData);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardHost: CMD2 failed - %r\n", Status));
    return Status;
  }
  
  // Parse CID register
  Status = ParseCidRegister(Private, RegisterData);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "SdCardHost: CID parsing failed - %r\n", Status));
    // Continue anyway as this is not critical for operation
  }
  
  // CMD3: Get RCA (Relative Card Address)
  Status = SdCardSendCommandHost(Private, SD_CMD3_SEND_RELATIVE_ADDR, 0, &Response);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardHost: CMD3 failed - %r\n", Status));
    return Status;
  }
  
  // Extract RCA from response (bits 31:16)
  Rca = (Response >> 16) & 0xFFFF;
  if (Rca == 0) {
    DEBUG((DEBUG_ERROR, "SdCardHost: Invalid RCA from CMD3: 0x%08X\n", Response));
    return EFI_DEVICE_ERROR;
  }
  
  Private->Rca = Rca;
  DEBUG((DEBUG_INFO, "SdCardHost: RCA assigned: 0x%04X\n", Rca));
  
  // CMD9: Get CSD
  Status = SdCardReadRegister(Private, SD_CMD9_SEND_CSD, Rca << 16, RegisterData);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardHost: CMD9 failed - %r\n", Status));
    return Status;
  }
  
  // Parse CSD register to get capacity and other card information
  Status = ParseCsdRegister(Private, RegisterData);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardHost: CSD parsing failed - %r\n", Status));
    return Status;
  }
  
  // CMD7: Select the card
  Status = SdCardSendCommandHost(Private, SD_CMD7_SELECT_DESELECT_CARD, Rca << 16, &Response);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardHost: CMD7 failed - %r\n", Status));
    return Status;
  }
  
  // For standard capacity cards, set block length to 512 bytes
  if (Private->CardType != CARD_TYPE_SD_V2_HC) {
    Status = SdCardSendCommandHost(Private, SD_CMD16_SET_BLOCKLEN, SD_BLOCK_SIZE, &Response);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "SdCardHost: CMD16 failed - %r\n", Status));
      return Status;
    }
    Private->BlockSize = SD_BLOCK_SIZE;
  } else {
    // High capacity cards use fixed 512-byte blocks
    Private->BlockSize = SD_BLOCK_SIZE;
  }
  
  DEBUG((DEBUG_INFO, "SdCardHost: Capacity: %Lu bytes, Block size: %u, Last block: %Lu\n", 
         Private->CapacityInBytes, Private->BlockSize, Private->LastBlock));
  DEBUG((DEBUG_INFO, "SdCardHost: Host mode initialization complete\n"));
  
  return EFI_SUCCESS;
}

/**
  Host mode read/write function.
**/
EFI_STATUS
EFIAPI
SdCardExecuteReadWriteHost (
  IN  SD_CARD_PRIVATE_DATA  *Private,
  IN  EFI_LBA               Lba,
  IN  UINTN                 BufferSize,
  IN  VOID                  *Buffer,
  IN  BOOLEAN               IsWrite
  )
{
  EFI_STATUS Status;
  UINT32 Response;
  UINT32 Address;
  UINTN BlockCount;
  UINT8 Command;
  
  if (Private->SdMmcPassThru == NULL) {
    return EFI_UNSUPPORTED;
  }
  
  // Calculate address based on card type
  if (Private->CardType == CARD_TYPE_SD_V2_HC) {
    // High capacity cards use block addressing
    Address = (UINT32)Lba;
  } else {
    // Standard capacity cards use byte addressing
    Address = (UINT32)(Lba * SD_BLOCK_SIZE);
  }
  
  // Calculate number of blocks
  BlockCount = BufferSize / SD_BLOCK_SIZE;
  
  if (BlockCount > 1) {
    // Multi-block transfer
    Command = IsWrite ? SD_CMD25_WRITE_MULTIPLE_BLOCK : SD_CMD18_READ_MULTIPLE_BLOCK;
  } else {
    // Single block transfer
    Command = IsWrite ? SD_CMD24_WRITE_BLOCK : SD_CMD17_READ_SINGLE_BLOCK;
  }
  
  // Send read/write command
  Status = SdCardSendCommandHost(Private, Command, Address, &Response);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "SdCardHost: %s command failed - %r\n", 
           IsWrite ? "Write" : "Read", Status));
    return Status;
  }
  
  // For multi-block reads, we need to send CMD12 to stop transmission
  if (BlockCount > 1 && !IsWrite) {
    // Read all blocks first, then send stop command
    // Note: Actual data transfer implementation depends on the host controller
    
    Status = SdCardSendCommandHost(Private, SD_CMD12_STOP_TRANSMISSION, 0, &Response);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_WARN, "SdCardHost: CMD12 failed - %r\n", Status));
    }
  }
  
  // For multi-block writes, the card will automatically stop after the specified number of blocks
  // or when it receives the stop transmission token
  
  return Status;
}

/**
  Handles hotplug events in host mode.
  @param[in] Private  SD card private data
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
HandleHotplugHost (
  IN SD_CARD_PRIVATE_DATA  *Private
  )
{
  // TODO: Implement hotplug detection for host mode
  // This would typically involve checking card detect pins or controller status
  
  DEBUG((DEBUG_INFO, "SdCardHost: Hotplug handling not implemented\n"));
  return EFI_UNSUPPORTED;
}

/**
  Gets the current card status in host mode.
  @param[in] Private  SD card private data
  @param[out] Status  Card status
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
GetCardStatusHost (
  IN  SD_CARD_PRIVATE_DATA  *Private,
  OUT UINT32                *Status
  )
{
  if (Private->SdMmcPassThru == NULL || Status == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  // Send CMD13 to get card status
  return SdCardSendCommandHost(Private, SD_CMD13_SEND_STATUS, Private->Rca << 16, Status);
}

/**
  Sets the bus width for higher performance.
  @param[in] Private  SD card private data
  @param[in] Width    Bus width (1, 4, or 8 bits)
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
SetBusWidthHost (
  IN SD_CARD_PRIVATE_DATA  *Private,
  IN UINT8                 Width
  )
{
  EFI_STATUS Status;
  UINT32 Response;
  
  if (Width != 1 && Width != 4 && Width != 8) {
    return EFI_INVALID_PARAMETER;
  }
  
  // Send CMD55 (APP_CMD) first
  Status = SdCardSendCommandHost(Private, SD_CMD55_APP_CMD, Private->Rca << 16, &Response);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  
  // Send ACMD6 to set bus width
  UINT32 Arg = 0;
  if (Width == 4) {
    Arg = 2; // 4-bit mode
  } else if (Width == 8) {
    Arg = 3; // 8-bit mode (eMMC only)
  } else {
    Arg = 0; // 1-bit mode
  }
  
  Status = SdCardSendCommandHost(Private, SD_ACMD6_SET_BUS_WIDTH, Arg, &Response);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  
  DEBUG((DEBUG_INFO, "SdCardHost: Bus width set to %d bits\n", Width));
  return EFI_SUCCESS;
}

/**
  Sets the bus speed for higher performance.
  @param[in] Private  SD card private data
  @param[in] Speed    Bus speed in Hz
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
SetBusSpeedHost (
  IN SD_CARD_PRIVATE_DATA  *Private,
  IN UINT32                Speed
  )
{
  // Bus speed configuration is controller-specific
  // This would typically involve programming the host controller's clock divider
  
  DEBUG((DEBUG_INFO, "SdCardHost: Bus speed setting not implemented\n"));
  return EFI_UNSUPPORTED;
}

/**
  Handles error recovery in host mode.
  @param[in] Private  SD card private data
  @param[in] Status   Error status to recover from
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
ErrorRecoveryHost (
  IN SD_CARD_PRIVATE_DATA  *Private,
  IN EFI_STATUS            Status
  )
{
  // Simple error recovery: reset the card and reinitialize
  DEBUG((DEBUG_INFO, "SdCardHost: Attempting error recovery\n"));
  
  // Send CMD0 to reset the card
  UINT32 Response;
  EFI_STATUS ResetStatus = SdCardSendCommandHost(Private, SD_CMD0_GO_IDLE_STATE, 0, &Response);
  if (EFI_ERROR(ResetStatus)) {
    DEBUG((DEBUG_ERROR, "SdCardHost: Error recovery failed - %r\n", ResetStatus));
    return ResetStatus;
  }
  
  // Reinitialize the card
  return SdCardInitializeHost(Private);
}