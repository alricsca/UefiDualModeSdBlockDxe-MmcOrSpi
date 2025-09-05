#include "SdCardBlockIo.h"
#include "DriverLib.h"
#include "HostIo.h"
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SdMmcPassThru.h>
#include <Protocol/SpiHc.h>

// Use these standardized implementations instead of multiple versions
/**
  Calculates CRC7 for SD card commands.
  Polynomial: x^7 + x^3 + 1 (0x89)
**/
UINT8
EFIAPI
SdCardCalculateCrc7 (
  IN CONST UINT8  *Data,
  IN UINTN        Length
  )
{
  UINT8 Crc = 0;
  UINTN i, j;

  for (i = 0; i < Length; i++) {
    Crc ^= Data[i];
    for (j = 0; j < 8; j++) {
      if (Crc & 0x80) {
        Crc = (UINT8)((Crc << 1) ^ 0x89);
      } else {
        Crc = (UINT8)(Crc << 1);
      }
    }
  }

  // SD protocol requires CRC7 shifted left by 1 with stop bit
  return (Crc << 1) | 0x01;
}

/**
  Calculates CRC16 for SD card data blocks.
  Polynomial: x^16 + x^12 + x^5 + 1 (0x1021)
**/
UINT16
EFIAPI
SdCardCalculateCrc16 (
  IN CONST UINT8  *Data,
  IN UINTN        Length
  )
{
  UINT16 Crc = 0;
  UINTN i, j;

  for (i = 0; i < Length; i++) {
    Crc ^= (UINT16)Data[i] << 8;
    for (j = 0; j < 8; j++) {
      if (Crc & 0x8000) {
        Crc = (UINT16)((Crc << 1) ^ 0x1021);
      } else {
        Crc = (UINT16)(Crc << 1);
      }
    }
  }

  return Crc;
}

