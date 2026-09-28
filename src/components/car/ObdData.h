#pragma once

#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    // A snapshot of the OBD-II values the Car app shows. A real ELM327 transport and the
    // built-in simulator both fill this in, so the UI does not care where the numbers come from.
    struct ObdData {
      bool connected = false;   // true once a data source is producing values
      uint16_t speedKmh = 0;    // PID 0x0D
      uint16_t rpm = 0;         // PID 0x0C
      int16_t coolantTempC = 0; // PID 0x05
      int16_t intakeTempC = 0;  // PID 0x0F
      uint16_t mapKpa = 0;      // PID 0x0B, manifold absolute pressure
      uint8_t throttlePct = 0;  // PID 0x11
      uint16_t o2Millivolt = 0; // PID 0x14, bank 1 sensor 1
      uint16_t batteryMillivolt = 0; // ELM327 "AT RV"
      uint16_t barometerKpa = 101;   // PID 0x33, for boost/vacuum reference (falls back to 101)
    };

    // Interface so the Car app can be driven by the simulator now and a BLE ELM327 later.
    class ObdSource {
    public:
      virtual ~ObdSource() = default;
      // Advance the model / poll the dongle. Called from the app's refresh task.
      virtual void Update() = 0;
      virtual const ObdData& Current() const = 0;
    };
  }
}
