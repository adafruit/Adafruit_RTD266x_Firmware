# Display controls and OSD

The first target is the UC-586 with an 800x480 RGB panel. The hardware has a
separate OSD engine suitable for menus and small splash graphics. It is not a
general-purpose RGB framebuffer accessible to the 8051.

## What is implemented

The startup screen fills the panel with a black background and a centered white
Adafruit flower and wordmark bitmap. `src/app/monitor.c` holds that screen for
one second after uploading its tiles before acquiring input video.
`src/rtd/video.c` keeps the display clock and
timing generator running independently of input sync during this interval.

`tools/bmp_to_header.py` prepares one-bit or four-bit 12x18 tiles during the
build. `src/rtd/osd.c` uploads those bytes and builds a row/character map in
OSD SRAM. `osd_show_splash()` and `osd_show_no_signal()` each load and show their
own bitmap and palette; `osd_hide()` removes the overlay. The application owns
the full-screen background and handover to video. Each show initializes the OSD,
including after a scaler reset. Replace `assets/splash.bmp` and rebuild to change the artwork;
its format, dimensions and license are documented in `assets/README.md`.
BMP decoding and palette quantization happen automatically during the build.
There are up to 15 visible colors plus transparent index zero. This uses the
OSD palette and SRAM, not a full-color framebuffer.

The independent `NO_SIGNAL_BMP` build option defaults to `assets/no-signal.bmp`.
The default no-signal appearance uses a black free-running background and loads
this bitmap once. The live menu also offers plain black or blue. It remains
visible through input qualification, then hides on
successful video acquisition. No-signal artwork works with the startup splash
disabled. The two images reuse OSD SRAM rather than being resident together.

`osd_show_input()` replaces the bitmap with five rows of white text on opaque
black, inset 16 pixels from the top left. Its original 5x7 diagnostic alphabet
is expanded inside 12x18 glyphs and shown with global 2x zoom; character rows
remain 1x. The same global zoom preserves the bitmap's calibrated frame origin.
The input overlay lasts three seconds after acquisition when Connection Popup
is enabled, without pausing input
monitoring. Rejected input keeps its measured geometry, estimated refresh,
horizontal frequency, totals, polarity and first rejection reason visible.
Unknown measurements appear as `--`; a digital timeout shows the no-signal card.
The text and bitmap share SRAM and palette and replace one another on transitions.
The scaler can clear CR6C.0 when it automatically switches to background
(manual p65). `osd_service()` restores that port while the renderer has a visible
overlay; `osd_hide()` cancels restoration. This fixed the missing input overlay
on the UC-586's transition into live HSTX video. Camera captures then showed
640x480, approximately 60.2 Hz, 31.53 kHz and totals 800x524 at the top left,
followed by unobstructed video after expiry.

## Live menu and artwork preview

