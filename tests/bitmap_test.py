#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the public BMP conversion command with real image files."""
import os
import hashlib
import importlib.util
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

CONVERTER = Path(__file__).resolve().parents[1] / "tools" / "bmp_to_header.py"
SPEC = importlib.util.spec_from_file_location("bmp_to_header", CONVERTER)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def unpack_tiles(packed, dictionary, expected_size):
    """Independent word-packet reader with explicit metadata/bounds checks."""
    if len(dictionary) > 160 or any(len(word) != 3 for word in dictionary):
        raise ValueError("Invalid word dictionary")
    if expected_size < 0 or expected_size % 3:
        raise ValueError("Invalid decoded byte count")
    data = bytearray()
    offset = 0
    previous = None
    terminated = False
    while offset < len(packed):
        control = packed[offset]
        offset += 1
        if control == 255:
            if offset != len(packed):
                raise ValueError("Trailing bytes after END")
            terminated = True
            break
        if control < len(dictionary):
            previous = dictionary[control]
            data.extend(previous)
        elif control >= 192:
            if previous is None:
                raise ValueError("Repeat before first word")
            data.extend(previous * (control - 191))
        else:
            length = (control - len(dictionary) + 1) * 3
            if offset + length > len(packed):
                raise ValueError("Truncated literal packet")
            data.extend(packed[offset:offset + length])
            offset += length
            previous = bytes(data[-3:])
        if len(data) > expected_size:
            raise ValueError("Packet exceeds decoded byte count")
    if not terminated:
        raise ValueError("Missing END marker")
    if len(data) != expected_size:
        raise ValueError("Decoded byte count does not match")
    return data


class BitmapTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "input.bmp"
        self.output = Path(self.temp.name) / "output.h"

    def convert(self, success=True):
        result = subprocess.run(
            [sys.executable, str(CONVERTER), str(self.source), str(self.output)],
            capture_output=True, text=True,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)

    def check_header(self, width, height, expected=None, bpp=1, palette=None):
        header = self.output.read_text()
        for name, value in (("WIDTH", width), ("HEIGHT", height), ("BPP", bpp)):
            match = re.search(r"#define\s+SPLASH_BITMAP_" + name + r"\s+(\d+)", header)
            self.assertIsNotNone(match, header)
            self.assertEqual(int(match[1]), value)
        match = re.search(r"splash_bitmap\s*\[\s*\]\s*=\s*\{([^}]*)\}", header, re.S)
        self.assertIsNotNone(match, header)
        actual = bytes(int(value.strip().rstrip("uU"), 0)
                       for value in match[1].split(",") if value.strip())
        if expected is not None:
            self.assertEqual(actual, expected)
        match = re.search(r"splash_palette\s*\[\s*\]\s*\[3\]\s*=\s*\{(.*?)\};",
                          header, re.S)
        self.assertIsNotNone(match, header)
        values = [int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", match[1])]
        actual_palette = [tuple(values[index:index + 3]) for index in range(0, len(values), 3)]
        count = re.search(r"#define\s+SPLASH_PALETTE_COLORS\s+(\d+)", header)
        self.assertIsNotNone(count, header)
        self.assertEqual(int(count[1]), len(actual_palette))
        if palette is not None:
            self.assertEqual(actual_palette, palette)
        return actual, actual_palette

    def test_color_preservation_black_key_and_nibble_padding(self):
        black, red, blue, green, dark = (0, 0, 0), (255, 0, 0), (0, 0, 255), (0, 255, 0), (1, 0, 0)
        image = Image.new("RGB", (5, 2))
        image.putdata([black, red, blue, green, dark, blue, black, red, dark, green])
        image.save(self.source)
        self.convert()
        self.check_header(5, 2, bytes((0x04, 0x12, 0x30, 0x10, 0x43, 0x20)),
                          bpp=4, palette=[black, blue, green, dark, red])

    def test_one_foreground_color_uses_one_bit(self):
        image = Image.new("P", (9, 2))
        image.putpalette([0, 0, 0, 17, 23, 31] + [0] * 762)
        for point in ((0, 0), (8, 0), (3, 1), (7, 1)):
            image.putpixel(point, 1)
        image.save(self.source)
        self.convert()
        self.check_header(9, 2, bytes((0x80, 0x80, 0x11, 0)),
                          palette=[(0, 0, 0), (17, 23, 31)])

    def tile_bytes(self):
        header = self.output.read_text()
        match = re.search(r"splash_tiles\s*\[\s*\]\s*=\s*\{([^}]*)\}", header, re.S)
        self.assertIsNotNone(match, header)
        self.assertRegex(header, r"#ifndef __SDCC_mcs51\s+/\*[^*]*\*/\s+"
                                 r"static const OSD_CODE uint8_t splash_bitmap")
        packed = bytes(int(value.strip(), 0) for value in match[1].split(",") if value.strip())
        count = re.search(r"#define\s+SPLASH_DICTIONARY_WORDS\s+(\d+)", header)
        self.assertIsNotNone(count)
        count = int(count[1])
        table = re.search(r"splash_dictionary\s*\[\s*\]\s*\[3\]\s*=\s*\{(.*?)\};",
                          header, re.S)
        self.assertIsNotNone(table)
        values = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", table[1]))
        self.assertEqual(len(values), max(1, count) * 3)
        dictionary = [values[index:index + 3] for index in range(0, count * 3, 3)]
        size = re.search(r"#define\s+SPLASH_TILE_BYTES\s+(\d+)", header)
        self.assertIsNotNone(size)
        return unpack_tiles(packed, dictionary, int(size[1]))

    def test_lossless_packets_and_boundaries(self):
        self.assertEqual(MODULE.pack_tiles(b""), ([], b"\xff"))
        self.assertEqual(unpack_tiles(b"\xff", [], 0), b"")
        self.assertEqual(MODULE.pack_tiles(b"abc"), ([], b"\0abc\xff"))
        self.assertEqual(MODULE.pack_tiles(b"abc" * 2), ([], b"\0abc\xc0\xff"))
        self.assertEqual(MODULE.pack_tiles(b"abc" * 64), ([], b"\0abc\xfe\xff"))
        self.assertEqual(MODULE.pack_tiles(b"abc" * 65), ([], b"\0abc\xfe\xc0\xff"))
        self.assertEqual(MODULE.pack_tiles(b"abc" * 66), ([], b"\0abc\xfe\xc1\xff"))
        self.assertEqual(MODULE.pack_tiles(b"abcxyzabcxyz"),
                         ([b"abc", b"xyz"], bytes((0, 1, 0, 1, 255))))
        unique = b"".join(index.to_bytes(3, "little") for index in range(384))
        for words in (191, 192, 193, 384):
            raw = unique[:words * 3]
            dictionary, packed = MODULE.pack_tiles(raw)
            self.assertFalse(dictionary)
            self.assertEqual(unpack_tiles(packed, dictionary, len(raw)), raw)
            self.assertEqual(len(packed), len(raw) + (words + 191) // 192 + 1)
        for data in (bytes(3), bytes(6), bytes(9), bytes(381), bytes(387),
                     bytes(765), bytes(771), bytes(range(255)), bytes(range(256)) * 3,
                     bytes(range(126)) + b"\xfe" * 387 + bytes(range(129)),
                     bytes((i * 71 + i // 17) & 255 for i in range(9720))):
            with self.subTest(size=len(data), prefix=data[:4]):
                dictionary, packed = MODULE.pack_tiles(data)
                self.assertLessEqual(len(dictionary), 160)
                self.assertEqual(unpack_tiles(packed, dictionary, len(data)), data)
        for data in (b"x", b"xx", b"abcd"):
            with self.assertRaisesRegex(ValueError, "complete three-byte"):
                MODULE.pack_tiles(data)

    def test_dictionary_counts_run_starts_and_caps_at_160(self):
        # A single long run needs no dictionary entry; nonconsecutive reuse does.
        self.assertEqual(MODULE.pack_tiles(b"abc" * 1000)[0], [])
        self.assertEqual(MODULE.pack_tiles(b"abc" * 1000 + b"xyzabcxyz")[0],
                         [b"abc", b"xyz"])
        words = [bytes((index, 0, 0)) for index in range(161)]
        raw = b"".join(words * 2)
        dictionary, packed = MODULE.pack_tiles(raw)
        self.assertEqual(dictionary, words[:160])
        self.assertEqual(unpack_tiles(packed, dictionary, len(raw)), raw)
        # Dictionary index159, literal at160, repeat boundaries192/254, END255.
        packet = bytes((159, 160)) + b"xyz" + bytes((192, 254, 255))
        self.assertEqual(unpack_tiles(packet, dictionary, 66 * 3),
                         dictionary[159] + b"xyz" * 65)

    def test_packet_reader_rejects_malformed_metadata_and_streams(self):
        for packet, dictionary, size in (
                (b"\xc0\xff", [], 3), (b"\xfe\xff", [], 189),
                (b"\0ab", [], 3), (b"\xbfabc\xff", [], 576),
                (b"\0\xff", [b"ab"], 3), (b"\0\xff", [b"abc"] * 161, 3),
                (b"\0abc\xc0\xff", [], 3), (b"\0abc\xff", [], 6),
                (b"\xff", [], 1), (b"\xff", [], -3)):
            with self.subTest(packet=packet, words=len(dictionary), size=size):
                with self.assertRaises(ValueError):
                    unpack_tiles(packet, dictionary, size)
        for packet, size in ((b"", 0), (b"\0abc", 3)):
            with self.assertRaisesRegex(ValueError, "Missing END"):
                unpack_tiles(packet, [], size)
        for packet, size in ((b"\xff\xff", 0), (b"\0abc\xff\0xyz", 3)):
            with self.assertRaisesRegex(ValueError, "Trailing bytes"):
                unpack_tiles(packet, [], size)

    def test_color_fixture_preserves_existing_tile_bytes(self):
        # Golden decoded with the prior RLE converter. This fixed fixture has
        # 15 foreground colors plus black, so it does not invoke quantization.
        # Public assets remain freely replaceable without changing this test.
        self.source = Path(__file__).resolve().parent / "color_splash.bmp"
        self.convert()
        self.assertEqual(hashlib.sha256(self.tile_bytes()).hexdigest(),
                         "e72568978876bda1b0cde9b2e04e662889e761de8caaa18f7c03c42b6cff6fab")

    def test_centered_color_tile_planes_and_byte_lanes(self):
        image = Image.new("RGB", (2, 2))
        image.putdata([(255, 0, 0), (0, 255, 0), (0, 0, 255), (0, 0, 0)])
        image.save(self.source)
        self.convert()
        # Sorted palette: black=0, blue=1, green=2, red=3. Centered pixels
        # land at x=5,6 and y=8,9. Plane 0 rows are 0x040/0x040;
        # plane 1 rows are 0x060/0. Hardware stores the bottom row first.
        expected = bytearray(108)
        expected[12:15] = bytes((0x40, 0x00, 0x04))
        expected[39:42] = bytes((0x00, 0x00, 0x06))
        self.assertEqual(self.tile_bytes(), expected)

    def test_monochrome_tiles_are_row_major(self):
        image = Image.new("1", (24, 36))
        for point in ((0, 0), (13, 1), (2, 18), (15, 19)):
            image.putpixel(point, 255)
        image.save(self.source)
        self.convert()
        expected = bytearray(4 * 27)
        expected[0:3] = bytes((0x00, 0x00, 0x80))
        expected[27:30] = bytes((0x00, 0x04, 0x00))
        expected[54:57] = bytes((0x00, 0x00, 0x20))
        expected[81:84] = bytes((0x00, 0x01, 0x00))
        self.assertEqual(self.tile_bytes(), expected)

    def test_all_black_has_white_fallback_palette(self):
        Image.new("RGB", (3, 2), "black").save(self.source)
        self.convert()
        self.check_header(3, 2, bytes(2), palette=[(0, 0, 0), (255, 255, 255)])

    def test_quantization_reserves_transparent_index(self):
        pixels = [(index, index * 37 % 256, index * 73 % 256) for index in range(64)]
        pixels[31] = (0, 0, 0)
        image = Image.new("RGB", (32, 2))
        image.putdata(pixels)
        image.save(self.source)
        self.convert()
        data, palette = self.check_header(32, 2, bpp=4)
        self.assertEqual(palette[0], (0, 0, 0))
        self.assertGreater(len(palette), 2)
        self.assertLessEqual(len(palette), 16)
        indices = [index for byte in data for index in (byte >> 4, byte & 15)]
        self.assertEqual(len(indices), len(pixels))
        for pixel, index in zip(pixels, indices):
            self.assertEqual(index == 0, pixel == (0, 0, 0))
            self.assertLess(index, len(palette))
        original = self.output.read_bytes()
        self.convert()
        self.assertEqual(self.output.read_bytes(), original)

    def test_one_bit_bmp(self):
        image = Image.new("1", (9, 2))
        for point in ((1, 0), (8, 0), (0, 1), (7, 1)):
            image.putpixel(point, 255)
        image.save(self.source)
        self.convert()
        self.check_header(9, 2, bytes((0x40, 0x80, 0x81, 0x00)),
                          palette=[(0, 0, 0), (255, 255, 255)])

    def test_maximum_size(self):
        Image.new("RGB", (192, 108), "white").save(self.source)
        self.convert()
        self.check_header(192, 108, bytes((255,)) * (24 * 108))

    def test_large_images_fit_without_cropping(self):
        for original, fitted in (((800, 480), (180, 108)),
                                 ((480, 800), (65, 108)),
                                 ((1600, 200), (192, 24)),
                                 ((193, 1), (192, 1)),
                                 ((1, 109), (1, 108))):
            with self.subTest(original=original):
                Image.new("RGB", original, "white").save(self.source)
                self.convert()
                self.check_header(*fitted)

    def write_rgb565(self, width, rows):
        # Real 16-bit BI_BITFIELDS BMP, including bottom-up rows and DWORD padding.
        stride = (width * 2 + 3) & ~3
        data = b"".join(row.ljust(stride, b"\0") for row in reversed(rows))
        offset = 14 + 40 + 12
        header = struct.pack("<2sIHHI", b"BM", offset + len(data), 0, 0, offset)
        info = struct.pack("<IiiHHIIiiII", 40, width, len(rows), 1, 16, 3,
                           len(data), 0, 0, 0, 0)
        masks = struct.pack("<III", 0xf800, 0x07e0, 0x001f)
        self.source.write_bytes(header + info + masks + data)

    def test_rgb565_colors_orientation_and_row_padding(self):
        self.write_rgb565(3, [struct.pack("<3H", 0xf800, 0x07e0, 0x001f),
                              struct.pack("<3H", 0, 0xffff, 0xf800)])
        self.convert()
        self.check_header(3, 2, bytes((0x32, 0x10, 0x04, 0x30)), bpp=4,
                          palette=[(0, 0, 0), (0, 0, 255), (0, 255, 0),
                                   (255, 0, 0), (255, 255, 255)])

    def test_full_panel_rgb565_scales_with_exact_colors_and_black_key(self):
        top = struct.pack("<H", 0x001f) * 400 + struct.pack("<H", 0xf800) * 400
        bottom = bytes(800) + struct.pack("<H", 0x07e0) * 400
        self.write_rgb565(800, [top] * 240 + [bottom] * 240)
        self.convert()
        expected = (bytes((0x11,)) * 45 + bytes((0x33,)) * 45) * 54
        expected += (bytes(45) + bytes((0x22,)) * 45) * 54
        self.check_header(180, 108, expected, bpp=4,
                          palette=[(0, 0, 0), (0, 0, 255), (0, 255, 0), (255, 0, 0)])

    def test_rejected_inputs_preserve_output(self):
        self.output.write_text("preserve existing header")
        Image.new("RGB", (9, 2)).save(self.source, format="PNG")
        self.convert(success=False)  # File signature, not just .bmp suffix.
        self.assertEqual(self.output.read_text(), "preserve existing header")
        Image.new("RGB", (9, 2)).save(self.source)
        invalid = bytearray(self.source.read_bytes())
        invalid[18:22] = bytes(4)  # BITMAPINFOHEADER width = zero.
        self.source.write_bytes(invalid)
        self.convert(success=False)
        self.assertEqual(self.output.read_text(), "preserve existing header")

    def test_identical_conversion_keeps_mtime(self):
        Image.new("RGB", (9, 2), "white").save(self.source)
        self.convert()
        content = self.output.read_bytes()
        stamp = 1_600_000_000_000_000_000
        os.utime(self.output, ns=(stamp, stamp))
        before = self.output.stat().st_mtime_ns
        self.convert()
        self.assertEqual(self.output.read_bytes(), content)
        self.assertEqual(self.output.stat().st_mtime_ns, before)


if __name__ == "__main__":
    unittest.main()
