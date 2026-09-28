# Car app: BLE OBD-II adapter link

Status: design approved in conversation 2026-09-28, spec awaiting review.
Target: `1.16.1+uo2` dev builds.

## Goal

Make the Car app show live data from a real BLE OBD-II adapter instead of only the built-in
simulator. The adapter in hand is a **Vgate iCar Pro Bluetooth 4.0 (BLE)**.

Add two buttons, **CONNECT** and **DEVS**, side by side under the existing menu buttons:

- **CONNECT** links to the last adapter used.
- **DEVS** scans for adapters and lets you pick one, which then becomes the saved adapter.

## Decisions already made

| Question | Decision |
|---|---|
| Phone and adapter share one BLE connection (`BLE_MAX_CONNECTIONS` = 1) | The car takes the radio **only while the Car app is open**. Leaving the app disconnects the adapter and gives the radio back to the phone. |
| Screen timeout while linked | The screen **stays on while an adapter is linked**, and returns to normal on disconnect or leaving the app. |
| Where the code lives | A Car-owned link in `components/car/`, plus two small system messages. See "Approaches considered". |

## Adapter facts

The Vgate iCar Pro BLE 4.0 shows up as two devices:

- `IOS-VLINK`: the BLE one, which the watch uses.
- `ANDROID-VLINK`: Bluetooth Classic, which the nRF52832 cannot see or use.

`IOS-Vlink` exposes:

- Service `E7810A71-73AE-499D-8C15-FAA9AEF0C3F2`
- One characteristic, `BEF8D6C9-9C21-4C9E-B632-BD58C1009F9F`, used for **both** write and notify

It speaks the ELM327 command set. Replies end with a `>` prompt. No pairing or PIN is needed.

Sources:
- <https://afshari.lu/post/213-elm/>
- <https://manuals.plus/vgate/icar-pro-ble-4-0-manual>

Common clone layouts, supported as fallbacks:

| Service | Notify | Write |
|---|---|---|
| `FFF0` | `FFF1` | `FFF2` |
| `FFE0` | `FFE1` | `FFE1` (one characteristic, HM-10 style) |

## Approaches considered

1. **Chosen: a Car-owned link plus two system messages.** The BLE code lives in
   `components/car/`, and the Car screen drives it. SystemTask gains two messages to hand the
   radio over and back. About 10 new lines, in two upstream files the fork already edits. This is
   the smallest rebase surface and matches "car only while the app is open".
2. **A new BLE client inside `NimbleController`**, like `CurrentTimeClient`. More idiomatic for
   InfiniTime, and it could run in the background, but that wasn't wanted. It touches
   `NimbleController`, `Controllers.h`, `DisplayApp` and `SystemTask`, the biggest rebase hotspots.
3. **Everything through SystemTask messages.** The cleanest on threading, but every scan,
   connect and write needs a message plus a return path. Too much core plumbing.

## Components

### `Elm327Session` (`components/car/Elm327Session.{h,cpp}`)

The adapter protocol only, with no BLE or LVGL dependency, so it can be tested on the host like
`SnakeGame`.

**Interface**
- `void Received(const uint8_t* data, size_t len)`: feed bytes from the adapter. Replies arrive
  in chunks of up to 20 bytes. The session reassembles them until the `>` prompt.
- `bool NextCommand(char* out, size_t cap)`: the next command to send, including `\r`, or false
  if the session is waiting on a reply.
- `void Tick(uint32_t nowMs)`: drives timeouts.
- `void SetPollSet(PollSet set)`: which values to poll, depending on the current view.
- `const ObdData& Data() const` and `State state() const`.

**Startup sequence**
`ATE0` (echo off), `ATL0` (linefeeds off), `ATS0` (spaces off), `ATH0` (headers off), `ATSP0`
(auto-detect protocol), then `0100`.

`0100` can print `SEARCHING...` and take several seconds while the adapter probes the car's
protocols.

**States**
`Starting` → `Searching` → `Ready`, or `EcuNotResponding`. The session stays linked in
`EcuNotResponding` and retries `0100` every 5 s, because the ignition may simply be off.

**Polling** is round-robin over the current poll set, one command in flight at a time:

| Poll set | Values |
|---|---|
| Menu | none (idle, which saves radio time) |
| Speed | `010D` speed, `010C` RPM |
| Boost | `010B` MAP; `0133` barometric pressure once per link |
| HUD | `0105` coolant, `010F` intake, `010C` RPM, `0111` throttle, `0114` O2 B1S1, `010D` speed, `010B` MAP, and `ATRV` battery every 5th poll |

