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
build and compresses them with lossless word dictionaries, literals and repeats. `src/rtd/osd.c`
expands those packets directly into the OSD data port and builds a row/character map in
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
black, inset 16 pixels from the top left. It uses native 12x18, two-bit
antialiased glyphs shown with global 2x zoom; character rows remain 1x. The
95-character ASCII font includes lowercase, uppercase, digits and punctuation.
It is rasterized from the bundled OFL Roboto Mono source. This is a
monospaced design with one shared pen origin and baseline; glyphs are not
individually centered. It replaces the earlier proportional Roboto Condensed
glyphs that had been forced into equal-width cells, and the older expanded
5x7 alphabet. The [font guide](../assets/fonts/README.md)
documents licensing and deterministic regeneration. The same global zoom
preserves the bitmap's calibrated frame origin.
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
plus Color, No Signal, OSD Setup, System and reset-confirmation pages.
A left icon rail selects the four categories; Menu
opens the selected category's controls. Within a page, Menu enters/leaves
adjustment; up/down move selection or change a value. Back leaves adjustment,
returns to the icon rail, or closes the menu from the rail.
Nested Back returns to the exact parent row. Menu Settings has seven rows in a
five-row viewport; moving down to System or Back reveals the final rows.
English is the only menu language; translated menus and font coverage are
deferred.
The RP2350 tester can send these events over DDC/CI while video runs. Physical
key decoding remains pending. The [DDC/CI reference](ddcci.md) documents commands,
setting values and menu-state readback. The earlier menu implementation's six
live pages, virtual navigation, editing and setting readback passed
[bench validation](ddcci.md#transport-and-validation);
picture, aspect, mute and backlight off/wake also passed the checks recorded there.
Controller host tests cover the added pages, pagination, VCP bounds, setting
calls, reset confirmation, timer wrap and persistence. On 2026-09-30, v40 bench
checks passed the added pages, pagination, reset Cancel, top-right placement,
maximum background transparency, one-minute sleep/wake and the complete burn-in
cycle and exit. v43 passed immediate factory-reset readback of nineteen defaults
and sharpness readbacks at 50 ms spacing. Camera comparison at sharpness 0/100
showed a modest edge change in Fill mode, without image calibration. v44 passed
saturation and forced-aspect geometry checks on native 800x480/525-line input.
See the dated
[bench details](ddcci.md#transport-and-validation).

Picture's `Image brightness` changes pixel values. Display's `LED backlight`
is disabled and shows a gray `--`: PWM1 requests for 100%, 25% and 0% were
accepted, but three camera captures showed no visible brightness change.
Contrast, audio mute, aspect and the runtime options below are wired
to the shared settings controller. Picture now also offers a Color submenu
with independent red/green/blue gain and saturation, plus horizontal sharpness.
Each ranges from 0–100 with neutral/default 50. Sharpness below 50 softens the
horizontal filter; above 50 sharpens it. Volume provides 0–100% linear amplitude;
rotation and mirror remain disabled pending a qualified panel control path.
The separate P6.4/pin54 backlight power gate passed physical off/wake checks.
Keep and Fill are always available. `ASPECT_4_3=1` and `ASPECT_16_9=1`, both
enabled by default, add their respective forced ratios. The 16:9 build showed
the complete 640x480 grid, including all fifteen rows, in an 800x450 viewport;
input off/on recovery and bottom-positioned menus also passed. With v44 and
native 800x480/525-line input, Keep retained the complete grid, forced 4:3
retained all 25 columns, 15 rows and both borders in a centered 640-pixel image
with 80-pixel sidebars, and forced 16:9 retained the full image in an 800x450
letterbox. Input off/on restored forced 4:3. CVT 4:3 still needs a physical
check; CVT 16:9 deliberately falls back to Keep. Native-800 audio continuity
is under investigation separately from these geometry checks. Unsupported current
timings fall back to Keep while retaining the preference, and DDC readback
reports the effective aspect.

v44's corrected saturation precision showed luminance bars at 0, reduced
chroma at 25 and the original colors at 50, with the DDC sweep passing. Gray
ramps at 0/100 were broadly preserved with a common camera/panel blue cast;
this is a functional check rather than color calibration. Saturation-100
arithmetic is host tested; maximal primaries clip at that setting.

Live menus use a 360x216 panel at native 1x size, centered by default, with a thin outline,
dark navy body, title and four original category icons in a left rail. Each icon occupies four
12x18 glyphs, producing a 24x36 panel-pixel image. Blue backgrounds
mark focused icons or selected control rows; the active category turns cyan
when focus moves to its controls. Unavailable controls keep gray labels and
values. Values align at the right edge; percentage controls have a track below
the rows, and the footer changes to cyan during adjustment.
OSD Setup adds horizontal and vertical position from 0–100, with 50 centered,
and background transparency from 0–100. Horizontal positions follow four-pixel
hardware steps. Transparency selects eight blend levels from opaque to 7/8
video while foreground text remains opaque. These preferences affect only the
live menu; splash, no-signal artwork and timing-popup placement stay unchanged.

System contains a 0–120-minute sleep timer, transient burn-in test and factory
reset. Reset opens a confirmation with Cancel selected; Back cancels as well.
Confirming restores all preferences, including the build-time splash default,
then returns to System. Its normal deferred EEPROM save preserves those defaults
for the next boot. The DDC reset command performs the same action directly.

Twelve rows of 30 map entries occupy words `0x010..0x177`, below the font base
at `0x180`. The shared cache contains 122 two-bit glyphs: 95 text characters,
16 icon quadrants and 11 original border/arrow symbols. Each glyph occupies
18 words (54 bytes); the cache ends at `0xA13`. Splash or no-signal bitmap tiles
invalidate this cache. Four coverage levels select background, two edge colors
and foreground from the palette. The generated glyphs, original icons and
renderer pass the host model and board checks described below.

On 2026-09-29, the v31 two-bit font and icon rail were flashed to the UC-586,
with all 512 KiB verified and protection restored to `0x0C`. Camera captures
show readable mixed-case labels, category icons, selected rows and the four
pages plus No Signal. DDC readback confirmed navigation, category-preserving
Back, adjustment and disabled-row behavior. A foreground cable obscures the
lower icons and footer, so their complete geometry is checked by the decoded
SRAM previews. Camera colors are not calibrated to the software palette.
The no-signal bitmap, reloaded input font, three-second timing popup, reopening
Picture, and ten-second menu expiry also passed. See [bench details](ddcci.md#transport-and-validation).

The subsequent v32 font-only update uses Roboto Mono 17 px/500. Host checks
pass with the same 5,130-byte glyph table size; full 512 KiB readback matched
SHA256 `40a1963225cdb296134cabd623f2aabcc132a2411219dfc0606e119dc3aacbfc`,
and protection returned to `0x0C`. New Picture and Settings photos show readable
labels and the complete footer. Navigation and the normal ten-second timeout
setting were checked without changing the menu controller or layout.

The host OSD test can export the rendered SRAM as 800x480 PPM previews,
without a board or firmware flash. After `make OUT=build/menu-style check`, run
`build/menu-style/tests/osd_test build/menu-style/osd-preview`. This produces
16 `-tabN-{rail,pane}-{full,short}.ppm` scenes for the four categories, plus
`-picture.ppm` and `-display.ppm`. Values in these host previews are samples,
not saved settings.

Historical validation: the 2026-09-29 v30 bench build, with the earlier ten-row,
one-bit menu artwork, passed full 512 KiB readback and restored flash
protection to `0x0C` (full-image SHA256
`80b96393c09178ac4b1c8b2695368e2f9be2b99037fcaa0f74f930f002c1aa22`).
All six live pages and Picture adjustment matched DDC menu-state readback.
Camera captures showed the frame, title icons, aligned values and footer;
a foreground cable obscured part of the left edge and some captures were soft.
The decoded SRAM previews independently cover complete geometry and palette
values. No-signal bitmap display, return to video, reopening Picture after the
bitmap, and automatic menu dismissal also passed. Host checks cover restoring
the input overlay's white-on-black palette and font after live menus. These
captures predate the current two-bit font and category rail.

`make MENU_PREVIEW=1` separately cycles through the main menu and Picture, Audio, Display
and Menu Settings after the startup splash. Each has three sample variants,
held for 1.5 seconds after its upload, before returning to normal acquisition.
Font upload adds time between pages. All values are artwork samples: the preview
does not read buttons or change video, audio, backlight or stored settings.
The normal build leaves the preview disabled.

The separate static preview's seven-row, 30-column layout is centered at
720x252 panel pixels. It now shares the native two-bit font and uses green titles, blue
selection rows and green/gray slider tracks. The map ends before the font base.
The variants exercise 0/50/100 percent tracks, mute on/off, alternative aspect
labels, timeout values and a highlighted Back row. Rotation and mirror show
gray `--` placeholders because their board controls are not implemented.
The preview's `FILL` sample does not change scaling; the live Display menu does.
Host checks cover every page/variant, slider endpoints, centering, palette and
return from the larger menu map to the five-row input overlay.

Historical validation: on 2026-09-29, the UC-586 camera sequence with the
previous one-bit diagnostic alphabet confirmed all four static submenus at
0/50/100 percent, the selection and Back rows, and readable labels without
clipping. After the sequence, the measured input overlay appeared over live
color bars and expired normally. Full 512 KiB readback matched the preview
image, and flash protection was restored to `0x0C`.

The [RTD2668 Adafruit guide](https://learn.adafruit.com/hdmi-uberguide/rtd2668-hdmi-vga-ntsc-pal-driver-board-audio)
documents Color, Sound and Function menus. Its Menu/select, Auto/back and
plus/minus navigation is a reference for the interaction. The requested left
icon rail, artwork and rendering code here are independently implemented.

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
fonts at word `0x180`; they do not overlap. The 28 tiles occupy 756 bytes of
dedicated OSD SRAM, not 8051 XRAM. The tile stream and word dictionary occupy
415 compressed bytes in flash, alongside a six-byte RGB palette. Host tests also receive the original 704-byte row-major
bitmap; the 8051 build excludes that reference copy.

Four-bit palette tiles occupy 36 words each: four consecutive one-bit planes,
with palette bit zero first. Each plane uses the same nine-word packing as a
monochrome tile. A map entry `0x90, tile_index, 0` selects LUT colors and a
transparent background; its selector is limited to seven bits. At the current
font base, the documented SRAM range holds exactly 96 such tiles. The manual's
address table on printed page 358 specifies `0x000..0xEFF`: 3,840 words, or
11,520 bytes. A 12-bit address field and the host model's 4,096-word array do
not establish that `0xF00..0xFFF` is usable SRAM. With font base `0x180`,
`(0xF00 - 0x180) / 36 = 96`; the converter's 192x108 limit requires at most
those 96 tiles, ending at `0xEFF`. Frame position and 4x scaling are
identical in both formats. Planar ordering was derived from bounded reference
data analysis; the project includes only its own color chart and the attributed
Adafruit bitmap. The 15-color chart, its transparent gaps, and the low-bit-first
plane order were checked on the UC-586, followed by return to input video.

## Runtime settings

Settings persist by default (`SETTINGS=1`) in two 32-byte records in the
UC-586's separate EEPROM at `0x4C0..0x4FF`; `SETTINGS=0` makes preferences
session-only. The retained vendor flash tail is not written. Two matching
complete EEPROM backups preceded the first write. Setting `FFC0` bit 3 fixed
physical P6 input readback on the stock code's P6.6/P6.7 I2C pair. Hardware
saves and restoration of ten changed preferences passed a whole-chip reset
with application XRAM cleared; physical power-disconnect testing remains
pending. Cooperative saves service DDC between completed bus transactions,
preserving a snapshot while later changes queue for another save. The v39 release
passed rapid DDC reads and continuous audio during saving; a newer menu volume
change during a save also restored after reset. See the
[storage qualification and diagnostic interface](ddcci.md#settings-storage).
The expanded 21-byte payload preserves the old eleven-byte prefix. Existing
records restore those preferences and use defaults for new controls; the next
save migrates atomically without enlarging the reservation. Unknown record
formats remain protected. Host legacy-migration checks pass. On v43, ten
nondefault settings in the expanded record restored after whole-chip reset:
RGB gains, sharpness, saturation, OSD X/Y/transparency, volume and aspect mode
3. This is not an actual board power-removal test; that check and physical
legacy-record migration qualification remain separate.

| Setting | Choices | Default |
| --- | --- | --- |
| RGB gains / Saturation / H Sharpness | 0–100 each | 50 |
| OSD H/V position | 0–100 across the visible panel | 50 centered |
| OSD Transparency | 0–100, mapped to eight blend levels | 0 opaque |
| Startup Splash | Off / On, at startup and soft-power resume | Build-time `SPLASH` value until a preference is saved |
| Connection Popup | Off / On | On |
| Menu Timeout | Never / 5 / 10 / 20 seconds | 10 seconds |
| No Signal Background | Black / Blue / Test bitmap | Test bitmap |
| No Signal Sleep After | Never / 1 / 2 / 5 / 10 / 20 / 30 / 40 / 50 / 60 seconds | Never |
| Sleep (minutes) | Off / 1–120 minutes, independent of signal | Off |
| Burn-in | Off / On; not persisted | Off after reset or power transition |

Saved preferences restore before the startup splash. With `SETTINGS=0`, reset
restores the build-time `SPLASH` default. The same runtime setting controls
resume after soft power off/on (`D6=4`, then `D6=1`). No-signal timeout
requests backlight power off, keeps monitoring input and requests power on after
valid video is acquired. An open menu postpones sleep and requests its backlight
on. The P6.4 gate visibly switched the backlight off on expiry and restored it
when valid video returned, while `D6` stayed on. The test selected a two-second
timeout after signal had already been absent longer than that; it did not
precisely time the delay from initial loss. `Never` disables no-signal sleep.
The newer v41 check displayed the compressed no-signal artwork and confirmed
physical backlight off with the 30-second no-signal timeout selected.

The separate minute-based sleep timer enters soft power off even with valid
input or an open menu. Its saved interval starts at boot, when the timer is set,
or when soft power resumes. It requires a power-key event or DDC power-on to
resume; valid input alone wakes only no-signal sleep. Host tests cover expiry
across the millisecond-counter wrap and a full new interval after resume.

Burn-in is a panel test that cycles red, green, blue, white and black every two
seconds using the free-running background. It stops input audio and video but
keeps menu/DDC control active. Turning it off reacquires input. It is never
saved, and reset, factory reset or a soft-power transition clears it.

## Hardware capability and verification boundary

| Control | Hardware basis | Project status |
| --- | --- | --- |
| Full-screen startup | Free-running display background plus a centered bitmap | One-second hold after bitmap upload; input video is enabled afterward |
| Text menus | Row and character maps, 12x18 two-bit glyphs, 16-color palette, transparent background | Original icon rail/font plus v40 added submenus, pagination and reset Cancel bench tested; physical keys pending |
| Small graphic splash | 1-, 2- or 4-bit tiles in dedicated OSD SRAM, with shared palette and window effects | Automatic BMP conversion; one-bit or four-bit tiles, 15 visible colors plus transparency, 4x scale |
| Video brightness | Per-channel RGB additive coefficients, separate from backlight power | Live 0–100 control; setting 75 visibly lifted black to gray, then restored to neutral 50 |
| Video contrast | Per-channel RGB multiplicative coefficients | Live 0–100 control; setting 25 visibly darkened the picture, then restored to neutral 50 |
| RGB gains and saturation | Per-channel gain and color conversion coefficients | Live 0–100 controls, neutral 50; v44 saturation 0/25/50 and DDC sweep passed; gray ramp broadly preserved at 0/100, not color calibration |
| Horizontal sharpness | Programmable horizontal scaler filter | v43 immediate readbacks at 0/100/50 passed; Fill camera comparison showed a modest edge change, not calibrated image quality |
| OSD position and transparency | Frame delay and background blending controls | v40 top-right position and transparency 100 bench checks passed |
| Forced aspect | Scaler geometry and timing paths, enabled by default | 16:9 qualified on 640x480; v44 native 800x480 Keep/4:3/16:9 complete-grid checks and 4:3 input recovery passed; CVT 4:3 untested, CVT 16:9 falls back to Keep |
| Sleep timer / factory reset | Shared controller and existing soft-power/EEPROM paths | v40 60-second intentional sleep and DDC wake passed; v43 immediate reset readback returned nineteen checked defaults |
| Burn-in | Free-running solid-color background | v40 physical RGB/white/black cycle at two-second intervals and exit passed |
| Audio volume | HDMI manual digital gain before I2S | Live 0–100 amplitude control; 50% and 25% measured approximately -6 dB and -12 dB relative to 100% |
| Persistent preferences | Separate 24LC16B EEPROM, two records with CRC and commit-last marker | v43 restored ten nondefault expanded preferences after whole-chip reset; legacy migration host-tested; actual power-disconnect test pending |
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

A concrete mechanism is panel scan-direction control. The
[ORTD2662 board profile](https://github.com/KerJoe/ORTD2662/blob/master/config/board_config.h)
assigns vertical mirroring to RTD pin 98 and horizontal mirroring to pin 99;
its [initialization](https://github.com/KerJoe/ORTD2662/blob/master/core/main.c)
configures these as GPIO outputs. The local vendor reference's `SetPanelLR`
and `SetPanelUD` functions likewise change board-defined GPIO values, and its
TCON menu cycles four scan-direction combinations when enabled for the panel.
These are behavioral references, not code used by this project.

| Reference board | Horizontal / vertical scan outputs | GPIO value registers | Pin-selection fields |
| --- | --- | --- | --- |
| ORTD2662 | Pin 99/P5.5 / pin 98/P5.4 | `0xffc4` / `0xffc3` | `0xff9d[5:3]` / `0xff9f[7:6]` |
| Vendor CF_V266B, CF_TC2660, CF_TC266A | Pin 121/P7.3 / pin 122/P7.2 | `0xffd2` / `0xffd1` | `0xffa4[1:0]` / `0xffa4[3:2]` |
| Vendor PCB800099, CF_TV2661X | Pin 103/P7.5 / pin 104/P7.4 | `0xffd4` / `0xffd3` | `0xffa0[6:4]` / `0xffa0[3:1]` |

For the UC-586, first establish whether equivalent signals reach its actual
panel or adapter. The [KD50G21-40NT-A1 reference panel pinout](https://cdn-shop.adafruit.com/datasheets/KD50G21-40NT-A1.pdf)
does not expose scan-direction controls on its 40-pin connector; the exact attached panel
and controller wiring still need identification. The UC-586 stock mux values
(`FF9F=1C`, `FF9D=1B`, `FFA0=32`, `FFA4=00`) do not configure any of these
pairs as two scan-direction outputs. No UC-586 flip/rotation path is qualified,
so this firmware does not write those pins. Reversing both panel scan
directions could provide 180-degree rotation on a suitably wired panel;
qualification requires its actual pinout, connections and signal polarities.

No applicable 90/270-degree full-video rotation path has been established.
LCDWIKI's [HDMI display-direction instructions](https://www.lcdwiki.com/res/Show_Direction_and_Touch/How_to_change_display_direction-HDMI-Capacitive_Touch-V1.2.pdf)
use Raspberry Pi `display_rotate` settings for rotation and flipping. That
changes the source output and does not establish rotation inside the MPI5001's
RTD scaler. OSD rotation and LVDS lane reversal are separate controls.

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
- Coalesce preference changes into the qualified EEPROM reservation, preserving
  the previous complete record until readback confirms the new one. The existing
  flash tail is not free scratch storage.

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
  bit 12 selects SRAM versus frame/window registers. Addresses count words;
  the SRAM map explicitly lists `0x000..0xEFF` (3,840 three-byte words).
- **Pages 383-389:** frame position/enable, font-map bases, compression and OSD
  rotation. The manual has conflicting older references to frame register
  `0x002`; the detailed frame-control table describes `0x003`. This driver
  avoids rotation and compression.
- **Pages 390-398:** row commands, character selection and packed font layout.
  One 12x18 one-bit glyph occupies nine 24-bit words; a two-bit glyph occupies
  18 and a four-bit glyph 36. The two-bit character command on pages 396-397
  assigns four palette colors, with shared high bits for the 00/11 and 01/10
  pairs. Background 00 mapped to palette 0 or 8 is transparent.

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
It begins with a 512-byte `HAOYU Electronics RTD2660` container header followed
by a 120,768-byte encoded body. The big-endian length at offset `0x20` is
120,748. Comparing the RGB and LVDS 1024x600 updates finds only one changed
header byte and two changed, aligned 16-byte body blocks (file offsets
`0x1160` and `0x11160`).
Hundreds of repeated 16-byte blocks and changes confined to whole blocks
suggest ECB-style block encryption; the algorithm and key remain unconfirmed.
The payload has not been decoded and must not be treated as raw flash or
disassembled as executable 8051 code. A raw Haoyue scaler flash dump, or the
USB updater's decoder, is needed before identifying its register writes.

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

The register manual's `PIN_SHARE_CTRL06` (`0xFF9C`, bits 7:6, pin 54) lists
only GPIO input, open-drain output, push-pull output and ADCA4 input. There is
no hardware PWM function on this pin. Leave UC-586 dimming disabled; software
PWM or a wiring change is outside the current implementation. Backlight on/off
and no-signal sleep/wake remain supported.
