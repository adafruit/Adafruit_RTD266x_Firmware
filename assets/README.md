# Startup bitmap

`splash.bmp` contains the unmodified 82x64 flower and wordmark pixels from
[`splash1_data` in Adafruit_SH110x](https://github.com/adafruit/Adafruit_SH110x/blob/master/splash.h).
The asset retains that project's BSD-3-Clause license in
`LICENSE-adafruit-logo.txt`; the new renderer is MIT licensed.

To customize the splash, edit or replace `assets/splash.bmp`, then run `make`.
The build automatically converts it to `build/<variant>/generated/splash_bitmap.h`
before compiling. That header is generated: edit the BMP, not the byte array.
Conversion happens on the build computer; the firmware needs no BMP decoder.

Alternatively, select another file without replacing the supplied logo:

```sh
make SPLASH_BMP=path/to/my-logo.bmp
```

Switching files is detected even when the selected BMP has an older timestamp.
Running ordinary `make` afterward selects the supplied default again.

Use a monochrome or color BMP, including 16-bit RGB565 files. Larger images
automatically shrink to fit 192x108 pixels while preserving aspect ratio,
without cropping. Smaller images keep their original dimensions. Nearest-neighbor
resizing preserves palette colors and transparent black without introducing halos.
For example, an 800x480 BMP becomes 180x108 pixels, displayed as 720x432 on the
full-screen blue background.

Pure black (`RGB 0,0,0`) is transparent and shows the blue background. Up to
15 other colors are preserved exactly; richer images are quantized to 15
foreground colors without dithering. Single-foreground-color artwork uses
compact one-bit tiles; multicolor artwork uses four-bit palette tiles. No
RGB565 framebuffer is used: 16-bit input is converted to this OSD palette.
Retain the appropriate license for your own artwork.

The converter uses Python 3 and Pillow (`pip3 install Pillow`). It can also be
run directly: `python3 tools/bmp_to_header.py input.bmp output.h`. It preserves
the output timestamp when the result is unchanged. The generated firmware data
contains an RGB palette and ready-to-upload OSD tiles. The 8051 does no pixel
packing during startup. A row-major reference is included only for host tests:
one-bit pixels are MSB first, and four-bit indices use the high nibble first.
The default firmware asset is 756 tile bytes plus a six-byte palette.

The converter pads and centers the image within 12x18 tiles; the driver displays those
tiles at 4x scale. The supplied logo appears as 328x256 pixels inside a
336x288 tile rectangle. On the current 800x480 panel, artwork up to 192x108
pixels fits at that scale. Conversion scales larger images down, and the build
checks data length and SRAM bounds. The largest color image uses 96 tiles and
10,368 bytes of dedicated OSD SRAM. An image whose scaled tile rectangle
exceeds the panel is not shown.

`tests/color_splash.bmp` is a palette-order test chart: transparent/red/green/blue,
yellow/cyan/magenta/white, gray/maroon/olive/navy, teal/purple/orange/lime. Build it
with `make SPLASH_BMP=tests/color_splash.bmp` to exercise the color renderer.

`rainbow-splash.bmp` is an 800x480 rainbow demo with a white Adafruit logo.
Build it with `make SPLASH_BMP=assets/rainbow-splash.bmp`. It uses the same
automatic resizing and palette conversion, displaying at 720x432 on the blue
background for five seconds. The default logo remains available as `splash.bmp`.
The rainbow artwork was generated with the built-in image-generation tool,
using the high-resolution logo from page 3 of the official
[Adafruit brand guide](https://cdn-blog.adafruit.com/uploads/2017/03/adafruit_brand_identity_guidelines_update.pdf)
as a reference. Adafruit retains its logo and trademark rights.

The startup background and five-second duration are in `src/app/monitor.c`.
No vendor bitmap, compressed logo block or runtime decompressor is needed.
