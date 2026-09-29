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

    def check_header(self, width, height, expected):
        header = self.output.read_text()
        for name, value in (("WIDTH", width), ("HEIGHT", height)):
            match = re.search(r"#define\s+SPLASH_BITMAP_" + name + r"\s+(\d+)", header)
            self.assertIsNotNone(match, header)
            self.assertEqual(int(match[1]), value)
        match = re.search(r"splash_bitmap\s*\[\s*\]\s*=\s*\{([^}]*)\}", header, re.S)
        self.assertIsNotNone(match, header)
        actual = bytes(int(value.strip().rstrip("uU"), 0)
                       for value in match[1].split(",") if value.strip())
        self.assertEqual(actual, expected)

    def test_color_threshold_orientation_and_byte_padding(self):
        image = Image.new("RGB", (9, 3))
        for point in ((0, 0), (8, 0), (3, 2), (7, 2)):
            image.putpixel(point, (255, 255, 255))
        for x, color in enumerate(((127, 127, 127), (128, 128, 128),
                                   (255, 0, 0), (0, 255, 0), (0, 0, 255))):
            image.putpixel((x, 1), color)
        image.save(self.source)
        self.convert()
        self.check_header(9, 3, bytes((0x80, 0x80, 0x50, 0x00, 0x11, 0x00)))

    def test_one_bit_bmp(self):
        image = Image.new("1", (9, 2))
        for point in ((1, 0), (8, 0), (0, 1), (7, 1)):
            image.putpixel(point, 255)
        image.save(self.source)
        self.convert()
        self.check_header(9, 2, bytes((0x40, 0x80, 0x81, 0x00)))

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
