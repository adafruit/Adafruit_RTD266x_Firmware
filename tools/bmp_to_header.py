#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Convert a BMP to packed 1-bit or 4-bit pixels for the RTD OSD."""
import argparse
from collections import Counter
from pathlib import Path

from PIL import Image, UnidentifiedImageError


def pack_tiles(data):
    """Return a small word dictionary and lossless packets in SRAM byte order.

    For N dictionary words (0..160), tags below N select one three-byte word.
    Tags N..191 copy tag-N+1 literal words; 192..254 repeat the preceding word
    tag-191 times. Tag255 ends the stream, including an empty stream. A repeat
    never appears before the first output word.
    """
    if len(data) % 3:
        raise ValueError("Tile data must contain complete three-byte SRAM words")
    words = [bytes(data[offset:offset + 3]) for offset in range(0, len(data), 3)]
    # A run needs only its first word encoded; counting every repeated word
    # would waste dictionary entries on words already handled by repeat tags.
    frequency = Counter(word for index, word in enumerate(words)
                        if index == 0 or word != words[index - 1])
    candidates = sorted(frequency, key=lambda word: (-frequency[word], word))
    dictionary = [word for word in candidates if frequency[word] >= 2][:160]
    lookup = {word: index for index, word in enumerate(dictionary)}
    count = len(dictionary)
    packed = bytearray()
    offset = 0
    previous = None
    while offset < len(words):
        if words[offset] == previous:
            start = offset
            while offset < len(words) and words[offset] == previous and offset - start < 63:
                offset += 1
            packed.append(191 + offset - start)
        elif words[offset] in lookup:
            previous = words[offset]
            packed.append(lookup[previous])
            offset += 1
        else:
            start = offset
            while offset < len(words) and offset - start < 192 - count:
                word = words[offset]
                if word in lookup or word == previous:
                    break
                previous = word
                offset += 1
            packed.append(count + offset - start - 1)
            packed.extend(b"".join(words[start:offset]))
    packed.append(255)
    return dictionary, packed


