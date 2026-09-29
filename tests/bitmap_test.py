#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the public BMP conversion command with real image files."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

CONVERTER = Path(__file__).resolve().parents[1] / "tools" / "bmp_to_header.py"


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
        return bytes(int(value.strip(), 0) for value in match[1].split(",") if value.strip())

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

    def test_rejected_inputs_preserve_output(self):
        self.output.write_text("preserve existing header")
        for dimensions in ((193, 1), (1, 109)):
            with self.subTest(dimensions=dimensions):
                Image.new("RGB", dimensions).save(self.source)
                self.convert(success=False)
                self.assertEqual(self.output.read_text(), "preserve existing header")
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
