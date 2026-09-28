# Car BLE Adapter Link Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Car app shows live data from a real BLE ELM327 adapter (Vgate iCar Pro BLE 4.0), with CONNECT and DEVS buttons under the existing menu buttons.

**Architecture:** An `Elm327Session` speaks the adapter protocol with no BLE or LVGL dependency and is host-tested. `ObdBleLink` is the NimBLE central glue: it has static lifetime, and its header has no NimBLE types, so the simulator and host tests can supply their own `.cpp`. `ObdBleSource` moves bytes between the two on the display task. Two new system messages (`CarLinkBegin`, `CarLinkEnd`) hand the single BLE connection between the phone and the car without touching the saved Bluetooth setting.

**Tech Stack:** C++20, InfiniTime 1.16.1 (FreeRTOS, LVGL 7, NimBLE host), littlefs, g++ host tests, InfiniSim.

**Spec:** `docs/superpowers/specs/2026-09-28-car-ble-adapter-design.md`

## Global Constraints

- **Adapter UUIDs:** Vgate service `E7810A71-73AE-499D-8C15-FAA9AEF0C3F2` with one write+notify characteristic `BEF8D6C9-9C21-4C9E-B632-BD58C1009F9F`. Fallback services `FFF0` and `FFE0`.
- **Startup commands**, in order: `ATE0`, `ATL0`, `ATS0`, `ATH0`, `ATSP0`, then `0100`. Replies end with `>`.
- **Upstream core files touched:** only `src/systemtask/Messages.h` and `src/systemtask/SystemTask.cpp`. `Settings.h` must not change.
- **NimBLE headers** are included only by `src/components/car/ObdBleLink.cpp`. Include them the way the rest of InfiniTime does:
  ```cpp
  #define min // workaround: nimble's min/max macros conflict with libstdc++
  #define max
  #include <host/ble_gap.h>
  // ...
  #undef max
  #undef min
  ```
- **Radio handover:** the car takes the radio only while the Car app is open. Leaving the app always disconnects and sends `CarLinkEnd`.
- **Screen:** `System::WakeLock` is held exactly while the link state is `Linked`.
- **Saved adapter:** stored in `/car/adapter.dat`.
- **Status text:** at most 20 characters. Adapter names are truncated to fit.
- **Copy:** no em dashes or en dashes anywhere, including commits, docs and UI strings.
- **Version:** do not change `FORK_REVISION`. Builds are `1.16.1+uo2.dev<N>`.
- **InfiniSim** changes live under `.deps/InfiniSim` and are not committed to the fork.
- **Commits** end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

### Commands used throughout

Host tests, run from the repo root. `<test>` and `<sources>` vary per task:

```bash
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -Itests tests/car/<test>.cpp <sources> -o /tmp/<test> && /tmp/<test>
```

Firmware build, using a fresh directory because the app list is a CMake cache variable:

```bash
V=/home/shoots/.venvs/ptos-build
export PATH="$V/bin:$PWD/.deps/node_modules/.bin:$PATH"
rm -rf build-ble
cmake -S . -B build-ble -DCMAKE_BUILD_TYPE=Release \
  -DARM_NONE_EABI_TOOLCHAIN_PATH=$PWD/.deps/gcc-arm-none-eabi-10.3-2021.10 \
  -DNRF5_SDK_PATH=$PWD/.deps/nRF5_SDK_15.3.0_59ac345 \
  -DBUILD_DFU=1 -DBUILD_RESOURCES=1 -DTARGET_DEVICE=PINETIME \
  -DPython3_EXECUTABLE=$V/bin/python
cmake --build build-ble --target pinetime-mcuboot-app -j"$(nproc)" 2>&1 | tail -5
grep "FLASH:" -r build-ble --include=*.log || true
```

After the first configure, rebuild with just the last `cmake --build` line.

## Review Focus

1. **Leaving the Car app while a connect is in flight.**
   - Risk: the adapter connection completes after the screen is gone.
   - Expected: it is torn down, and the phone gets the radio back.
   - Pinned by: Task 3 (a `CONNECT` event while not `Connecting` terminates the connection) and a Task 7 simulator scenario.
2. **Ignition switched off mid-drive.**
   - Risk: stale numbers shown as live.
   - Expected: after 6 consecutive failed PID replies the session goes to `EcuNotResponding` and `connected` becomes false.
   - Pinned by: a Task 1 test.
3. **A car that does not support PID `0x33` (barometric pressure).**
   - Risk: the Boost view asks for `0133` forever and never reads MAP.
   - Expected: `0133` is asked once per link, whatever the reply.
   - Pinned by: a Task 1 test.
4. **Replies split at any byte.**
   - Cases: `>` in a notification of its own, and `\r\n` line endings.
   - Expected: parsed identically.
   - Pinned by: Task 1 one-byte-chunk and CRLF tests.
5. **A corrupt, truncated or older-version `/car/adapter.dat`.**
   - Expected: treated as "no saved adapter", never a crash or a garbage address.
   - Pinned by: Task 2 codec tests.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/components/car/Elm327Session.{h,cpp}` (new) | ELM327 protocol state machine and PID parsing. No BLE or LVGL. |
| `src/components/car/SavedAdapter.{h,cpp}` (new) | The saved-adapter record and its byte encoding. No filesystem. |
| `src/components/car/AdapterStore.{h,cpp}` (new) | Load and save that record in `/car/adapter.dat` via `Controllers::FS`. |
| `src/components/car/ObdBleLink.h` (new) | Link interface and shared state. No NimBLE types. |
| `src/components/car/ObdBleLink.cpp` (new) | NimBLE implementation: scan, connect, discover, subscribe, write, receive. |
| `src/components/car/ObdBleSource.{h,cpp}` (new) | `ObdSource` combining the link and a session. Time comes from an injected clock. |
| `src/systemtask/Messages.h`, `SystemTask.cpp` (modify) | `CarLinkBegin` / `CarLinkEnd`. |
| `src/displayapp/screens/Car.{h,cpp}` (modify) | CONNECT / DEVS, Devices view, status line, link tracking, wake lock. |
| `src/CMakeLists.txt` (modify) | Add the new `.cpp` files. |
| `tests/car/test_elm327.cpp` (new) | Session tests. |
| `tests/car/test_saved_adapter.cpp` (new) | Codec tests. |
| `tests/car/FakeObdBleLink.{h,cpp}` (new) | Host test double for the link. |
| `tests/car/test_obd_ble_source.cpp` (new) | Glue tests against the fake link. |
| `.deps/InfiniSim/sim/components/car/ObdBleLink.cpp` (new, local only) | Scripted fake link for screenshots. |
| `docs/car-ble-hardware-checklist.md` (new) | What to run on the real Vgate. |

---

### Task 1: Elm327Session

**Files:**
- Create: `src/components/car/Elm327Session.h`, `src/components/car/Elm327Session.cpp`
- Test: `tests/car/test_elm327.cpp`

**Interfaces:**
- Consumes: `Pinetime::Controllers::ObdData` from `src/components/car/ObdData.h` (existing).
- Produces:
  - `class Pinetime::Controllers::Elm327Session`
  - `enum class State { Starting, Searching, Ready, EcuNotResponding }`
  - `enum class PollSet { None, Speed, Boost, Hud }`
  - `void Reset(uint32_t nowMs)`
  - `void Received(const uint8_t* data, size_t length)`
  - `bool NextCommand(char* out, size_t cap, uint32_t nowMs)`
  - `void Tick(uint32_t nowMs)`
  - `void SetPollSet(PollSet)`
  - `State GetState() const`
  - `const ObdData& Data() const`

**Deviations from the spec in this task** (Task 8 amends the spec to match):
- The `0100` probe gets a 15 s timeout instead of 1.5 s, because `SEARCHING...` legitimately takes several seconds.
- Six consecutive failed PID replies drop a `Ready` session to `EcuNotResponding`. This covers the ignition being switched off mid-drive.
- `0133` is asked once per link whatever the reply, so cars without that PID still get MAP polled.

- [ ] **Step 1: Write the failing test**

Create `tests/car/test_elm327.cpp`:

```cpp
// Host-side tests for Pinetime::Controllers::Elm327Session. From the repo root:
//   g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/car/test_elm327.cpp src/components/car/Elm327Session.cpp -o /tmp/test_elm327 && /tmp/test_elm327
#include "components/car/Elm327Session.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

using Pinetime::Controllers::Elm327Session;
using State = Elm327Session::State;
using PollSet = Elm327Session::PollSet;

static int failures = 0;
#define CHECK(cond)                                                                                  \
  do {                                                                                               \
    if (!(cond)) {                                                                                   \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                  \
      failures++;                                                                                    \
    }                                                                                                \
  } while (0)

// Feeds text in chunks, like BLE notifications (20 bytes at the default MTU).
static void Feed(Elm327Session& s, const char* text, size_t chunk = 20) {
  const size_t n = strlen(text);
  for (size_t i = 0; i < n; i += chunk) {
    s.Received(reinterpret_cast<const uint8_t*>(text + i), std::min(chunk, n - i));
  }
}

static std::string Next(Elm327Session& s, uint32_t now) {
  char buf[16];
  return s.NextCommand(buf, sizeof buf, now) ? std::string(buf) : std::string();
}

// Runs the start-up exchange; leaves the session Ready at time `now`.
static void BringUp(Elm327Session& s, uint32_t& now, size_t chunk = 20) {
  s.Reset(now);
  const char* expected[] = {"ATE0\r", "ATL0\r", "ATS0\r", "ATH0\r", "ATSP0\r"};
  const char* replies[] = {"ATE0\rOK\r\r>", "OK\r\r>", "OK\r\r>", "OK\r\r>", "OK\r\r>"};
  for (int i = 0; i < 5; i++) {
    CHECK(s.GetState() == State::Starting);
    CHECK(Next(s, now) == expected[i]);
    CHECK(Next(s, now).empty()); // waits for the prompt
    Feed(s, replies[i], chunk);
    now += 10;
  }
  CHECK(Next(s, now) == "0100\r");
  CHECK(s.GetState() == State::Searching);
  Feed(s, "SEARCHING...\r4100BE1FA813\r\r>", chunk);
  CHECK(s.GetState() == State::Ready);
  CHECK(!s.Data().connected); // no value read yet
}

static void TestStartup() {
  std::puts("startup sequence, echo ignored");
  Elm327Session s;
  uint32_t now = 1000;
  BringUp(s, now);
}

static void TestOneByteChunksAndLonePrompt() {
  std::puts("one-byte chunks and a prompt on its own");
  Elm327Session s;
  uint32_t now = 0;
  BringUp(s, now, 1);
  s.SetPollSet(PollSet::Speed);
  CHECK(Next(s, now) == "010D\r");
  Feed(s, "410D3C\r\r");
  Feed(s, ">");
  CHECK(s.Data().speedKmh == 60);
}

static void TestCrlf() {
  std::puts("CRLF line endings");
  Elm327Session s;
  uint32_t now = 0;
  BringUp(s, now);
  s.SetPollSet(PollSet::Speed);
  Next(s, now);
  Feed(s, "\r\n410D3C\r\n\r\n>");
  CHECK(s.Data().speedKmh == 60);
}

static void TestSpeedPolling() {
  std::puts("speed view alternates speed and rpm");
  Elm327Session s;
  uint32_t now = 0;
  BringUp(s, now);
  s.SetPollSet(PollSet::Speed);
  CHECK(Next(s, now) == "010D\r");
  Feed(s, "410D3C\r\r>");
  CHECK(Next(s, now) == "010C\r");
  Feed(s, "410C1AF8\r\r>");
  CHECK(s.Data().speedKmh == 60);
  CHECK(s.Data().rpm == 1726);
  CHECK(s.Data().connected);
  CHECK(Next(s, now) == "010D\r");
}

static void TestHudFormulasAndVoltageEveryFifth() {
  std::puts("hud order, every formula, ATRV every 5th poll");
  Elm327Session s;
  uint32_t now = 0;
  BringUp(s, now);
  s.SetPollSet(PollSet::Hud);
  struct Step {
    const char* command;
    const char* reply;
  } steps[] = {
    {"0105\r", "41057B\r\r>"},
    {"010F\r", "410F32\r\r>"},
    {"010C\r", "410C0FA0\r\r>"},
    {"0111\r", "411180\r\r>"},
    {"ATRV\r", "12.6V\r\r>"},
    {"0114\r", "41145A80\r\r>"},
    {"010D\r", "410D48\r\r>"},
    {"010B\r", "410B65\r\r>"},
  };
  for (const auto& step : steps) {
    CHECK(Next(s, now) == step.command);
    Feed(s, step.reply);
  }
  CHECK(s.Data().coolantTempC == 83);
  CHECK(s.Data().intakeTempC == 10);
  CHECK(s.Data().rpm == 1000);
  CHECK(s.Data().throttlePct == 50);
  CHECK(s.Data().batteryMillivolt == 12600);
  CHECK(s.Data().o2Millivolt == 450);
  CHECK(s.Data().speedKmh == 72);
  CHECK(s.Data().mapKpa == 101);
  CHECK(Next(s, now) == "0105\r"); // wraps round
  Feed(s, "41057B\r\r>");
  CHECK(Next(s, now) == "ATRV\r"); // 10th poll
  Feed(s, "14.35V\r\r>");
  CHECK(s.Data().batteryMillivolt == 14350);
}

static void TestBoostAsksBarometerOnceEvenIfUnsupported() {
  std::puts("boost asks 0133 once, then MAP, even if 0133 unsupported");
  Elm327Session s;
  uint32_t now = 0;
  BringUp(s, now);
  s.SetPollSet(PollSet::Boost);
  CHECK(Next(s, now) == "0133\r");
  Feed(s, "NO DATA\r\r>");
  CHECK(s.Data().barometerKpa == 101); // default kept
  CHECK(Next(s, now) == "010B\r");
  Feed(s, "410B8C\r\r>");
  CHECK(Next(s, now) == "010B\r");
  CHECK(s.Data().mapKpa == 140);

  Elm327Session t;
  now = 0;
  BringUp(t, now);
  t.SetPollSet(PollSet::Boost);
  CHECK(Next(t, now) == "0133\r");
  Feed(t, "413362\r\r>");
  CHECK(t.Data().barometerKpa == 98);
}

static void TestNoDataKeepsValues() {
  std::puts("NO DATA keeps the previous value");
  Elm327Session s;
  uint32_t now = 0;
  BringUp(s, now);
  s.SetPollSet(PollSet::Speed);
  Next(s, now);
  Feed(s, "410D3C\r\r>");
  Next(s, now);
  Feed(s, "NO DATA\r\r>");
  Next(s, now);
  Feed(s, "NO DATA\r\r>");
  CHECK(s.Data().speedKmh == 60);
  CHECK(s.Data().connected);
  CHECK(s.GetState() == State::Ready);
}

static void TestGarbageMismatchAndSpaces() {
  std::puts("?, garbage, mismatched PID and spaced replies");
  Elm327Session s;
  uint32_t now = 0;
  BringUp(s, now);
  s.SetPollSet(PollSet::Speed);
  CHECK(Next(s, now) == "010D\r");
  Feed(s, "?\r\r>");
  CHECK(Next(s, now) == "010C\r");
  Feed(s, "410D3C\r\r>"); // answer for the wrong PID: ignored
  CHECK(s.Data().rpm == 0);
  CHECK(s.Data().speedKmh == 0);
  CHECK(Next(s, now) == "010D\r");
  Feed(s, "41 0D 3C\r\r>");
  CHECK(s.Data().speedKmh == 60);
  CHECK(Next(s, now) == "010C\r");
  std::string junk(200, 'A');
  junk += ">";
  Feed(s, junk.c_str()); // overflows the reply buffer
  CHECK(Next(s, now) == "010D\r");
}

