# Video implementation evidence

This driver is a documented reimplementation. Its structure, validation,
arithmetic, and generated linear filter are new. Register addresses and hardware
behavior were learned from the RTD2660 register manual and earlier UC-586 bench
experiments using the [ORTD2662 project](https://github.com/tkdesign-jp/ORTD2662).
No vendor source, stock firmware, or inherited filter table is included here.
The earlier firmware's results informed this implementation; its own bench
results are listed in the README.

## Supported timing contract

The panel profile describes physical units: 800x480, 1000 clocks per line,
525 lines per frame, nominal clock 31.5 MHz, negative HS/VS, positive DE,
HS width 48, VS width 3, active start (88, 32). The hardware encodes total
horizontal clocks minus four and horizontal output edges minus ten (manual
pp31–34). The fixed-last-line registers receive the physical 1000/525 totals.

Input is deliberately limited to two measured negative-sync modes near 60 Hz:

| Input | Total | Capture start registers | Frame delay register codes |
| --- | --- | --- | --- |
| 800x480 | 1000x525 | H=86, V=32 | CR40=2, CR41=40 |
| 640x480 | 800x525 | H=142, V=35 | CR40=5, CR41=44 |

The capture offsets and frame delay codes produced aligned test grids on the
earlier UC-586 build. They are empirical calibration, not a general mode solver.
The manual p42 describes CR41 as `16*code + 16` clocks for nonzero codes; older
bench notes called these `16*code`. The driver preserves the measured register
codes and makes no stronger claim about the physical delay.

Input measurement uses separate digital and crystal-clock measurements, with
bounded start/pop-up waits. Digital counters were observed to return one less
than total/active size on this board. The analog vertical count was 524 or 525
for the same source. The manual pp48–50 documents the fractional horizontal
measurement as a 16-line average and its four fractional bits in CR56.
Both modes must measure 31.3–31.7 kHz with negative HS/VS.

## Output clock

Manual pp131–135 describe DPLL divider encoding, charge-pump ratio, fine tuning,
and fixed-last-line controls. The UC-586 working configuration requires an
additional factor of two relative to the older manual's example. With N=8,
divisor=4, and the board's 27 MHz crystal, each M step is 421875 Hz. The code
chooses an integer M below the target and applies an upward fractional offset.
This empirical clock relationship is not a claim about every RTD266x part.

The clock routine accepts only 30–33 MHz, and mode application accepts only the
narrow supported line-rate range. All firmware arithmetic stays within 32 bits;
the host test compares clock recovery with a 64-bit reference calculation.

## Receiver and scaling

CR49[1:0]=00 selects TMDS on the tested chip, although the older RTD2660 manual
marks it reserved. Page 2 AB[1:0]=3 and B5[7]=1 are observed receiver settings;
their analog rationale remains unresolved. The UC-586 uses port 0 with both
differential polarity and red/blue lane swaps (page 2 A7=0x6F), selected by its
board configuration. Automatic HDMI/DVI detection is enabled; this driver does
not provide HDCP keys or audio support.

Horizontal expansion uses the documented 20-bit input/output fraction
(manual pp38–39), rounded to 0xCCCCD for 640/800. Bypassed axes receive
0xFFFFF. The full line buffer is enabled, vertical upscaling is bypassed, and
the downscaler and its auxiliary buffer are bypassed.

The new filter is a triangular linear-interpolation kernel generated at startup:
for `p=0..15`, the stored tap weights are
`[0, 16+32*p, 1008-32*p, 0]`. Every phase sums to 1024; there are no negative
lobes. The 4-tap/32-phase interpretation, half-phase alignment, and normalization
are inferences from numerical inspection of a working filter, not guarantees
in the manual. A 640x480 grid expanded to the full panel with its border visible
in the first fresh-firmware bench test. Detailed filter response and color
fidelity have not been characterized.
The manual p41 specifies 64 stored 12-bit coefficients, low byte first, with
the other half supplied by symmetry. The new table is loaded into inactive
bank 2 for both color paths and selected only for horizontal filtering.

## Startup and missing input

The panel starts in free-running mode with CR28 bit7 forcing the timing
generator on, bit5 selecting the full-screen background, and bit3 clear
(manual pp30–31). This raster does not depend on input VSYNC. The application
shows its bitmap over that background for one second after uploading the tiles,
then starts measuring video.
Mode application configures capture and scaling before selecting frame sync
and revealing input pixels. Signal loss returns to the free-running background.

## Bench diagnostics

With `TRACE=1`, EDID bytes 95–107 contain one hexadecimal error digit followed
by three four-digit hexadecimal fields. The checksum is updated before external
DDC access is re-enabled. `include/rtd/video.h` defines the error codes and field
meanings. For example, `03581020C0000` means success, a 16-line period count of
13697, 524 vertical lines, and negative H/V sync. Timeout fields may be stale.

MCU scratch register `0xff19` receives the error code; `0xfff2` receives the
ASCII error character read back from EDID RAM. Read those through ISP only.
An ISP read pauses and restarts the MCU, so it is not a passive live trace.
Normal builds instead leave startup/acquisition stage markers in those bytes.

## Deliberate limits

- No general input timing solver or support for arbitrary refresh rates.
- No automatic DPLL rewrite on every measured clock fluctuation. The app should
  reacquire on loss or mode change; repeated fine-clock changes previously
  destabilized a working display.
- No claim that the generic RGB888 panel assumptions fit other boards.
- Host tests cover register encoding and rejection behavior, not analog lock,
  the filter's physical mapping, color fidelity, or thermal/electrical limits.
