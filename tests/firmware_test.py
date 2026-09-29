#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check actual linked interrupt targets, not just ISR source declarations."""
from pathlib import Path
import re
import sys

image = Path(sys.argv[1]).read_bytes()
link_map = Path(sys.argv[2]).read_text()
assert len(image) == 65536, "expected a complete bank0 image"
vectors = {
    0x00: "__sdcc_gsinit_startup",
    0x03: "_external0_interrupt",
    0x0B: "_timer0_interrupt",
    0x13: "_external1_interrupt",
    0x1B: "_timer1_interrupt",
    0x23: "_serial_interrupt",
    0x2B: "_timer2_interrupt",
}
for address, symbol in vectors.items():
    match = re.search(r"^C:\s+([0-9A-Fa-f]+)\s+" + symbol + r"\s", link_map, re.M)
    assert match, f"missing linked symbol: {symbol}"
    destination = int(match[1], 16)
    assert image[address] == 0x02, f"missing LJMP at vector {address:#x}"
    assert int.from_bytes(image[address + 1:address + 3], "big") == destination, symbol
print("Linked reset and all six interrupt vectors pass")
