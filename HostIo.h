#ifndef HOST_IO_H_
#define HOST_IO_H_

#include "SdCardBlockIo.h"
#include "SdCardDxe.h"
#include <Library/BaseLib.h>

//
// SD Command definitions
//
#define SD_CMD0_GO_IDLE_STATE           0
#define SD_CMD2_ALL_SEND_CID            2
#define SD_CMD3_SEND_RELATIVE_ADDR      3
#define SD_CMD7_SELECT_DESELECT_CARD    7
#define SD_CMD8_SEND_IF_COND            8
#define SD_CMD9_SEND_CSD                9
#define SD_CMD12_STOP_TRANSMISSION      12
#define SD_CMD13_SEND_STATUS            13
#define SD_CMD16_SET_BLOCKLEN           16
#define SD_CMD17_READ_SINGLE_BLOCK      17
#define SD_CMD18_READ_MULTIPLE_BLOCK    18
#define SD_CMD24_WRITE_BLOCK            24
#define SD_CMD25_WRITE_MULTIPLE_BLOCK   25
#define SD_ACMD41_SD_SEND_OP_COND       41
#define SD_CMD55_APP_CMD                55
#define SD_CMD58_READ_OCR               58
#define SD_ACMD6_SET_BUS_WIDTH          6

//
// SD Command arguments and response bits
//
#define SD_CHECK_VOLTAGE_PATTERN    0x1AA
#define SD_HCS                      (1U << 30)
#define OCR_POWERUP_BIT             (1U << 31)
#define OCR_CCS_BIT                 (1U << 30) // Card Capacity Status bit
#define SD_BLOCK_SIZE               512
#define R1_IDLE_STATE               (1 << 0)
#define R1_ERASE_RESET              (1 << 1)
#define R1_ILLEGAL_COMMAND          (1 << 2)
#define R1_COM_CRC_ERROR            (1 << 3)
#define R1_ERASE_SEQUENCE_ERROR     (1 << 4)
#define R1_ADDRESS_ERROR            (1 << 5)
#define R1_PARAMETER_ERROR          (1 << 6)

/**
  Initializes the SD card in Host mode.
**/
EFI_STATUS
EFIAPI
SdCardInitializeHost (
  IN SD_CARD_PRIVATE_DATA   *Private
  );

/**
  Host mode read/write function.
  
  @param[in]      Private     SD card private data
  @param[in]      Lba         Logical Block Address to read from/write to
  @param[in]      BufferSize  Size of the buffer in bytes
  @param[in,out]  Buffer      For write operations: pointer to data to write (IN)
                              For read operations: pointer to buffer for read data (OUT)
  @param[in]      IsWrite     TRUE for write operation, FALSE for read operation
  
  @retval EFI_SUCCESS           The operation completed successfully
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid
  @retval EFI_NO_MEDIA          The device has no media
  @retval EFI_DEVICE_ERROR      The device reported an error
**/
EFI_STATUS
EFIAPI
SdCardExecuteReadWriteHost (
  IN     SD_CARD_PRIVATE_DATA  *Private,
  IN     EFI_LBA               Lba,
  IN     UINTN                 BufferSize,
  IN OUT VOID                  *Buffer,
  IN     BOOLEAN               IsWrite
  );

/**
  Maps SD card specific error codes to EFI_STATUS values.
  @param[in] SdError  SD card error code from R1 response
  @return Corresponding EFI_STATUS value
**/
EFI_STATUS
EFIAPI
SdCardMapSdErrorToEfiStatus (
  IN UINT32  SdError
  );

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
  );

/**
  Handles hotplug events in host mode.
  @param[in] Private  SD card private data
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
HandleHotplugHost (
  IN SD_CARD_PRIVATE_DATA  *Private
  );

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
  );

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
  );

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
  );

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
  );

#endif // HOST_IO_H_