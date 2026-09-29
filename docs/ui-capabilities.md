# Display controls and OSD

The first target is the UC-586 with an 800x480 RGB panel. The hardware has a
separate OSD engine suitable for menus and small splash graphics. It is not a
general-purpose RGB framebuffer accessible to the 8051.

## What is implemented

The startup screen fills the panel with a black background and a centered white
Adafruit flower and wordmark bitmap. `src/app/monitor.c` holds that screen for
five seconds before acquiring input video. `src/rtd/video.c` keeps the display clock and
timing generator running independently of input sync during this interval.

`tools/bmp_to_header.py` prepares one-bit or four-bit 12x18 tiles during the
build. `src/rtd/osd.c` uploads those bytes and builds a row/character map in
OSD SRAM. Initialization leaves the bitmap off;
`osd_show_splash()` enables it and `osd_hide()` removes it. The application owns
the full-screen background and handover to video. Initialize the OSD again
after a scaler reset. Replace `assets/splash.bmp` and rebuild to change the artwork;
its format, dimensions and license are documented in `assets/README.md`.
BMP decoding and palette quantization happen automatically during the build.
There are up to 15 visible colors plus transparent index zero. This uses the
OSD palette and SRAM, not a full-color framebuffer.

The SRAM port accepts Byte0, Byte1, Byte2 while each glyph's first scan line
occupies bits 23:12. Before global zoom, horizontal frame delay uses four-pixel
units and vertical delay uses lines. Global 2x zoom doubles those delays as
well as the glyphs. The UC-586 board profile subtracts a measured 32-panel-pixel
horizontal offset from the frame delay; using the video origin directly shifted
the splash to the right. The startup screen can be disabled at build time.
The supplied 82x64 Adafruit logo is an existing BSD-licensed bitmap, with no
extracted RTD firmware font or logo. Its pixels are unchanged.

The converter centers the bitmap in a 7x4 tile rectangle, adding one transparent
pixel on each side and four above and below. Each OSD row and the global frame
request 2x scale, producing a 328x256 logo within a 336x288 rectangle. The
row map starts at SRAM word zero, character selections at word `0x010`, and
fonts at word `0x100`; they do not overlap. The 28 tiles occupy 756 bytes of
dedicated OSD SRAM, not 8051 XRAM. Those tile bytes and a six-byte RGB palette
are stored in flash. Host tests also receive the original 704-byte row-major
bitmap; the 8051 build excludes that reference copy.

Four-bit palette tiles occupy 36 words each: four consecutive one-bit planes,
with palette bit zero first. Each plane uses the same nine-word packing as a
monochrome tile. A map entry `0x90, tile_index, 0` selects LUT colors and a
transparent background; its selector is limited to seven bits. At the current
font base, the lower 12 KiB SRAM bank holds 106 such tiles. The converter's
192x108 limit requires at most 96 tiles. Frame position and 4x scaling are
identical in both formats. Planar ordering was derived from bounded reference
data analysis; the project includes only its own color chart and the attributed
Adafruit bitmap. The 15-color chart, its transparent gaps, and the low-bit-first
plane order were checked on the UC-586, followed by return to input video.

## Hardware capability and verification boundary

| Control | Hardware basis | Project status |
| --- | --- | --- |
| Full-screen startup | Free-running display background plus a centered bitmap | Five-second startup screen; input video is enabled afterward |
| Text menus | Row and character maps, proportional 12x18 tiles, 16-color palette, transparent background | Minimal splash driver; menu navigation and full font still needed |
| Small graphic splash | 1-, 2- or 4-bit tiles in dedicated OSD SRAM, with shared palette and window effects | Automatic BMP conversion; one-bit or four-bit tiles, 15 visible colors plus transparency, 4x scale |
| Video brightness | Per-channel RGB additive coefficients, separate from backlight power | Documented, not yet exposed or bench verified |
| Video contrast | Per-channel RGB multiplicative coefficients | Documented, not yet exposed or bench verified |
| Backlight level | Six 12-bit PWM channels and multiplexed output pins | UC-586 stock contains PWM1/pin103 adjustment code; physical brightness response unverified |
| One or five buttons | GPIO and ADC key-sensing inputs are available | Stock UC-586 main polls pin 53 and toggles pin 54; ADC ladder code is also present but not evidence of connected keys |
| OSD orientation | Dedicated glyph packing/rotation controls | Documented, not implemented; affects overlay only |
| Video horizontal mirror / vertical flip / 180-degree rotation | Shipped RTD2660H boards expose these modes; panel scan-direction GPIOs are one known firmware mechanism | Board-dependent; investigate the UC-586 connection before implementing |
| Video 90-degree / 270-degree rotation | No applicable video path established in the sources examined so far | Open investigation, distinct from 180-degree scan reversal; no implementation yet |