**Parsing** (with `ATS0`, replies look like `410D3C`):
- Check the `41` + PID echo, then apply the standard formulas:

  | PID | Value | Formula |
  |---|---|---|
  | `0D` | speed | A km/h |
  | `0C` | RPM | (256A+B)/4 |
  | `05` | coolant | A-40 °C |
  | `0F` | intake | A-40 °C |
  | `0B` | MAP | A kPa |
  | `11` | throttle | A*100/255 % |
  | `14` | O2 B1S1 | A*5 mV |
  | `33` | barometric pressure | A kPa |

  `ATRV` replies look like `12.6V` and are stored as millivolts.
- Ignore a trailing `\r` and blank lines.
- `NO DATA`: keep the previous value.
- `?`, `STOPPED`, or an unparseable reply: drop it and move on.
- No `>` within 1500 ms: time out and move on.

`data.connected` is true only in `Ready` and after at least one valid PID reply.

### `ObdBleLink` (`components/car/ObdBleLink.{h,cpp}`)

The NimBLE central glue. It has **static lifetime**: one instance for the life of the firmware,
not owned by the screen. NimBLE can deliver GAP/GATT events after the Car screen has been
destroyed (for example, the disconnect event that follows `ble_gap_terminate`), and a
screen-owned object would be used after free.

**Operations**
- `StartScan(ms)`: `ble_gap_disc`, active scan for the names. Collects up to 8 unique
  advertisers that have a name, keeping the best RSSI for each.
- `Connect(addr)`: `ble_gap_connect` with its own event callback, 5 s timeout.
- `Disconnect()`.
- `Write(bytes)`: write-with-response or write-without-response, whichever the characteristic's
  properties allow.

**After connecting**
1. Exchange MTU (`ble_gattc_exchange_mtu`). Keep working at the default 20-byte payload if that
   fails.
2. Discover services and try the UUID list in order: Vgate 128-bit, then `FFF0`, then `FFE0`.
3. Discover characteristics and descriptors. Enable notifications by writing `0x0001` to the
   notify characteristic's CCCD (found by descriptor discovery, not guessed as handle+1).

**Threading.** Callbacks run on the NimBLE host task, while the Car screen runs on the display
task. The link only copies notified bytes into a 256-byte ring buffer and updates a small state
struct, both under a FreeRTOS critical section. The screen drains them on the display task in
`Car::Refresh`. All protocol and UI work happens on the display task. Calling `ble_gap_*` and
`ble_gattc_*` from the display task is fine: the NimBLE host APIs take `ble_hs_lock` internally,
and InfiniTime already calls them from SystemTask rather than the host task.

**Link states:** `Idle`, `Scanning`, `Connecting`, `Discovering`, `Linked`, `Failed(reason)`,
`Lost`.

### `ObdBleSource : ObdSource` (`components/car/ObdBleSource.{h,cpp}`)

Combines the link and a session.

`Update()`, called from `Car::Refresh`, does the following:
1. Drain the link's bytes into the session.
2. Tick the session.
3. Send the session's next command.

`Current()` returns the session's data. The Car screen uses it in place of `disconnected` when
not in demo mode. The existing `ObdSource` interface is unchanged.

### Saved adapter: `/car/adapter.dat`

A small versioned record on littlefs, via the `FS` controller that apps already receive:

| Field | Size |
|---|---|
| version byte | 1 byte |
| BLE address | 6 bytes |
| address type | 1 byte |
| name | up to 20 bytes, zero-terminated |

- **Written** after a successful link started from DEVS.
- **Read** when the Car app opens.

Upstream's `Settings` struct is deliberately **not** extended. Changing its layout bumps the
settings version, which resets every user's settings on upgrade.

### System messages (`systemtask/Messages.h`, `SystemTask.cpp`)

- `CarLinkBegin` calls `nimbleController.DisableRadio()`: it drops the phone connection and stops
  advertising. The saved setting is not touched.
- `CarLinkEnd` calls `nimbleController.EnableRadio()` only if `settingsController.GetBleRadioEnabled()`.

Consequences:
- A reboot mid-drive comes back with Bluetooth in its normal state.
- While the car holds the radio, `BleController` reports the radio as disabled, so watch faces
  show Bluetooth as off. That is accurate.
- `NimbleController` only restarts advertising on `ADV_COMPLETE` when the radio is enabled, so
  the phone cannot grab the link back mid-drive.

The Car screen sends `CarLinkBegin` before connecting. It waits until `bleController.IsConnected()`
is false (up to 1 s) before calling `ble_gap_connect`, because the phone's disconnect must
complete first with one connection allowed.