static void TestReplyTimeout() {
  std::puts("a missing reply times out after 1500 ms");
  Elm327Session s;
  uint32_t now = 0;
  BringUp(s, now);
  s.SetPollSet(PollSet::Speed);
  CHECK(Next(s, now) == "010D\r");
  s.Tick(now + 1499);
  CHECK(Next(s, now + 1499).empty());
  s.Tick(now + 1500);
  CHECK(Next(s, now + 1500) == "010C\r");
}

static void TestEcuNotRespondingAndRecovery() {
  std::puts("ignition off at start, then recovery");
  Elm327Session s;
  uint32_t now = 0;
  s.Reset(now);
  for (int i = 0; i < 5; i++) {
    Next(s, now);
    Feed(s, "OK\r\r>");
  }
  CHECK(Next(s, now) == "0100\r");
  Feed(s, "SEARCHING...\rUNABLE TO CONNECT\r\r>");
  CHECK(s.GetState() == State::EcuNotResponding);
  CHECK(Next(s, now + 4999).empty());
  CHECK(Next(s, now + 5000) == "0100\r");
  Feed(s, "4100BE1FA813\r\r>");
  CHECK(s.GetState() == State::Ready);
}

static void TestProbeTimeout() {
  std::puts("probe times out after 15 s, not 1.5 s");
  Elm327Session s;
  uint32_t now = 0;
  s.Reset(now);
  for (int i = 0; i < 5; i++) {
    Next(s, now);
    Feed(s, "OK\r\r>");
  }
  CHECK(Next(s, now) == "0100\r");
  s.Tick(now + 1500);
  CHECK(s.GetState() == State::Searching);
  s.Tick(now + 15000);
  CHECK(s.GetState() == State::EcuNotResponding);
}

static void TestIgnitionOffMidDrive() {
  std::puts("six failed PID replies mid-drive drop to EcuNotResponding");
  Elm327Session s;
  uint32_t now = 0;
  BringUp(s, now);
  s.SetPollSet(PollSet::Speed);
  Next(s, now);
  Feed(s, "410D3C\r\r>");
  CHECK(s.Data().connected);
  for (int i = 0; i < 5; i++) {
    Next(s, now);
    Feed(s, "NO DATA\r\r>");
  }
  CHECK(s.GetState() == State::Ready);
  Next(s, now);
  s.Tick(now + 1500); // a timeout counts as a miss too
  CHECK(s.GetState() == State::EcuNotResponding);
  CHECK(!s.Data().connected);
}

static void TestPollSetNoneIsIdle() {
  std::puts("menu poll set sends nothing");
  Elm327Session s;
  uint32_t now = 0;
  BringUp(s, now);
  s.SetPollSet(PollSet::None);
  CHECK(Next(s, now).empty());
}

