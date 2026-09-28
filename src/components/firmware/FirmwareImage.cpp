#include "components/firmware/FirmwareImage.h"

// Absolute symbols defined in nrf_common.ld: their address is their value
extern "C" {
extern const uint8_t TotalFlashUsed[];
extern const uint8_t TotalFlashSize[];
}

using namespace Pinetime::Controllers;

uint32_t FirmwareImage::UsedBytes() {
  return reinterpret_cast<uint32_t>(TotalFlashUsed);
}

uint32_t FirmwareImage::SlotBytes() {
  return reinterpret_cast<uint32_t>(TotalFlashSize);
}
