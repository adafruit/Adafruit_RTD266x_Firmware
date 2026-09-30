#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate the fixed RTD 12x18 font from the bundled OFL Roboto Mono source.

Reproduce with Pillow 12.3.0 / FreeType 2.14.3:
  python tools/font_to_header.py --preview build/menu-font.png
The font itself and generated glyph data are under OFL-1.1, not MIT.
"""

import argparse
import hashlib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "assets/fonts/RobotoMono-wght.ttf"
FONT_SHA256 = "66a80e79d17e4c7cabd162e2916578a4cc08fd19eef6e2a643305eae9c567b2b"
WIDTH, HEIGHT = 12, 18
FIRST, LAST = 32, 126


def glyphs():
    """Rasterize directly at native resolution; never enlarge a bitmap font."""
    if hashlib.sha256(FONT.read_bytes()).hexdigest() != FONT_SHA256:
        raise ValueError("Bundled source font does not match the documented SHA-256")
    font = ImageFont.truetype(str(FONT), 17, layout_engine=ImageFont.Layout.BASIC)
    font.set_variation_by_axes([500])
    # The pinned font's ASCII hmtx advances are all 1229/2048 em (10.20166 px).
    # FreeType grid-fitting rounds individual hinted advances to 10 or 11 px;
    # neither changes the common 12 px hardware cell or our shared pen origin.
    assert {font.getlength(chr(code)) for code in range(FIRST, LAST + 1)} <= {10, 11}
    result = []
    for code in range(FIRST, LAST + 1):
        source = Image.new("L", (48, 48))
        ImageDraw.Draw(source).text((17, 32), chr(code), font=font,
                                   anchor="ls", fill=255)
        source = source.point(lambda value: ((value * 3 + 127) // 255) * 85)
        bounds = source.getbbox()
        if bounds:
            assert bounds[0] >= 16 and bounds[2] <= 16 + WIDTH, chr(code)
            assert bounds[1] >= 18 and bounds[3] <= 18 + HEIGHT, chr(code)
        # Every glyph shares pen x=1 and baseline14. Preserve the monospace
        # face's bearings; no centering, resizing or punctuation exceptions.
        cell = source.crop((16, 18, 16 + WIDTH, 18 + HEIGHT))
        assert bool(cell.getbbox()) == (code != 32)
        result.append(cell)
    return result


def pack(cell):
    """Two 27-byte bitplanes, low bit first; SRAM byte lane 0 first."""
    packed = []
    for plane in range(2):
        rows = [sum((1 << (11 - x)) for x in range(WIDTH)
                    if (cell.getpixel((x, y)) // 85) & (1 << plane))
                for y in range(HEIGHT)]
        for y in range(0, HEIGHT, 2):
            top, bottom = rows[y:y + 2]
            word = [bottom & 255, ((top << 4) | (bottom >> 8)) & 255, top >> 4]
            assert ((word[2] << 4) | (word[1] >> 4)) == top
            assert (((word[1] & 15) << 8) | word[0]) == bottom
            packed.extend(word)
    assert len(packed) == 54
    return packed


def icons():
    """Original category artwork, supersampled for two-bit edge coverage."""
    result = []
    for kind in range(4):
        large = Image.new("L", (24 * 4, 36 * 4))
        draw = ImageDraw.Draw(large)

        def line(points):
            draw.line([(x * 4, y * 4) for x, y in points], fill=255,
                      width=6, joint="curve")

        def box(bounds):
            draw.rounded_rectangle(tuple(v * 4 for v in bounds), radius=4,
                                   outline=255, width=6)

        if kind == 0:  # Picture: framed mountain and sun.
            box((3, 8, 21, 28))
            draw.ellipse((6 * 4, 11 * 4, 10 * 4, 15 * 4), fill=255)
            line([(4, 24), (10, 18), (14, 22), (17, 19), (20, 23)])
        elif kind == 1:  # Audio: speaker cone and two sound waves.
            line([(3, 15), (8, 15), (13, 10), (13, 26), (8, 21), (3, 21), (3, 15)])
            draw.arc((11 * 4, 12 * 4, 19 * 4, 24 * 4), -65, 65, fill=255, width=6)
            draw.arc((10 * 4, 8 * 4, 23 * 4, 28 * 4), -60, 60, fill=255, width=6)
        elif kind == 2:  # Display: monitor, stand and foot.
            box((2, 9, 22, 24))
            line([(12, 24), (12, 29)])
            line([(8, 29), (16, 29)])
        else:  # Settings: three adjustment sliders.
            for x, y in ((5, 13), (12, 23), (19, 17)):
                line([(x, 8), (x, 28)])
                draw.ellipse(((x - 2) * 4, (y - 2) * 4,
                              (x + 2) * 4, (y + 2) * 4), fill=255)
                draw.ellipse(((x - 0.5) * 4, (y - 0.5) * 4,
                              (x + 0.5) * 4, (y + 0.5) * 4), fill=0)
        icon = large.resize((24, 36), Image.Resampling.BOX)
        icon = icon.point(lambda value: ((value * 3 + 127) // 255) * 85)
        assert icon.getbbox()
        result.append(icon)
    return result


def zero_runs(data):
    """Literal nonzero bytes; zero followed by a count encodes zero runs."""
    encoded = bytearray()
    offset = 0
    while offset < len(data):
        value = data[offset]
        offset += 1
        encoded.append(value)
        if value == 0:
            count = 1
            while offset < len(data) and data[offset] == 0 and count < 255:
                count += 1
                offset += 1
            encoded.append(count)
    # Independent expansion checks the entire stream, including runs crossing
    # glyph boundaries. Host register tests compare firmware output with the
    # uncompressed reference retained below, byte for byte.
    expanded = bytearray()
    stream = iter(encoded)
    for value in stream:
        expanded.extend(bytes(next(stream)) if value == 0 else bytes([value]))
    assert expanded == bytes(data)
    return encoded


def header(cells):
    packed = [pack(cell) for cell in cells]
    encoded = zero_runs(bytes(value for cell in packed for value in cell))
    lines = [
        "// SPDX-License-Identifier: OFL-1.1",
        "// Copyright 2015 The Roboto Mono Project Authors.",
        "// Derived native bitmap: RTD Menu Mono. See assets/fonts/OFL-RobotoMono.txt.",
        "// Generated by tools/font_to_header.py; do not edit glyph bytes.",
        "// Roboto Mono 17 px, weight 500; 12x18, 2-bpp, ASCII 32..126.",
        "// Shared pen x=1, baseline14; design advance10.20166px, hardware cell12.",
        "// Source SHA-256: " + FONT_SHA256,
        "#ifndef RTD_MENU_FONT_H", "#define RTD_MENU_FONT_H", "",
        "#include <stdint.h>", "",
        "#ifndef MENU_FONT_CODE", "#define MENU_FONT_CODE", "#endif", "",
        "#define MENU_FONT_FIRST 32", "#define MENU_FONT_COUNT 95",
        "#define MENU_FONT_WIDTH 12", "#define MENU_FONT_HEIGHT 18",
        "#define MENU_FONT_BPP 2", "#define MENU_FONT_BYTES 54", "",
        "/* Lossless zero-run stream: nonzero literals; 0,count repeats zero. */",
        "static const uint8_t MENU_FONT_CODE menu_font_rle[] = {",
    ]
    for offset in range(0, len(encoded), 16):
        lines.append("  " + ", ".join(f"0x{byte:02x}" for byte in encoded[offset:offset + 16]) + ",")
    lines.extend([
        "};", "", "/* Original bytes for independent host renderer checks only. */",
        "#ifdef MENU_FONT_REFERENCE",
        "static const uint8_t MENU_FONT_CODE menu_font[MENU_FONT_COUNT][MENU_FONT_BYTES] = {",
    ])
    for code, cell in enumerate(packed, FIRST):
        name = "space" if code == 32 else repr(chr(code))
        lines.append("  {" + ", ".join(f"0x{byte:02x}" for byte in cell)
                     + "}, /* " + str(code) + " " + name + " */")
    return "\n".join(lines + ["};", "#endif", "", "#endif", ""])


def icon_header(art):
    lines = ["// SPDX-License-Identifier: MIT",
             "// Original RTD menu category icons, generated by tools/font_to_header.py.",
             "// Quadrants: top-left, top-right, bottom-left, bottom-right.",
             "// Two 27-byte planes per glyph, low bit first; native icon size 24x36.",
             "#ifndef RTD_MENU_ICONS_H", "#define RTD_MENU_ICONS_H", "",
             "#include <stdint.h>", "#ifndef MENU_FONT_CODE",
             "#define MENU_FONT_CODE", "#endif", "",
             "#define MENU_ICON_COUNT 4", "#define MENU_ICON_GLYPHS 4",
             "#define MENU_ICON_BYTES 54", "",
             "static const uint8_t MENU_FONT_CODE menu_icons[MENU_ICON_COUNT][MENU_ICON_GLYPHS][MENU_ICON_BYTES] = {"]
    for name, icon in zip(("Picture", "Audio", "Display", "Settings"), art):
        lines.append("  { /* " + name + " */")
        for x, y in ((0, 0), (12, 0), (0, 18), (12, 18)):
            data = pack(icon.crop((x, y, x + 12, y + 18)))
            lines.append("    {" + ", ".join(f"0x{byte:02x}" for byte in data) + "},")
        lines.append("  },")
    return "\n".join(lines + ["};", "", "#endif", ""])


def preview(cells, art, path):
    """Show exact two-bit coverage at 2x inspection zoom, without interpolation."""
    sheet = Image.new("RGB", (832, 620), (12, 20, 36))
    draw = ImageDraw.Draw(sheet)
    draw.text((16, 8), "RTD Menu Mono - native 12x18 / preview 2x zoom", fill="white")
    for index, cell in enumerate(cells):
        x, y = 16 + (index % 16) * 48, 32 + (index // 16) * 50
        draw.rectangle((x, y, x + 25, y + 37), outline=(52, 76, 104))
        sheet.paste((255, 255, 255), (x + 1, y + 1),
                    cell.resize((24, 36), Image.Resampling.NEAREST))
        draw.text((x + 28, y + 12), f"{index + FIRST:02X}", fill=(132, 148, 168))
    samples = ["Picture  Audio  Display  Settings", "Image brightness        50%",
               "Contrast                75%", "Menu select   Back return"]
    for row, text in enumerate(samples):
        for column, char in enumerate(text):
            sheet.paste((72, 216, 192) if row == 0 else (255, 255, 255),
                        (16 + column * 24, 340 + row * 40),
                        cells[ord(char) - FIRST].resize((24, 36), Image.Resampling.NEAREST))
    for index, icon in enumerate(art):
        x = 24 + index * 200
        sheet.paste((72, 216, 192), (x, 520),
                    icon.resize((48, 72), Image.Resampling.NEAREST))
        draw.text((x + 60, 550), ("Picture", "Audio", "Display", "Settings")[index],
                  fill="white")
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "include/rtd/menu_font.h")
    parser.add_argument("--icons-output", type=Path, default=ROOT / "include/rtd/menu_icons.h")
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    cells = glyphs()
    art = icons()
    args.output.write_text(header(cells), encoding="ascii", newline="\n")
    args.icons_output.write_text(icon_header(art), encoding="ascii", newline="\n")
    if args.preview:
        preview(cells, art, args.preview)
    raw = bytes(value for cell in cells for value in pack(cell))
    print(f"Wrote {len(cells)} glyphs, {len(raw)} raw / "
          f"{len(zero_runs(raw))} compressed bytes to {args.output}")
    print(f"Wrote {len(art)} icons, {len(art) * 4 * 54} bytes to {args.icons_output}")


if __name__ == "__main__":
    main()
