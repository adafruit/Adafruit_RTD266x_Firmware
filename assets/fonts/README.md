# RTD Menu Sans bitmap source

`RobotoCondensed-wght.ttf` is the unmodified variable Roboto Condensed font
from [Google Fonts](https://github.com/google/fonts/tree/main/ofl/robotocondensed),
downloaded September 29, 2026. It is redistributable under SIL Open Font License
1.1; the complete upstream copyright and license are in
`OFL-RobotoCondensed.txt`. The generated bitmap font retains that license.

Source SHA-256:
`dace262afcee68a5276f200d8026c57221735c0118ab5fda8c2c0d3dc409a8d0`.

Rebuild with Python and Pillow 12.3.0 (FreeType 2.14.3):

```sh
python tools/font_to_header.py --preview build/menu-font.png
```

The converter selects weight 500 and a native size of 16 pixels, then quantizes
coverage to four levels (0, 85, 170, 255). All 95 printable ASCII characters are
drawn directly into 12×18 cells. No glyph is stretched or enlarged. Ink is
centered horizontally on a common baseline; tall punctuation is shifted to
retain blank outer scanlines. Parentheses use 15 pixels to fit without clipping.
Uppercase and lowercase are distinct. A glyph occupies eighteen 24-bit SRAM
words, 54 bytes: the low bitplane followed by the high bitplane, each using the
RTD controller's lane order. The included preview shows exact pixels at the
panel's 2× zoom.

Define `MENU_FONT_CODE` as `__code` before including `rtd/menu_font.h` in SDCC
firmware. It defaults to empty for host tools. The table occupies 5,130 bytes
of code space and requires no RAM copy.

`menu_icons.h` contains four original category icons (Picture, Audio, Display,
Settings), under MIT. The converter draws them at 4× resolution and averages
coverage into native 24×36 images before four-level quantization. Each icon
uses four 12×18 glyphs in top-left, top-right, bottom-left, bottom-right order.
All four icons occupy 864 bytes of code space.
