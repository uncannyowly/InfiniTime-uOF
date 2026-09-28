#include "components/car/ObdSimulator.h"

#include <cmath>
#include <lvgl/lvgl.h>

using namespace Pinetime::Controllers;

namespace {
  // Linear interpolation between keyframes of a repeating 48 second drive cycle.
  // Each entry: {time (s), throttle 0..1, target speed km/h, target rpm}
  struct Frame {
    float t;
    float throttle;
    float speed;
    float rpm;
  };

  constexpr Frame cycle[] = {
    {0.0f, 0.00f, 0.0f, 800.0f},    // idle
    {6.0f, 0.75f, 55.0f, 3400.0f},  // pull away
    {14.0f, 0.40f, 90.0f, 2600.0f}, // shift up, cruise builds
    {24.0f, 0.30f, 110.0f, 2400.0f},// highway cruise
    {30.0f, 0.90f, 150.0f, 4200.0f},// overtake
    {36.0f, 0.10f, 80.0f, 1800.0f}, // lift off
    {44.0f, 0.00f, 0.0f, 900.0f},   // brake to stop
    {48.0f, 0.00f, 0.0f, 800.0f},
  };
  constexpr float cycleLength = 48.0f;

  float Lerp(float a, float b, float f) {
    return a + (b - a) * f;
  }

  void Sample(float t, float& throttle, float& speed, float& rpm) {
    for (size_t i = 1; i < sizeof(cycle) / sizeof(cycle[0]); i++) {
      if (t <= cycle[i].t) {
        const float span = cycle[i].t - cycle[i - 1].t;
        const float f = span > 0 ? (t - cycle[i - 1].t) / span : 0.0f;
        throttle = Lerp(cycle[i - 1].throttle, cycle[i].throttle, f);
        speed = Lerp(cycle[i - 1].speed, cycle[i].speed, f);
        rpm = Lerp(cycle[i - 1].rpm, cycle[i].rpm, f);
        return;
      }
    }
    throttle = 0.0f;
    speed = 0.0f;
    rpm = 800.0f;
  }
}

void ObdSimulator::Update() {
  const uint32_t now = lv_tick_get();
  if (startTick == 0) {
    startTick = now;
    lastTick = now;
  }
  const float elapsed = (now - lastTick) / 1000.0f;
  lastTick = now;

  const float t = std::fmod((now - startTick) / 1000.0f, cycleLength);
  float throttle, speed, rpm;
  Sample(t, throttle, speed, rpm);

  data.connected = true;
  data.speedKmh = static_cast<uint16_t>(speed + 0.5f);
  data.rpm = static_cast<uint16_t>(rpm + 0.5f);
  data.throttlePct = static_cast<uint8_t>(throttle * 100.0f + 0.5f);

  // Manifold pressure: high vacuum at closed throttle, near/above atmospheric under load.
  // Model a mildly boosted engine so the boost gauge reaches positive pressure.
  const float atmospheric = 101.0f;
  const float map = Lerp(35.0f, 145.0f, throttle) + (rpm / 6000.0f) * 15.0f;
  data.mapKpa = static_cast<uint16_t>(map + 0.5f);
  data.barometerKpa = static_cast<uint16_t>(atmospheric);

  // Temperatures warm from ambient to operating temperature over the first ~90 s.
  warmup += elapsed / 90.0f;
  if (warmup > 1.0f) {
    warmup = 1.0f;
  }
  data.coolantTempC = static_cast<int16_t>(Lerp(20.0f, 90.0f, warmup) + throttle * 6.0f);
  data.intakeTempC = static_cast<int16_t>(Lerp(20.0f, 42.0f, warmup) + throttle * 8.0f);

  // Narrow-band O2 sensor swings 0.1-0.9 V a few times a second while in closed loop.
  const float o2 = 0.45f + 0.35f * std::sin((now / 120.0f));
  data.o2Millivolt = static_cast<uint16_t>(o2 * 1000.0f);

  // Charging system voltage: ~14.2 V running, sagging a touch under high load.
  data.batteryMillivolt = static_cast<uint16_t>((14.2f - throttle * 0.4f) * 1000.0f);
}