int main() {
  TestStartup();
  TestOneByteChunksAndLonePrompt();
  TestCrlf();
  TestSpeedPolling();
  TestHudFormulasAndVoltageEveryFifth();
  TestBoostAsksBarometerOnceEvenIfUnsupported();
  TestNoDataKeepsValues();
  TestGarbageMismatchAndSpaces();
  TestReplyTimeout();
  TestEcuNotRespondingAndRecovery();
  TestProbeTimeout();
  TestIgnitionOffMidDrive();
  TestPollSetNoneIsIdle();
  std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
```

Check the expected values by hand before trusting the test:
- `0x7B` = 123, minus 40 is 83.
- `0x0FA0` = 4000, divided by 4 is 1000.
- `0x80` = 128, and 128*100/255 = 50.
- `0x5A` = 90, and 90*5 = 450.
- `0x1AF8` = 6904, divided by 4 is 1726.

- [ ] **Step 2: Run the test to verify it fails**

Run: `g++ -std=c++20 -Wall -Wextra -Werror -Isrc tests/car/test_elm327.cpp -o /tmp/test_elm327`
Expected: compile error, `components/car/Elm327Session.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/components/car/Elm327Session.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include "components/car/ObdData.h"

namespace Pinetime {
  namespace Controllers {

    // An ELM327 conversation over any byte transport. No BLE or LVGL dependency, so it can be
    // tested off the watch: feed it received bytes, ask it what to send next, read ObdData back.
    // One command is in flight at a time; a reply ends at the ELM327's '>' prompt.
    class Elm327Session {
    public:
      enum class State : uint8_t { Starting, Searching, Ready, EcuNotResponding };
      enum class PollSet : uint8_t { None, Speed, Boost, Hud };

      static constexpr uint32_t replyTimeoutMs = 1500;
      static constexpr uint32_t probeTimeoutMs = 15000; // "0100" may print SEARCHING... for a while
      static constexpr uint32_t ecuRetryMs = 5000;
      static constexpr uint8_t missesBeforeEcuLost = 6;
      static constexpr size_t replyCapacity = 64;

      void Reset(uint32_t nowMs);
      void Received(const uint8_t* data, size_t length);
      // Writes the next command, '\r'-terminated and NUL-terminated, into out. Returns false while
      // a reply is still awaited, when there is nothing to send, or if cap < 8.
      bool NextCommand(char* out, size_t cap, uint32_t nowMs);
      void Tick(uint32_t nowMs);
      void SetPollSet(PollSet set);

      State GetState() const {
        return state;
      }

      const ObdData& Data() const {
        return data;
      }

    private:
      enum class Kind : uint8_t { None, Init, Probe, Pid, Voltage };

      void HandleReply();
      void CountMiss();
      bool ParsePid(uint8_t pid, const char* line);
      bool ParseVoltage(const char* line);

      State state = State::Starting;
      PollSet pollSet = PollSet::None;
      ObdData data {};

      Kind pendingKind = Kind::None;
      uint8_t pendingPid = 0;
      uint32_t sentAt = 0;
      uint32_t lastProbeAt = 0;

      uint8_t initStep = 0;
      uint8_t pollIndex = 0;
      uint8_t pollCount = 0;
      uint8_t misses = 0;
      bool barometerAsked = false;

      char reply[replyCapacity] {};
      size_t replyLength = 0;
      bool replyOverflow = false;
    };
  }
}
```

- [ ] **Step 4: Write the implementation**

Create `src/components/car/Elm327Session.cpp`:

```cpp
#include "components/car/Elm327Session.h"

#include <cstdio>
#include <cstring>

using namespace Pinetime::Controllers;

namespace {
  constexpr const char* initCommands[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  constexpr uint8_t initCount = sizeof(initCommands) / sizeof(initCommands[0]);

  constexpr uint8_t speedPids[] = {0x0D, 0x0C};
  constexpr uint8_t boostPids[] = {0x0B};
  constexpr uint8_t hudPids[] = {0x05, 0x0F, 0x0C, 0x11, 0x14, 0x0D, 0x0B};
  constexpr uint8_t hudVoltageEvery = 5; // every 5th HUD poll reads the battery with ATRV

  int HexValue(char c) {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    return -1;
  }

  // Reads hex byte pairs from text, skipping spaces, until the first non-hex character.
  size_t ParseHexBytes(const char* text, uint8_t* out, size_t cap) {
    size_t count = 0;
    int high = -1;
    for (const char* p = text; *p != '\0' && count < cap; p++) {
      if (*p == ' ') {
        continue;
      }
      const int value = HexValue(*p);
      if (value < 0) {
        break;
      }
      if (high < 0) {
        high = value;
      } else {
        out[count++] = static_cast<uint8_t>(high * 16 + value);
        high = -1;
      }
    }
    return count;
  }
}

void Elm327Session::Reset(uint32_t nowMs) {
  state = State::Starting;
  data = {};
  pendingKind = Kind::None;
  pendingPid = 0;
  sentAt = nowMs;
  lastProbeAt = nowMs;
  initStep = 0;
  pollIndex = 0;
  pollCount = 0;
  misses = 0;
  barometerAsked = false;
  replyLength = 0;
  replyOverflow = false;
}

void Elm327Session::SetPollSet(PollSet set) {
  if (set != pollSet) {
    pollSet = set;
    pollIndex = 0;
    pollCount = 0;
  }
}

bool Elm327Session::NextCommand(char* out, size_t cap, uint32_t nowMs) {
  if (pendingKind != Kind::None || cap < 8) {
    return false;
  }

  switch (state) {
    case State::Starting:
      if (initStep < initCount) {
        pendingKind = Kind::Init;
        snprintf(out, cap, "%s\r", initCommands[initStep]);
      } else {
        state = State::Searching;
        pendingKind = Kind::Probe;
        snprintf(out, cap, "0100\r");
      }
      break;
    case State::Searching:
      return false; // only between a probe's reply and the state change it causes
    case State::EcuNotResponding:
      if (nowMs - lastProbeAt < ecuRetryMs) {
        return false;
      }
      pendingKind = Kind::Probe;
      snprintf(out, cap, "0100\r");
      break;
    case State::Ready: {
      if (pollSet == PollSet::None) {
        return false;
      }
      if (pollSet == PollSet::Boost && !barometerAsked) {
        barometerAsked = true; // once per link, whatever the reply
        pendingKind = Kind::Pid;
        pendingPid = 0x33;
      } else if (pollSet == PollSet::Hud && pollCount % hudVoltageEvery == hudVoltageEvery - 1) {
        pendingKind = Kind::Voltage;
      } else {
        const uint8_t* pids = speedPids;
        uint8_t count = sizeof(speedPids);
        if (pollSet == PollSet::Boost) {
          pids = boostPids;
          count = sizeof(boostPids);
        } else if (pollSet == PollSet::Hud) {
          pids = hudPids;
          count = sizeof(hudPids);
        }
        pendingKind = Kind::Pid;
        pendingPid = pids[pollIndex % count];
        pollIndex = static_cast<uint8_t>((pollIndex + 1) % count);
      }
      pollCount++;
      if (pendingKind == Kind::Voltage) {
        snprintf(out, cap, "ATRV\r");
      } else {
        snprintf(out, cap, "01%02X\r", pendingPid);
      }
      break;
    }
  }

  if (pendingKind == Kind::Probe) {
    lastProbeAt = nowMs;
  }
  sentAt = nowMs;
  replyLength = 0;
  replyOverflow = false;
  return true;
}

void Elm327Session::Received(const uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; i++) {
    const char c = static_cast<char>(bytes[i]);
    if (c == '>') {
      reply[replyLength] = '\0';
      HandleReply();
      replyLength = 0;
      replyOverflow = false;
    } else if (c != '\0') {
      if (replyLength < replyCapacity - 1) {
        reply[replyLength++] = c;
      } else {
        replyOverflow = true;
      }
    }
  }
}

void Elm327Session::Tick(uint32_t nowMs) {
  if (pendingKind == Kind::None) {
    return;
  }
  const uint32_t limit = pendingKind == Kind::Probe ? probeTimeoutMs : replyTimeoutMs;
  if (nowMs - sentAt < limit) {
    return;
  }
  const Kind kind = pendingKind;
  pendingKind = Kind::None;
  replyLength = 0;
  replyOverflow = false;
  if (kind == Kind::Init) {
    initStep++;
  } else if (kind == Kind::Probe) {
    state = State::EcuNotResponding;
  } else if (kind == Kind::Pid) {
    CountMiss();
  }
}

void Elm327Session::CountMiss() {
  if (state == State::Ready && ++misses >= missesBeforeEcuLost) {
    state = State::EcuNotResponding;
    data.connected = false;
    lastProbeAt = sentAt;
    misses = 0;
  }
}

void Elm327Session::HandleReply() {
  const Kind kind = pendingKind;
  const uint8_t pid = pendingPid;
  pendingKind = Kind::None;
  if (kind == Kind::None) {
    return; // a prompt nobody asked for
  }

  // Lines end in '\r' (after ATL0) or "\r\n". Blank lines, our own echo (before ATE0 takes
  // effect) and "SEARCHING..." simply fail to match below.
  for (size_t i = 0; i < replyLength; i++) {
    if (reply[i] == '\r' || reply[i] == '\n') {
      reply[i] = '\0';
    }
  }
  bool matched = false;
  for (size_t start = 0; start < replyLength && !matched && !replyOverflow;) {
    const char* line = reply + start;
    start += strlen(line) + 1;
    if (*line == '\0') {
      continue;
    }
    switch (kind) {
      case Kind::Probe: {
        uint8_t bytes[2];
        matched = ParseHexBytes(line, bytes, sizeof bytes) == 2 && bytes[0] == 0x41 && bytes[1] == 0x00;
        break;
      }
      case Kind::Pid:
        matched = ParsePid(pid, line);
        break;
      case Kind::Voltage:
        matched = ParseVoltage(line);
        break;
      case Kind::Init:
      case Kind::None:
        break;
    }
  }

  switch (kind) {
    case Kind::Init:
      initStep++; // "OK", "?" or anything else: carry on regardless
      break;
    case Kind::Probe:
      state = matched ? State::Ready : State::EcuNotResponding;
      misses = 0;
      break;
    case Kind::Pid:
      if (matched) {
        misses = 0;
      } else {
        CountMiss();
      }
      break;
    case Kind::Voltage:
    case Kind::None:
      break;
  }
}

bool Elm327Session::ParsePid(uint8_t pid, const char* line) {
  uint8_t bytes[6];
  const size_t count = ParseHexBytes(line, bytes, sizeof bytes);
  if (count < 3 || bytes[0] != 0x41 || bytes[1] != pid) {
    return false;
  }
  const uint8_t a = bytes[2];
  switch (pid) {
    case 0x0D:
      data.speedKmh = a;
      break;
    case 0x0C:
      if (count < 4) {
        return false;
      }
      data.rpm = static_cast<uint16_t>((a * 256 + bytes[3]) / 4);
      break;
    case 0x05:
      data.coolantTempC = static_cast<int16_t>(a - 40);
      break;
    case 0x0F:
      data.intakeTempC = static_cast<int16_t>(a - 40);
      break;
    case 0x0B:
      data.mapKpa = a;
      break;
    case 0x11:
      data.throttlePct = static_cast<uint8_t>(a * 100 / 255);
      break;
    case 0x14:
      data.o2Millivolt = static_cast<uint16_t>(a * 5);
      break;
    case 0x33:
      data.barometerKpa = a;
      break;
    default:
      return false;
  }
  data.connected = true;
  return true;
}

bool Elm327Session::ParseVoltage(const char* line) {
  // "12.6V", "14.35V", "12V", with an optional space before the V.
  uint32_t whole = 0;
  size_t digits = 0;
  const char* p = line;
  while (*p >= '0' && *p <= '9') {
    whole = whole * 10 + static_cast<uint32_t>(*p - '0');
    p++;
    digits++;
  }
  if (digits == 0 || digits > 2) {
    return false;
  }
  uint32_t millivolts = whole * 1000;
  if (*p == '.') {
    p++;
    uint32_t scale = 100;
    while (*p >= '0' && *p <= '9') {
      millivolts += static_cast<uint32_t>(*p - '0') * scale;
      scale /= 10;
      p++;
    }
  }
  while (*p == ' ') {
    p++;
  }
  if (*p != 'V' && *p != 'v') {
    return false;
  }
  data.batteryMillivolt = static_cast<uint16_t>(millivolts);
  return true;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run:

```bash
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/car/test_elm327.cpp src/components/car/Elm327Session.cpp -o /tmp/test_elm327 && /tmp/test_elm327
```

Expected: every test name prints, then `ALL PASS`, with no sanitizer output.

- [ ] **Step 6: Mutation check**

Copy `Elm327Session.cpp` to `/tmp/mut/`, and in the copy change `barometerAsked = true; // once per link` to nothing. Build the test against the copy.

Expected: `TestBoostAsksBarometerOnceEvenIfUnsupported` fails. Delete the copy afterwards.

- [ ] **Step 7: Add to the firmware build and commit**

In `src/CMakeLists.txt`, after `components/car/ObdSimulator.cpp`, add `components/car/Elm327Session.cpp`. Then:

```bash
git add src/components/car/Elm327Session.h src/components/car/Elm327Session.cpp tests/car/test_elm327.cpp src/CMakeLists.txt
git commit -m "Add Elm327Session: ELM327 protocol with host tests

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Saved adapter record and store

**Files:**
- Create: `src/components/car/SavedAdapter.h`, `src/components/car/SavedAdapter.cpp`
- Create: `src/components/car/AdapterStore.h`, `src/components/car/AdapterStore.cpp`
- Test: `tests/car/test_saved_adapter.cpp`

**Interfaces:**
- Produces:
  - `struct Pinetime::Controllers::SavedAdapter { uint8_t addressType; uint8_t address[6]; char name[21]; static constexpr size_t nameCapacity = 21; }`
  - `SavedAdapterCodec::version` (`= 1`) and `SavedAdapterCodec::encodedSize` (`= 29`)
  - `size_t SavedAdapterCodec::Encode(const SavedAdapter&, uint8_t* out, size_t cap)`
  - `bool SavedAdapterCodec::Decode(const uint8_t* in, size_t length, SavedAdapter& out)`
  - `bool AdapterStore::Load(FS&, SavedAdapter&)`
  - `bool AdapterStore::Save(FS&, const SavedAdapter&)`

- [ ] **Step 1: Write the failing test**

Create `tests/car/test_saved_adapter.cpp`:

```cpp
// Host-side tests for the saved-adapter codec. From the repo root:
//   g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/car/test_saved_adapter.cpp src/components/car/SavedAdapter.cpp -o /tmp/test_saved_adapter && /tmp/test_saved_adapter
#include "components/car/SavedAdapter.h"
#include <cstdio>
#include <cstring>

using namespace Pinetime::Controllers;

static int failures = 0;
#define CHECK(cond)                                                                                  \
  do {                                                                                               \
    if (!(cond)) {                                                                                   \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                  \
      failures++;                                                                                    \
    }                                                                                                \
  } while (0)

static SavedAdapter Sample() {
  SavedAdapter a;
  a.addressType = 1;
  const uint8_t address[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  memcpy(a.address, address, 6);
  strcpy(a.name, "IOS-Vlink");
  return a;
}

int main() {
  std::puts("round trip");
  {
    uint8_t buffer[SavedAdapterCodec::encodedSize];
    CHECK(SavedAdapterCodec::Encode(Sample(), buffer, sizeof buffer) == SavedAdapterCodec::encodedSize);
    SavedAdapter back;
    CHECK(SavedAdapterCodec::Decode(buffer, sizeof buffer, back));
    CHECK(back.addressType == 1);
    CHECK(memcmp(back.address, Sample().address, 6) == 0);
    CHECK(strcmp(back.name, "IOS-Vlink") == 0);
  }
  std::puts("encode refuses a small buffer");
  {
    uint8_t buffer[10];
    CHECK(SavedAdapterCodec::Encode(Sample(), buffer, sizeof buffer) == 0);
  }
  std::puts("decode rejects truncated, other version, bad address type");
  {
    uint8_t buffer[SavedAdapterCodec::encodedSize];
    SavedAdapterCodec::Encode(Sample(), buffer, sizeof buffer);
    SavedAdapter out;
    CHECK(!SavedAdapterCodec::Decode(buffer, sizeof buffer - 1, out));
    buffer[0] = 2;
    CHECK(!SavedAdapterCodec::Decode(buffer, sizeof buffer, out));
    buffer[0] = SavedAdapterCodec::version;
    buffer[1] = 7;
    CHECK(!SavedAdapterCodec::Decode(buffer, sizeof buffer, out));
  }
  std::puts("unterminated name is cut, not overrun");
  {
    uint8_t buffer[SavedAdapterCodec::encodedSize];
    SavedAdapterCodec::Encode(Sample(), buffer, sizeof buffer);
    memset(buffer + 8, 'X', SavedAdapter::nameCapacity); // no NUL anywhere in the name
    SavedAdapter out;
    CHECK(SavedAdapterCodec::Decode(buffer, sizeof buffer, out));
    CHECK(strlen(out.name) == SavedAdapter::nameCapacity - 1);
  }
  std::puts("long name is truncated to 20 characters");
  {
    SavedAdapter a = Sample();
    memset(a.name, 'N', sizeof a.name); // deliberately unterminated
    uint8_t buffer[SavedAdapterCodec::encodedSize];
    CHECK(SavedAdapterCodec::Encode(a, buffer, sizeof buffer) == SavedAdapterCodec::encodedSize);
    SavedAdapter back;
    CHECK(SavedAdapterCodec::Decode(buffer, sizeof buffer, back));
    CHECK(strlen(back.name) == 20);
  }
  std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `g++ -std=c++20 -Isrc tests/car/test_saved_adapter.cpp -o /tmp/t`
Expected: `SavedAdapter.h: No such file or directory`.

- [ ] **Step 3: Implement the codec**

Create `src/components/car/SavedAdapter.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace Pinetime {
  namespace Controllers {

    // The adapter CONNECT links to. Stored in /car/adapter.dat rather than in Settings, because
    // changing the Settings layout resets every user's settings on upgrade.
    struct SavedAdapter {
      static constexpr size_t nameCapacity = 21; // 20 characters + NUL
      uint8_t addressType = 0;
      uint8_t address[6] {};
      char name[nameCapacity] {};
    };

    namespace SavedAdapterCodec {
      constexpr uint8_t version = 1;
      constexpr size_t encodedSize = 1 + 1 + 6 + SavedAdapter::nameCapacity;

      // Returns the number of bytes written, or 0 if cap is too small.
      size_t Encode(const SavedAdapter& adapter, uint8_t* out, size_t cap);
      // False for a short buffer, another version or an invalid address type.
      bool Decode(const uint8_t* in, size_t length, SavedAdapter& out);
    }
  }
}
```

Create `src/components/car/SavedAdapter.cpp`:

```cpp
#include "components/car/SavedAdapter.h"

#include <cstring>

using namespace Pinetime::Controllers;

size_t SavedAdapterCodec::Encode(const SavedAdapter& adapter, uint8_t* out, size_t cap) {
  if (cap < encodedSize) {
    return 0;
  }
  out[0] = version;
  out[1] = adapter.addressType;
  memcpy(out + 2, adapter.address, 6);
  uint8_t* name = out + 8;
  memset(name, 0, SavedAdapter::nameCapacity);
  for (size_t i = 0; i < SavedAdapter::nameCapacity - 1 && adapter.name[i] != '\0'; i++) {
    name[i] = static_cast<uint8_t>(adapter.name[i]);
  }
  return encodedSize;
}

bool SavedAdapterCodec::Decode(const uint8_t* in, size_t length, SavedAdapter& out) {
  // BLE address types run 0 (public) to 3 (random resolvable id).
  if (length < encodedSize || in[0] != version || in[1] > 3) {
    return false;
  }
  out.addressType = in[1];
  memcpy(out.address, in + 2, 6);
  memcpy(out.name, in + 8, SavedAdapter::nameCapacity);
  out.name[SavedAdapter::nameCapacity - 1] = '\0';
  return true;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run:

```bash
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/car/test_saved_adapter.cpp src/components/car/SavedAdapter.cpp -o /tmp/test_saved_adapter && /tmp/test_saved_adapter
```

Expected: `ALL PASS`.

- [ ] **Step 5: Implement the store**

Create `src/components/car/AdapterStore.h`:

```cpp
#pragma once

#include "components/car/SavedAdapter.h"

namespace Pinetime {
  namespace Controllers {
    class FS;

    namespace AdapterStore {
      // False when there is no file or it does not decode; out is then left unchanged.
      bool Load(FS& fs, SavedAdapter& out);
      bool Save(FS& fs, const SavedAdapter& adapter);
    }
  }
}
```

Create `src/components/car/AdapterStore.cpp`:

```cpp
#include "components/car/AdapterStore.h"
#include "components/fs/FS.h"

using namespace Pinetime::Controllers;

namespace {
  constexpr const char* directory = "/car";
  constexpr const char* path = "/car/adapter.dat";
}

bool AdapterStore::Load(FS& fs, SavedAdapter& out) {
  lfs_file_t file;
  if (fs.FileOpen(&file, path, LFS_O_RDONLY) != LFS_ERR_OK) {
    return false;
  }
  uint8_t buffer[SavedAdapterCodec::encodedSize];
  const int read = fs.FileRead(&file, buffer, sizeof(buffer));
  fs.FileClose(&file);
  SavedAdapter decoded;
  if (read != static_cast<int>(sizeof(buffer)) || !SavedAdapterCodec::Decode(buffer, sizeof(buffer), decoded)) {
    return false;
  }
  out = decoded;
  return true;
}

bool AdapterStore::Save(FS& fs, const SavedAdapter& adapter) {
  uint8_t buffer[SavedAdapterCodec::encodedSize];
  const size_t length = SavedAdapterCodec::Encode(adapter, buffer, sizeof(buffer));
  fs.DirCreate(directory); // returns an error when it already exists, which is fine
  lfs_file_t file;
  if (fs.FileOpen(&file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    return false;
  }
  const int written = fs.FileWrite(&file, buffer, static_cast<uint32_t>(length));
  fs.FileClose(&file);
  return written == static_cast<int>(length);
}
```

- [ ] **Step 6: Add to the build, compile the firmware, commit**

In `src/CMakeLists.txt`, after `components/car/Elm327Session.cpp`, add:

```
        components/car/SavedAdapter.cpp
        components/car/AdapterStore.cpp
```

Run the firmware build from "Commands used throughout". Expected: `Built target pinetime-mcuboot-app`, no warnings from the new files. The new code isn't referenced yet, so flash should barely change.

```bash
git add src/components/car/SavedAdapter.* src/components/car/AdapterStore.* tests/car/test_saved_adapter.cpp src/CMakeLists.txt
git commit -m "Add saved-adapter record and /car/adapter.dat store

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: ObdBleLink (NimBLE central)

**Files:**
- Create: `src/components/car/ObdBleLink.h`, `src/components/car/ObdBleLink.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `class Pinetime::Controllers::ObdBleLink`
  - `static ObdBleLink& Instance()`
  - `enum class State { Idle, Scanning, ScanDone, Connecting, Discovering, Linked, Failed, Lost }`
  - `struct Address { uint8_t type; uint8_t value[6]; }`
  - `struct FoundDevice { Address address; char name[21]; int8_t rssi; }`
  - `maxFound` (`= 8`) and `rxCapacity` (`= 256`)
  - `void StartScan(uint32_t durationMs)`
  - `void Connect(const Address&)`
  - `void Disconnect()`
  - `bool Write(const char* text)`
  - `size_t Read(uint8_t* out, size_t cap)`
  - `State GetState() const`
  - `size_t FoundCount() const`
  - `bool Found(size_t index, FoundDevice& out) const`
  - `friend struct ObdBleLinkCallbacks`, which every implementation (firmware, simulator, host test double) defines for itself to reach the private state.

This task has no host test. NimBLE can't run off the watch. The link is exercised through its fake in Task 5 and the simulator in Task 7, and for real on hardware in Task 8. Verification here is a clean firmware compile plus a review against the checklist in Step 4.

**Deviation from the spec:** no MTU exchange. Every command is under 20 bytes, the session reassembles replies across 20-byte notifications, and running an MTU exchange concurrently with service discovery adds ATT ordering risk for no gain. Task 8 amends the spec.

- [ ] **Step 1: Write the header**

Create `src/components/car/ObdBleLink.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    struct ObdBleLinkCallbacks; // defined by each implementation of ObdBleLink.cpp

    // The BLE side of the OBD-II adapter: scan, connect, find the serial characteristic, subscribe,
    // write, and hand received bytes over. No NimBLE types appear here, so the simulator and host
    // tests can build their own ObdBleLink.cpp against this header.
    //
    // One instance lives for the life of the firmware, not the Car screen: NimBLE can still deliver
    // events (such as the disconnect after a terminate) once the screen that started a link is gone.
    //
    // Thread safety: NimBLE callbacks run on the BLE host task; everything else runs on the display
    // task. The state below is only read or written inside a FreeRTOS critical section, and no
    // NimBLE call is ever made while one is held.
    class ObdBleLink {
    public:
      enum class State : uint8_t { Idle, Scanning, ScanDone, Connecting, Discovering, Linked, Failed, Lost };

      struct Address {
        uint8_t type;
        uint8_t value[6];
      };

      struct FoundDevice {
        Address address;
        char name[21];
        int8_t rssi;
      };

      static constexpr size_t maxFound = 8;
      static constexpr size_t rxCapacity = 256;

      static ObdBleLink& Instance();

      // Ignored while it would clash with the current state (for example Connect while Linked);
      // the Car screen calls Disconnect first. Scanning does not need the phone's connection dropped.
      void StartScan(uint32_t durationMs);
      void Connect(const Address& address);
      // Cancels a scan or a pending connect, or terminates the link. Always ends in Idle.
      void Disconnect();
      // Sends text to the adapter. False unless Linked.
      bool Write(const char* text);
      // Moves up to cap received bytes into out; returns how many.
      size_t Read(uint8_t* out, size_t cap);

      State GetState() const;
      size_t FoundCount() const;
      bool Found(size_t index, FoundDevice& out) const;

    private:
      friend struct ObdBleLinkCallbacks;
      ObdBleLink() = default;
      static ObdBleLink instance;

      State state = State::Idle;
      FoundDevice found[maxFound] {};
      size_t foundCount = 0;

      uint8_t rx[rxCapacity] {};
      size_t rxHead = 0;
      size_t rxTail = 0;

      uint16_t connHandle = 0xffff; // BLE_HS_CONN_HANDLE_NONE
      uint8_t candidate = 0;        // which serial service layout discovery is trying
      uint16_t serviceStart = 0;
      uint16_t serviceEnd = 0;
      uint16_t notifyHandle = 0;
      uint16_t writeHandle = 0;
      uint16_t cccdHandle = 0;
      bool writeWithoutResponse = false;
      bool combinedFound = false; // one characteristic that both notifies and accepts writes
    };
  }
}
```

- [ ] **Step 2: Write the implementation**

Create `src/components/car/ObdBleLink.cpp`:

```cpp
#include "components/car/ObdBleLink.h"

#include <cstring>
#include <FreeRTOS.h>
#include <task.h>
#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <host/ble_uuid.h>
#undef max
#undef min

using namespace Pinetime::Controllers;

ObdBleLink ObdBleLink::instance;

namespace {
  // Guards the shared state. Never make a NimBLE call while holding it.
  class Lock {
  public:
    Lock() {
      taskENTER_CRITICAL();
    }
    ~Lock() {
      taskEXIT_CRITICAL();
    }
  };

  // Vgate iCar Pro BLE ("IOS-Vlink"): E7810A71-73AE-499D-8C15-FAA9AEF0C3F2, bytes little-endian.
  const ble_uuid128_t vgateService =
    BLE_UUID128_INIT(0xF2, 0xC3, 0xF0, 0xAE, 0xA9, 0xFA, 0x15, 0x8C, 0x9D, 0x49, 0xAE, 0x73, 0x71, 0x0A, 0x81, 0xE7);
  const ble_uuid16_t fff0Service = BLE_UUID16_INIT(0xFFF0);
  const ble_uuid16_t ffe0Service = BLE_UUID16_INIT(0xFFE0);
  const ble_uuid_t* const serviceCandidates[] = {&vgateService.u, &fff0Service.u, &ffe0Service.u};
  constexpr uint8_t candidateCount = sizeof(serviceCandidates) / sizeof(serviceCandidates[0]);
  const ble_uuid16_t cccdUuid = BLE_UUID16_INIT(0x2902);

  bool Busy(ObdBleLink::State state) {
    return state == ObdBleLink::State::Scanning || state == ObdBleLink::State::Connecting ||
           state == ObdBleLink::State::Discovering || state == ObdBleLink::State::Linked;
  }
}

namespace Pinetime {
  namespace Controllers {
    struct ObdBleLinkCallbacks {
      static int OnGap(ble_gap_event* event, void* arg);
      static int OnService(uint16_t conn, const ble_gatt_error* error, const ble_gatt_svc* service, void* arg);
      static int OnCharacteristic(uint16_t conn, const ble_gatt_error* error, const ble_gatt_chr* chr, void* arg);
      static int OnDescriptor(uint16_t conn, const ble_gatt_error* error, uint16_t chrValHandle, const ble_gatt_dsc* dsc, void* arg);
      static int OnSubscribed(uint16_t conn, const ble_gatt_error* error, ble_gatt_attr* attr, void* arg);

      static bool IsDiscovering(ObdBleLink& link, uint16_t conn);
      static void DiscoverNextService(ObdBleLink& link);
      static void Fail(ObdBleLink& link);
      static void AddFound(ObdBleLink& link, const ble_addr_t& addr, const uint8_t* name, uint8_t nameLength, int8_t rssi);
      static void PushReceived(ObdBleLink& link, const uint8_t* data, size_t length);
    };
  }
}

ObdBleLink& ObdBleLink::Instance() {
  return instance;
}

ObdBleLink::State ObdBleLink::GetState() const {
  Lock lock;
  return state;
}

size_t ObdBleLink::FoundCount() const {
  Lock lock;
  return foundCount;
}

bool ObdBleLink::Found(size_t index, FoundDevice& out) const {
  Lock lock;
  if (index >= foundCount) {
    return false;
  }
  out = found[index];
  return true;
}

void ObdBleLink::StartScan(uint32_t durationMs) {
  {
    Lock lock;
    if (Busy(state)) {
      return;
    }
    foundCount = 0;
    state = State::Scanning;
  }
  uint8_t ownAddrType;
  ble_gap_disc_params params {};
  params.passive = 0; // active scan: the name is often only in the scan response
  params.filter_duplicates = 0;
  if (ble_hs_id_infer_auto(0, &ownAddrType) != 0 ||
      ble_gap_disc(ownAddrType, static_cast<int32_t>(durationMs), &params, ObdBleLinkCallbacks::OnGap, this) != 0) {
    Lock lock;
    state = State::ScanDone; // shows as an empty list rather than an adapter failure
  }
}

void ObdBleLink::Connect(const Address& address) {
  bool cancelScan;
  {
    Lock lock;
    if (state == State::Connecting || state == State::Discovering || state == State::Linked) {
      return;
    }
    cancelScan = state == State::Scanning;
    state = State::Connecting;
    connHandle = BLE_HS_CONN_HANDLE_NONE;
    candidate = 0;
    serviceStart = serviceEnd = 0;
    notifyHandle = writeHandle = cccdHandle = 0;
    writeWithoutResponse = false;
    combinedFound = false;
    rxHead = rxTail = 0;
  }
  if (cancelScan) {
    ble_gap_disc_cancel();
  }
  ble_addr_t peer;
  peer.type = address.type;
  memcpy(peer.val, address.value, sizeof(peer.val));
  uint8_t ownAddrType;
  if (ble_hs_id_infer_auto(0, &ownAddrType) != 0 ||
      ble_gap_connect(ownAddrType, &peer, 5000, nullptr, ObdBleLinkCallbacks::OnGap, this) != 0) {
    Lock lock;
    state = State::Failed;
  }
}

void ObdBleLink::Disconnect() {
  State previous;
  uint16_t handle;
  {
    Lock lock;
    previous = state;
    handle = connHandle;
    state = State::Idle;
    connHandle = BLE_HS_CONN_HANDLE_NONE; // later events for the old connection are ignored
    notifyHandle = writeHandle = cccdHandle = 0;
    rxHead = rxTail = 0;
  }
  switch (previous) {
    case State::Scanning:
      ble_gap_disc_cancel();
      break;
    case State::Connecting:
      ble_gap_conn_cancel();
      break;
    case State::Discovering:
    case State::Linked:
      ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
      break;
    default:
      break;
  }
}

bool ObdBleLink::Write(const char* text) {
  uint16_t conn;
  uint16_t handle;
  bool withoutResponse;
  {
    Lock lock;
    if (state != State::Linked) {
      return false;
    }
    conn = connHandle;
    handle = writeHandle;
    withoutResponse = writeWithoutResponse;
  }
  const auto length = static_cast<uint16_t>(strlen(text));
  const int rc = withoutResponse ? ble_gattc_write_no_rsp_flat(conn, handle, text, length)
                                 : ble_gattc_write_flat(conn, handle, text, length, nullptr, nullptr);
  return rc == 0;
}

size_t ObdBleLink::Read(uint8_t* out, size_t cap) {
  Lock lock;
  size_t count = 0;
  while (count < cap && rxTail != rxHead) {
    out[count++] = rx[rxTail];
    rxTail = (rxTail + 1) % rxCapacity;
  }
  return count;
}

// ---- callbacks (BLE host task) ----

bool ObdBleLinkCallbacks::IsDiscovering(ObdBleLink& link, uint16_t conn) {
  Lock lock;
  return link.state == ObdBleLink::State::Discovering && link.connHandle == conn;
}

void ObdBleLinkCallbacks::Fail(ObdBleLink& link) {
  uint16_t handle;
  {
    Lock lock;
    handle = link.connHandle;
    link.state = ObdBleLink::State::Failed;
  }
  if (handle != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
  }
}

void ObdBleLinkCallbacks::AddFound(ObdBleLink& link, const ble_addr_t& addr, const uint8_t* name, uint8_t nameLength, int8_t rssi) {
  Lock lock;
  size_t i = 0;
  for (; i < link.foundCount; i++) {
    const auto& known = link.found[i].address;
    if (known.type == addr.type && memcmp(known.value, addr.val, 6) == 0) {
      break;
    }
  }
  if (i == link.foundCount) {
    if (link.foundCount == ObdBleLink::maxFound) {
      return;
    }
    link.foundCount++;
    link.found[i].address.type = addr.type;
    memcpy(link.found[i].address.value, addr.val, 6);
    link.found[i].rssi = rssi;
  } else if (rssi > link.found[i].rssi) {
    link.found[i].rssi = rssi;
  }
  const size_t length = nameLength < sizeof(link.found[i].name) - 1 ? nameLength : sizeof(link.found[i].name) - 1;
  memcpy(link.found[i].name, name, length);
  link.found[i].name[length] = '\0';
}

void ObdBleLinkCallbacks::PushReceived(ObdBleLink& link, const uint8_t* data, size_t length) {
  Lock lock;
  for (size_t i = 0; i < length; i++) {
    const size_t next = (link.rxHead + 1) % ObdBleLink::rxCapacity;
    if (next == link.rxTail) {
      return; // full: drop; the session's timeout recovers
    }
    link.rx[link.rxHead] = data[i];
    link.rxHead = next;
  }
}

int ObdBleLinkCallbacks::OnGap(ble_gap_event* event, void* arg) {
  auto& link = *static_cast<ObdBleLink*>(arg);
  switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
      ble_hs_adv_fields fields;
      if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0 && fields.name != nullptr &&
          fields.name_len > 0) {
        AddFound(link, event->disc.addr, fields.name, fields.name_len, event->disc.rssi);
      }
      return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE: {
      Lock lock;
      if (link.state == ObdBleLink::State::Scanning) {
        link.state = ObdBleLink::State::ScanDone;
      }
      return 0;
    }
    case BLE_GAP_EVENT_CONNECT: {
      bool wanted;
      {
        Lock lock;
        wanted = link.state == ObdBleLink::State::Connecting;
        if (wanted && event->connect.status == 0) {
          link.connHandle = event->connect.conn_handle;
          link.state = ObdBleLink::State::Discovering;
        } else if (wanted) {
          link.state = ObdBleLink::State::Failed;
        }
      }
      if (!wanted) {
        // The Car screen gave up (Disconnect) before this connection completed: drop it.
        if (event->connect.status == 0) {
          ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
      }
      if (event->connect.status == 0) {
        DiscoverNextService(link);
      }
      return 0;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
      Lock lock;
      if (event->disconnect.conn.conn_handle != link.connHandle) {
        return 0; // an old connection, already forgotten by Disconnect()
      }
      link.state = link.state == ObdBleLink::State::Linked ? ObdBleLink::State::Lost : ObdBleLink::State::Failed;
      link.connHandle = BLE_HS_CONN_HANDLE_NONE;
      return 0;
    }
    case BLE_GAP_EVENT_NOTIFY_RX: {
      uint16_t notifyHandle;
      {
        Lock lock;
        notifyHandle = link.notifyHandle;
      }
      if (notifyHandle == 0 || event->notify_rx.attr_handle != notifyHandle) {
        return 0;
      }
      uint8_t chunk[32];
      const uint16_t total = OS_MBUF_PKTLEN(event->notify_rx.om);
      for (uint16_t offset = 0; offset < total;) {
        const uint16_t length = (total - offset) < sizeof(chunk) ? (total - offset) : sizeof(chunk);
        if (os_mbuf_copydata(event->notify_rx.om, offset, length, chunk) != 0) {
          break;
        }
        PushReceived(link, chunk, length);
        offset += length;
      }
      return 0;
    }
    default:
      return 0;
  }
}

void ObdBleLinkCallbacks::DiscoverNextService(ObdBleLink& link) {
  uint16_t conn;
  uint8_t index;
  {
    Lock lock;
    conn = link.connHandle;
    index = link.candidate;
  }
  if (index >= candidateCount) {
    Fail(link);
    return;
  }
  if (ble_gattc_disc_svc_by_uuid(conn, serviceCandidates[index], OnService, &link) != 0) {
    Fail(link);
  }
}

int ObdBleLinkCallbacks::OnService(uint16_t conn, const ble_gatt_error* error, const ble_gatt_svc* service, void* arg) {
  auto& link = *static_cast<ObdBleLink*>(arg);
  if (!IsDiscovering(link, conn)) {
    return 0;
  }
  if (error->status == 0 && service != nullptr) {
    Lock lock;
    if (link.serviceEnd == 0) {
      link.serviceStart = service->start_handle;
      link.serviceEnd = service->end_handle;
    }
    return 0;
  }
  // BLE_HS_EDONE (or an error) ends this candidate.
  uint16_t start;
  uint16_t end;
  {
    Lock lock;
    start = link.serviceStart;
    end = link.serviceEnd;
    if (end == 0) {
      link.candidate++;
    }
  }
  if (end == 0) {
    DiscoverNextService(link);
  } else if (ble_gattc_disc_all_chrs(conn, start, end, OnCharacteristic, &link) != 0) {
    Fail(link);
  }
  return 0;
}

int ObdBleLinkCallbacks::OnCharacteristic(uint16_t conn, const ble_gatt_error* error, const ble_gatt_chr* chr, void* arg) {
  auto& link = *static_cast<ObdBleLink*>(arg);
  if (!IsDiscovering(link, conn)) {
    return 0;
  }
  if (error->status == 0 && chr != nullptr) {
    Lock lock;
    const bool notifies = (chr->properties & BLE_GATT_CHR_PROP_NOTIFY) != 0;
    const bool noResponse = (chr->properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP) != 0;
    const bool writable = noResponse || (chr->properties & BLE_GATT_CHR_PROP_WRITE) != 0;
    // Prefer one characteristic that does both (Vgate, HM-10 style); otherwise the first of each.
    if (notifies && writable && !link.combinedFound) {
      link.combinedFound = true;
      link.notifyHandle = link.writeHandle = chr->val_handle;
      link.writeWithoutResponse = noResponse;
    } else if (!link.combinedFound) {
      if (notifies && link.notifyHandle == 0) {
        link.notifyHandle = chr->val_handle;
      }
      if (writable && link.writeHandle == 0) {
        link.writeHandle = chr->val_handle;
        link.writeWithoutResponse = noResponse;
      }
    }
    return 0;
  }
  uint16_t notify;
  uint16_t write;
  uint16_t end;
  {
    Lock lock;
    notify = link.notifyHandle;
    write = link.writeHandle;
    end = link.serviceEnd;
  }
  if (notify == 0 || write == 0) {
    Fail(link);
  } else if (ble_gattc_disc_all_dscs(conn, notify, end, OnDescriptor, &link) != 0) {
    Fail(link);
  }
  return 0;
}

int ObdBleLinkCallbacks::OnDescriptor(uint16_t conn,
                                      const ble_gatt_error* error,
                                      uint16_t /*chrValHandle*/,
                                      const ble_gatt_dsc* dsc,
                                      void* arg) {
  auto& link = *static_cast<ObdBleLink*>(arg);
  if (!IsDiscovering(link, conn)) {
    return 0;
  }
  if (error->status == 0 && dsc != nullptr) {
    // Discovery runs from the notify characteristic to the end of the service, so the first
    // CCCD reported is the notify characteristic's own.
    Lock lock;
    if (link.cccdHandle == 0 && ble_uuid_cmp(&dsc->uuid.u, &cccdUuid.u) == 0) {
      link.cccdHandle = dsc->handle;
    }
    return 0;
  }
  uint16_t cccd;
  {
    Lock lock;
    cccd = link.cccdHandle;
  }
  static const uint8_t enableNotifications[2] = {0x01, 0x00};
  if (cccd == 0 || ble_gattc_write_flat(conn, cccd, enableNotifications, sizeof(enableNotifications), OnSubscribed, &link) != 0) {
    Fail(link);
  }
  return 0;
}

int ObdBleLinkCallbacks::OnSubscribed(uint16_t conn, const ble_gatt_error* error, ble_gatt_attr* /*attr*/, void* arg) {
  auto& link = *static_cast<ObdBleLink*>(arg);
  if (!IsDiscovering(link, conn)) {
    return 0;
  }
  if (error->status != 0) {
    Fail(link);
    return 0;
  }
  Lock lock;
  link.state = ObdBleLink::State::Linked;
  return 0;
}
```

- [ ] **Step 3: Add to the build and compile**

In `src/CMakeLists.txt`, after `components/car/AdapterStore.cpp`, add `components/car/ObdBleLink.cpp`. Run the firmware build.

Expected: builds with no warnings from `ObdBleLink.cpp`. If `BLE_ERR_REM_USER_CONN_TERM` is undeclared, add `#include <nimble/ble.h>` inside the `min`/`max` guard.

- [ ] **Step 4: Self-review against this checklist before committing**

- Every NimBLE call is outside a `Lock` scope. Search the file for `Lock lock;` blocks and confirm none contains a `ble_` call.
- A `CONNECT` event with state not `Connecting` terminates the new connection. This is Review Focus 1.
- `DISCONNECT` compares `conn_handle` before changing state.
- Every GATT callback starts with `IsDiscovering`.
- `NOTIFY_RX` ignores handles other than `notifyHandle`.

- [ ] **Step 5: Commit**

```bash
git add src/components/car/ObdBleLink.h src/components/car/ObdBleLink.cpp src/CMakeLists.txt
git commit -m "Add ObdBleLink: NimBLE central link to BLE ELM327 adapters

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: CarLinkBegin / CarLinkEnd system messages

**Files:**
- Modify: `src/systemtask/Messages.h`
- Modify: `src/systemtask/SystemTask.cpp` (the `switch` that handles `Messages::BleRadioEnableToggle`, around line 373)

**Interfaces:**
- Produces: `Pinetime::System::Messages::CarLinkBegin` and `Pinetime::System::Messages::CarLinkEnd`, pushed with `systemTask.PushMessage(...)`.

- [ ] **Step 1: Add the enum values**

In `src/systemtask/Messages.h`, replace:

```cpp
      BleRadioEnableToggle
    };
```

with:

```cpp
      BleRadioEnableToggle,
      CarLinkBegin, // the Car app takes the BLE radio from the phone
      CarLinkEnd,   // and gives it back, if Bluetooth is enabled in Settings
    };
