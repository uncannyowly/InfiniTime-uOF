#pragma once

#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    // Size of the running firmware image in the MCU's internal flash
    namespace FirmwareImage {
      // Bytes taken by code and initialised data
      uint32_t UsedBytes();
      // Bytes available to the application image
      uint32_t SlotBytes();
    }
  }
}
