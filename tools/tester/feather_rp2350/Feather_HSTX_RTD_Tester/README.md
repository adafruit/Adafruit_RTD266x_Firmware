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
recovery workflow. Return to `mode 640` after programming and chip reset.

Bench validation on 2026-09-29 used a UC-586 RTD2660H with W25X40 EF3013 flash:
DDC returned the 256-byte EDID with valid checksums, programming verified all
512 KiB and restored protection 0x0C, and the same HDMI connection then carried
video and audio. The display's headphone jack produced a measured 1000.0 Hz
tone; source off/on muted and restored it. The 640x480 grid filled the panel.
Both source audio channels carry the same tone, so this does not test stereo
separation. Verify DDC and matching backups on each board before programming.
