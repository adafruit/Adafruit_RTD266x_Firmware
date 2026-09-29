# Startup bitmap

`splash.bmp` contains the unmodified 82x64 flower and wordmark pixels from
[`splash1_data` in Adafruit_SH110x](https://github.com/adafruit/Adafruit_SH110x/blob/master/splash.h).
The asset retains that project's BSD-3-Clause license in
`LICENSE-adafruit-logo.txt`; the new renderer is MIT licensed.

To customize the splash, edit or replace `assets/splash.bmp`, then run `make`.
The build automatically converts it to `splash_bitmap.h` before compiling.
That header is generated: edit the BMP, not the byte array. Conversion happens
on the build computer; the firmware needs no BMP decoder.

Use a BMP between 1x1 and 192x108 pixels. Both monochrome and color BMPs work:
pixels with grayscale brightness 128 or greater become white, and darker
pixels show the blue background. There is no dithering or automatic resizing.
For predictable artwork, use white shapes on black. If you replace the logo,
retain the appropriate license for your own artwork.

The converter uses Python 3 and Pillow (`pip3 install Pillow`). It can also be
run directly: `python3 tools/bmp_to_header.py input.bmp output.h`. It preserves
the output timestamp when the resulting pixels and dimensions are unchanged.
The generated data is one bit per pixel, MSB first, with byte-padded rows.
The default bitmap is 704 bytes with an 11-byte stride.

The driver pads and centers the image within 12x18 tiles and displays those
tiles at 4x scale. The supplied logo appears as 328x256 pixels inside a
336x288 tile rectangle. On the current 800x480 panel, artwork up to 192x108
pixels fits at that scale. Conversion rejects larger images, and the build
checks data length and SRAM bounds;
an image whose scaled tile rectangle exceeds the panel is not shown.

The startup background and five-second duration are in `src/app/monitor.c`.
No vendor bitmap, compressed logo block or runtime decompressor is needed.