```

- [ ] **Step 2: Handle them**

In `src/systemtask/SystemTask.cpp`, directly after the `case Messages::BleRadioEnableToggle:` block's `break;`, add:

```cpp
        case Messages::CarLinkBegin:
          // Drops the phone and stops advertising, without touching the saved setting, so a
          // reboot mid-drive comes back with Bluetooth in its normal state.
          nimbleController.DisableRadio();
          break;
        case Messages::CarLinkEnd:
          if (settingsController.GetBleRadioEnabled()) {
            nimbleController.EnableRadio();
          }
          break;
```

- [ ] **Step 3: Build the firmware and the simulator**

Run the firmware build. Expected: success.

Then build the simulator, which compiles the real `SystemTask.cpp` against its stub `NimbleController`:

```bash
cd .deps/InfiniSim && cmake --build build-local --target infinisim -j"$(nproc)" 2>&1 | tail -3; cd -
```

Expected: `Built target infinisim`.

- [ ] **Step 4: Commit**

```bash
git add src/systemtask/Messages.h src/systemtask/SystemTask.cpp
git commit -m "Add CarLinkBegin/CarLinkEnd: hand the BLE radio between phone and car

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: ObdBleSource, with a host test double for the link

**Files:**
- Create: `src/components/car/ObdBleSource.h`, `src/components/car/ObdBleSource.cpp`
- Create: `tests/car/FakeObdBleLink.h`, `tests/car/FakeObdBleLink.cpp`
- Test: `tests/car/test_obd_ble_source.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes:
  - From Task 1: `Elm327Session` and its `PollSet` / `State`.
  - From Task 3: `ObdBleLink::State`, `Read`, `Write`, `GetState`.
  - The existing `ObdSource` from `components/car/ObdData.h`.
- Produces:
  - `class ObdBleSource : public ObdSource`
  - `using Clock = uint32_t (*)()`
  - `ObdBleSource(ObdBleLink&, Clock)`
  - `void Update() override`
  - `const ObdData& Current() const override`
  - `void SetPollSet(Elm327Session::PollSet)`
  - `bool Started() const`
  - `Elm327Session::State SessionState() const`
- Test double API, in `tests/car/FakeObdBleLink.h`:
  - `FakeLink::SetState(ObdBleLink::State)`
  - `FakeLink::Inject(const char*)`
  - `FakeLink::TakeWrites()`, which returns a `std::string` of everything written, concatenated

- [ ] **Step 1: Write the test double**

Create `tests/car/FakeObdBleLink.h`:

```cpp
#pragma once