### Car screen changes (`displayapp/screens/Car.{h,cpp}`)

**Menu layout (240×240)**

| Element | Position |
|---|---|
| Title and DEMO chip | unchanged |
| SPEED, BOOST / VAC, HUD buttons | height 42 → 34, at y = 40, 80, 120 |
| New row: `CONNECT` and `DEVS` | each 96 × 34 at y = 162, 8 px apart |
| Status line | y = 212 |

**CONNECT**
- With a saved adapter: connects to it. The label becomes `DISCONNECT` while connecting or linked.
- With no saved adapter: the status reads `USE DEVS FIRST`.

**DEVS** opens a new `View::Devices`:
- A 6 s scan, then a list of up to 6 named devices with RSSI.
- Names containing `VLINK`, `OBD` or `ELM` (case-insensitive) sort first and are highlighted.
- Tapping a row connects to it; on success it becomes the saved adapter.
- `RESCAN` repeats the scan.
- A footnote reads `ANDROID-VLINK is Bluetooth Classic and won't appear`.
- Swipe back or the button returns to the menu. That doesn't cancel an in-progress link.

**Status line texts**

| State | Text |
|---|---|
| no saved adapter | `NO ADAPTER` |
| demo mode on | `DEMO DATA` |
| scanning | `SCANNING` |
| connecting | `CONNECTING <name>` |
| startup commands | `STARTING ELM327` |
| protocol search | `SEARCHING PROTOCOL` |
| ready | `LINKED <name>` |
| ignition off | `ECU NOT RESPONDING` |
| connect failed | `ADAPTER NOT FOUND` |
| link dropped | `ADAPTER LOST` |

The name is truncated to fit 20 characters.

**Demo mode** still works and takes precedence in the views. While demo is on, CONNECT and DEVS
still work, so you can link and then turn demo off.

**Screen on:** a `System::WakeLock` is held while the link is `Linked` and released on any other
state, disconnect, or leaving the app.

**Leaving the app** (destructor): `Disconnect()`, release the wake lock, send `CarLinkEnd`.

**Poll set** follows the current view: Menu/Devices → none, Speed, Boost, HUD.

## Error handling

| Situation | Behaviour |
|---|---|
| Connect times out (5 s) or fails | Status `ADAPTER NOT FOUND`, send `CarLinkEnd`. |
| Service or characteristic not found | Disconnect, status `ADAPTER NOT FOUND`, `CarLinkEnd`. |
| Link drops while linked | Status `ADAPTER LOST`, release wake lock, `CarLinkEnd`, views show no data. |
| Ignition off / ECU silent | Stay linked, `ECU NOT RESPONDING`, retry `0100` every 5 s. |
| Garbage or partial replies | Discarded by the session; the next poll continues. |

There is **no automatic reconnect** in this version. After `ADAPTER LOST`, tap CONNECT.

## Testing

- **Host tests:** `tests/car/test_elm327.cpp` runs `Elm327Session` against recorded or
  hand-written adapter transcripts. Covers:
  - replies split across 20-byte chunks
  - `SEARCHING...` then success
  - `NO DATA`
  - `?`
  - timeouts
  - every PID formula
  - the `ATRV` format
  - poll-set switching
  - `EcuNotResponding` → `Ready` recovery
- **Simulator:** InfiniSim has no BLE. A simulator-only fake link (in `.deps/InfiniSim`, not the
  firmware) walks through every status, so the menu, Devs list and status line can be
  screenshotted.
- **Firmware build:** clean build, check the flash delta (estimate 6 to 9 KB of about 43 KB free),
  apps registered.
- **Real hardware:** the BLE path can only be proven on the Vgate. The user runs a checklist:
  - scan finds `IOS-Vlink`
  - connect and save
  - `LINKED` appears and the phone icon goes off
  - values appear with the engine running
  - `ECU NOT RESPONDING` with the ignition off
  - leaving the app gives the phone back
  - a reboot mid-link comes back normal

## Out of scope

- MPH. Speed is hardcoded to KM/H today, and fixing that is a separate change.
- Automatic reconnect.
- Staying connected in the background after leaving the app.
- More than one saved adapter.
- Diagnostic trouble codes.

## Risks

- **NimBLE central use from the display task** is reasoned about from NimBLE's internal locking,
  not proven on this firmware. If it misbehaves on hardware, the fallback is approach 3's
  message routing for the GAP/GATT calls only.
- **Heap:** central-role GATT discovery allocates from NimBLE's own pools, which are already
  configured with `BLE_ROLE_CENTRAL` on. It should be verified on hardware, with no other apps
  open.
- **Flash:** about 43 KB free before this change.