def convert(source, output, symbol="splash"):
    with Image.open(source) as image:
        if image.format != "BMP":
            raise ValueError("Input must be a BMP file")
        # Decode indexed and RGB565 BMPs before resizing or applying the black key.
        image = image.convert("RGB")
        # Fit without cropping or enlarging small artwork. Nearest-neighbor
        # preserves exact palette colors and avoids halos around transparent black.
        image.thumbnail((192, 108), Image.Resampling.NEAREST)
        width, height = image.size
        rgb_bytes = image.tobytes()
        pixels = [tuple(rgb_bytes[offset:offset + 3])
                  for offset in range(0, len(rgb_bytes), 3)]

    black = (0, 0, 0)
    visible = [pixel for pixel in pixels if pixel != black]
    colors = sorted(set(visible))
    if len(colors) <= 1:
        bpp = 1
        palette = [black, colors[0] if colors else (255, 255, 255)]
        indices = [int(pixel != black) for pixel in pixels]
    else:
        bpp = 4
        if len(colors) <= 15:
            palette = [black] + colors
            lookup = {color: index for index, color in enumerate(palette)}
            indices = [lookup[pixel] for pixel in pixels]
        else:
            # Transparent pixels must not consume any quantizer color slots.
            foreground = Image.new("RGB", (len(visible), 1))
            foreground.putdata(visible)
            quantized = foreground.quantize(
                colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE
            )
            quantized_indices = list(quantized.tobytes())
            used = sorted(set(quantized_indices))
            rgb = quantized.getpalette()
            palette = [black] + [tuple(rgb[3 * index:3 * index + 3]) for index in used]
            lookup = {index: position + 1 for position, index in enumerate(used)}
            foreground_indices = iter(lookup[index] for index in quantized_indices)
            indices = [0 if pixel == black else next(foreground_indices) for pixel in pixels]

    stride = (width * bpp + 7) // 8
    data = bytearray(stride * height)
    for offset, index in enumerate(indices):
        y, x = divmod(offset, width)
        if bpp == 1:
            data[y * stride + x // 8] |= index << (7 - x % 8)
        else:
            data[y * stride + x // 2] |= index << (4 if x % 2 == 0 else 0)

    columns, rows = (width + 11) // 12, (height + 17) // 18
    padded_width, padded_height = columns * 12, rows * 18
    pad_x, pad_y = (padded_width - width) // 2, (padded_height - height) // 2
    padded = [0] * (padded_width * padded_height)
    for offset, index in enumerate(indices):
        y, x = divmod(offset, width)
        padded[(y + pad_y) * padded_width + x + pad_x] = index

    tiles = bytearray()
    for tile_y in range(rows):
        for tile_x in range(columns):
            for plane in range(bpp):
                for row in range(0, 18, 2):
                    start = (tile_y * 18 + row) * padded_width + tile_x * 12
                    top = sum(((padded[start + x] >> plane) & 1) << (11 - x)
                              for x in range(12))
                    bottom = sum(((padded[start + padded_width + x] >> plane) & 1) << (11 - x)
                                 for x in range(12))
                    tiles.extend((bottom & 255, ((top << 4) | (bottom >> 8)) & 255, top >> 4))

    dictionary, packed = pack_tiles(tiles)
    lines = [
        "/* Generated by tools/bmp_to_header.py; edit the input BMP instead.",
        " * Pixel data retains the artwork's license; see assets/README.md.",
        " * Black is transparent index 0; rows are padded to whole bytes.",
        " * 1-bit pixels are MSB first; 4-bit pixels use the high nibble first.",
        " * Tiles are centered 12x18 bitplanes, low palette bit first, losslessly packed.",
        " * N dictionary words: tags <N select a word; N..191 copy tag-N+1 literal words;",
        " * 192..254 repeat the previous word tag-191 times; 255 ends the stream.",
        " * Words contain three bytes in SRAM byte-lane order.",
        " */",
        "#ifndef RTD_SPLASH_BITMAP_H",
        "#define RTD_SPLASH_BITMAP_H",
        "",
        f"#define SPLASH_BITMAP_WIDTH {width}u",
        f"#define SPLASH_BITMAP_HEIGHT {height}u",
        f"#define SPLASH_BITMAP_BPP {bpp}u",
        f"#define SPLASH_PALETTE_COLORS {len(palette)}u",
        f"#define SPLASH_TILE_BYTES {len(tiles)}u",
        f"#define SPLASH_DICTIONARY_WORDS {len(dictionary)}u",
        "",
        "static const OSD_CODE uint8_t splash_palette[][3] = {",
    ]
    for color in palette:
        lines.append("    {" + ", ".join(f"0x{value:02x}" for value in color) + "},")
    lines.extend(["};", "", "static const OSD_CODE uint8_t splash_dictionary[][3] = {"])
    # Keep the C array valid when N=0; the decoder never reads this sentinel.
    for word in dictionary or [bytes(3)]:
        lines.append("    {" + ", ".join(f"0x{value:02x}" for value in word) + "},")
    lines.extend(["};", "", "static const OSD_CODE uint8_t splash_tiles[] = {"])
    for start in range(0, len(packed), 12):
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in packed[start:start + 12]) + ",")
    lines.extend(["};", "", "#ifndef __SDCC_mcs51",
                  "/* Row-major source retained for host verification only. */",
                  "static const OSD_CODE uint8_t splash_bitmap[] = {"])
    for start in range(0, len(data), stride):
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in data[start:start + stride]) + ",")
    lines.extend(["};", "#endif", "", "#endif", ""])
    header = "\n".join(lines).replace("SPLASH", symbol.upper()).replace("splash", symbol)
    encoded = header.encode("ascii")
    if not output.exists() or output.read_bytes() != encoded:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(encoded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--symbol", choices=("splash", "no_signal"), default="splash")
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error("Input and output must be different files")
    try:
        convert(args.input, args.output, args.symbol)
    except (OSError, UnidentifiedImageError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
