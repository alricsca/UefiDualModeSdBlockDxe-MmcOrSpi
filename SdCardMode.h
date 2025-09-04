#ifndef __SD_CARD_MODE_H__
#define __SD_CARD_MODE_H__

#include "SdCardBlockIo.h"

// Mode detection and fallback functions
SD_CARD_MODE EFIAPI SdCardProbeMode(IN EFI_HANDLE ControllerHandle, IN BOOLEAN ForceSpi);
EFI_STATUS EFIAPI SdCardHandleModeFallback(IN SD_CARD_PRIVATE_DATA *Private, IN EFI_STATUS InitializationStatus);
BOOLEAN EFIAPI ValidateMode(IN EFI_HANDLE ControllerHandle, IN SD_CARD_MODE Mode);
CONST CHAR8* EFIAPI GetModeName(IN SD_CARD_MODE Mode);

#endif // __SD_CARD_MODE_H__