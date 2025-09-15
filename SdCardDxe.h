#ifndef __SD_CARD_DXE_H__
#define __SD_CARD_DXE_H__

#include <Uefi.h>
#include <Protocol/SpiConfiguration.h>  // Add this include
#include <Protocol/BlockIo.h>
#include <Protocol/SpiHc.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/SdMmcPassThru.h>
#include <Protocol/DevicePath.h>
#include <Protocol/ComponentName2.h>
#include <Library/BaseLib.h>
#include "SdCardBlockIo.h"

// Define SD Card Device Path GUID
extern EFI_GUID gSdCardDevicePathGuid;

typedef enum {
  SD_CARD_MODE_UNKNOWN,
  SD_CARD_MODE_HOST,
  SD_CARD_MODE_SPI
} SD_CARD_MODE;

typedef enum {
  CARD_TYPE_UNKNOWN,
  CARD_TYPE_SD_V1,
  CARD_TYPE_SD_V2_SC,
  CARD_TYPE_SD_V2_HC,
  CARD_TYPE_MMC
} CARD_TYPE;

#define SD_CARD_PRIVATE_DATA_SIGNATURE SIGNATURE_32('s', 'd', 'c', 'd')

typedef struct _SD_CARD_PRIVATE_DATA {
  // Driver and Device Identification
  UINT32                            Signature;          // Structure signature for validation
  EFI_DRIVER_BINDING_PROTOCOL       *DriverBinding;     // Driver binding protocol instance
  EFI_HANDLE                        Handle;             // Device handle
  EFI_DEVICE_PATH_PROTOCOL          *DevicePath;        // Device path for this card

  // Card Configuration and State
  SD_CARD_MODE                      Mode;               // Operation mode (SPI or MMC)
  CARD_TYPE                         CardType;           // Type of card (SDSC, SDHC, SDXC, etc.)
  BOOLEAN                           IsHighCapacity;     // TRUE for SDHC/SDXC cards
  BOOLEAN                           IsInitialized;      // Card initialization status
  UINT16                            Rca;                // Relative Card Address
  
  // Bus Configuration
  UINT32                            MaxClockHz;         // Maximum supported clock frequency
  UINT32                            CurrentClockHz;     // Current operational clock frequency
  UINT8                             BusWidth;           // Data bus width (1, 2, or 4 bits)

  // Card Registers
  UINT8                             Csd[16];            // Card-Specific Data register
  UINT8                             Cid[16];            // Card Identification register
  UINT8                             Ocr[4];             // Operation Conditions register
  UINT8                             Scr[8];             // SD Configuration register

  // Capacity Information
  UINT64                            CapacityInBytes;    // Total card capacity in bytes
  UINT32                            BlockSize;          // Block size (typically 512 bytes)
  EFI_LBA                           LastBlock;          // Last logical block address

  // SPI Mode Specific Configuration
  UINT8                             SpiChipSelect;      // SPI chip select line
  UINT8                             SpiMode;            // SPI mode (0, 1, 2, or 3)
  UINT8                             SpiReadDummyCycles; // Dummy cycles for read operations
  UINT8                             SpiWriteDummyCycles;// Dummy cycles for write operations
  
  // SPI Command Opcodes
  UINT8                             SpiReadCommand;     // Read command opcode
  UINT8                             SpiWriteCommand;    // Write command opcode
  UINT8                             SpiReadStatusCommand; // Read status command opcode
  UINT8                             SpiEraseCommand;    // Erase command opcode
  UINT8                             SpiAppCommand;      // Application command prefix
  UINT8                             SpiAppOpCommand;    // Application operation command

  // SPI Transfer Buffers
  UINT8                             SpiCommandBuffer[6];      // Command buffer (6 bytes)
  UINT8                             SpiResponseBuffer[16];    // Response buffer
  UINT8                             SpiDataBuffer[512 + 2];   // Data buffer + CRC

  // SPI Transfer Settings
  UINT32                            SpiTransferTimeout; // Transfer timeout in microseconds
  UINT32                            SpiMaxRetries;      // Maximum retry attempts

  // Protocol Instances
  EFI_SD_MMC_PASS_THRU_PROTOCOL     *SdMmcPassThru;     // SD/MMC PassThru protocol
  EFI_SPI_HC_PROTOCOL               *SpiHcProtocol;     // SPI Host Controller protocol
  EFI_SPI_PERIPHERAL                *SpiPeripheral;     // SPI Peripheral instance

  // Block I/O Protocol
  EFI_BLOCK_IO_PROTOCOL             BlockIo;            // Block I/O protocol instance
  EFI_BLOCK_IO_MEDIA                BlockMedia;         // Block I/O media information

} SD_CARD_PRIVATE_DATA;

#define SD_CARD_PRIVATE_DATA_FROM_BLOCK_IO(a) \
  CR (a, SD_CARD_PRIVATE_DATA, BlockIo, SD_CARD_PRIVATE_DATA_SIGNATURE)

extern EFI_GUID gSdCardDevicePathGuid;
extern EFI_COMPONENT_NAME2_PROTOCOL gSdCardComponentName2;

//
// Function prototypes
//
EFI_STATUS
EFIAPI
SdCardInitialize (
  IN SD_CARD_PRIVATE_DATA *Private
  );

EFI_STATUS
EFIAPI
SdCardExecuteReadWrite (
  IN  SD_CARD_PRIVATE_DATA  *Private,
  IN  EFI_LBA               Lba,
  IN  UINTN                 BufferSize,
  IN  VOID                  *Buffer,
  IN  BOOLEAN               IsWrite
  );

//
// Media functions from SdCardMedia.c
//
EFI_STATUS
EFIAPI
SdCardMediaReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                ExtendedVerification
  );

EFI_STATUS
EFIAPI
SdCardMediaReadBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  OUT VOID                  *Buffer
  );

EFI_STATUS
EFIAPI
SdCardMediaWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                   *Buffer
  );

EFI_STATUS
EFIAPI
SdCardMediaFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  );

// Component Name Protocol functions
EFI_STATUS
EFIAPI
GetDriverName (
  IN  EFI_COMPONENT_NAME2_PROTOCOL  *This,
  IN  CHAR8                         *Language,
  OUT CHAR16                        **DriverName
  );

EFI_STATUS
EFIAPI
GetControllerName (
  IN  EFI_COMPONENT_NAME2_PROTOCOL  *This,
  IN  EFI_HANDLE                    ControllerHandle,
  IN  EFI_HANDLE                    ChildHandle OPTIONAL,
  IN  CHAR8                         *Language,
  OUT CHAR16                        **ControllerName
  );

extern EFI_DRIVER_BINDING_PROTOCOL gSdCardDriverBinding;

#endif // __SD_CARD_DXE_H__