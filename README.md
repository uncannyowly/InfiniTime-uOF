# InfiniTime 1.16.1F

A one-off fork of [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime) **1.16.1** for the
[PineTime](https://pine64.org/devices/pinetime/), with four extra watch faces compiled in.

The `F` is for *faces*. Nothing else is changed — this is upstream 1.16.1 (commit `e172b9b3`) plus
four new `Screen` subclasses, one font, and the registration boilerplate to hook them up. No
upstream behaviour is modified, no features are removed.

| | |
|---|---|
| ![LCARS](screenshots/lcars.png) | ![LCARS Neon](screenshots/lcars-neon.png) |
| **LCARS** | **LCARS Neon** |
| ![Cyberdeck](screenshots/cyberdeck.png) | ![Cyberdeck NetOps](screenshots/cyberdeck-netops.png) |
| **Cyberdeck** | **Cyberdeck NetOps** |

## The faces

All four are drawn entirely in code — coloured LVGL rectangles and text labels. There are no
bitmap assets, so nothing has to be flashed to the external SPI flash for them to work, and
`IsAvailable()` returns true unconditionally.

### LCARS / LCARS Neon

A *Star Trek: TNG* console frame adapted to a 240×240 square. A curved elbow sweeps from the
header into a left sidebar, which carries four data rows — weather, battery, step count and heart
rate — each with its own colour block and caption. The header shows the date, a stardate-styled
`SD <year>.<day-of-year>`, a `COMM` indicator that dims when Bluetooth drops, and an `MSG` flag
that appears on unread notifications. Seconds and AM/PM sit as small captions inside the sidebar.

Both variants are the same 253-line implementation. `LcarsPalette` is a struct of eight semantic
colour roles (`primary`, `header`, `alert`, `dim`, …), so the layout is skinned twice:
`lcarsClassic` is the TNG amber/mauve/periwinkle set, `lcarsNeon` swaps in magenta and cyan.
Adding a third variant means adding one more `constexpr LcarsPalette`.

LVGL 7 has no per-corner border radius, so the elbows are built by overlaying squared-off blocks
on a rounded rectangle and carving the concave inner curve with a black rounded rect — cheaper
than masking, and visually identical at this size.

### Cyberdeck / Cyberdeck NetOps

A green-on-black terminal readout with bracketed panel headers. **Cyberdeck** is a large digital
clock over a 2×2 grid of power, steps, heart rate and weather panels, each with a small bar meter.
**NetOps** replaces the big clock with a miniature analogue clock beside an animated panel:
a scrolling waveform, a rolling four-byte hex dump, and a progress meter cycling through
`SCAN → BREACH → DECRYPT → UPLINK`.

To be clear, that panel is **decoration**. It is driven by a small PRNG and does nothing; the
watch is not scanning or breaching anything. It just looks the part.

## Fonts

The LCARS faces use [Antonio](https://fonts.google.com/specimen/Antonio) Bold — a condensed
grotesque, much closer to the LCARS register than InfiniTime's stock JetBrains Mono. It is
generated at build time from `Antonio-Bold.ttf` into three sizes (64 / 24 / 16) at 2 bits per
pixel, so the large numerals are antialiased rather than the stock 1-bit.

Antonio is licensed under the SIL Open Font License; see
[`Antonio-OFL.txt`](src/displayapp/fonts/Antonio-OFL.txt).

Antonio's generated glyph ranges are uppercase-only, which is why the weather condition string is
upper-cased at runtime before display.

## Building

Standard InfiniTime build. You need the ARM toolchain, the nRF5 SDK,
[`lv_font_conv`](https://github.com/lvgl/lv_font_conv), and Python with `Pillow` and
`adafruit-nrfutil`:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DARM_NONE_EABI_TOOLCHAIN_PATH=<path to gcc-arm-none-eabi> \
  -DNRF5_SDK_PATH=<path to nRF5_SDK_15.3.0> \
  -DBUILD_DFU=1 -DBUILD_RESOURCES=1 \
  -DTARGET_DEVICE=PINETIME

cmake --build build --target pinetime-mcuboot-app -j"$(nproc)"
```

The flashable package lands at `build/src/pinetime-mcuboot-app-dfu-1.16.1.zip`, or grab it from
[Releases](../../releases).

To build a subset of faces, override the watch face list at configure time:

```sh
cmake -S . -B build -DENABLE_WATCHFACES="WatchFace::Digital, WatchFace::Lcars" ...
```

See [upstream's documentation](https://github.com/InfiniTimeOrg/InfiniTime/blob/main/doc/buildAndProgram.md)
for full build and flashing instructions, and [`README.InfiniTime.md`](README.InfiniTime.md) for
the original project README.

## Installing

Flash `pinetime-mcuboot-app-dfu-1.16.1.zip` over Bluetooth with any InfiniTime-compatible
updater — [Gadgetbridge](https://gadgetbridge.org/), [InfiniLink](https://github.com/InfiniTimeOrg/InfiniLink),
or [Siglo](https://github.com/alexr4535/siglo). The watch reports its version as `1.16.1F` in
Settings → System Info so you can tell it apart from stock.

This is a normal InfiniTime image with a valid bootloader header — reverting is just flashing an
official release back over it.

## Status and support

This is a personal one-off, pinned to 1.16.1. It is not tracking upstream and there is no promise
of rebases onto later releases. It has not been submitted to InfiniTime and is not endorsed by
the InfiniTime project or by Pine64.

Bug reports about *these four faces* are welcome. Anything else belongs
[upstream](https://github.com/InfiniTimeOrg/InfiniTime/issues).

## Licence

GPLv3, inherited from InfiniTime — see [`LICENSE`](LICENSE). The new watch faces are GPLv3 on the
same terms. Antonio is under the SIL OFL, as noted above.

All credit for the firmware itself goes to the
[InfiniTime contributors](https://github.com/InfiniTimeOrg/InfiniTime/graphs/contributors); this
fork only adds four screens to their work.
