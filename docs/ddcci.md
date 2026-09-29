# Live menus and DDC/CI

The firmware exposes its settings controller over DDC/CI at seven-bit I2C
address `0x37`. The Feather RP2350 HSTX tester can send commands while video
runs or while its video output is off, without ISP or a reset. Physical button
sampling is not yet implemented. Live menu navigation, setting readback and
DDC transactions during drawing have passed the bench checks below.

Use the shared CLI at
`tools/tester/feather_rp2040/Feather_DVI_RTD_Tester/host.py` with an RP2350 tester:

```sh
python host.py key menu
python host.py key down
python host.py key menu
python host.py menu-state
python host.py key back
python host.py vcp-set 0x8d 1
python host.py vcp-get 0x8d
python host.py vcp-set 0x8d 2
```

Add `--port` or `--serial` before the subcommand to select a tester. Menu selects
a row or enters/leaves adjustment; up/down move or adjust; back returns one
level. Percentage adjustments step by five. Disabled rows do not enter edit
mode. The main pages are Picture, Audio, Display and Menu Settings; No Signal
is a submenu of Menu Settings.

## Control map

Codes below are hexadecimal; values are decimal unless prefixed with `0x`.
All settings are session-only. The controller does not write flash or other
nonvolatile storage.

| VCP | Control | Accepted values / readback |
| --- | --- | --- |
| `12` | Image contrast | 0–100, neutral/default 50 |
| `8D` | Audio mute | 1 mute, 2 unmute |
| `D6` | Soft power | 1 on, 4 off |
| `DF` | VCP version, read-only | `0x0202` |
| `E0` | Virtual key event | 1 menu, 2 back, 4 up, 8 down, 16 power toggle; reads 0 |
| `E1` | Menu state, read-only | `(page << 8) \| (selection << 1) \| editing` |
| `E2` | Image brightness | 0–100, neutral/default 50 |
| `E3` | Aspect | 0 Keep (default), 1 Fill |
| `E4` | Startup Splash on soft-power resume | 0 off, 1 on; default follows build-time `SPLASH` |
| `E5` | Connection Popup | 0 off, 1 on (default) |
| `E6` | No Signal Background | 0 black, 1 blue, 2 test bitmap (default) |
| `E7` | No Signal Sleep After | 0 Never (default), 1=1s, 2=2s, 3=5s, 4=10s, 5=20s |
| `E8` | Menu Timeout | 0 Never, 1=5s, 2=10s (default), 3=20s |

The mute and power values follow [ddcutil's MCCS reference](https://www.ddcutil.com/vcpinfo_output/).
`E0`–`E8` are project-specific. LED backlight (`10`) is unsupported and omitted
from the capabilities string; its menu row is disabled with a gray `--`.
Volume (`62`), mirror and rotation are also unavailable. Image brightness
changes pixel values independently of LED backlight.

For `E1`, page numbers are 0 closed, 1 main, 2 Picture, 3 Audio, 4 Display,
5 Menu Settings and 6 No Signal. Selection is zero-based; editing is bit zero.
For example, `0x0201` means Picture, first row, adjustment active. Selection
bits are relevant while a menu is open. `menu-state` returns the raw VCP value.

Startup Splash changes resume after `D6=4` then `D6=1`; cold boot still follows
the build-time `SPLASH` option. No-signal sleep requests backlight power off after
the selected delay and on when valid video is acquired. An
open menu postpones sleep and wakes the backlight. Soft power off also stops
audio and blanks video; DDC/CI remains serviced for resume. Settings persistence
requires a separate NVM design; the retained vendor flash tail is not storage.

The PWM1 experiment accepted VCP requests for 100%, 25% and 0%, but three camera
captures showed unchanged brightness. Level adjustment is therefore disabled;
`board_backlight_available()` returns false. Separate `board_backlight_power()`
uses P6.4 (pin 54), `0xFFCB` bit 0, following the stock button's output path.
The gate's physical backlight-off and wake behavior was verified on the UC-586.
This provides on/off control, not adjustable LED brightness.

## Transport and validation

The firmware implements Get VCP (`01`), Set VCP (`03`) and capabilities requests
(`F3`). Get VCP replies contain the maximum and current value. The host validates
the checksum, header, echoed code, result and type. Set VCP has no application
reply: a bus ACK alone does not establish that a setting was accepted; read it
back to check. Unsupported gets report unsupported; invalid sets leave settings
unchanged. No host command is automatically retried.

The host spaces transactions by 50 ms. The tester's raw `ddc HEXPACKET 0` and
`ddc - N` operations are bounded to 32 bytes and refuse a known active ISP
session. Capabilities replies are fragmented in groups of at most ten text
bytes. See the [RP2350 tester instructions](../tools/tester/feather_rp2350/Feather_HSTX_RTD_Tester/README.md)
for transport details. Programming still requires `mode off` and the existing
backup, authorization, readback and protection safeguards.

After programming, explicitly run `python host.py reset-chip` before returning
the tester to `mode 640`. An ISP-only MCU restart retained DDC peripheral state
on the bench; a whole-chip reset restored the live interface.

On 2026-09-29, the UC-586 v29 image passed full 512 KiB readback and protection
restoration to `0x0C`. Its full-image SHA256 was
`f0525e30e37b741788cc8fa8b253bec8f05ffc5d78698511090317afe4c42b26`.
Camera captures confirmed all six live pages: Main, Picture, Audio, Display,
Menu Settings and No Signal. Virtual keys, edit mode and Back matched `E1`
state readback; disabled rows could not enter adjustment. Brightness, contrast,
aspect, splash, popup and timeout settings round-tripped. With polling during
drawing, 50 ms DDC transaction spacing passed while menus rendered. A simultaneous
30-second audio capture had no measured dropouts; see the
[audio results](audio.md#validation).

The same v29 build then passed these physical checks:

- Contrast 25 darkened the picture; image brightness 75 lifted black to gray.
  Both were restored to neutral 50.
- VGA Keep showed a centered 640-pixel grid with sidebars; Fill expanded it to
  the 800-pixel panel width. Keep was restored.
- Virtual-key mute produced `8D=1` and reduced recorded RMS by about 36 dB;
  `8D=2` restored the tone. This was attenuation, not measured zero silence.
- `D6=4` visibly darkened the panel; `D6=1` recovered it. No Signal Black and
  Blue were visually confirmed. Test bitmap selection read back successfully,
  but its transition capture was inconclusive; it was taken only two seconds
  after selection and may have preceded bitmap upload completion.
- Selecting `E7=2` after input had already been absent for more than two seconds
  switched the backlight off while firmware remained running and `D6` stayed
  1. Returning the tester to `mode 640` visibly woke the panel to color bars,
  with final `D6=1`.

The last test verifies configured expiry and valid-signal wake, not a precisely
measured two-second interval from initial signal loss. It confirms P6.4/pin 54
as the physical backlight gate; the ineffective PWM1 dimming path stays disabled.

`MENU_PREVIEW=1` is a separate static artwork exercise with sample values. Its
previous camera validation does not establish live menu, DDC or PWM behavior.
