// SPDX-License-Identifier: MIT
#include "rtd/io.h"
#include "rtd/osd.h"
#include "rtd/panel.h"

#ifdef __SDCC_mcs51
#define OSD_CODE __code
#else
#define OSD_CODE
#endif

/* Realtek RTD2660 register manual, pp. 64-65, 81, 358-359, 383-398.
 * All accesses below are to the common scaler page. The OSD address selects
 * a three-byte word, unlike the scaler's byte-addressed register space.
 */
#define OSD_MAP_BASE 0x0010
#define OSD_FONT_BASE 0x0100
#define OSD_SRAM 0x1000
#define OSD_ALL_BYTES 0xc000
#define SPLASH_WIDTH 384u
#define SPLASH_HEIGHT 144u

/* Original five-column, seven-row block lettering: A D F R U I T 2 6 X, blank.
 * Each source pixel becomes a 2x2 block inside a 12x18 hardware tile.
 * These patterns were drawn for this project; no vendor font is included.
 */
static const OSD_CODE uint8_t letters[11][7] = {
    {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    {0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c},
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
    {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f},
    {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
    {0x07, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
    {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
    {0, 0, 0, 0, 0, 0, 0},
};
static const OSD_CODE uint8_t splash_letters[16] = {
    0, 1, 0, 2, 3, 4, 5, 6, /* ADAFRUIT */
    10, 3, 6, 1, 7, 8, 8, 9 /* Half-width space, RTD266X */
};

static void select_word(uint16_t address) {
  /* This small layout stays below the extended SRAM bank at 12 KiB. */
  rtd_update(0, 0x93, 0x08, 0);
  rtd_write(0, 0x90, (uint8_t)(address >> 8));
  rtd_write(0, 0x91, (uint8_t)address);
}

static void write_word(uint16_t address, uint8_t a, uint8_t b, uint8_t c) {
  select_word(OSD_ALL_BYTES | address);
  rtd_write(0, 0x92, a);
  rtd_write(0, 0x92, b);
  rtd_write(0, 0x92, c);
}

static void set_frame(uint8_t enabled) {
  /* Global 2x zoom also doubles both frame delays. Horizontal delay counts
   * groups of four pixels before zoom; vertical delay counts lines.
   * Center the title within the active raster, then include blanking.
   */
  uint16_t x = (panel.hstart + (panel.width - SPLASH_WIDTH) / 2) / 8;
  uint16_t y = (panel.vstart + (panel.height - SPLASH_HEIGHT) / 2) / 2;
  write_word(0, (uint8_t)(y >> 3), (uint8_t)(x >> 2),
             (uint8_t)(((x & 3) << 6) | ((y & 7) << 3) | enabled));
}

void osd_hide(void) {
  rtd_update(0, 0x6c, 0x01, 0);
  set_frame(0);
  /* Manual p383 requires global double width off when OSD is inactive. */
  write_word(3, 0, 0, 0);
}

void osd_init(void) {
  uint8_t glyph, row, bit, i;
  uint16_t pixels;

  osd_hide();
  /* Keep global zoom off until show; disable compression and scrolling.
   * Row zoom (p393) combines with global zoom (p384) for 4x hardware tiles.
   */
  write_word(3, 0, 0, 0);
  write_word(5, 0, 0, 0);
  write_word(8, 0, 0, 0);
  rtd_update(0, 0x6c, 0x1f, 0);

  /* Font-select base 0x010; one-bit font base 0x100 (both word units). */
  write_word(4, OSD_MAP_BASE & 0xff,
             ((OSD_MAP_BASE >> 4) & 0xf0) | (OSD_FONT_BASE & 0x0f),
             OSD_FONT_BASE >> 4);

  /* Two rows, 18-pixel tile height encoded as height minus one. The row
   * commands request 2x width and height; the map ends before font data.
   */
  write_word(OSD_SRAM | 0, 0x83, (uint8_t)(17u << 3), 8);
  write_word(OSD_SRAM | 1, 0x83, (uint8_t)(17u << 3), 8);
  write_word(OSD_SRAM | 2, 0, 0, 0);
  for (i = 0; i < sizeof(splash_letters); ++i) {
    /* A six-pixel blank shifts the seven-letter second line by half a tile.
     * Both lines then share a center; palette0 keeps the background clear.
     */
    write_word(OSD_SRAM | (OSD_MAP_BASE + i), i == 8 ? 0x86 : 0x8c,
               splash_letters[i], 0x10);
  }

  select_word(OSD_ALL_BYTES | OSD_SRAM | OSD_FONT_BASE);
  for (glyph = 0; glyph < sizeof(letters) / sizeof(letters[0]); ++glyph) {
    for (row = 0; row < 9; ++row) {
      pixels = 0;
      if (row > 0 && row < 8) {
        for (bit = 0; bit < 5; ++bit) {
          pixels <<= 2;
          if (letters[glyph][row - 1] & (0x10 >> bit)) {
            pixels |= 3;
          }
        }
        pixels <<= 1;
      }
      /* Byte lanes upload low to high (Byte0, Byte1, Byte2). The first
       * scan line is bits23:12, so it starts in Byte2, not Byte0.
       * Two identical 12-bit scan lines form this 24-bit SRAM word.
       */
      rtd_write(0, 0x92, (uint8_t)pixels);
      rtd_write(0, 0x92, (uint8_t)((pixels << 4) | (pixels >> 8)));
      rtd_write(0, 0x92, (uint8_t)(pixels >> 4));
    }
  }

  /* White at palette entry 1; palette entry 0 is transparent for these tiles. */
  rtd_write(0, 0x6e, 0x83);
  rtd_write(0, 0x6f, 255);
  rtd_write(0, 0x6f, 255);
  rtd_write(0, 0x6f, 255);
  rtd_write(0, 0x6e, 0);
}

void osd_show_splash(void) {
  /* Global 2x width/height scales the already doubled character rows. */
  write_word(3, 0, 0x03, 0);
  set_frame(1);
  rtd_update(0, 0x6c, 0x01, 0x01);
}
