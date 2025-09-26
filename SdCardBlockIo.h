#ifndef SD_CARD_Block_IO_H_
#define SD_CARD_Block_IO_H_
#include <Uefi.h>
#include <Protocol/SpiHc.h>
#include <Protocol/SdMmcPassThru.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/BlockIo.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include "SdCardDxe.h"
/**
  @file
  SD Card Driver Header - Provides definitions for SD card operations in both
  MMC host mode and SPI mode.

  Copyright (c) 2023, Your Company. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Specification Reference:
  - SD Physical Layer Specification Version 7.10
  - SDIO Card Specification Version 3.00
  - UEFI Specification Version 2.8
**/

#pragma pack(1)

// SD Command Definitions
#define CMD0 0    ///< GO_IDLE_STATE - Reset the card to idle state
#define CMD1 1    ///< SEND_OP_COND (MMC) - Send operating condition
#define CMD2 2    ///< ALL_SEND_CID - Ask all cards to send CID
#define CMD3 3    ///< SEND_RELATIVE_ADDR - Ask card to publish RCA
#define CMD6 6    ///< SWITCH_FUNC - Switch card function
#define CMD7 7    ///< SELECT/DESELECT_CARD - Select/deselect card by RCA
#define CMD8 8    ///< SEND_IF_COND - Send interface condition
#define CMD9 9    ///< SEND_CSD - Send CSD data
#define CMD10 10  ///< SEND_CID - Send CID data
#define CMD12 12  ///< STOP_TRANSMISSION - Stop multi-block transfer
#define CMD13 13  ///< SEND_STATUS - Send card status
#define CMD16 16  ///< SET_BLOCKLEN - Set block length
#define CMD17 17  ///< READ_SINGLE_BLOCK - Read single block
#define CMD18 18  ///< READ_MULTIPLE_BLOCK - Read multiple blocks
#define CMD23 23  ///< SET_BLOCK_COUNT (MMC) - Set block count
#define CMD24 24  ///< WRITE_BLOCK - Write single block
#define CMD25 25  ///< WRITE_MULTIPLE_BLOCK - Write multiple blocks
#define CMD32 32  ///< ERASE_WR_BLK_START - Set erase start address
#define CMD33 33  ///< ERASE_WR_BLK_END - Set erase end address
#define CMD38 38  ///< ERASE - Erase selected blocks
#define CMD55 55  ///< APP_CMD - Prefix for application commands
#define CMD58 58  ///< READ_OCR - Read OCR register
#define CMD59 59  ///< CRC_ON_OFF - Turn CRC checking on/off
#define ACMD41 41 ///< SD_APP_OP_COND - Send operating condition (SD)
#define ACMD23 23 ///< SET_WR_BLK_ERASE_COUNT (SD) - Set pre-erase count

// Response Types
#define R1 1  ///< Normal response command
#define R1B 2 ///< Response with busy
#define R2 3  ///< CID, CSD register
#define R3 4  ///< OCR register
#define R6 5  ///< Published RCA response
#define R7 6  ///< Card interface condition

// UHS-I Modes
typedef enum
{
  SDR12 = 0, ///< Default Speed (0-12.5MHz)
  SDR25,     ///< High Speed (0-25MHz)
  SDR50,     ///< SDR50 (0-50MHz)
  SDR104,    ///< SDR104 (0-104MHz)
  DDR50,     ///< DDR50 (0-50MHz DDR)
  UHS_MODE_MAX
} UHS_MODE;

// Power Management States
typedef enum
{
  POWER_OFF = 0,
  POWER_ON,
  POWER_LOW,
  POWER_SUSPEND
} POWER_STATE;

// CSD Structure
typedef struct
{
  UINT8 CSDStructure;
  UINT8 TAAC;
  UINT8 NSAC;
  UINT8 TRAN_SPEED;
  UINT16 CCC;
  UINT8 READ_BL_LEN;
  UINT8 READ_BL_PARTIAL;
  UINT8 WRITE_BLK_MISALIGN;
  UINT8 READ_BLK_MISALIGN;
  UINT8 DSR_IMP;
  UINT16 C_SIZE;
  UINT8 VDD_R_CURR_MIN;
  UINT8 VDD_R_CURR_MAX;
  UINT8 VDD_W_CURR_MIN;
  UINT8 VDD_W_CURR_MAX;
  UINT8 C_SIZE_MULT;
  UINT8 ERASE_BLK_EN;
  UINT8 SECTOR_SIZE;
  UINT8 WP_GRP_SIZE;
  UINT8 WP_GRP_ENABLE;
  UINT8 R2W_FACTOR;
  UINT8 WRITE_BL_LEN;
  UINT8 WRITE_BL_PARTIAL;
  UINT8 FILE_FORMAT_GRP;
  UINT8 COPY;
  UINT8 PERM_WRITE_PROTECT;
  UINT8 TMP_WRITE_PROTECT;
  UINT8 FILE_FORMAT;
} SD_CSD;

// CID Structure
typedef struct
{
  UINT8 ManufacturerID;
  CHAR8 OEM_AppID[2];
  CHAR8 ProductName[5];
  UINT8 ProductRevision;
  UINT32 ProductSerialNumber;
  UINT8 ManufacturingYear;
  UINT8 ManufacturingMonth;
} SD_CID;

// SCR Structure
typedef struct
{
  UINT8 SCR_Structure;
  UINT8 SD_Spec;
  UINT8 DataStatAfterErase;
  UINT8 SD_Security;
  UINT8 SD_Bus_Widths;
  UINT8 SD_Spec3;
  UINT8 EX_Security;
  UINT8 SD_Spec4;
  UINT8 CMD_SUPPORT;
} SD_SCR;

#pragma pack()

EFI_STATUS
EFIAPI
SdCardParseCsdRegister(
    IN CONST UINT8 *Csd,
    OUT UINT64 *Capacity,
    OUT UINT32 *BlockSize,
    OUT BOOLEAN *IsHighCapacity);
/**
  Implementation of EFI_BLOCK_IO_PROTOCOL.ReadBlocks()

  @param[in]  This        Pointer to the Block I/O protocol instance
  @param[in]  MediaId     ID of the media, changes every time the media is replaced
  @param[in]  Lba         Starting Logical Block Address to read from
  @param[in]  BufferSize  Size of Buffer, must be a multiple of device block size
  @param[out] Buffer      Pointer to the destination buffer for the data

  @retval EFI_SUCCESS           The data was read correctly from the device
  @retval EFI_DEVICE_ERROR      The device reported an error while reading
  @retval EFI_NO_MEDIA          There is no media in the device
  @retval EFI_MEDIA_CHANGED     The MediaId does not match the current device
  @retval EFI_BAD_BUFFER_SIZE   BufferSize is not a multiple of the block size
  @retval EFI_INVALID_PARAMETER The read request contains an invalid LBA
**/
EFI_STATUS
EFIAPI
SdCardBlockIoReadBlocks(
    IN EFI_BLOCK_IO_PROTOCOL *This,
    IN UINT32 MediaId,
    IN EFI_LBA Lba,
    IN UINTN BufferSize,
    OUT VOID *Buffer);
#endif // SD_CARD_Block_IO_H_