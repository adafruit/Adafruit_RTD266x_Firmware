# RTD Menu Mono bitmap source

`RobotoMono-wght.ttf` is the unmodified variable Roboto Mono font from
[Google Fonts](https://github.com/google/fonts/tree/main/ofl/robotomono),
downloaded September 29, 2026. It is redistributable under SIL Open Font License
1.1; the complete upstream copyright and license are in `OFL-RobotoMono.txt`.
The generated bitmap font retains that license.

Source SHA-256:
`66a80e79d17e4c7cabd162e2916578a4cc08fd19eef6e2a643305eae9c567b2b`.

Rebuild with Python and Pillow 12.3.0 (FreeType 2.14.3):

```sh
python tools/font_to_header.py --preview build/menu-font.png
```

The converter uses a real monospaced face at 17 pixels, medium weight 500.
It quantizes native coverage to four levels (0, 85, 170, 255). All 95 printable
ASCII characters share pen x=1 and baseline y=14 in a 12×18 cell. No character
is centered separately, stretched, resized or moved to fit. Tall punctuation
can occupy the outer scanlines; all ink fits without clipping. Uppercase and
lowercase are distinct, and the face preserves its designed side bearings.

Every ASCII character has exactly the same source advance, 1,229 font units
out of 2,048 units per em: 10.20166 pixels at this size. FreeType's grid fitting
reports individual hinted advances of 10 or 11 pixels; it does not change the
source font's equal advance metrics. The 12-pixel hardware cell adds about
1.80 native pixels of uniform tracking relative to the designed advance. This
uses a monospaced font throughout; it does not strip kerning from a proportional
face. The next larger 18-pixel size needs 19 vertical pixels for the complete
ASCII set, so it cannot fit this hardware cell without changing glyphs.

A glyph occupies eighteen 24-bit SRAM words, 54 bytes: the low bitplane followed
by the high bitplane, each using the RTD controller's lane order. The preview
shows exact pixels enlarged 2× for inspection and the actual 12-pixel cell advance.
The live menu displays these cells at native 1× size.

Define `MENU_FONT_CODE` as `__code` before including `rtd/menu_font.h` in SDCC
firmware. It defaults to empty for host tools. The table occupies 5,130 bytes
of code space and requires no RAM copy. No kerning table or proportional
compositor is required.

`menu_icons.h` contains four original category icons (Picture, Audio, Display,
Settings), under MIT. The converter draws them at 4× resolution and averages
coverage into native 24×36 images before four-level quantization. Each icon
uses four 12×18 glyphs in top-left, top-right, bottom-left, bottom-right order.
All four icons occupy 864 bytes of code space. Their bytes are unchanged by
this font update.

The earlier `RobotoCondensed-wght.ttf` and `OFL-RobotoCondensed.txt` remain as
the licensed source for historical fixed-cell previews. Their source SHA-256 is
`dace262afcee68a5276f200d8026c57221735c0118ab5fda8c2c0d3dc409a8d0`.
