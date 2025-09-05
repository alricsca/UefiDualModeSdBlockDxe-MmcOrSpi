
#ifndef __DRIVER_LIB_H__
#define __DRIVER_LIB_H__
#include <Uefi.h>
#include <Protocol/SpiHc.h>
#include "SdCardBlockIo.h"
#include <Library/BaseLib.h>      // For basic arithmetic operations
#include <Library/BaseMemoryLib.h> // For memory operations (SetMem, CopyMem)
#include <Library/DebugLib.h>     // For debug output
#include <Library/MemoryAllocationLib.h> // For memory allocation

//
// Forward declaration
//
typedef struct _SD_CARD_PRIVATE_DATA SD_CARD_PRIVATE_DATA;

//
// CRC Polynomial Definitions
//
#define CRC7_POLYNOMIAL          0x89  // x^7 + x^3 + 1
#define CRC16_POLYNOMIAL         0x1021 // x^16 + x^12 + x^5 + 1

//
// Memory Allocation Constants
//
#define SD_CARD_MEMORY_TAG       SIGNATURE_32('S','D','C','D')

//
// Alignment Requirements
//
#define SD_CARD_DMA_ALIGNMENT    4
#define SD_CARD_SPI_ALIGNMENT    1

//
// OCR Register Bits
//
#define OCR_POWER_UP_STATUS_BIT  (1 << 31)
#define OCR_VDD_VOLTAGE_WINDOW   0xFF8000

//
// R1 Response Bits
//
#define R1_IDLE_STATE            (1 << 0)
#define R1_ERASE_RESET           (1 << 1)
#define R1_ILLEGAL_COMMAND       (1 << 2)
#define R1_COM_CRC_ERROR         (1 << 3)
#define R1_ERASE_SEQUENCE_ERROR  (1 << 4)
#define R1_ADDRESS_ERROR         (1 << 5)
#define R1_PARAMETER_ERROR       (1 << 6)

/**
  Checks the Card Capacity Status (CCS) bit in the OCR register.
  @param[in] Ocr  OCR register value
  @return TRUE if the card is high capacity (SDHC/SDXC), FALSE otherwise
**/
BOOLEAN
EFIAPI
SdCardIsHighCapacityFromOcr (
  IN UINT32  Ocr
  );

/**
  Packs an SD command, argument, and CRC into a 6-byte buffer.
  @param[in]  Cmd     Command index
  @param[in]  Arg     Command argument
  @param[in]  Crc     CRC value
  @param[out] Buffer  Output buffer (must be at least 6 bytes)
**/
VOID
EFIAPI
SdCardPackCommand (
  IN UINT8   Cmd,
  IN UINT32  Arg,
  IN UINT8   Crc,
  OUT UINT8  *Buffer
  );

/**
  Parses the CSD register to extract capacity and other parameters.
  @param[in]  Csd            CSD register data (16 bytes)
  @param[out] Capacity       Extracted capacity in bytes
  @param[out] BlockSize      Extracted block size
  @param[out] IsHighCapacity TRUE if card is high capacity
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
SdCardParseCsdRegister (
  IN  CONST UINT8  *Csd,
  OUT UINT64       *Capacity,
  OUT UINT32       *BlockSize,
  OUT BOOLEAN      *IsHighCapacity
  );

/**
  Calculates CRC7 for SD card commands.
  Polynomial: x^7 + x^3 + 1 (0x89)
  @param[in] Data    Data to calculate CRC for
  @param[in] Length  Length of data
  @return CRC7 value
**/
UINT8
EFIAPI
SdCardCalculateCrc7 (
  IN CONST UINT8  *Data,
  IN UINTN        Length
  );

/**
  Allocates and zero-initializes memory with debug logging.
  @param[in] Size  Number of bytes to allocate
  @return Pointer to allocated memory, or NULL on failure
**/
VOID *
EFIAPI
SdCardAllocateZeroPool (
  IN UINTN  Size
  );

/**
  Delays for a specified number of microseconds using the Stall function.
  @param[in] Microseconds  Number of microseconds to delay
**/
VOID
EFIAPI
SdCardMicroSecondDelay (
  IN UINTN  Microseconds
  );

/**
  Delays for a specified number of milliseconds using the Stall function.
  @param[in] Milliseconds  Number of milliseconds to delay
**/
VOID
EFIAPI
SdCardMilliSecondDelay (
  IN UINTN  Milliseconds
  );

/**
  Converts a frequency in Hz to the closest SD clock divisor value.
  @param[in] BaseFrequency    Base frequency of the controller
  @param[in] TargetFrequency  Desired frequency
  @return Clock divisor value
**/
UINT32
EFIAPI
SdCardCalculateClockDivisor (
  IN UINT32  BaseFrequency,
  IN UINT32  TargetFrequency
  );

/**
  Checks if a buffer is properly aligned for DMA.
  @param[in] Buffer    Buffer to check
  @param[in] Alignment Required alignment
  @return TRUE if buffer is properly aligned, FALSE otherwise
**/
BOOLEAN
EFIAPI
SdCardIsBufferAligned (
  IN VOID    *Buffer,
  IN UINTN   Alignment
  );

/**
  Creates a bounce buffer if the original buffer is not properly aligned.
  @param[in]  OriginalBuffer  Original buffer
  @param[in]  BufferSize      Size of buffer
  @param[in]  Alignment       Required alignment
  @param[out] BounceBuffer    Pointer to bounce buffer
  @return EFI_STATUS
**/
EFI_STATUS
EFIAPI
SdCardCreateBounceBuffer (
  IN  VOID     *OriginalBuffer,
  IN  UINTN    BufferSize,
  IN  UINTN    Alignment,
  OUT VOID     **BounceBuffer
  );

/**
  Copies data to or from a bounce buffer.
  @param[in] Direction       Direction of copy (TRUE = to bounce, FALSE = from bounce)
  @param[in] OriginalBuffer  Original buffer
  @param[in] BounceBuffer    Bounce buffer
  @param[in] BufferSize      Size of buffer
**/
VOID
EFIAPI
SdCardHandleBounceBuffer (
  IN BOOLEAN  Direction,
  IN VOID     *OriginalBuffer,
  IN VOID     *BounceBuffer,
  IN UINTN    BufferSize
  );

/**
  Frees a bounce buffer.
  @param[in] BounceBuffer  Bounce buffer to free
**/
VOID
EFIAPI
SdCardFreeBounceBuffer (
  IN VOID  *BounceBuffer
  );

/**
  Handles mode fallback when initial mode initialization fails.
  @param[in] Private               SD card private data
  @param[in] InitializationStatus  Status from the initial mode initialization attempt
  @return EFI_STATUS indicating whether fallback was successful
**/
EFI_STATUS
EFIAPI
SdCardHandleModeFallback (
  IN SD_CARD_PRIVATE_DATA  *Private,
  IN EFI_STATUS            InitializationStatus
  );

  /**
  Calculates CRC7 for SD card commands.
  Polynomial: x^7 + x^3 + 1 (0x89)
  @param[in] Data    Data to calculate CRC for
  @param[in] Length  Length of data
  @return CRC7 value
**/
UINT8
EFIAPI
SdCardCalculateCrc7 (
  IN CONST UINT8  *Data,
  IN UINTN        Length
  );

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
#endif // __DRIVER_LIB_H__