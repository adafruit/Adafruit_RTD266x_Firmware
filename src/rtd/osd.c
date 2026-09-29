// SPDX-License-Identifier: MIT
#include "rtd/io.h"
#include "rtd/board.h"
#include "rtd/osd.h"
#include "rtd/panel.h"

#ifdef __SDCC_mcs51
#define OSD_CODE __code
#else
#define OSD_CODE
#endif
#include "splash_bitmap.h"

/* Realtek RTD2660 register manual, pp. 64-65, 81, 358-359, 383-398.
 * All accesses below are to the common scaler page. The OSD address selects
 * a three-byte word, unlike the scaler's byte-addressed register space.
 */
#define OSD_MAP_BASE 0x0010
#define OSD_FONT_BASE 0x0100
#define OSD_SRAM 0x1000
#define OSD_ALL_BYTES 0xc000
#define TILE_COLUMNS ((SPLASH_BITMAP_WIDTH + 11u) / 12u)
#define TILE_ROWS ((SPLASH_BITMAP_HEIGHT + 17u) / 18u)
#define TILE_COUNT (TILE_COLUMNS * TILE_ROWS)
#define TILE_WORDS (9u * SPLASH_BITMAP_BPP)
#define SPLASH_WIDTH (TILE_COLUMNS * 12u * 4u)
#define SPLASH_HEIGHT (TILE_ROWS * 18u * 4u)

#if SPLASH_BITMAP_WIDTH == 0 || SPLASH_BITMAP_HEIGHT == 0
#error Splash bitmap dimensions must be nonzero
#endif
#if SPLASH_BITMAP_BPP != 1 && SPLASH_BITMAP_BPP != 4
#error Splash bitmap must use one or four bits per pixel
#endif
#if SPLASH_PALETTE_COLORS < 2 || SPLASH_PALETTE_COLORS > 16
#error Splash palette must contain between two and sixteen entries
#endif
/* Map entries and tile data must not overlap or use the extended SRAM bank.
 * This driver uses a fixed 4x display scale. LUT tiles use seven-bit IDs.
 */
#if TILE_ROWS >= OSD_MAP_BASE || TILE_COUNT > OSD_FONT_BASE - OSD_MAP_BASE
#error Splash bitmap does not fit the OSD map
#endif
#if SPLASH_BITMAP_BPP == 4 && TILE_COUNT > 128
#error Color splash exceeds seven-bit tile selectors
#endif
#if OSD_FONT_BASE + TILE_COUNT * TILE_WORDS > 4096u
#error Splash bitmap exceeds the first 12 KiB OSD SRAM bank
#endif
_Static_assert(sizeof(splash_tiles) == TILE_COUNT * TILE_WORDS * 3u,
               "Splash dimensions do not match the generated tile data");

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
   * Center in the active raster, including blanking and the board's measured
   * horizontal OSD correction. The OSD origin differs from the video origin.
   */
  uint16_t x = (panel.hstart + (panel.width - SPLASH_WIDTH) / 2 -
                BOARD_OSD_X_CORRECTION) / 8;
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
  uint8_t tile, row, color, channel;
  uint16_t byte;

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
#if SPLASH_BITMAP_BPP == 4
    /* LUT mode: seven-bit tile index; pixel zero selects background zero. */
    write_word(OSD_SRAM | (OSD_MAP_BASE + tile), 0x90, tile, 0);
#else
    write_word(OSD_SRAM | (OSD_MAP_BASE + tile), 0x8c, tile, 0x10);
#endif
  }

  select_word(OSD_ALL_BYTES | OSD_SRAM | OSD_FONT_BASE);
  /* Build-time conversion provides complete planes, low palette bit first,
   * with each 24-bit SRAM word already in low/middle/high byte-lane order.
   * Upload directly: the 8051 does no image decoding or per-pixel packing.
   */
  for (byte = 0; byte < sizeof(splash_tiles); ++byte) {
    rtd_write(0, 0x92, splash_tiles[byte]);
  }

  /* Palette zero is the transparent background in both tile modes. */
  rtd_write(0, 0x6e, 0x80);
  for (color = 0; color < SPLASH_PALETTE_COLORS; ++color) {
    for (channel = 0; channel < 3; ++channel) {
      rtd_write(0, 0x6f, splash_palette[color][channel]);
    }
  }
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