#include <string>
#include "components/car/ObdBleLink.h"

// Host test double: stands in for src/components/car/ObdBleLink.cpp.
namespace FakeLink {
  void SetState(Pinetime::Controllers::ObdBleLink::State state);
  void Inject(const char* text); // as if the adapter had notified these bytes
  std::string TakeWrites();      // everything written since the last call
}
```

Create `tests/car/FakeObdBleLink.cpp`:

```cpp
#include "FakeObdBleLink.h"

#include <cstring>

using Pinetime::Controllers::ObdBleLink;

namespace {
  std::string writes;
}

namespace Pinetime {
  namespace Controllers {
    struct ObdBleLinkCallbacks {
      static void SetState(ObdBleLink& link, ObdBleLink::State state) {
        link.state = state;
      }
      static void Push(ObdBleLink& link, const char* text) {
        for (const char* p = text; *p != '\0'; p++) {
          const size_t next = (link.rxHead + 1) % ObdBleLink::rxCapacity;
          if (next == link.rxTail) {
            return;
          }
          link.rx[link.rxHead] = static_cast<uint8_t>(*p);
          link.rxHead = next;
        }
      }
    };
  }
}

ObdBleLink ObdBleLink::instance;

ObdBleLink& ObdBleLink::Instance() {
  return instance;
}

ObdBleLink::State ObdBleLink::GetState() const {
  return state;
}

size_t ObdBleLink::FoundCount() const {
  return foundCount;
}

bool ObdBleLink::Found(size_t index, FoundDevice& out) const {
  if (index >= foundCount) {
    return false;
  }
  out = found[index];
  return true;
}

void ObdBleLink::StartScan(uint32_t) {
}

void ObdBleLink::Connect(const Address&) {
}

void ObdBleLink::Disconnect() {
  state = State::Idle;
  rxHead = rxTail = 0;
}

bool ObdBleLink::Write(const char* text) {
  if (state != State::Linked) {
    return false;
  }
  writes += text;
  return true;
}

size_t ObdBleLink::Read(uint8_t* out, size_t cap) {
  size_t count = 0;
  while (count < cap && rxTail != rxHead) {
    out[count++] = rx[rxTail];
    rxTail = (rxTail + 1) % rxCapacity;
  }
  return count;
}

void FakeLink::SetState(ObdBleLink::State state) {
  Pinetime::Controllers::ObdBleLinkCallbacks::SetState(ObdBleLink::Instance(), state);
}

void FakeLink::Inject(const char* text) {
  Pinetime::Controllers::ObdBleLinkCallbacks::Push(ObdBleLink::Instance(), text);
}

std::string FakeLink::TakeWrites() {
  std::string out;
  out.swap(writes);
  return out;
}
```

- [ ] **Step 2: Write the failing test**

Create `tests/car/test_obd_ble_source.cpp`:

```cpp
// Host-side tests for ObdBleSource against a fake link. From the repo root:
//   g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -Itests/car tests/car/test_obd_ble_source.cpp tests/car/FakeObdBleLink.cpp src/components/car/ObdBleSource.cpp src/components/car/Elm327Session.cpp -o /tmp/test_obd_ble_source && /tmp/test_obd_ble_source
#include "FakeObdBleLink.h"
#include "components/car/ObdBleSource.h"
#include <cstdio>

using namespace Pinetime::Controllers;

static int failures = 0;
#define CHECK(cond)                                                                                  \
  do {                                                                                               \
    if (!(cond)) {                                                                                   \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                  \
      failures++;                                                                                    \
    }                                                                                                \
  } while (0)

static uint32_t fakeNow = 0;
static uint32_t FakeClock() {
  return fakeNow;
}

// Answers each start-up command in turn until the session is Ready.
static void AnswerStartup(ObdBleSource& source) {
  const char* replies[] = {"OK\r\r>", "OK\r\r>", "OK\r\r>", "OK\r\r>", "OK\r\r>", "4100BE1FA813\r\r>"};
  for (const char* reply : replies) {
    source.Update();
    FakeLink::Inject(reply);
  }
  source.Update(); // consumes the probe reply
}