The manual's **LVDS Mirror** setting exchanges LVDS physical output order. It
does not mean horizontal image mirroring, and the UC-586 target uses RGB TTL.

Whole-screen rotation is a real board feature, not just OSD orientation.
Adafruit's [Mini RTD2660H driver guide](https://learn.adafruit.com/hdmi-uberguide/mini-rtd2660-hdmi-driver-board)
documents SYS1 through SYS4 modes including mirroring and 180-degree rotation.
That evidence establishes the shipped behavior; it does not identify how every
RTD2660H board implements it.

A concrete mechanism to investigate is panel scan-direction control. The
[ORTD2662 board profile](https://github.com/KerJoe/ORTD2662/blob/master/config/board_config.h)
assigns vertical mirroring to RTD pin 98 and horizontal mirroring to pin 99;
its [initialization](https://github.com/KerJoe/ORTD2662/blob/master/core/main.c)
configures these as GPIO outputs. This is behavioral reference, not code used
by this project. In the RTD manual these pins are P5.4 and P5.5: pin 98 uses
`0xff9f[7:6]` for pin selection and `0xffc3` for its GPIO value; pin 99 uses
`0xff9d[5:3]` and `0xffc4`. These addresses make useful Ghidra cross-reference
targets when analyzing a stock firmware with the SYS1-SYS4 menu.

For the UC-586, first establish whether equivalent signals reach its actual
panel or adapter. The KD50G21-40NT-A1 reference panel pinout does not expose
scan-direction controls on its 40-pin connector; the exact attached panel
and controller wiring still need identification. Thus this firmware currently
has no flip/rotation control, while support on a suitably wired board remains
an intended extension. Reversing both panel scan directions could provide a
180-degree mode without buffering a full frame; this is a mechanism hypothesis
for the shipped SYS modes, not a completed UC-586 measurement.

## Interface boundaries for later firmware

- Keep button decoding, backlight wiring and electrical polarity in the board
  profile. An unavailable control should return unavailable; it should not
  silently toggle an unverified pin.
- Have the UI consume logical key events: menu/select, back, increase, decrease
  and power. GPIO buttons and ADC resistor ladders can then share one menu.
  Debouncing, long presses and repeat belong between raw sampling and the menu.
- A single firmware-readable key could cycle controls and use a long press to
  select. The UC-586 stock code provides a specific pin 53 input/pin 54 output
  path to investigate, detailed below; button presses and visible backlight
  response still need correlation with that path.
- Keep video brightness/contrast separate from LED backlight level in both the
  API and menu. The former modifies pixel values; the latter controls light.
- Keep menu state and settings independent of the OSD renderer. This lets a
  serial/debug interface exercise settings before physical keys are decoded.
- Store user settings only after a confirmed edit, with a defined flash region
  and erase policy. The existing flash tail is not free scratch storage.

## Register references

The reference is Realtek's **RTD2660 series, preliminary version 1.00, June
2007**. Page numbers below are the printed pages. A public
[manual mirror](https://electropeak.com/pub/media/wysiwyg/files/RTD2660.pdf) is
linked for reference; the PDF is not distributed by this repository.

- **Pages 60-62:** common register `0x62` enables RGB brightness and contrast.
  `0x64` selects coefficient access through `0x65`. Set A offsets 0-2 are
  brightness (neutral `0x80`); offsets 3-5 are contrast (unity `0x80`). Coefficient
  precision and highlight-window selection must be considered before exposing
  a user scale.
- **Pages 64-65:** `0x6c` controls overlay composition; `0x6e/0x6f` access the
  16-entry RGB palette. Palette bytes are R, G, B in order.
- **Page 79:** `0x8c`, subaddress `0xa3`, bit 6 is LVDS wire-order mirror.
- **Page 81:** `0x90/0x91` set the OSD address and `0x92` writes data. `0x93`
  includes extended SRAM addressing and write/buffer status.
- **Pages 284-288:** MCU ADC key-sensing facilities. The A ADC starts at
  `0xff08`; five conversion results at `0xff09` through `0xff0d` use bits 7:2.
- **Pages 300-305:** six PWM channels, clock divisors, 12-bit duties and optional
  double buffering. Pin assignment is a separate board decision.
- **Pages 330-331 and 341:** pin 98/P5.4 and pin 99/P5.5 pin-sharing and GPIO
  registers, relevant to the known panel scan-direction approach above.
- **Pages 358-359:** OSD address bits 15:14 select the byte lane or all three;
  bit 12 selects SRAM versus frame/window registers. Addresses count words.
- **Pages 383-389:** frame position/enable, font-map bases, compression and OSD
  rotation. The manual has conflicting older references to frame register
  `0x002`; the detailed frame-control table describes `0x003`. This driver
  avoids rotation and compression.
- **Pages 390-398:** row commands, character selection and packed font layout.
  One 12x18 one-bit glyph occupies nine 24-bit words.

## Using firmware dumps and Ghidra

Stock firmware remains useful behavioral evidence. Preserve the original dump
and its hash outside this repository, then use disassembly to identify writes
to the specific ADC, GPIO, pin-sharing or PWM registers above. Compare writes
before and after a known stock-menu action where possible. Record the firmware
identity, register sequence, input action and observed hardware result.

Those observations can resolve board pin mappings and undocumented ordering.
They are not a reason to copy a vendor menu, font, logo, or whole decompiled
function into this implementation. Ghidra results alone also cannot establish
that a pin is connected to the backlight; confirm against the schematic,
continuity or measured behavior.

### Existing images and analysis findings

The uploaded reference collection contains three Haoyue video update files
(RGB 800x480, RGB 1024x600 and LVDS 1024x600), three separate USB-touch firmware
files, EDID samples and a historical Keil build. USB-touch files are not RTD
scaler firmware. The saved [vendor index and release notes](https://www.haoyuelectronics.com/service/RTD2660H-Driver-Board/)
make `video_firmware-RGB_800x480-v3_1.bin` a useful backlight/OSD candidate: v3.1
adds backlight adjustment, while v2.0 adds logo and OSD changes. Five-button
support or rotation in that particular image has not been established.

That candidate is 121,280 bytes, SHA256
`f2f9151a782c2e9ec71f75086ee884d6cb3696402b6c9562f3dae2699360f749`.
It begins with a `HAOYU Electronics RTD2660` container header rather than an
8051 reset vector. It must not be treated as a raw flash image. Its payload
format has not been decoded here.

An existing Ghidra analysis of the UC-586 original bank zero includes 769
functions and an instruction listing. Bank one is retained as raw data; the
whole banked program has not been exhaustively analyzed. The full original
dump is identified by SHA256
`49e363dbf3f97332470b2770893514b4f6616d5feb2dbc98cbce7c76e9147b55`.
The following are observations from its instructions, not copied functions:

| Stock code address | Observation | Interpretation / limit |
| --- | --- | --- |
| `0xcfb6`, specifically `0xcfc0..0xcff7` | Main polls `0xffca`, waits after a low input, waits for release, toggles a saved flag and writes `0xffcb` | P6.3/pin 53 input toggles P6.4/pin 54 output; strong candidate for the physical backlight button |
| `0xcc8f` | Pin setup includes `0xff99=0x02` and `0xff9c=0xa4` | Pin 53 is GPIO input and pin 54 is push-pull output, matching that main-loop path |
| `0xcc8f` | `0xffa0=0x32` | Pin103 is routed to PWM1 |
| `0x14ea` | Sets bit1 in `0xff3a`, sets bit7 in `0xff46`, then writes `0xff48=0x02` | PLL clock selected; the bit7 write is a buffer trigger, not polarity inversion; only PWM1 enabled |
| `0xe5f2`, dispatch through `0xdaba` | Reads a setting at `0xfbf7`, scales by 255/100, subtracts from 255 and writes PWM1 high duty at `0xff4b` | Candidate inverted brightness curve; physical LED polarity/connection not yet measured |
| `0xa665`, ADC helper `0xe6e7` | Samples ADC3 twice and compares against a small set of ladder levels | Generic key-decoding code exists, but pin53 is configured GPIO rather than ADC by this startup |

The ADC ladder centers are 0, 10, 18, 28 and 43 in six-bit ADC units, with a
tolerance below three counts; 18 and 28 map to the same logical result. These
are reverse-engineering clues, not a verified five-button board configuration.

The inspected bank-zero listing does not establish programmed PWM divisor
values. The manual's reset divisors and its 243 MHz PLL example would imply
about 59.3 kHz, but that is not a measured stock frequency. An explicit crystal
clock selection with PWM1 first-stage divider zero and first-stage output
selected would instead give `27 MHz / 4096 = 6591.797 Hz`. That provides a known
frequency for a later controlled probe. Under the stock-inferred brightness
curve, high-duty bytes `0x00`, `0x80`, `0x00` represent 100%, 50%, 100% requests
when the lower duty nibble is zero. This is an experimental sequence to verify,
not a currently supported brightness API. Preserve the prior pin mux, clock,
enable and duty state before testing, and restore it afterward.
