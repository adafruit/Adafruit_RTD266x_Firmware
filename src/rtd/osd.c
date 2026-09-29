// SPDX-License-Identifier: MIT
#include "rtd/io.h"
#include "rtd/osd.h"
#include "rtd/panel.h"

#ifdef __SDCC_mcs51
#define OSD_CODE __code
#else
#define OSD_CODE
#endif
#include "../../assets/splash_bitmap.h"

/* Realtek RTD2660 register manual, pp. 64-65, 81, 358-359, 383-398.
 * All accesses below are to the common scaler page. The OSD address selects
 * a three-byte word, unlike the scaler's byte-addressed register space.
 */
#define OSD_MAP_BASE 0x0010
#define OSD_FONT_BASE 0x0100
#define OSD_SRAM 0x1000
#define OSD_ALL_BYTES 0xc000
#define BITMAP_STRIDE ((SPLASH_BITMAP_WIDTH + 7u) / 8u)
#define TILE_COLUMNS ((SPLASH_BITMAP_WIDTH + 11u) / 12u)
#define TILE_ROWS ((SPLASH_BITMAP_HEIGHT + 17u) / 18u)
#define TILE_COUNT (TILE_COLUMNS * TILE_ROWS)
#define BITMAP_PAD_X ((TILE_COLUMNS * 12u - SPLASH_BITMAP_WIDTH) / 2u)
#define BITMAP_PAD_Y ((TILE_ROWS * 18u - SPLASH_BITMAP_HEIGHT) / 2u)
#define SPLASH_WIDTH (TILE_COLUMNS * 12u * 4u)
#define SPLASH_HEIGHT (TILE_ROWS * 18u * 4u)

#if SPLASH_BITMAP_WIDTH == 0 || SPLASH_BITMAP_HEIGHT == 0
#error Splash bitmap dimensions must be nonzero
#endif
/* Map entries and tile data must not overlap or use the extended SRAM bank.
 * This driver uses eight-bit tile IDs and a fixed 4x display scale.
 */
#if TILE_ROWS >= OSD_MAP_BASE || TILE_COUNT > OSD_FONT_BASE - OSD_MAP_BASE
#error Splash bitmap does not fit the OSD map
#endif
#if OSD_FONT_BASE + TILE_COUNT * 9u > 4096u
#error Splash bitmap exceeds the first 12 KiB OSD SRAM bank
#endif
_Static_assert(sizeof(splash_bitmap) == BITMAP_STRIDE * SPLASH_BITMAP_HEIGHT,
               "Splash bitmap dimensions do not match its data");

/* Extract twelve adjacent pixels from the ordinary row-major bitmap.
 * Center it within a whole-tile rectangle; padding stays transparent.
 */
static uint16_t bitmap_line(uint16_t x, uint16_t y) {
  uint8_t bit;
  uint16_t pixels = 0;
  x -= BITMAP_PAD_X;
  y -= BITMAP_PAD_Y;
  for (bit = 0; bit < 12; ++bit, ++x) {
    pixels <<= 1;
    if (x < SPLASH_BITMAP_WIDTH && y < SPLASH_BITMAP_HEIGHT &&
        (splash_bitmap[y * BITMAP_STRIDE + x / 8] & (0x80u >> (x & 7)))) {
      pixels |= 1;
    }
  }
  return pixels;
}

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
   * Center the bitmap within the active raster, then include blanking.
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
  uint8_t tile, row;
  uint16_t x, y, first, second;

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

  /* 18-pixel tile height encoded as height minus one. The row
   * commands request 2x width and height; the map ends before font data.
   */
  for (row = 0; row < TILE_ROWS; ++row) {
    write_word(OSD_SRAM | row, 0x83, (uint8_t)(17u << 3), TILE_COLUMNS);
  }
  write_word(OSD_SRAM | TILE_ROWS, 0, 0, 0);
  for (tile = 0; tile < TILE_COUNT; ++tile) {
    write_word(OSD_SRAM | (OSD_MAP_BASE + tile), 0x8c, tile, 0x10);
  }

  select_word(OSD_ALL_BYTES | OSD_SRAM | OSD_FONT_BASE);
  for (tile = 0; tile < TILE_COUNT; ++tile) {
    x = (tile % TILE_COLUMNS) * 12u;
    y = (tile / TILE_COLUMNS) * 18u;
    for (row = 0; row < 9; ++row) {
      first = bitmap_line(x, y + row * 2u);
      second = bitmap_line(x, y + row * 2u + 1u);
      /* Byte lanes upload low to high (Byte0, Byte1, Byte2). The first
       * scan line is bits23:12, so it starts in Byte2, not Byte0.
       * Two consecutive 12-bit scan lines form this 24-bit SRAM word.
       */
      rtd_write(0, 0x92, (uint8_t)second);
      rtd_write(0, 0x92, (uint8_t)((first << 4) | (second >> 8)));
      rtd_write(0, 0x92, (uint8_t)(first >> 4));
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
  if (SPLASH_WIDTH > panel.width || SPLASH_HEIGHT > panel.height) {
    return;
  }
  /* Global 2x width/height scales the already doubled character rows. */
  write_word(3, 0, 0x03, 0);
  set_frame(1);
  rtd_update(0, 0x6c, 0x01, 0x01);
}
