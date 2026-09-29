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

The default build shows a full-screen black startup background with a centered
white Adafruit flower and wordmark bitmap. The splash stays visible for one
second after loading, then the firmware acquires and displays input video.
The panel free-runs during startup, so the screen does not require an input
signal. The bitmap is split into tiles
stored in the scaler's OSD SRAM. Replace `assets/splash.bmp` and run `make` to
customize it; the build generates the bitmap header automatically using Pillow.
BMPs can contain up to 15 visible colors plus transparent black; richer images
are quantized automatically. Use `make SPLASH_BMP=path/to/my-logo.bmp` to select
another file. Larger BMPs, including full-size 16-bit RGB565 images, automatically
shrink to fit 192x108 pixels without cropping and are displayed at 4x scale.
RGB565 input is converted to the OSD palette; this is not a full-color framebuffer.
See the [custom splash guide](assets/README.md) for the complete BMP-to-firmware
workflow, example commands, timing and image limits. Use
`make SPLASH=0` to omit it; that build uses a separate `-splash0` directory.

When input is absent, a separate Adafruit TV test card displays "NO SIGNAL" on
the black background. It disappears when valid video returns. Customize it with
`make NO_SIGNAL_BMP=path/to/my-no-signal.bmp check`; this works even with
`SPLASH=0` and uses the same BMP dimensions, palette conversion and centering.

On acquisition, a top-left overlay shows the input resolution, estimated refresh
rate, horizontal frequency, sync polarity and measured totals for three seconds.
Unsupported input keeps its measured settings and the first rejection reason on
screen. Missing measurements display `--`; disconnected input retains the TV
test card. These messages do not expand the supported video modes.

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
| `src/rtd/` | Video and audio acquisition, clocks, scaling, EDID and OSD |
| `src/app/` | Firmware policy: acquire inputs, handle loss and show a splash |
| `include/rtd/` | Small interfaces between those layers |
| `assets/` | Editable startup and no-signal BMPs, with artwork attribution |
| `tools/` | Build-time BMP palette conversion |
| `tests/` | Host checks for timing arithmetic, register encoding and rejection paths |

The video driver names the scaler page on every register access. The interrupt
handler only maintains time; it never competes for the shared scaler gateway.
Panel profiles use physical units, leaving register encoding inside the driver.
There is no dynamic allocation or dependency on a proprietary runtime.

Only the supplied UC-586/800x480 combination is implemented. Video acceptance
supports native 800x480 at 1000x525 timing, PicoDVI's alternate 800x480 at
992x500 timing, and horizontally stretched 640x480 at 800x525 timing. All are
near 60 Hz. These are explicit timing profiles, not arbitrary mode detection;
see the [timing contract](docs/video-registers.md#supported-timing-contract).

## Display controls

The OSD renderer is separate from video and application policy. Future menus
can share logical key events across a single-button board and a five-button
GPIO/ADC board. Board profiles must establish the actual wiring before enabling
backlight or image-orientation controls.

Brightness/contrast adjust the video pixels; backlight control adjusts the
panel light. Shipped RTD2660H boards demonstrate whole-screen 180-degree
rotation and mirroring, but the mechanism and available modes are board
dependent. See the [capability and firmware-analysis notes](docs/ui-capabilities.md).
The first audio profile implements stereo 48 kHz LPCM through the UC-586's
CS4334 DAC. See [audio support and validation](docs/audio.md) for its current
bench status and limits. A complete menu, settings persistence, audio volume
controls and arbitrary video modes are not implemented.

## Programming

Use the included [Feather tester/programmer](tools/tester/README.md).
Its RP2350 HSTX variant supports both HDMI audio testing and programming through
the same connected HDMI cable. Select `mode off` before programming and return
to `mode 640` afterward; mode changes reboot the Feather.
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

From `tools/tester/feather_rp2040/Feather_DVI_RTD_Tester`, use the shared
`host.py` for either Feather:

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
aligned the alternate 992x500-total 800x480 grid, expanded 640x480 text and grids
to the full panel, displayed the no-signal test card on signal loss, and
reacquired native video when the source returned. The full-screen startup
background and centered bitmap were verified after a whole-chip reset with
the source enabled and disabled, followed by handover to video. These checks
used the Feather DVI source, not a general
HDMI compatibility suite. Physical cold boot was tested on the initial video
implementation; the bitmap splash was tested by whole-chip reset. Bitmap upload
precedes the one-second hold, so the black background appears before the logo.
The color renderer was checked on the UC-586 with a 15-color chart, transparent
gaps and return to video. Packing tiles during conversion makes the chart
visible in the first startup capture, about two seconds after reset.

SDCC 4.5.0 and host checks pass for splash-on, splash-off and diagnostic builds,
including a binary/map check that all six interrupt vectors reach the linked
handlers and a pixel-by-pixel reconstruction of the bitmap from OSD writes.
BMP tests cover color preservation, palette reduction, transparent black,
row/tile/plane ordering, size limits and unchanged generated output.
The current default build uses 31,970 bytes of flash and 183 bytes
of XRAM; its 64 KiB bank0 SHA256 is
`cfe76d26529bca1022987db2805220712bf995cb695b6c7da1e248e9abcf6c22`.
Programming verified all 512 KiB and restored the original protection byte
`0x0C`. Builds, code and register notes are provided; stock firmware dumps and
the preserved original flash tail are not distributed.

The RP2350 HSTX audio test confirmed a clean 1 kHz analog tone from the UC-586
headphone jack, muting on source loss and recovery with video. Splash, no-signal
artwork and the scaled grid remained visible. The input timing overlay did not
appear in this run's captures and needs further investigation. See the
[audio measurements and limits](docs/audio.md#validation).
