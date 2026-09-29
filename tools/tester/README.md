# RTD266x Feather tester and programmer

The tester sketches, shared RTD266x ISP driver and Python host CLI are maintained
here alongside the display firmware. A checkout of this repository contains
both sides of the programming connection.

- [Feather RP2350 HSTX](feather_rp2350/Feather_HSTX_RTD_Tester/README.md):
  640x480 HDMI video with a 48 kHz audio test tone, live DDC/CI controls and flash
  programming through the same HDMI cable and Adafruit HSTX-to-DVI adapter.
- [Feather RP2040 DVI](feather_rp2040/Feather_DVI_RTD_Tester/README.md):
  640x480 and 800x480 video patterns, plus flash programming.

Both use `feather_rp2040/Feather_DVI_RTD_Tester/host.py` and the single
`RTD266xISP.cpp/.h` implementation in that directory. The HSTX sketch includes
the shared driver through relative wrappers; follow its documented compile
command. Install the Python dependency with `pip3 install pyserial`.

From this repository's root:

```sh
python tools/tester/feather_rp2040/Feather_DVI_RTD_Tester/host.py info
```

Use `--port` or `--serial` before the command to select a Feather when needed.
Programming requires video off, a matching verified backup and explicit
`--allow-write`; follow the full backup/recovery instructions in the RP2040
tester's README. The enabled flash profile is W25X40, JEDEC EF3013, 512 KiB.
After programming, explicitly run `reset-chip` while still in `mode off`.
An MCU-only restart retained DDC state on the UC-586; whole-chip reset cleared it.

The RP2350 sketch also supports `vcp-get`, `vcp-set`, `key` and `menu-state`
through the shared host CLI, in either video mode or off mode. These live
transactions use address `0x37` without entering ISP. For example:

```sh
python tools/tester/feather_rp2040/Feather_DVI_RTD_Tester/host.py key menu
python tools/tester/feather_rp2040/Feather_DVI_RTD_Tester/host.py vcp-set 0x8d 1
python tools/tester/feather_rp2040/Feather_DVI_RTD_Tester/host.py vcp-set 0x8d 2
```

The last two commands mute and unmute. See the [firmware control map](../../docs/ddcci.md)
for supported codes, session-only settings and validation limits. The RP2040
tester does not yet implement the live DDC transport.

Imported from Adafruit_Arduino_Tester_Code commit
[`0511f0f`](https://github.com/adafruit/Adafruit_Arduino_Tester_Code/commit/0511f0f1d34d7b2bfbf8b580b19ed6e7d6655b74).
Future RTD tester and ISP-driver changes belong in this firmware repository.
