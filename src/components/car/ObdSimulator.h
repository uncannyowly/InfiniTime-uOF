#pragma once

#include "components/car/ObdData.h"

namespace Pinetime {
  namespace Controllers {
    // Produces a looping drive cycle (idle -> accelerate -> cruise -> decelerate) so the
    // Car app's gauges move without a real dongle. Swap this for a BLE ELM327 source later.
    class ObdSimulator : public ObdSource {
    public:
      void Update() override;
      const ObdData& Current() const override {
        return data;
      }

    private:
      ObdData data;
      uint32_t startTick = 0;
      uint32_t lastTick = 0;
      float warmup = 0.0f; // 0..1, coolant/intake warming to operating temperature
    };
  }
}