int main() {
  ObdBleLink& link = ObdBleLink::Instance();

  std::puts("not linked: no writes, not connected");
  {
    link.Disconnect();
    ObdBleSource source(link, FakeClock);
    source.Update();
    CHECK(FakeLink::TakeWrites().empty());
    CHECK(!source.Current().connected);
    CHECK(!source.Started());
  }

  std::puts("linked: start-up, then speed polling feeds Current()");
  {
    link.Disconnect();
    FakeLink::TakeWrites();
    ObdBleSource source(link, FakeClock);
    source.SetPollSet(Elm327Session::PollSet::Speed);
    FakeLink::SetState(ObdBleLink::State::Linked);
    source.Update();
    CHECK(source.Started());
    CHECK(FakeLink::TakeWrites() == "ATE0\r");
    FakeLink::Inject("OK\r\r>");
    for (int i = 0; i < 4; i++) {
      source.Update();
      FakeLink::Inject("OK\r\r>");
    }
    source.Update();
    CHECK(FakeLink::TakeWrites() == "ATL0\rATS0\rATH0\rATSP0\r0100\r");
    FakeLink::Inject("4100BE1FA813\r\r>");
    source.Update();
    CHECK(source.SessionState() == Elm327Session::State::Ready);
    CHECK(FakeLink::TakeWrites() == "010D\r");
    FakeLink::Inject("410D3C\r\r>");
    source.Update();
    CHECK(source.Current().connected);
    CHECK(source.Current().speedKmh == 60);
  }

  std::puts("link lost: back to disconnected; relink restarts the session");
  {
    link.Disconnect();
    FakeLink::TakeWrites();
    ObdBleSource source(link, FakeClock);
    source.SetPollSet(Elm327Session::PollSet::Speed);
    FakeLink::SetState(ObdBleLink::State::Linked);
    AnswerStartup(source);
    FakeLink::Inject("410D3C\r\r>");
    source.Update();
    CHECK(source.Current().connected);
    FakeLink::SetState(ObdBleLink::State::Lost);
    source.Update();
    CHECK(!source.Current().connected);
    CHECK(!source.Started());
    FakeLink::TakeWrites();
    FakeLink::SetState(ObdBleLink::State::Linked);
    source.Update();
    CHECK(FakeLink::TakeWrites() == "ATE0\r");
  }

  std::puts("timeouts use the injected clock");
  {
    link.Disconnect();
    FakeLink::TakeWrites();
    fakeNow = 0;
    ObdBleSource source(link, FakeClock);
    FakeLink::SetState(ObdBleLink::State::Linked);
    source.Update(); // ATE0, never answered
    FakeLink::TakeWrites();
    fakeNow = 1499;
    source.Update();
    CHECK(FakeLink::TakeWrites().empty());
    fakeNow = 1500;
    source.Update();
    CHECK(FakeLink::TakeWrites() == "ATL0\r");
  }

  std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Run it to verify it fails**

Run the command at the top of the test file. Expected: `components/car/ObdBleSource.h: No such file or directory`.

- [ ] **Step 4: Implement**

Create `src/components/car/ObdBleSource.h`:

```cpp
#pragma once

#include <cstdint>
#include "components/car/Elm327Session.h"
#include "components/car/ObdData.h"

namespace Pinetime {
  namespace Controllers {
    class ObdBleLink;

    // ObdSource backed by a real adapter: moves bytes between ObdBleLink and an Elm327Session.
    // Runs entirely on the caller's task (the Car screen's refresh, on the display task).
    // The clock is injected so the class runs in host tests; the Car screen passes lv_tick_get.
    class ObdBleSource : public ObdSource {
    public:
      using Clock = uint32_t (*)();

      ObdBleSource(ObdBleLink& link, Clock clock);

      void Update() override;
      const ObdData& Current() const override;

      void SetPollSet(Elm327Session::PollSet set) {
        session.SetPollSet(set);
      }

      // True once the link is up and the session has started talking to the adapter.
      bool Started() const {
        return started;
      }

      Elm327Session::State SessionState() const {
        return session.GetState();
      }

    private:
      ObdBleLink& link;
      Clock clock;
      Elm327Session session;
      bool started = false;
      ObdData disconnected {};
    };
  }
}
```

Create `src/components/car/ObdBleSource.cpp`:

```cpp
#include "components/car/ObdBleSource.h"
#include "components/car/ObdBleLink.h"

using namespace Pinetime::Controllers;

ObdBleSource::ObdBleSource(ObdBleLink& link, Clock clock) : link {link}, clock {clock} {
}

void ObdBleSource::Update() {
  if (link.GetState() != ObdBleLink::State::Linked) {
    started = false;
    return;
  }
  const uint32_t now = clock();
  if (!started) {
    session.Reset(now);
    started = true;
  }
  uint8_t buffer[64];
  size_t count;
  while ((count = link.Read(buffer, sizeof(buffer))) > 0) {
    session.Received(buffer, count);
  }
  session.Tick(now);
  char command[12];
  if (session.NextCommand(command, sizeof(command), now)) {
    link.Write(command);
  }
}

const ObdData& ObdBleSource::Current() const {
  return started ? session.Data() : disconnected;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run the command at the top of `tests/car/test_obd_ble_source.cpp`. Expected: `ALL PASS`.

Also re-run the Task 1 and Task 2 tests. Expected: `ALL PASS` for both.

- [ ] **Step 6: Add to the build, compile, commit**

In `src/CMakeLists.txt`, after `components/car/ObdBleLink.cpp`, add `components/car/ObdBleSource.cpp`. Run the firmware build. Expected: success.

```bash
git add src/components/car/ObdBleSource.* tests/car/FakeObdBleLink.* tests/car/test_obd_ble_source.cpp src/CMakeLists.txt
git commit -m "Add ObdBleSource: drive an Elm327Session over ObdBleLink

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Car screen (CONNECT, DEVS, Devices view, status, wake lock)

**Files:**
- Modify: `src/displayapp/screens/Car.h` (replace the whole file)
- Modify: `src/displayapp/screens/Car.cpp`

**Interfaces:**
- Consumes:
  - From Task 3: `ObdBleLink::Instance()`, `StartScan`, `Connect`, `Disconnect`, `GetState`, `FoundCount`, `Found`, `Address`, `FoundDevice`, `maxFound`.
  - From Task 5: `ObdBleSource(ObdBleLink&, Clock)`, `Update`, `Current`, `SetPollSet`, `Started`, `SessionState`.
  - From Task 2: `AdapterStore::Load` / `Save`, `SavedAdapter`.
  - From Task 4: `System::Messages::CarLinkBegin` / `CarLinkEnd`.
  - Existing: `System::WakeLock(SystemTask&)` with idempotent `Lock` / `Release`, `Controllers::Ble::IsConnected()`, `AppControllers::systemTask`, `bleController`, `filesystem`.
- Produces: `Car(System::SystemTask&, const Controllers::Ble&, Controllers::FS&)` and the updated `AppTraits<Apps::Car>::Create`.

**Layout (240x240)**

| Element | Position |
|---|---|
| View buttons | height 34 at y = 40, 80, 120 |
| CONNECT | x 20, y 162, 96 x 34 |
| DEVS | x 124, y 162, 96 x 34 |
| Menu status line | top-centre, y 212 |

**Devices view**

| Element | Position |
|---|---|
| `DEVS` title | top-left |
| Status | top-right, same line as the title |
| Device rows (up to 6) | 224 x 24 at y = 28 + 26*i |
| Footnote `ANDROID-VLINK won't appear` | y 186 |
| RESCAN | 120 x 30 at x 60, y 206 |

**Deviations from the spec in this task** (Task 8 amends the spec):
- In the Devices view, the status shares the title line.
- The footnote is shortened to fit one line.
- When a saved adapter exists and nothing is happening, the status reads `SAVED <name>`.
- DEVS while linked shows `DISCONNECT FIRST` instead of scanning.
- The side button returns to the menu from any other view.

- [ ] **Step 1: Replace `Car.h`**

Replace the whole of `src/displayapp/screens/Car.h` with:

```cpp
#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>
#include "displayapp/screens/Screen.h"
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "components/car/ObdSimulator.h"
#include "components/car/ObdBleLink.h"
#include "components/car/ObdBleSource.h"
#include "components/car/SavedAdapter.h"
#include "systemtask/WakeLock.h"
#include "Symbols.h"

namespace Pinetime {
  namespace Controllers {
    class Ble;
    class FS;
  }

  namespace System {
    class SystemTask;
  }

  namespace Applications {
    namespace Screens {

      // OBD-II car dashboard. A menu leads to a Speed dial, a Boost/Vacuum dial and a multi-stat
      // HUD. Data comes from a BLE ELM327 adapter (CONNECT links the saved one, DEVS finds one) or,
      // in demo mode, the built-in simulator. While linked the car holds the BLE radio instead of
      // the phone and the screen stays on; leaving the app always gives the radio back.
      class Car : public Screen {
      public:
        Car(System::SystemTask& systemTask, const Controllers::Ble& bleController, Controllers::FS& filesystem);
        ~Car() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;
        bool OnButtonPushed() override;

        void OnMenuButton(uint8_t code);

        struct MenuItem {
          Car* self;
          uint8_t code;
        };

        // MenuItem codes beyond the View values
        static constexpr uint8_t toggleDemo = 255;
        static constexpr uint8_t connectButton = 254;
        static constexpr uint8_t devicesButton = 253;
        static constexpr uint8_t rescanButton = 252;
        static constexpr uint8_t deviceBase = 200; // + index into ObdBleLink's found list

      private:
        enum class View : uint8_t { Menu, Speed, Boost, Hud, Devices };
        enum class Phase : uint8_t { Idle, WaitingForRadio, Active };

        static constexpr uint32_t scanDurationMs = 6000;
        static constexpr uint32_t radioWaitMs = 1000;
        static constexpr size_t maxListed = 6;
        static constexpr size_t maxItems = 16;

        const Controllers::ObdData& Data() const;

        void SwitchTo(View view);
        void BuildMenu();
        void BuildSpeed();
        void BuildBoost();
        void BuildHud();
        void BuildDevices();
        void RefreshSpeed();
        void RefreshBoost();
        void RefreshHud();
        void RefreshStatus();

        void BeginScan();
        void BeginLink(const Controllers::ObdBleLink::Address& address, const char* name, bool saveOnSuccess);
        void EndLink();
        void TrackLink();
        void ReturnRadio();
        bool LinkBusy() const;
        uint32_t ComposeStatus(char* out, size_t cap) const;

        MenuItem* NewItem(uint8_t code);
        lv_obj_t* CreateButton(lv_coord_t x,
                               lv_coord_t y,
                               lv_coord_t w,
                               lv_coord_t h,
                               uint32_t color,
                               const char* text,
                               const lv_font_t* font,
                               uint8_t code);
        lv_obj_t* CreateStatTile(lv_coord_t x, lv_coord_t y, const char* caption, uint32_t color, lv_obj_t** valueLabel);

        System::SystemTask& systemTask;
        const Controllers::Ble& bleController;
        Controllers::FS& filesystem;
        System::WakeLock wakeLock;

        Controllers::ObdSimulator obd;
        Controllers::ObdBleLink& link;
        Controllers::ObdBleSource bleSource;

        Controllers::SavedAdapter saved {};
        bool hasSaved = false;

        Phase phase = Phase::Idle;
        bool radioTaken = false; // CarLinkBegin sent and not yet matched by CarLinkEnd
        bool saveOnLink = false;
        uint32_t radioWaitStart = 0;
        Controllers::ObdBleLink::Address pendingAddress {};
        char linkName[Controllers::SavedAdapter::nameCapacity] {};
        const char* notice = nullptr; // one-off message shown until the next button press
        size_t listedCount = 0;
        bool listedScanning = false;
        char statusText[24] {};

        bool demoMode = false; // off by default

        View currentView = View::Menu;
        View pendingView = View::Menu;
        bool viewDirty = true;

        MenuItem items[maxItems];
        uint8_t itemCount = 0;

        // Speed view
        lv_obj_t* speedGauge = nullptr;
        lv_obj_t* speedValue = nullptr;
        lv_obj_t* speedUnit = nullptr;
        lv_obj_t* rpmBar = nullptr;
        lv_obj_t* rpmValue = nullptr;

        // Boost view
        lv_obj_t* boostGauge = nullptr;
        lv_obj_t* boostValue = nullptr;
        lv_obj_t* boostUnit = nullptr;

        // HUD view
        lv_obj_t* hudCoolant = nullptr;
        lv_obj_t* hudIntake = nullptr;
        lv_obj_t* hudRpm = nullptr;
        lv_obj_t* hudThrottle = nullptr;
        lv_obj_t* hudO2 = nullptr;
        lv_obj_t* hudBattery = nullptr;
        lv_obj_t* hudSpeed = nullptr;
        lv_obj_t* hudMap = nullptr;

        lv_obj_t* statusLabel = nullptr;

        lv_task_t* taskRefresh = nullptr;
      };
    }

    template <>
    struct AppTraits<Apps::Car> {
      static constexpr Apps app = Apps::Car;
      static constexpr const char* icon = Screens::Symbols::car;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::Car(*controllers.systemTask, controllers.bleController, controllers.filesystem);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
```

Before replacing the file, check that the old `Car.h` has no other members than those listed here, apart from `menuItems` and `disconnected`, which are intentionally removed:

```bash
git show HEAD:src/displayapp/screens/Car.h | grep -n "lv_obj_t\*\|bool \|View \|Controllers::"
```

- [ ] **Step 2: Update the top of `Car.cpp`**

Add these includes under the existing ones in `src/displayapp/screens/Car.cpp`:

```cpp
#include <cctype>
#include <cstdio>
#include <cstring>
#include "components/car/AdapterStore.h"
#include "components/ble/BleController.h"
#include "systemtask/SystemTask.h"
```

Add these to the anonymous namespace, after `MenuButtonHandler`:

```cpp
  // Adapters sort first in the Devices list: IOS-Vlink, "OBDII", "ELM327 ..." and the like.
  bool LooksLikeAdapter(const char* name) {
    char upper[24];
    size_t i = 0;
    for (; name[i] != '\0' && i < sizeof(upper) - 1; i++) {
      upper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
    }
    upper[i] = '\0';
    return strstr(upper, "VLINK") != nullptr || strstr(upper, "OBD") != nullptr || strstr(upper, "ELM") != nullptr;
  }

  uint32_t TickMs() {
    return lv_tick_get();
  }
```

`MenuButtonHandler` stays as it is, except that it now reads `item->code`:

```cpp
  void MenuButtonHandler(lv_obj_t* obj, lv_event_t event) {
    if (event != LV_EVENT_CLICKED) {
      return;
    }
    auto* item = static_cast<Car::MenuItem*>(obj->user_data);
    if (item != nullptr) {
      item->self->OnMenuButton(item->code);
    }
  }
```

- [ ] **Step 3: Replace the constructor through `OnTouchEvent`**

Replace everything from `Car::Car() {` down to the end of `Car::OnTouchEvent` with:

```cpp
Car::Car(System::SystemTask& systemTask, const Controllers::Ble& bleController, Controllers::FS& filesystem)
  : systemTask {systemTask},
    bleController {bleController},
    filesystem {filesystem},
    wakeLock {systemTask},
    link {Controllers::ObdBleLink::Instance()},
    bleSource {link, TickMs} {
  link.Disconnect(); // clear whatever a previous visit left behind
  hasSaved = Controllers::AdapterStore::Load(filesystem, saved);
  lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
  SwitchTo(View::Menu);
}

Car::~Car() {
  EndLink();
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void Car::OnMenuButton(uint8_t code) {
  notice = nullptr;
  switch (code) {
    case toggleDemo:
      demoMode = !demoMode;
      viewDirty = true; // rebuild the menu so the chip updates
      return;
    case connectButton:
      if (LinkBusy()) {
        EndLink();
      } else if (!hasSaved) {
        notice = "USE DEVS FIRST";
      } else {
        Controllers::ObdBleLink::Address address {saved.addressType, {}};
        memcpy(address.value, saved.address, sizeof(address.value));
        BeginLink(address, saved.name, false);
      }
      viewDirty = true; // relabel CONNECT / DISCONNECT
      return;
    case devicesButton:
      SwitchTo(View::Devices);
      BeginScan();
      return;
    case rescanButton:
      BeginScan();
      viewDirty = true;
      return;
    default:
      break;
  }
  if (code >= deviceBase && code < deviceBase + Controllers::ObdBleLink::maxFound) {
    Controllers::ObdBleLink::FoundDevice device;
    if (link.Found(code - deviceBase, device)) {
      BeginLink(device.address, device.name, true);
      SwitchTo(View::Menu);
    }
    return;
  }
  SwitchTo(static_cast<View>(code));
}

const Pinetime::Controllers::ObdData& Car::Data() const {
  return demoMode ? obd.Current() : bleSource.Current();
}

void Car::SwitchTo(View view) {
  pendingView = view;
  viewDirty = true;
}

bool Car::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  // Swipe down returns to the menu from any other view; on the menu, let the OS handle it (exit).
  if (event == TouchEvents::SwipeDown && currentView != View::Menu) {
    SwitchTo(View::Menu);
    return true;
  }
  return false;
}

bool Car::OnButtonPushed() {
  if (currentView != View::Menu) {
    SwitchTo(View::Menu);
    return true;
  }
  return false; // on the menu the button leaves the app, which ends any link
}

bool Car::LinkBusy() const {
  if (phase != Phase::Idle) {
    return true;
  }
  const auto state = link.GetState();
  return state == Controllers::ObdBleLink::State::Connecting || state == Controllers::ObdBleLink::State::Discovering ||
         state == Controllers::ObdBleLink::State::Linked;
}

void Car::BeginScan() {
  if (LinkBusy()) {
    notice = "DISCONNECT FIRST";
    return;
  }
  listedCount = 0;
  link.StartScan(scanDurationMs);
}

void Car::BeginLink(const Controllers::ObdBleLink::Address& address, const char* name, bool saveOnSuccess) {
  link.Disconnect(); // stops a scan, clears an old failure
  pendingAddress = address;
  strncpy(linkName, name, sizeof(linkName) - 1);
  linkName[sizeof(linkName) - 1] = '\0';
  saveOnLink = saveOnSuccess;
  if (!radioTaken) {
    systemTask.PushMessage(System::Messages::CarLinkBegin);
    radioTaken = true;
  }
  radioWaitStart = lv_tick_get();
  phase = Phase::WaitingForRadio;
}

void Car::EndLink() {
  link.Disconnect();
  phase = Phase::Idle;
  wakeLock.Release();
  ReturnRadio();
}

void Car::ReturnRadio() {
  if (radioTaken) {
    systemTask.PushMessage(System::Messages::CarLinkEnd);
    radioTaken = false;
  }
}

void Car::TrackLink() {
  // Only one BLE connection is allowed, so wait for the phone to let go first.
  if (phase == Phase::WaitingForRadio && (!bleController.IsConnected() || lv_tick_elaps(radioWaitStart) >= radioWaitMs)) {
    link.Connect(pendingAddress);
    phase = Phase::Active;
  }
  if (phase != Phase::Active) {
    return;
  }
  switch (link.GetState()) {
    case Controllers::ObdBleLink::State::Linked:
      wakeLock.Lock();
      if (saveOnLink) {
        saved.addressType = pendingAddress.type;
        memcpy(saved.address, pendingAddress.value, sizeof(saved.address));
        memcpy(saved.name, linkName, sizeof(saved.name));
        Controllers::AdapterStore::Save(filesystem, saved);
        hasSaved = true;
        saveOnLink = false;
      }
      break;
    case Controllers::ObdBleLink::State::Failed:
    case Controllers::ObdBleLink::State::Lost:
    case Controllers::ObdBleLink::State::Idle:
      wakeLock.Release();
      ReturnRadio();
      phase = Phase::Idle;
      if (currentView == View::Menu) {
        viewDirty = true; // DISCONNECT goes back to CONNECT
      }
      break;
    default:
      break;
  }
}
```

- [ ] **Step 4: Replace `BuildMenu` and add the button helpers and `BuildDevices`**

Replace the whole of `Car::BuildMenu()` with the code below. Keep `Car::CreateStatTile` as it is.

```cpp
Car::MenuItem* Car::NewItem(uint8_t code) {
  if (itemCount >= maxItems) {
    return nullptr;
  }
  items[itemCount] = {this, code};
  return &items[itemCount++];
}

lv_obj_t* Car::CreateButton(lv_coord_t x,
                            lv_coord_t y,
                            lv_coord_t w,
                            lv_coord_t h,
                            uint32_t color,
                            const char* text,
                            const lv_font_t* font,
                            uint8_t code) {
  lv_obj_t* btn = lv_btn_create(lv_scr_act(), nullptr);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorPanel));
  lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
  lv_obj_set_style_local_border_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
  lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 6);
  btn->user_data = NewItem(code);
  lv_obj_set_event_cb(btn, MenuButtonHandler);
  lv_obj_t* label = lv_label_create(btn, nullptr);
  StyleLabel(label, font, color);
  lv_label_set_text(label, text); // copied: device names are temporaries
  return btn;
}

void Car::BuildMenu() {
  itemCount = 0;

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(title, true);
  lv_label_set_text_fmt(title, "#00d5ff %s# CAR", Symbols::car);
  lv_obj_align(title, nullptr, LV_ALIGN_IN_TOP_LEFT, 8, 8);

  // DEMO toggle chip, top-right
  lv_obj_t* chip = lv_btn_create(lv_scr_act(), nullptr);
  lv_obj_set_size(chip, 74, 26);
  lv_obj_align(chip, nullptr, LV_ALIGN_IN_TOP_RIGHT, -6, 6);
  const uint32_t chipColor = demoMode ? colorBoost : colorCaption;
  lv_obj_set_style_local_bg_color(chip, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(demoMode ? colorBoost : colorPanel));
  lv_obj_set_style_local_border_width(chip, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
  lv_obj_set_style_local_border_color(chip, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(chipColor));
  lv_obj_set_style_local_radius(chip, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 13);
  chip->user_data = NewItem(toggleDemo);
  lv_obj_set_event_cb(chip, MenuButtonHandler);
  lv_obj_t* chipLabel = lv_label_create(chip, nullptr);
  StyleLabel(chipLabel, &jetbrains_mono_bold_14, demoMode ? 0x000000 : chipColor);
  lv_label_set_text_static(chipLabel, demoMode ? "DEMO ON" : "DEMO");

  const char* labels[3] = {"SPEED", "BOOST / VAC", "HUD"};
  const uint32_t colors[3] = {colorSpeed, colorBoost, colorAccent};
  for (uint8_t i = 0; i < 3; i++) {
    CreateButton(20,
                 static_cast<lv_coord_t>(40 + i * 40),
                 screenSize - 40,
                 34,
                 colors[i],
                 labels[i],
                 &jetbrains_mono_bold_20,
                 static_cast<uint8_t>(static_cast<uint8_t>(View::Speed) + i));
  }

  const bool busy = LinkBusy();
  CreateButton(20, 162, 96, 34, busy ? colorWarn : colorSpeed, busy ? "DISCONNECT" : "CONNECT", &jetbrains_mono_bold_14, connectButton);
  CreateButton(124, 162, 96, 34, colorAccent, "DEVS", &jetbrains_mono_bold_14, devicesButton);

  statusLabel = lv_label_create(lv_scr_act(), nullptr);
  StyleLabel(statusLabel, &jetbrains_mono_bold_14, colorCaption);
  statusText[0] = '\0'; // force RefreshStatus to write it
  RefreshStatus();
}

void Car::BuildDevices() {
  itemCount = 0;

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  StyleLabel(title, &jetbrains_mono_bold_14, colorAccent);
  lv_label_set_text_static(title, "DEVS");
  lv_obj_set_pos(title, 8, 8);

  // Adapters first, then strongest signal. Codes keep the link's own index.
  Controllers::ObdBleLink::FoundDevice devices[Controllers::ObdBleLink::maxFound];
  uint8_t order[Controllers::ObdBleLink::maxFound];
  size_t count = 0;
  for (size_t i = 0; i < Controllers::ObdBleLink::maxFound && link.Found(i, devices[count]); i++) {
    order[count] = static_cast<uint8_t>(i);
    count++;
  }
  auto before = [&](size_t a, size_t b) {
    const bool adapterA = LooksLikeAdapter(devices[a].name);
    const bool adapterB = LooksLikeAdapter(devices[b].name);
    return adapterA != adapterB ? adapterA : devices[a].rssi > devices[b].rssi;
  };
  size_t rank[Controllers::ObdBleLink::maxFound];
  for (size_t i = 0; i < count; i++) {
    rank[i] = i;
  }
  for (size_t i = 1; i < count; i++) { // insertion sort: at most 8 entries
    for (size_t j = i; j > 0 && before(rank[j], rank[j - 1]); j--) {
      const size_t t = rank[j];
      rank[j] = rank[j - 1];
      rank[j - 1] = t;
    }
  }

  listedCount = count;
  listedScanning = link.GetState() == Controllers::ObdBleLink::State::Scanning;

  const size_t shown = count < maxListed ? count : maxListed;
  for (size_t r = 0; r < shown; r++) {
    const auto& device = devices[rank[r]];
    char text[32];
    snprintf(text, sizeof(text), "%-14.14s %4d", device.name, device.rssi);
    CreateButton(8,
                 static_cast<lv_coord_t>(28 + r * 26),
                 screenSize - 16,
                 24,
                 LooksLikeAdapter(device.name) ? colorSpeed : colorCaption,
                 text,
                 &jetbrains_mono_bold_14,
                 static_cast<uint8_t>(deviceBase + order[rank[r]]));
  }
  if (shown == 0 && !listedScanning) {
    lv_obj_t* none = lv_label_create(lv_scr_act(), nullptr);
    StyleLabel(none, &jetbrains_mono_bold_14, colorWarn);
    lv_label_set_text_static(none, "NO DEVICES FOUND");
    lv_obj_align(none, nullptr, LV_ALIGN_IN_TOP_MID, 0, 90);
  }

  lv_obj_t* footnote = lv_label_create(lv_scr_act(), nullptr);
  StyleLabel(footnote, &jetbrains_mono_bold_14, colorCaption);
  lv_label_set_text_static(footnote, "ANDROID-VLINK won't appear");
  lv_obj_set_pos(footnote, 8, 186);

  CreateButton(60, 206, 120, 30, colorAccent, "RESCAN", &jetbrains_mono_bold_14, rescanButton);

  statusLabel = lv_label_create(lv_scr_act(), nullptr);
  StyleLabel(statusLabel, &jetbrains_mono_bold_14, colorCaption);
  statusText[0] = '\0';
  RefreshStatus();
}
```

- [ ] **Step 5: Replace `Refresh` and add the status functions**

Replace the whole of `Car::Refresh()` with the code below. Keep `RefreshSpeed`, `RefreshBoost` and `RefreshHud` as they are.

```cpp
void Car::Refresh() {
  TrackLink();

  if (currentView == View::Devices) {
    const bool scanning = link.GetState() == Controllers::ObdBleLink::State::Scanning;
    if (link.FoundCount() != listedCount || scanning != listedScanning) {
      viewDirty = true;
    }
  }

  if (viewDirty) {
    viewDirty = false;
    lv_obj_clean(lv_scr_act());
    speedGauge = boostGauge = nullptr;
    hudCoolant = nullptr;
    statusLabel = nullptr;
    currentView = pendingView;
    switch (currentView) {
      case View::Menu:
        BuildMenu();
        break;
      case View::Speed:
        BuildSpeed();
        break;
      case View::Boost:
        BuildBoost();
        break;
      case View::Hud:
        BuildHud();
        break;
      case View::Devices:
        BuildDevices();
        break;
    }
  }

  // Poll only what is on screen.
  switch (currentView) {
    case View::Speed:
      bleSource.SetPollSet(Controllers::Elm327Session::PollSet::Speed);
      break;
    case View::Boost:
      bleSource.SetPollSet(Controllers::Elm327Session::PollSet::Boost);
      break;
    case View::Hud:
      bleSource.SetPollSet(Controllers::Elm327Session::PollSet::Hud);
      break;
    case View::Menu:
    case View::Devices:
      bleSource.SetPollSet(Controllers::Elm327Session::PollSet::None);
      break;
  }
  bleSource.Update();
  if (demoMode) {
    obd.Update();
  }

  switch (currentView) {
    case View::Speed:
      RefreshSpeed();
      break;
    case View::Boost:
      RefreshBoost();
      break;
    case View::Hud:
      RefreshHud();
      break;
    case View::Menu:
    case View::Devices:
      RefreshStatus();
      break;
  }
}

uint32_t Car::ComposeStatus(char* out, size_t cap) const {
  using LinkState = Controllers::ObdBleLink::State;
  using SessionState = Controllers::Elm327Session::State;
  const LinkState state = link.GetState();
  if (notice != nullptr) {
    snprintf(out, cap, "%s", notice);
    return colorWarn;
  }
  if (state == LinkState::Scanning) {
    snprintf(out, cap, "SCANNING");
    return colorAccent;
  }
  if (phase == Phase::WaitingForRadio || state == LinkState::Connecting || state == LinkState::Discovering) {
    snprintf(out, cap, "CONNECTING %.9s", linkName);
    return colorAccent;
  }
  if (state == LinkState::Linked) {
    if (!bleSource.Started() || bleSource.SessionState() == SessionState::Starting) {
      snprintf(out, cap, "STARTING ELM327");
      return colorAccent;
    }
    switch (bleSource.SessionState()) {
      case SessionState::Searching:
        snprintf(out, cap, "SEARCHING PROTOCOL");
        return colorAccent;
      case SessionState::EcuNotResponding:
        snprintf(out, cap, "ECU NOT RESPONDING");
        return colorWarn;
      default:
        snprintf(out, cap, "LINKED %.13s", linkName);
        return colorSpeed;
    }
  }
  if (state == LinkState::Failed) {
    snprintf(out, cap, "ADAPTER NOT FOUND");
    return colorWarn;
  }
  if (state == LinkState::Lost) {
    snprintf(out, cap, "ADAPTER LOST");
    return colorWarn;
  }
  if (currentView == View::Devices && state == LinkState::ScanDone) {
    snprintf(out, cap, "%u FOUND", static_cast<unsigned>(link.FoundCount()));
    return colorCaption;
  }
  if (demoMode) {
    snprintf(out, cap, "DEMO DATA");
    return colorBoost;
  }
  if (hasSaved) {
    snprintf(out, cap, "SAVED %.14s", saved.name);
    return colorCaption;
  }
  snprintf(out, cap, "NO ADAPTER");
  return colorWarn;
}

void Car::RefreshStatus() {
  if (statusLabel == nullptr) {
    return;
  }
  char text[sizeof(statusText)];
  const uint32_t color = ComposeStatus(text, sizeof(text));
  if (strcmp(text, statusText) == 0) {
    return;
  }
  memcpy(statusText, text, sizeof(statusText));
  lv_label_set_text(statusLabel, statusText);
  lv_obj_set_style_local_text_color(statusLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
  if (currentView == View::Devices) {
    lv_obj_align(statusLabel, nullptr, LV_ALIGN_IN_TOP_RIGHT, -8, 8);
  } else {
    lv_obj_align(statusLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 212);
  }
}
```

- [ ] **Step 6: Remove what is now dead**

Search the file for `menuItems` and `disconnected`. Expected: no hits in `Car.cpp`.

The old `BuildMenu` created the status label at y 200 and set `NO ADAPTER` / `DEMO DATA` itself. Confirm that code is gone, since `RefreshStatus` owns it now.

- [ ] **Step 7: Build and verify**

Run the firmware build, with a fresh directory. Expected: success and no warnings in `Car.cpp`.

Then:

```bash
NM=.deps/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-nm
$NM -C build-ble/src/pinetime-mcuboot-app-*.out | grep -c "ObdBleLinkCallbacks::OnGap"
```

Expected: `1`.

Note the flash line. Expected: about 6 to 9 KB above 428,344 bytes, and below 470,000.

- [ ] **Step 8: Commit**

```bash
git add src/displayapp/screens/Car.h src/displayapp/screens/Car.cpp
git commit -m "Car: CONNECT/DEVS, Devices view and real adapter link

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Simulator fake link and screenshots

**Files (local only, not committed to the fork):**
- Create: `.deps/InfiniSim/sim/components/car/ObdBleLink.cpp`
- Modify: `.deps/InfiniSim/CMakeLists.txt`

**Interfaces:**
- Consumes: the `ObdBleLink.h` interface and fields from Task 3.
- Produces: a scripted link for the simulator, driven by these environment variables:
  - `INFINISIM_CAR_FAIL=notfound|lost`
  - `INFINISIM_CAR_ECU=off`

- [ ] **Step 1: Write the fake**

Create `.deps/InfiniSim/sim/components/car/ObdBleLink.cpp`:

```cpp
// Simulator stand-in for src/components/car/ObdBleLink.cpp: no radio, a scripted adapter.
//   INFINISIM_CAR_FAIL=notfound  connect fails after 1.2 s
//   INFINISIM_CAR_FAIL=lost      link drops 8 s after linking
//   INFINISIM_CAR_ECU=off        adapter answers but the car does not (ignition off)
#include "components/car/ObdBleLink.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <lvgl/lvgl.h>

using namespace Pinetime::Controllers;

ObdBleLink ObdBleLink::instance;

namespace {
  uint32_t scanStart = 0;
  uint32_t scanDuration = 0;
  uint32_t connectStart = 0;
  uint32_t linkedAt = 0;

  bool Env(const char* name, const char* value) {
    const char* v = getenv(name);
    return v != nullptr && strcmp(v, value) == 0;
  }

  struct Scripted {
    uint32_t atMs;
    const char* name;
    int8_t rssi;
    uint8_t lastByte;
  } scripted[] = {
    {700, "IOS-Vlink", -58, 0x01},
    {1500, "Pixel Buds Pro", -71, 0x02},
    {2500, "OBDII", -80, 0x03},
  };
}

namespace Pinetime {
  namespace Controllers {
    struct ObdBleLinkCallbacks {
      static void Advance(ObdBleLink& link) {
        const uint32_t now = lv_tick_get();
        if (link.state == ObdBleLink::State::Scanning) {
          for (const auto& s : scripted) {
            bool known = false;
            for (size_t i = 0; i < link.foundCount; i++) {
              known |= link.found[i].address.value[5] == s.lastByte;
            }
            if (!known && now - scanStart >= s.atMs && link.foundCount < ObdBleLink::maxFound) {
              auto& d = link.found[link.foundCount++];
              d.address = {0, {0xC0, 0xFF, 0xEE, 0x00, 0x00, s.lastByte}};
              snprintf(d.name, sizeof(d.name), "%s", s.name);
              d.rssi = s.rssi;
            }
          }
          if (now - scanStart >= scanDuration) {
            link.state = ObdBleLink::State::ScanDone;
          }
        } else if (link.state == ObdBleLink::State::Connecting && now - connectStart >= 600) {
          link.state = ObdBleLink::State::Discovering;
        } else if (link.state == ObdBleLink::State::Discovering && now - connectStart >= 1200) {
          link.state = Env("INFINISIM_CAR_FAIL", "notfound") ? ObdBleLink::State::Failed : ObdBleLink::State::Linked;
          linkedAt = now;
        } else if (link.state == ObdBleLink::State::Linked && Env("INFINISIM_CAR_FAIL", "lost") && now - linkedAt >= 8000) {
          link.state = ObdBleLink::State::Lost;
        }
      }

      static void Reply(ObdBleLink& link, const char* text) {
        for (const char* p = text; *p != '\0'; p++) {
          const size_t next = (link.rxHead + 1) % ObdBleLink::rxCapacity;
          if (next == link.rxTail) {
            return;
          }
          link.rx[link.rxHead] = static_cast<uint8_t>(*p);
          link.rxHead = next;
        }
      }
    };
  }
}

ObdBleLink& ObdBleLink::Instance() {
  return instance;
}

ObdBleLink::State ObdBleLink::GetState() const {
  ObdBleLinkCallbacks::Advance(const_cast<ObdBleLink&>(*this)); // the fake advances lazily
  return state;
}

size_t ObdBleLink::FoundCount() const {
  ObdBleLinkCallbacks::Advance(const_cast<ObdBleLink&>(*this));
  return foundCount;
}

bool ObdBleLink::Found(size_t index, FoundDevice& out) const {
  if (index >= foundCount) {
    return false;
  }
  out = found[index];
  return true;
}

void ObdBleLink::StartScan(uint32_t durationMs) {
  if (state == State::Scanning || state == State::Connecting || state == State::Discovering || state == State::Linked) {
    return;
  }
  foundCount = 0;
  scanStart = lv_tick_get();
  scanDuration = durationMs;
  state = State::Scanning;
}

void ObdBleLink::Connect(const Address&) {
  if (state == State::Connecting || state == State::Discovering || state == State::Linked) {
    return;
  }
  connectStart = lv_tick_get();
  rxHead = rxTail = 0;
  state = State::Connecting;
}

void ObdBleLink::Disconnect() {
  state = State::Idle;
  rxHead = rxTail = 0;
}

bool ObdBleLink::Write(const char* text) {
  if (state != State::Linked) {
    return false;
  }
  const float t = static_cast<float>(lv_tick_get()) / 1000.0f;
  char reply[48];
  if (strncmp(text, "AT", 2) == 0 && strncmp(text, "ATRV", 4) != 0) {
    snprintf(reply, sizeof(reply), "OK\r\r>");
  } else if (strncmp(text, "ATRV", 4) == 0) {
    snprintf(reply, sizeof(reply), "14.2V\r\r>");
  } else if (strncmp(text, "0100", 4) == 0) {
    snprintf(reply, sizeof(reply), Env("INFINISIM_CAR_ECU", "off") ? "SEARCHING...\rUNABLE TO CONNECT\r\r>" : "SEARCHING...\r4100BE1FA813\r\r>");
  } else if (strncmp(text, "010D", 4) == 0) {
    snprintf(reply, sizeof(reply), "410D%02X\r\r>", static_cast<int>(70 + 25 * std::sin(t * 0.4f)));
  } else if (strncmp(text, "010C", 4) == 0) {
    const int rpm4 = static_cast<int>((2400 + 900 * std::sin(t * 0.7f)) * 4);
    snprintf(reply, sizeof(reply), "410C%02X%02X\r\r>", (rpm4 >> 8) & 0xff, rpm4 & 0xff);
  } else if (strncmp(text, "010B", 4) == 0) {
    snprintf(reply, sizeof(reply), "410B%02X\r\r>", static_cast<int>(120 + 40 * std::sin(t * 0.9f)));
  } else if (strncmp(text, "0133", 4) == 0) {
    snprintf(reply, sizeof(reply), "413365\r\r>");
  } else if (strncmp(text, "0105", 4) == 0) {
    snprintf(reply, sizeof(reply), "41057B\r\r>");
  } else if (strncmp(text, "010F", 4) == 0) {
    snprintf(reply, sizeof(reply), "410F3A\r\r>");
  } else if (strncmp(text, "0111", 4) == 0) {
    snprintf(reply, sizeof(reply), "4111%02X\r\r>", static_cast<int>(60 + 50 * std::sin(t)));
  } else if (strncmp(text, "0114", 4) == 0) {
    snprintf(reply, sizeof(reply), "41145A80\r\r>");
  } else {
    snprintf(reply, sizeof(reply), "?\r\r>");
  }
  ObdBleLinkCallbacks::Reply(*this, reply);
  return true;
}

size_t ObdBleLink::Read(uint8_t* out, size_t cap) {
  size_t count = 0;
  while (count < cap && rxTail != rxHead) {
    out[count++] = rx[rxTail];
    rxTail = (rxTail + 1) % rxCapacity;
  }
  return count;
}
```

- [ ] **Step 2: Add the sources to the simulator**

In `.deps/InfiniSim/CMakeLists.txt`, after the `${InfiniTime_DIR}/src/components/snake/SnakeGame.cpp` line, add:

```
  ${InfiniTime_DIR}/src/components/car/Elm327Session.h
  ${InfiniTime_DIR}/src/components/car/Elm327Session.cpp
  ${InfiniTime_DIR}/src/components/car/SavedAdapter.h
  ${InfiniTime_DIR}/src/components/car/SavedAdapter.cpp
  ${InfiniTime_DIR}/src/components/car/AdapterStore.h
  ${InfiniTime_DIR}/src/components/car/AdapterStore.cpp
  ${InfiniTime_DIR}/src/components/car/ObdBleSource.h
  ${InfiniTime_DIR}/src/components/car/ObdBleSource.cpp
  ${InfiniTime_DIR}/src/components/car/ObdBleLink.h
  sim/components/car/ObdBleLink.cpp
```

Then rebuild, from a fresh configure because a source was added:

```bash
cd .deps/InfiniSim && V=/home/shoots/.venvs/ptos-build && export PATH="$V/bin:/mnt/data/git/PTOS/.deps/node_modules/.bin:$PATH" \
  && rm -rf build-local && cmake -S . -B build-local -DInfiniTime_DIR=/mnt/data/git/PTOS -DPython3_EXECUTABLE=$V/bin/python > /dev/null \
  && cmake --build build-local -j"$(nproc)" --target infinisim 2>&1 | tail -2; cd -
```

Expected: `Built target infinisim`.

- [ ] **Step 3: Scenario A (DEVS, pick the adapter, link, go to Speed)**

The simulator fires `INFINISIM_TAPS` entries at frames 60, 120, 180 and so on. Hold `4` is a tap. `20,10` is a harmless filler tap on the title.

```bash
# The simulator keeps its flash image (spiNorFlash.raw) in the working directory: a fresh dir is a fresh watch.
S=/tmp/claude-1000/-mnt-data-git/bd4f29eb-bb09-44c7-b4b2-028b4d1580b0/scratchpad/car-shots/a && rm -rf $S && mkdir -p $S && cd $S
INFINISIM_STAY_AWAKE=1 INFINISIM_OPEN=car \
INFINISIM_TAPS='68,179,4;172,179,4;20,10,4;20,10,4;20,10,4;20,10,4;120,40,4;20,10,4;20,10,4;120,57,4' \
INFINISIM_SHOT_FRAMES=90,200,390,450,560,700 INFINISIM_AUTOSHOT=1 INFINISIM_AUTOSHOT_FRAME=720 \
timeout 90 /mnt/data/git/PTOS/.deps/InfiniSim/build-local/infinisim --hide-status; ls; cd -
```

Expected screenshots, in order:
1. The menu with `USE DEVS FIRST`.
2. The Devices view while scanning.
3. The Devices view with `3 FOUND` and `IOS-Vlink` first, highlighted.
4. The menu with `CONNECTING IOS-Vlink` or `STARTING ELM327`.
5. The menu with `LINKED IOS-Vlink` and a `DISCONNECT` button.
6. The Speed view with moving numbers.

If a frame lands on a transition, shift that `SHOT_FRAMES` entry by 30 and rerun.

- [ ] **Step 4: Scenario B (reopen: saved adapter and CONNECT)**

Rerun without deleting `spiNorFlash.raw`, so the saved adapter persists:

```bash
S=/tmp/claude-1000/-mnt-data-git/bd4f29eb-bb09-44c7-b4b2-028b4d1580b0/scratchpad/car-shots/b && rm -rf $S && mkdir -p $S && cp /tmp/claude-1000/-mnt-data-git/bd4f29eb-bb09-44c7-b4b2-028b4d1580b0/scratchpad/car-shots/a/spiNorFlash.raw $S/ && cd $S
INFINISIM_STAY_AWAKE=1 INFINISIM_OPEN=car INFINISIM_TAPS='68,179,4' INFINISIM_SHOT_FRAMES=50,180 \
INFINISIM_AUTOSHOT=1 INFINISIM_AUTOSHOT_FRAME=200 timeout 60 /mnt/data/git/PTOS/.deps/InfiniSim/build-local/infinisim --hide-status; cd -
```

Expected:
1. `SAVED IOS-Vlink` before the tap.
2. `LINKED IOS-Vlink` after it.

If it shows `NO ADAPTER`, the flash image was not carried over: confirm `spiNorFlash.raw` exists in `$S` before the run.

- [ ] **Step 5: Scenarios C, D, E (failure statuses) and F (leave mid-connect)**

Run Scenario B again three times, adding one environment variable each time:
- `INFINISIM_CAR_FAIL=notfound`. Expected: `ADAPTER NOT FOUND`, and the button reads `CONNECT` again.
- `INFINISIM_CAR_ECU=off`. Expected: `ECU NOT RESPONDING` while still linked.
- `INFINISIM_CAR_FAIL=lost`, with `INFINISIM_SHOT_FRAMES` after about 10 s (frame 450). Expected: `ADAPTER LOST`.

For Scenario F (Review Focus 1), leave the app mid-connect. Tap CONNECT at frame 60 and press the side button with `INFINISIM_HOLD` (press at frame 75, release at 78). The fake links about 1.2 s (48 frames) after the tap, so the status is still `CONNECTING` at that point. Then reopen the app on the same flash:

```bash
S=/tmp/claude-1000/-mnt-data-git/bd4f29eb-bb09-44c7-b4b2-028b4d1580b0/scratchpad/car-shots/f && rm -rf $S && mkdir -p $S && cp /tmp/claude-1000/-mnt-data-git/bd4f29eb-bb09-44c7-b4b2-028b4d1580b0/scratchpad/car-shots/a/spiNorFlash.raw $S/ && cd $S
INFINISIM_STAY_AWAKE=1 INFINISIM_OPEN=car INFINISIM_TAPS='68,179,4' INFINISIM_HOLD=75,3 INFINISIM_SHOT_FRAMES=70,120 \
  INFINISIM_AUTOSHOT=1 INFINISIM_AUTOSHOT_FRAME=140 timeout 60 /mnt/data/git/PTOS/.deps/InfiniSim/build-local/infinisim --hide-status 2>&1 | tail -3
INFINISIM_STAY_AWAKE=1 INFINISIM_OPEN=car INFINISIM_AUTOSHOT=1 INFINISIM_AUTOSHOT_FRAME=60 \
  timeout 60 /mnt/data/git/PTOS/.deps/InfiniSim/build-local/infinisim --hide-status 2>&1 | tail -3; cd -
```

Expected:
- Frame 70 shows `CONNECTING IOS-Vlink`.
- Frame 120 shows the watch face, because the app was left.
- The second run shows the Car menu with `SAVED IOS-Vlink` and a `CONNECT` button, not `DISCONNECT`.
- Neither run prints a crash.

The real race, a NimBLE connect completing after the screen is gone, can only happen on hardware. It is covered by Task 3's handling of a `CONNECT` event while not `Connecting`, and by hardware checklist step 6.

- [ ] **Step 6: Keep the screenshots**

Copy the clearest shots into the repo as:
- `screenshots/car-menu-connect.png`
- `screenshots/car-devs.png`
- `screenshots/car-linked.png`
- `screenshots/car-ecu-off.png`

```bash
git add -f screenshots/car-menu-connect.png screenshots/car-devs.png screenshots/car-linked.png screenshots/car-ecu-off.png
git commit -m "Add Car adapter link screenshots from the simulator

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Docs, spec amendments, hardware checklist, final verification

**Files:**
- Create: `docs/car-ble-hardware-checklist.md`
- Modify: `docs/superpowers/specs/2026-09-28-car-ble-adapter-design.md`, `README.md`, `CHANGELOG.uo.md`

- [ ] **Step 1: Amend the spec to match what was built**

Add a section `## Changes during implementation` at the end of the spec, with one line each:

- The probe timeout is 15 s; `SEARCHING...` takes longer than the 1.5 s reply timeout.
- Six consecutive failed PID replies drop `Ready` to `EcuNotResponding` and clear `connected` (ignition off mid-drive).
- `0133` is asked once per link whatever the reply.
- No MTU exchange: commands fit 20 bytes and replies are reassembled.
- Scanning does not take the radio from the phone; only connecting does.
- Devices view: the status shares the title line, up to 6 rows of 24 px, and the footnote reads `ANDROID-VLINK won't appear`.
- Idle with a saved adapter shows `SAVED <name>`; DEVS while linked shows `DISCONNECT FIRST`.
- The side button returns to the menu from any other view.

- [ ] **Step 2: Write the hardware checklist**

Create `docs/car-ble-hardware-checklist.md`:

```markdown
# Car BLE adapter: hardware checklist

Run on a PineTime with this build and a Vgate iCar Pro BLE 4.0 plugged into a car. The simulator
cannot test the radio, so this list is the only proof the BLE path works.

1. With the phone connected, open Car, then DEVS. `IOS-Vlink` appears first, highlighted.
   `ANDROID-VLINK` does not appear.
2. Tap `IOS-Vlink`. The status goes CONNECTING, then STARTING ELM327, then (engine on)
   SEARCHING PROTOCOL, then LINKED. The phone app shows the watch as disconnected.
3. Open SPEED with the engine running. Speed and RPM update at least twice a second.
4. Open BOOST / VAC and HUD. Values are plausible; battery reads about 13.5 to 14.5 V when running.
5. Leave the screen alone for a minute while linked. It stays on.
6. Press the button to the menu, then leave the app. The phone reconnects by itself within
   about 30 seconds, and notifications work again.
7. Reopen Car. The status reads SAVED IOS-Vlink. CONNECT links without scanning.
8. Switch the ignition off while linked in SPEED. Within about 10 s the status reads
   ECU NOT RESPONDING and the dial shows no data.
9. Unplug the adapter while linked. The status reads ADAPTER LOST, the screen times out
   normally again, and the phone reconnects.
10. Turn Bluetooth off in Settings, open Car, link, then leave. Bluetooth stays off.
11. Reboot the watch while linked (hold the button). It comes back with Bluetooth on and the
    phone reconnects.

Record anything that differs, with the step number, in an issue on the fork.
```

- [ ] **Step 3: Update the README and changelog**

In `README.md`'s **Car (OBD-II dashboard)** section:
- Replace the paragraph starting "**The BLE adapter transport is not implemented yet.**" with a description of CONNECT, DEVS, the radio handover and the screen staying on.
- Add a line saying the BLE path is **untested on hardware until `docs/car-ble-hardware-checklist.md` has been run**.
- Keep the BLE-only explanation.
- Add `screenshots/car-devs.png` and `screenshots/car-linked.png` below the existing HUD image.

In `CHANGELOG.uo.md`, under Unreleased, add:

```markdown
- Car: real BLE OBD-II adapter link. CONNECT links the saved adapter, DEVS scans and saves a new
  one. While linked the car takes the BLE radio from the phone and the screen stays on; leaving
  the app always hands the radio back. Tested on the host and in the simulator; hardware checklist
  in `docs/car-ble-hardware-checklist.md`.
```

Check for dashes:

```bash
grep -c "[—–]" README.md CHANGELOG.uo.md docs/car-ble-hardware-checklist.md docs/superpowers/specs/2026-09-28-car-ble-adapter-design.md
```

Expected: `0` for each.

- [ ] **Step 4: Final verification**

Run all three host tests. Expected: `ALL PASS` three times:

```bash
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/car/test_elm327.cpp src/components/car/Elm327Session.cpp -o /tmp/t1 && /tmp/t1 | tail -1
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/car/test_saved_adapter.cpp src/components/car/SavedAdapter.cpp -o /tmp/t2 && /tmp/t2 | tail -1
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -Itests/car tests/car/test_obd_ble_source.cpp tests/car/FakeObdBleLink.cpp src/components/car/ObdBleSource.cpp src/components/car/Elm327Session.cpp -o /tmp/t3 && /tmp/t3 | tail -1
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/snake/test_snake.cpp src/components/snake/SnakeGame.cpp -o /tmp/t4 && /tmp/t4 | tail -1
```

The last line is the Snake regression test; expect `ALL PASS` there too.

Then run the firmware build from a fresh directory. Expected:
- `Built target pinetime-mcuboot-app`
- `FLASH:` below 470,000 bytes
- the image contains `1.16.1+uo2.dev`

Record the flash figure in the commit message.

- [ ] **Step 5: Commit and push**

```bash
git add docs/car-ble-hardware-checklist.md docs/superpowers/specs/2026-09-28-car-ble-adapter-design.md README.md CHANGELOG.uo.md
git commit -m "Document the Car BLE adapter link and its hardware checklist

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin main
```