The normal firmware implements Picture, Audio, Display and Menu Settings menus,
plus a No Signal submenu. Menu selects a row or enters/leaves adjustment; up/down
move selection or change a value; back leaves adjustment or returns one level.
The RP2350 tester can send these events over DDC/CI while video runs. Physical
key decoding remains pending. The [DDC/CI reference](ddcci.md) documents commands,
setting values and menu-state readback. All six live pages, virtual navigation,
editing and setting readback passed [bench validation](ddcci.md#transport-and-validation);
picture, aspect, mute and backlight off/wake also passed the checks recorded there.

Picture's `IMAGE BRIGHTNESS` changes pixel values. Display's `LED BACKLIGHT`
is disabled and shows a gray `--`: PWM1 requests for 100%, 25% and 0% were
accepted, but three camera captures showed no visible brightness change.
Contrast, audio mute, Keep/Fill aspect and the runtime options below are wired
to the shared settings controller. Volume, rotation and mirror remain disabled.
The separate P6.4/pin54 backlight power gate passed physical off/wake checks.

`make MENU_PREVIEW=1` separately cycles through the main menu and Picture, Audio, Display
and Menu Settings after the startup splash. Each has three sample variants,
held for 1.5 seconds after its upload, before returning to normal acquisition.
Font upload adds time between pages. All values are artwork samples: the preview
does not read buttons or change video, audio, backlight or stored settings.
The normal build leaves the preview disabled.

The seven-row, 30-column layout is centered at 720x252 panel pixels. It reuses
the diagnostic alphabet, adding a percent glyph, and uses green titles, blue
selection rows and green/gray slider tracks. The map ends before the font base.
The variants exercise 0/50/100 percent tracks, mute on/off, alternative aspect
labels, timeout values and a highlighted Back row. Rotation and mirror show
gray `--` placeholders because their board controls are not implemented.
The preview's `FILL` sample does not change scaling; the live Display menu does.
Host checks cover every page/variant, slider endpoints, centering, palette and
return from the larger menu map to the five-row input overlay.

On 2026-09-29, the UC-586 camera sequence confirmed all four submenus at
0/50/100 percent, the selection and Back rows, and readable labels without
clipping. After the sequence, the measured input overlay appeared over live
color bars and expired normally. Full 512 KiB readback matched the preview
image, and flash protection was restored to `0x0C`.

The [classic RTD2660 Adafruit guide](https://learn.adafruit.com/hdmi-uberguide/rtd2660-hdmi-vga-ntsc-pal-driver-board)
includes photographed Color, OSD and Function menus. Its Menu/select,
Auto/back and plus/minus navigation is a reference for the interaction;
the artwork and rendering code here are independently implemented.

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

## Runtime settings

Settings are held in RAM for the current session; no settings are written to
the retained vendor flash tail. Persistence is a separate future NVM task.

| Setting | Choices | Default |
| --- | --- | --- |
| Startup Splash | Off / On, on soft-power resume | Build-time `SPLASH` value |
| Connection Popup | Off / On | On |
| Menu Timeout | Never / 5 / 10 / 20 seconds | 10 seconds |
| No Signal Background | Black / Blue / Test bitmap | Test bitmap |
| No Signal Sleep After | Never / 1 / 2 / 5 / 10 / 20 seconds | Never |

Cold boot follows the build-time `SPLASH` option. The runtime splash setting
controls resume after soft power off/on (`D6=4`, then `D6=1`). No-signal timeout
requests backlight power off, keeps monitoring input and requests power on after
valid video is acquired. An open menu postpones sleep and requests its backlight
on. The P6.4 gate visibly switched the backlight off on expiry and restored it
when valid video returned, while `D6` stayed on. The test selected a two-second
timeout after signal had already been absent longer than that; it did not
precisely time the delay from initial loss. `Never` disables automatic sleep.

## Hardware capability and verification boundary

| Control | Hardware basis | Project status |
| --- | --- | --- |
| Full-screen startup | Free-running display background plus a centered bitmap | One-second hold after bitmap upload; input video is enabled afterward |
| Text menus | Row and character maps, proportional 12x18 tiles, 16-color palette, transparent background | Six live pages, DDC/CI navigation and editing bench verified; physical keys pending |
| Small graphic splash | 1-, 2- or 4-bit tiles in dedicated OSD SRAM, with shared palette and window effects | Automatic BMP conversion; one-bit or four-bit tiles, 15 visible colors plus transparency, 4x scale |
| Video brightness | Per-channel RGB additive coefficients, separate from backlight power | Live 0–100 control; setting 75 visibly lifted black to gray, then restored to neutral 50 |
| Video contrast | Per-channel RGB multiplicative coefficients | Live 0–100 control; setting 25 visibly darkened the picture, then restored to neutral 50 |
| Backlight level | Six 12-bit PWM channels and multiplexed output pins | Disabled; PWM1 requests at 100/25/0% produced unchanged brightness in three camera captures; VCP `10` unsupported and omitted from capabilities |
| Backlight power | Stock button toggles P6.4/pin 54 through `0xFFCB` bit 0 | Physical off/on and no-signal sleep/wake verified; level dimming remains unavailable |
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
  path detailed below. Pin54 backlight control is now verified; physical pin53
  button sampling and interaction still need implementation and testing.
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
selected would instead give `27 MHz / 4096 = 6591.797 Hz`. The candidate PWM1
experiment selected that crystal clock and wrote all twelve duty bits:
100% maps to 0,
50% to 2048, and 0% to 4095. It preserves other channels' shared fields and
commits through FF46 bit7. FF48 bit6 is cleared to enable twelve-bit duty.
VCP requests for 100%, 25% and 0% were accepted, but three camera captures showed
unchanged brightness. This does not establish PWM1 as the LED dimming input.
Level adjustment is now disabled and `board_backlight_available()` returns false.

Separate `board_backlight_power()` drives the stock button's P6.4/pin 54 output
through `0xFFCB` bit 0. Camera checks verified a dark panel on soft power off,
recovery on power on, and no-signal backlight sleep followed by visible wake
when valid video returned. No dimming capability is implied by this on/off gate.

On 2026-09-29, Limor confirmed by multimeter continuity that the six-pin
backlight boost IC's EN connects to RTD2660H pin 54 (P6.4). This establishes the
board connection independently of the firmware and camera tests. The boost
IC's identity and whether EN supports PWM dimming remain unverified.
