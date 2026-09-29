# Startup bitmap

`splash_bitmap.h` contains the unmodified 82x64 flower and wordmark pixels from
[`splash1_data` in Adafruit_SH110x](https://github.com/adafruit/Adafruit_SH110x/blob/master/splash.h).
The asset retains that project's BSD-3-Clause license in
`LICENSE-adafruit-logo.txt`; the new renderer is MIT licensed.

To substitute artwork, replace the two dimensions and byte array in this
header. Data is one bit per pixel, left-to-right and top-to-bottom, with each
row padded to a whole byte. Bit7 is the leftmost pixel. A one selects white;
a zero leaves the full-screen blue background visible. The default bitmap is
704 bytes, with an 11-byte stride.

The driver pads and centers the image within 12x18 tiles and displays those
tiles at 4x scale. The supplied logo appears as 328x256 pixels inside a
336x288 tile rectangle. On the current 800x480 panel, artwork up to 192x108
pixels fits at that scale. The build checks data length and SRAM bounds;
an image whose scaled tile rectangle exceeds the panel is not shown.

The startup background and five-second duration are in `src/app/monitor.c`.
No vendor bitmap, compressed logo block or runtime decompressor is needed.
