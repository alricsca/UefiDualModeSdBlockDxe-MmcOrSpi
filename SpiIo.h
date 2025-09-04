#ifndef SPI_IO_H_
#define SPI_IO_H_

#include "SdCardDxe.h"

// SPI-Mode SD/MMC Commands - based on specs.json
#define CMD0    0   // GO_IDLE_STATE
#define CMD1    1   // SEND_OP_COND (for MMC)
#define CMD8    8   // SEND_IF_COND (for SDv2)
#define CMD9    9   // SEND_CSD
#define CMD10   10  // SEND_CID
#define CMD12   12  // STOP_TRANSMISSION
#define CMD16   16  // SET_BLOCKLEN
#define CMD17   17  // READ_SINGLE_BLOCK
#define CMD18   18  // READ_MULTIPLE_BLOCK
#define CMD23   23  // SET_BLOCK_COUNT (for MMC)
#define CMD24   24  // WRITE_BLOCK
#define CMD25   25  // WRITE_MULTIPLE_BLOCK
#define ACMD23  23  // SET_WR_BLOCK_ERASE_COUNT (for SD)

#define ACMD41  41  // APP_SEND_OP_COND (for SD)
#define CMD55   55  // APP_CMD
#define CMD58   58  // READ_OCR
#define CMD59   59  // CRC_ON_OFF

/**
  Calculates CRC16 for SD card data blocks.
  Polynomial: x^16 + x^12 + x^5 + 1 (0x1021)
  @param[in] Data    Data to calculate CRC for
  @param[in] Length  Length of data
  @return CRC16 value
**/
UINT16
EFIAPI
SdCardCalculateCrc16 (
  IN CONST UINT8  *Data,
  IN UINTN        Length
  );



// Command Arguments
#define CMD8_ARG_V2     0x000001AA  // Argument for CMD8 for v2 cards
#define CMD8_CHECK_PATTERN 0xAA     // Expected check pattern in R7 response
#define ACMD41_ARG_HCS  0x40000000  // HCS bit for ACMD41

// R1 Response Bits
#define R1_RESPONSE_RECV        BIT7 // Top bit must be 0

// OCR Register Bits
#define OCR_POWER_UP_STATUS     BIT31 // Power up status

#ifndef OCR_CCS_BIT
#define OCR_CCS_BIT             (1 << 6) // CCS bit for SDHC/SDXC detection
#endif

// Data Tokens
#define DATA_TOKEN_READ_START       0xFE
#define DATA_TOKEN_WRITE_SINGLE    0xFE
#define DATA_TOKEN_WRITE_MULTI     0xFC
#define DATA_TOKEN_WRITE_MULTI_STOP 0xFD

// Data Response Packet (after a write)
#define DATA_RESP_MASK          0x1F
#define DATA_RESP_ACCEPTED      0x05
#define DATA_RESP_CRC_ERROR     0x0B
#define DATA_RESP_WRITE_ERROR   0x0D

// Misc Constants

// CSD register size
#define CSD_REGISTER_SIZE      16

// CID register size
#define CID_REGISTER_SIZE       16

// Standard block size
#define SD_BLOCK_SIZE           512

// Maximum SPI clock frequency during initialization (400 kHz)
#define SPI_INIT_CLOCK_HZ       400000

// Maximum SPI clock frequency during data transfer (up to 25 MHz for SD)
#define SPI_MAX_CLOCK_HZ        25000000

// Maximum retries for waiting operations
#define MAX_WAIT_RETRIES        1000000

// Timeout values (in microseconds)
#define CMD_TIMEOUT_US          100000
#define READ_TIMEOUT_US         300000
#define WRITE_TIMEOUT_US        600000

// R1 Response Flags

// SPI internal helpers (prototypes)
EFI_STATUS EFIAPI SdCardSendCommandSpi(SD_CARD_PRIVATE_DATA *Private, UINT8 Command, UINT32 Argument, UINT8 *Response);
EFI_STATUS EFIAPI SdCardWaitNotBusySpi(SD_CARD_PRIVATE_DATA *Private);
EFI_STATUS EFIAPI SdCardReadDataBlockSpi(SD_CARD_PRIVATE_DATA *Private, UINTN Length, UINT8 *Buffer);
EFI_STATUS EFIAPI SdCardWriteDataBlockSpi(SD_CARD_PRIVATE_DATA *Private, UINT8 Token, UINTN Length, CONST UINT8 *Buffer);
EFI_STATUS EFIAPI SdCardParseCsdSpi(SD_CARD_PRIVATE_DATA *Private, UINT8 *Csd);
EFI_STATUS EFIAPI SdCardInitializeSpi(SD_CARD_PRIVATE_DATA *Private);
EFI_STATUS EFIAPI SdCardExecuteReadWriteSpi(SD_CARD_PRIVATE_DATA *Private, EFI_LBA Lba, UINTN BufferSize, VOID *Buffer, BOOLEAN IsWrite);

#endif // SPI_IO_H_
