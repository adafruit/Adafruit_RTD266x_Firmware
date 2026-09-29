# Custom splash screens

The UC-586 can show your own centered BMP on a black startup screen, using
up to 15 visible colors plus transparent black. No Keil tools are required.

## Quick start

1. Create a BMP in your image editor. An 800x480 canvas matches the panel's
   aspect ratio; use bold artwork and large lettering for the OSD's lower
   resolution. RGB565, RGB color, indexed-color and monochrome BMPs work.
2. Save it in `assets/`, for example `assets/my-splash.bmp`.
3. From the repository root, build and check that exact artwork:

   ```sh
   make SPLASH_BMP=assets/my-splash.bmp check
   ```

   To try the included rainbow demo:

   ```sh
   make SPLASH_BMP=assets/rainbow-splash.bmp check
   ```

4. Follow the [programming instructions](../README.md#programming) to install
   `build/uc586-rgb800x480-monitor-splash1/firmware.bin`. This is a 64 KiB bank0,
   not a complete flash image: preserve the rest of your board's verified backup.
5. Restart the display. The image is held for one second after its tiles load,
   then input video takes over. Hardware initialization, tile upload and video
   acquisition add to total startup time; the one second is the visible hold.

Keep passing `SPLASH_BMP=...` on subsequent builds, or replace `assets/splash.bmp`
to make your image the default. Ordinary `make` selects the supplied default
path again. For a filename with spaces, quote the assignment:
`make "SPLASH_BMP=assets/my splash.bmp" check`.

Use `make SPLASH=0` to omit the splash. To change its hold time, edit
`SPLASH_DURATION_MS` in `src/app/monitor.c` (milliseconds), then rebuild.
The startup `video_background(0, 0, 0)` calls in that file select black.

## Artwork format and limits

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
full-screen black background.

Pure black (`RGB 0,0,0`) is transparent and shows the black background. Up to
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
automatic resizing and palette conversion, displaying at 720x432 on the black
background for one second. The default logo remains available as `splash.bmp`.
The rainbow artwork was generated with the built-in image-generation tool,
using the high-resolution logo from page 3 of the official
[Adafruit brand guide](https://cdn-blog.adafruit.com/uploads/2017/03/adafruit_brand_identity_guidelines_update.pdf)
as a reference. Adafruit retains its logo and trademark rights.

The startup background and one-second duration are in `src/app/monitor.c`.
No vendor bitmap, compressed logo block or runtime decompressor is needed.

## No-signal screen

`no-signal.bmp` is the default Adafruit TV test card with a "NO SIGNAL" message.
It appears when input is absent, including startup without HDMI, and disappears
when valid video is acquired. The panel continues running without input. The
bitmap loads once per signal-loss event, so it stays steady between input checks.

Select your own image independently of the startup splash:

```sh
make SPLASH_BMP=assets/rainbow-splash.bmp NO_SIGNAL_BMP=assets/my-no-signal.bmp check
```

Or replace `assets/no-signal.bmp` to change the default. The converter handles
both images identically: aspect-preserving resize to 192x108, 4x display scale,
up to 15 visible colors, transparent black and a black display background.
The no-signal screen remains enabled with `SPLASH=0`. Returning input must pass
the normal two-sample acquisition checks before the overlay is hidden.

The test-card artwork was generated with the built-in image-generation tool,
using the same official high-resolution Adafruit logo reference as the rainbow
demo. It is a decorative status screen, not a calibrated broadcast test source.
