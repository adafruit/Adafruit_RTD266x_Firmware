# Adafruit RTD266x Firmware

Experimental RTD2660H display-controller firmware built with SDCC. The first
target is a UCTRONICS UC-586 with a generic 5-inch RGB888 800x480 panel.
No Keil compiler, vendor library, stock firmware image or inherited source tree
is required to build it.

This is a new implementation informed by register documentation, firmware
analysis and earlier bench experiments with
[ORTD2662](https://github.com/KerJoe/ORTD2662) and
[tkdesign-jp's port](https://github.com/tkdesign-jp/ORTD2662). It is not a formal
clean-room implementation. Hardware facts and unresolved assumptions are
recorded in [video notes](docs/video-registers.md) and
[display-control notes](docs/ui-capabilities.md). New source is MIT licensed.

## Build

Use GNU Make and SDCC on Linux, macOS or Windows through WSL. On Ubuntu:

```sh
sudo apt install sdcc make gcc python3 python3-pil
make
make check
```

The default build is equivalent to:

```sh
make BOARD=uc586 PANEL=rgb800x480 APP=monitor SPLASH=1
```

It produces `build/uc586-rgb800x480-monitor-splash1/firmware.bin`, a 65536-byte
bank0 image. Local development uses SDCC 4.5.0. Firmware variants get separate
output directories so changing a board, panel, application or splash setting
cannot reuse the other variant's object files.

The default build shows a full-screen blue startup background with a centered
white Adafruit flower and wordmark bitmap. Incoming video stays hidden for five
seconds, then the firmware acquires and displays it. The panel free-runs during startup so
the screen does not require an input signal. The bitmap is split into tiles
stored in the scaler's OSD SRAM. Replace `assets/splash.bmp` and run `make` to
customize it; the build generates the bitmap header automatically using Pillow.
See the [artwork format and attribution](assets/README.md). Use
`make SPLASH=0` to omit it; that build uses a separate `-splash0` directory.

`make TRACE=1` enables bench diagnostics in the EDID ASCII descriptor and MCU
scratch registers. Its output directory ends in `-trace`. This changes the
descriptor during measurement; use the default `TRACE=0` for ordinary display
operation. See [diagnostic decoding](docs/video-registers.md#bench-diagnostics).

## Design

| Directory | Responsibility |
| --- | --- |
| `boards/` | Pin routing, buttons, backlight and other board wiring |
| `panels/` | Physical pixel and line timings |
| `src/platform/` | 8051 startup, timer, scaler gateway and EDID SRAM access |
| `src/rtd/` | Video acquisition, clock/scaler configuration, EDID generation and OSD |
| `src/app/` | Firmware policy: acquire video, handle loss and show a splash |
| `include/rtd/` | Small interfaces between those layers |
| `assets/` | Startup bitmap and its license |
| `tests/` | Host checks for timing arithmetic, register encoding and rejection paths |

The video driver names the scaler page on every register access. The interrupt
handler only maintains time; it never competes for the shared scaler gateway.
Panel profiles use physical units, leaving register encoding inside the driver.
There is no dynamic allocation or dependency on a proprietary runtime.

Only the supplied UC-586/800x480 combination is implemented. Video acceptance
is currently limited to native 800x480 and horizontally stretched 640x480 at
the documented 60 Hz timings. This is not a generic automatic monitor firmware.

## Display controls

The OSD renderer is separate from video and application policy. Future menus
can share logical key events across a single-button board and a five-button
GPIO/ADC board. Board profiles must establish the actual wiring before enabling
backlight or image-orientation controls.

Brightness/contrast adjust the video pixels; backlight control adjusts the
panel light. Shipped RTD2660H boards demonstrate whole-screen 180-degree
rotation and mirroring, but the mechanism and available modes are board
dependent. See the [capability and firmware-analysis notes](docs/ui-capabilities.md).
Audio, a complete menu, settings persistence and arbitrary video modes are not
implemented in this first version.

## Programming

Use the [Feather DVI tester/programmer](https://github.com/adafruit/Adafruit_Arduino_Tester_Code/pull/23).
Save matching complete reads of your own board's original flash and preserve
its protection state before programming. The tested UC-586 has a W25X40
(`EF3013`) with 512 KiB of flash. Other boards can have different flash and pins.

The programmer expects a full image. Build one from the new bank0 and the
untouched tail of your own verified backup:

```python
from pathlib import Path

bank0 = Path("build/uc586-rgb800x480-monitor-splash1/firmware.bin").read_bytes()
original = Path("original.bin").read_bytes()
assert len(bank0) == 65536 and len(original) == 524288
with open("firmware-full.bin", "xb") as output:
    output.write(bank0 + original[65536:])
```

From the tester directory, use its `host.py`:

```sh
python host.py program /path/to/firmware-full.bin \
  --backup /path/to/current-verified.bin --allow-write --receipt program.json
python host.py reset-chip
```

The backup must match the image currently installed, which is the original
only on the first run. The programmer compares the complete current image,
writes changed sectors, verifies all bytes and restores flash protection.
The final command requests a whole-chip reset; an ISP-only MCU restart can
retain peripheral state.
Keep unexpected readbacks before making further changes. The retained flash
tail is not linked into the new program and is not a settings-storage area.

## Validation status

On 2026-09-29, the fresh implementation displayed native 800x480 text and grids,
expanded 640x480 text and grids to the full panel, blanked on signal loss, and
reacquired native video when the source returned. The full-screen startup
background and centered bitmap were verified after a whole-chip reset with
the source enabled and disabled, followed by handover to video. These checks
used the Feather DVI source, not a general
HDMI compatibility suite. Physical cold boot was tested on the initial video
implementation; the bitmap splash was tested by whole-chip reset. Bitmap upload
precedes the five-second hold, so the blue background appears before the logo.

SDCC 4.5.0 and host checks pass for splash-on, splash-off and diagnostic builds,
including a binary/map check that all six interrupt vectors reach the linked
handlers and a pixel-by-pixel reconstruction of the bitmap from OSD writes.
The tested default program uses 12,163 bytes of flash and 132 bytes
of XRAM; its 64 KiB bank0 SHA256 is
`c2ecc173c9d777066b49d473e1916bf759a0c722873666a55dd3edc1155e0098`.
Programming verified all 512 KiB and restored the original protection byte
`0x0C`. Builds, code and register notes are provided; stock firmware dumps and
the preserved original flash tail are not distributed.