/**
  Checks the Card Capacity Status (CCS) bit in the OCR register.
  @param[in] Ocr  OCR register value
  @return TRUE if the card is high capacity (SDHC/SDXC), FALSE otherwise
**/
BOOLEAN
EFIAPI
SdCardIsHighCapacityFromOcr (
  IN UINT32  Ocr
  )
{
  return (Ocr & OCR_CCS_BIT) != 0;
}

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
  )
{
  if (Buffer == NULL) {
    DEBUG((DEBUG_ERROR, "SdCardPackCommand: NULL buffer\n"));
    return;
  }
  
  Buffer[0] = (UINT8)(0x40 | (Cmd & 0x3F));  // Start bit (0) + transmission bit (1) + command index
  Buffer[1] = (UINT8)(Arg >> 24);            // Argument[31:24]
  Buffer[2] = (UINT8)(Arg >> 16);            // Argument[23:16]
  Buffer[3] = (UINT8)(Arg >> 8);             // Argument[15:8]
  Buffer[4] = (UINT8)(Arg & 0xFF);           // Argument[7:0]
  Buffer[5] = Crc;                           // CRC + end bit (1)
}

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
  )
{
  UINT32 CSize;
  UINT8 ReadBlLen;
  UINT16 CSizeMult;
  UINT64 BlockCount;
  UINT64 BlockLen;
  
  if (Csd == NULL || Capacity == NULL || BlockSize == NULL || IsHighCapacity == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  // Check CSD structure version
  UINT8 CsdStructure = (Csd[0] & 0xC0) >> 6;
  
  if (CsdStructure == 1) {
    // CSD Version 2.0 (for High Capacity cards)
    *IsHighCapacity = TRUE;
    
    // C_SIZE is 22 bits [69:48] in CSD
    CSize = ((Csd[7] & 0x3F) << 16) | (Csd[8] << 8) | Csd[9];
    
    // Capacity = (C_SIZE + 1) * 512K bytes
    *Capacity = (UINT64)(CSize + 1) * 512 * 1024;
    *BlockSize = 512; // Fixed block size for HC cards
    
    DEBUG((DEBUG_INFO, "SdCardParseCsdRegister: HC card, C_SIZE=%u, Capacity=%llu bytes\n", 
           CSize, *Capacity));
  } else if (CsdStructure == 0) {
    // CSD Version 1.0 (for Standard Capacity cards)
    *IsHighCapacity = FALSE;
    
    // READ_BL_LEN is 4 bits [83:80] in CSD (max read block length)
    ReadBlLen = Csd[5] & 0x0F;
    
    // C_SIZE is 12 bits [73:62] in CSD
    CSize = ((Csd[6] & 0x03) << 10) | (Csd[7] << 2) | ((Csd[8] & 0xC0) >> 6);
    
    // C_SIZE_MULT is 3 bits [49:47] in CSD
    CSizeMult = (UINT16)(((Csd[9] & 0x03) << 1) | ((Csd[10] & 0x80) >> 7));
    
    // Calculate capacity
    BlockLen = 1ULL << ReadBlLen;                    // Block length
    BlockCount = (UINT64)(CSize + 1) * (1ULL << (CSizeMult + 2)); // Number of blocks
    *Capacity = BlockCount * BlockLen;               // Total capacity
    
    // Standard capacity cards use variable block size, but we'll standardize to 512 bytes
    *BlockSize = 512;
    
    DEBUG((DEBUG_INFO, "SdCardParseCsdRegister: SC card, C_SIZE=%u, C_SIZE_MULT=%u, READ_BL_LEN=%u, Capacity=%llu bytes\n", 
           CSize, CSizeMult, ReadBlLen, *Capacity));
  } else {
    DEBUG((DEBUG_ERROR, "SdCardParseCsdRegister: Unknown CSD structure version: %d\n", CsdStructure));
    return EFI_UNSUPPORTED;
  }
  
  return EFI_SUCCESS;
}

/**
 * Allocates and zero-initializes memory with debug logging.
 * @param[in] Size  Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 **/
VOID *
EFIAPI
SdCardAllocateZeroPool (
  IN UINTN  Size
)
{
  VOID *Buffer = AllocateZeroPool(Size);
  
  if (Buffer == NULL) {
    DEBUG((DEBUG_ERROR, "SdCardAllocateZeroPool: Failed to allocate %u bytes\n", Size));
  } else {
    DEBUG((DEBUG_VERBOSE, "SdCardAllocateZeroPool: Allocated %u bytes at %p\n", Size, Buffer));
  }
  
  return Buffer;
}
/**
  Delays for a specified number of microseconds using the Stall function.
  @param[in] Microseconds  Number of microseconds to delay
**/
VOID
EFIAPI
SdCardMicroSecondDelay (
  IN UINTN  Microseconds
  )
{
  gBS->Stall(Microseconds);
}

/**
  Delays for a specified number of milliseconds using the Stall function.
  @param[in] Milliseconds  Number of milliseconds to delay
**/
VOID
EFIAPI
SdCardMilliSecondDelay (
  IN UINTN  Milliseconds
  )
{
  gBS->Stall(Milliseconds * 1000);
}

/**
  Converts a frequency in Hz to the closest SD clock divisor value.
  @param[in] BaseFrequency  Base frequency of the controller
  @param[in] TargetFrequency  Desired frequency
  @return Clock divisor value
**/
UINT32
EFIAPI
SdCardCalculateClockDivisor (
  IN UINT32  BaseFrequency,
  IN UINT32  TargetFrequency
  )
{
  if (TargetFrequency == 0 || TargetFrequency >= BaseFrequency) {
    return 0; // Use base frequency
  }
  
  UINT32 Divisor = (BaseFrequency + TargetFrequency - 1) / TargetFrequency;
  
  // Round up to nearest even divisor (required by some controllers)
  if (Divisor % 2 != 0) {
    Divisor++;
  }
  
  return Divisor;
}

/**
  Checks if a buffer is properly aligned for DMA.
  @param[in] Buffer  Buffer to check
  @param[in] Alignment  Required alignment
  @return TRUE if buffer is properly aligned, FALSE otherwise
**/
BOOLEAN
EFIAPI
SdCardIsBufferAligned (
  IN VOID    *Buffer,
  IN UINTN   Alignment
  )
{
  return ((UINTN)Buffer & (Alignment - 1)) == 0;
}

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
  )
{
  if (BounceBuffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  *BounceBuffer = NULL;
  
  if (SdCardIsBufferAligned(OriginalBuffer, Alignment)) {
    // Buffer is already aligned, no bounce buffer needed
    return EFI_SUCCESS;
  }
  
  // Allocate aligned bounce buffer
  *BounceBuffer = AllocatePool(BufferSize + Alignment - 1);
  if (*BounceBuffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  
  // Align the buffer
  *BounceBuffer = (VOID*)(((UINTN)*BounceBuffer + Alignment - 1) & ~(Alignment - 1));
  
  return EFI_SUCCESS;
}

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
  )
{
  if (Direction) {
    // Copy to bounce buffer (for writes)
    CopyMem(BounceBuffer, OriginalBuffer, BufferSize);
  } else {
    // Copy from bounce buffer (for reads)
    CopyMem(OriginalBuffer, BounceBuffer, BufferSize);
  }
}

/**
  Frees a bounce buffer.
  @param[in] BounceBuffer  Bounce buffer to free
**/
VOID
EFIAPI
SdCardFreeBounceBuffer (
  IN VOID  *BounceBuffer
  )
{
  if (BounceBuffer != NULL) {
    FreePool(BounceBuffer);
  }
}


