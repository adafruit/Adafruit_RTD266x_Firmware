# UC-586 HDMI audio

The first audio profile accepts **two-channel 48 kHz LPCM** and routes the first
I2S stereo pair to the board's CS4334-KSZ DAC and headphone jack. Its EDID CTA
extension advertises 16-bit samples at 48 kHz only. The basic-audio flag stays
clear because that flag also promises 32 and 44.1 kHz. Compressed audio,
multichannel downmixing and other sample rates are not provided.

## Board and register evidence

The PCB photograph identifies the CS4334-KSZ below the flash. Stock bank0
function 0xCC8F and the public RTD2660 manual pp331–332 agree on the mux:

| MCU register | Relevant selection | Output |
| --- | --- | --- |
| FF9F bits2:0 = 4 | Pin108 | MCLK |
| FFA1 bits6:4 = 5 | Pin109 | SCLK |
| FFA1 bits2:0 = 5 | Pin110 | LRCK |
| FFA2 bits6:4 = 5 | Pin111 | SD0 |

These selections were already present in the UC-586 board initialization.
[Cirrus CS4334 datasheet DS248F3](https://media.digikey.com/pdf/Data%20Sheets/Cirrus%20Logic%20PDFs/CS4334,5,8,9.pdf)
specifies standard I2S with a one-bit delay and supports 256fs MCLK at 48 kHz:
12.288 MHz. It starts automatically with clocks; there is no DAC control bus.
Allow about 230 ms for its startup and output ramp after clocks become active.

The public RTD register manual omits the HDMI audio subregisters. Their addresses
and control semantics were learned from the supplied RTD reference's `Hdmi.C`,
`ScalerDef.h`, `SystemTable.h` and `Adjust.c`, plus the earlier experimental
SDCC implementation. The new fixed-rate driver does not include their source,
objects or tables. Register values alone do not establish board-level operation.

## Clock recovery and muting

Page2 C9/CA addresses the HDMI audio registers, with C8 bit0 clear. The driver
captures the 20-bit N/CTS values and an 11-bit crystal count over 1024 TMDS
periods. For the board's 27 MHz crystal:

`sample_rate = 216000000 * N / (crystal_count * CTS)`

Dividing the denominator by N first avoids 64-bit arithmetic; the resulting
error near 48 kHz is under 11 Hz. Counts outside approximately 47.6–48.4 kHz
remain muted. Zero fields and invalid link/audio status are rejected.

The 48 kHz PLL profile targets a 196.608 MHz VCO and 12.288 MHz MCLK. The register
codes are M=13, S=0x84 and D=0x1E2F. With multiplier 15, nominal PLL frequency is
202.5 MHz; D is `(202500000-196608000)*128/(202500000/2048)+100`. The final 100 is
an empirical reference bias, not independently characterized here. Matching D
readback confirms coefficient acceptance; it is not a physical lock measurement.

The application services audio about every 10 ms between video measurements.
All clock waits have deadlines. Physical outputs remain muted through a
500 ms settling period and a further 500 ms FIFO observation period. FIFO flags
use direct write-one-to-clear accesses. The shared AV-control register preserves
video while configuring audio; video independently handles AVMute release.

Signal loss, unsupported timing, non-LPCM, AVMute, FIFO errors, hardware mute,
or a changed sample rate stops output and requires reacquisition. N/CTS can remain
stale after disconnect, so rate arithmetic alone never enables the output.
Only I2S is enabled; SPDIF remains disabled.

The live Audio menu and DDC/CI VCP `0x8D` share a mute setting:
`1` mutes and `2` unmutes, following the
[MCCS values documented by ddcutil](https://www.ddcutil.com/vcpinfo_output/).
Unmute permits output only when the existing link, format and clock checks pass;
it does not override fault muting. Soft power off (`0xD6=4`) also stops audio.
Volume uses VCP `0x62`, 0–100% linear amplitude, default 100. At 100 the gain
stage is bypassed for exact unity; 1–99 maps to the nearest coefficient out of
256. Zero gates audio output, independently of the user's mute preference.
Changing volume cannot override link, format, clock or FIFO fault muting.
Attenuation is reapplied before output is enabled after signal reacquisition.
Live menu and DDC transactions are validated in the [DDC/CI reference](ddcci.md).

The related [Realtek RTD2473AD/2483AD specification](https://285624.selcdn.ru/syms1/iblock/577/577e16164a649de516db8925973bd77a/901ce322e9fb261fcfbfc83cddc88a40.pdf),
pp156–158, identifies manual gain enable as HDMI index `03` bit3, with bit6
clear. Index `05` is gain/256; index `06` controls automatic ramping rather
than a second stereo channel. The RTD2660 reference uses the same register
names. The following UC-586 measurement qualifies this mapping on our chip.

## Validation

On 2026-09-30, the experimental 800x480 HSTX tester mode passed video grid
checks with Keep, 4:3 and 16:9, including source off/on recovery, but **failed
audio continuity**. At volume 50, active portions carried the 1 kHz tone near
-21.36 dBFS; periodic mutes and pops produced analysis-window levels from
-70.83 to -13.16 dBFS. A comparison after returning to 640x480 measured
1000 Hz at -21.426 dBFS, with all 20 windows between -21.441 and -21.412 dBFS
and no observed dropouts. Use the tester's **mode 640 for normal audio
testing**. The 800-mode failure remains unresolved; its working video does
not qualify its audio path.

On 2026-09-29, the v34 volume probe measured these levels through the existing
C-Media analog capture path while the HSTX source sent the same 1 kHz tone:

| Volume | RMS dBFS | Relative to 100% |
| --- | ---: | ---: |
| 100 | -15.375 | 0 dB |
| 50 | -21.382 | -6.007 dB |
| 25 | -27.355 | -11.980 dB |
| 0 | -51.433 | -36.058 dB |
| 100, restored | -15.358 | +0.017 dB |

The pre-update baseline was -15.359 dBFS. The 50% and 25% changes agree with
linear amplitude attenuation. Zero matches the existing output-mute behavior
in this capture path; it is not a claim of zero analog noise. Each result
excludes the first and last second of a four-second recording. Both channels
carried the same source tone; this does not establish stereo separation.

Host tests cover arithmetic limits, rate rejection, delayed unmute, PLL timeout,
FIFO and watchdog failures, loss/reacquisition, video-bit preservation and timer
wraparound. These checks do not model analog clock lock or physical sound.

On 2026-09-29, a Feather RP2350 HSTX with `Adafruit_DVI_Audio` transmitted a
1 kHz sine wave in both channels at 48 kHz while displaying 640x480 video.
A C-Media USB audio adapter captured the UC-586 headphone jack at 48 kHz:

- With source amplitude 1000/32768, the measured tone was 1000.0 Hz, RMS
  -15.4 dBFS and peak -12.1 dBFS. Harmonics two through five were each at least
  51.7 dB below the fundamental in the three-second analysis window.
- Disabling the source reduced capture RMS to -70.2 dBFS. Re-enabling it
  restored the 1 kHz tone at -15.4 dBFS and restored video.
- An initial amplitude of 6000 overloaded the analog recording path despite
  staying below PCM full scale. The tester now defaults to 1000.

This confirms the HDMI-to-DAC analog path on this board, not calibrated DAC
performance. Both transmitted channels were identical, and the USB capture
channels were effectively duplicates; stereo separation and channel mapping
remain untested. Other sample rates, compressed audio and injected AVMute/FIFO
faults have host-model coverage or deliberate rejection, not bench validation.

A later 30-second capture during live menu navigation retained the 1 kHz tone
at -15.394 dBFS RMS. Consecutive 50 ms windows ranged from -15.405 to -15.385 dBFS,
with no measured dropouts. This checks audio continuity while menus draw and
DDC commands run. A separate virtual-key mute check returned `8D=1` and reduced
capture RMS to -51.54891 dBFS; unmute returned `8D=2` and restored -15.39195 dBFS.
The approximately 36 dB reduction confirms the mute control's effect in this
recording path, not zero silence. The image and live-control checks are recorded in the
[DDC/CI validation](ddcci.md#transport-and-validation).

The RP2350 also programmed the display over HDMI DDC without a cable swap.
Full 512 KiB readback matched and flash protection returned to 0x0C. The new
256-byte EDID read back with both checksums valid. Camera checks confirmed the
splash, no-signal artwork, scaled grid and video recovery. A later OSD fix
restored the input timing overlay by maintaining its enable across hardware
background transitions; camera captures confirmed visible text and expiry.
