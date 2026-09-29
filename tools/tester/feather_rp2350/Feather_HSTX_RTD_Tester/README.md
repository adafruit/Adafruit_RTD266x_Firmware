# Feather RP2350 HSTX RTD tester

Use a Feather RP2350 HSTX, the Adafruit 22-pin HSTX-to-DVI adapter and its
FPC cable. Leave the HDMI cable connected to the RTD display for both video
testing and flash programming. Power the display separately.

The adapter uses GPIO2/3 for HDMI DDC. On this board Arduino `Wire` maps to
physical I2C1. HSTX positive/negative pairs are clock 14/15, data0 18/19,
data1 16/17 and data2 12/13.

Install the Adafruit DVI Audio and Adafruit GFX libraries and the Arduino-Pico
RP2040/RP2350 board package. Leave CPU speed at its default 150 MHz. Video
mode uses the library's 252 MHz clock; programming mode stays at 150 MHz.

From this sketch directory, compile with PowerShell:

```powershell
$sketch = (Get-Location).Path.Replace('\', '/')
arduino-cli compile --fqbn rp2040:rp2040:adafruit_feather_rp2350_hstx --build-property "compiler.cpp.extra_flags=-I$sketch" .
```

The include flag lets Arduino's temporary build find the relative shared
`RTD266xISP.cpp/.h` wrappers. The driver remains maintained in the RP2040 DVI
tester; this sketch does not keep a second copy.

Use the existing host CLI from `../../feather_rp2040/Feather_DVI_RTD_Tester/`:

```powershell
python host.py --serial YOUR_FEATHER_USB_SERIAL info
python host.py --serial YOUR_FEATHER_USB_SERIAL mode 640
python host.py --serial YOUR_FEATHER_USB_SERIAL pattern grid
python host.py --serial YOUR_FEATHER_USB_SERIAL mode off
```

Normal boot defaults to `off`. Mode changes reboot the Feather; wait for USB
to reconnect. Only `off` and `640` are supported; `800` and `panel` are rejected.
Mode 640 displays a 320x240 RGB565 canvas doubled to 640x480 and transmits a
1 kHz stereo tone at 48 kHz, amplitude 1000. The low amplitude leaves recording
headroom at the USB microphone input used on the bench. Patterns match the
RP2040 tester.

Protocol 1 and the `FeatherDVI-RTD` hello identifier remain compatible with
the same host CLI. USB output contains JSON responses, with no periodic FPS
messages. All ISP/flash and reset commands require `mode off`, enforced by
the sketch as well as the host. Use the original tester's documented full
backup, explicit write authorization, readback verification, and protection
recovery workflow. Explicitly run `python host.py reset-chip` after programming,
while still in `mode off`, then return to `mode 640`. The UC-586 retained DDC
state after an MCU-only restart; a whole-chip reset restored live DDC operation.

## Live DDC/CI controls

With RTD firmware that implements DDC/CI, use these shared host commands in
either `mode 640` or `mode off`. They talk to I2C address `0x37` while the RTD
firmware runs; they do not enter ISP or reset either board.

```powershell
python host.py --serial YOUR_FEATHER_USB_SERIAL vcp-get 0xe2
python host.py --serial YOUR_FEATHER_USB_SERIAL vcp-set 0xe2 60
python host.py --serial YOUR_FEATHER_USB_SERIAL key menu
python host.py --serial YOUR_FEATHER_USB_SERIAL key down
python host.py --serial YOUR_FEATHER_USB_SERIAL menu-state
python host.py --serial YOUR_FEATHER_USB_SERIAL key back
python host.py --serial YOUR_FEATHER_USB_SERIAL vcp-set 0x8d 1
python host.py --serial YOUR_FEATHER_USB_SERIAL vcp-set 0x8d 2
```

VCP codes and values accept decimal or `0x`-prefixed hexadecimal. Get replies
include the current and maximum values; the host checks the response checksum,
header, VCP code, result and type. Set commands have no application-level reply:
the reported ACK confirms the bus transfer, so use a get to inspect the result.
No command is automatically retried.

`key` sends the project's vendor VCP `0xe0`: menu=1, back=2, up=4, down=8,
power=16. `menu-state` reads vendor VCP `0xe1` and returns its numeric state.
These vendor controls require the matching Adafruit RTD firmware; they are not
standard commands for unrelated monitors.

Menu selects/finishes an adjustment, up/down move or adjust, and back returns
one level. `0x8D=1` mutes audio; `0x8D=2` unmutes. `0xD6=4` requests soft power
off and `0xD6=1` resumes. The [firmware control map](../../../../docs/ddcci.md)
lists every code and decodes menu state. Settings are session-only; startup
splash selection applies to soft-power resume, while cold boot uses `SPLASH`.
LED brightness adjustment is disabled because PWM1 requests produced no visible
change. The separate P6.4 backlight gate passed physical off/on and signal-wake
checks on the UC-586.

For direct `Client.command()` use, `ddc HEXPACKET 0` writes 1–32 bytes to `0x37`;
`ddc - N` reads 1–32 bytes. Writes return `written`; reads return hexadecimal
`data`. Supply the complete DDC/CI packet, including its checksum. The host
waits 50 ms after each transaction, allowing request processing and receive-FIFO
turnaround without pausing the Feather's audio loop for that delay. The sketch
limits each live bus operation to a 10 ms timeout and restores the existing ISP
timeout afterward. Raw DDC commands also refuse a known active ISP session.
The older RP2040 tester sketch does not implement this new `ddc` command.

Live DDC at 50 ms spacing, all six menu pages, editing and setting readback passed
the [UC-586 validation](../../../../docs/ddcci.md#transport-and-validation), with
continuous audio during navigation. Separate camera/audio checks confirmed
picture adjustments, Keep/Fill aspect, mute, soft power and no-signal backlight
sleep/wake. The following results cover the earlier EDID, ISP and audio tests.

Bench validation on 2026-09-29 used a UC-586 RTD2660H with W25X40 EF3013 flash:
DDC returned the 256-byte EDID with valid checksums, programming verified all
512 KiB and restored protection 0x0C, and the same HDMI connection then carried
video and audio. The display's headphone jack produced a measured 1000.0 Hz
tone; source off/on muted and restored it. The 640x480 grid filled the panel.
Both source audio channels carry the same tone, so this does not test stereo
separation. Verify DDC and matching backups on each board before programming.
