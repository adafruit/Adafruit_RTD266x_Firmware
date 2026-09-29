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
#include "no_signal_bitmap.h"

/* Realtek RTD2660 register manual, pp. 64-65, 81, 358-359, 383-398.
 * All accesses below are to the common scaler page. The OSD address selects
 * a three-byte word, unlike the scaler's byte-addressed register space.
 */
#define OSD_MAP_BASE 0x0010
#define OSD_FONT_BASE 0x0100
#define OSD_SRAM 0x1000
#define OSD_ALL_BYTES 0xc000
/* At most 16 columns x 6 rows: maps fit before word 0x100, selectors stay
 * below 128, and even 4-bpp fonts fit in the lower 12 KiB SRAM bank. */
#define CHECK_BITMAP(prefix, name) \
  _Static_assert(prefix##_BITMAP_WIDTH > 0 && prefix##_BITMAP_WIDTH <= 192 && \
                 prefix##_BITMAP_HEIGHT > 0 && prefix##_BITMAP_HEIGHT <= 108, \
                 "Bitmap dimensions exceed the OSD budget"); \
  _Static_assert((prefix##_BITMAP_BPP == 1 || prefix##_BITMAP_BPP == 4) && \
                 prefix##_PALETTE_COLORS >= 2 && prefix##_PALETTE_COLORS <= 16, \
                 "Unsupported bitmap color format"); \
  _Static_assert(sizeof(name##_tiles) == ((prefix##_BITMAP_WIDTH + 11u) / 12u) * \
                 ((prefix##_BITMAP_HEIGHT + 17u) / 18u) * 27u * prefix##_BITMAP_BPP, \
                 "Bitmap dimensions do not match tile data")
CHECK_BITMAP(SPLASH, splash);
CHECK_BITMAP(NO_SIGNAL, no_signal);

static uint16_t active_width, active_height;

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
  uint16_t x = (panel.hstart + (panel.width - active_width) / 2 -
                BOARD_OSD_X_CORRECTION) / 8;
  uint16_t y = (panel.vstart + (panel.height - active_height) / 2) / 2;
  write_word(0, (uint8_t)(y >> 3), (uint8_t)(x >> 2),
             (uint8_t)(((x & 3) << 6) | ((y & 7) << 3) | enabled));
}

void osd_hide(void) {
  rtd_update(0, 0x6c, 0x01, 0);
  write_word(0, 0, 0, 0);
  /* Manual p383 requires global double width off when OSD is inactive. */
  write_word(3, 0, 0, 0);
}

static void load_bitmap(uint8_t missing_input) {
  const OSD_CODE uint8_t *tiles, *colors;
  uint16_t tile_bytes;
  uint8_t columns, rows, map_mode, map_background, palette_count;
  uint8_t tile, row, color, channel;
  uint16_t byte;

  if (missing_input) {
    columns = (NO_SIGNAL_BITMAP_WIDTH + 11u) / 12u;
    rows = (NO_SIGNAL_BITMAP_HEIGHT + 17u) / 18u;
    map_mode = NO_SIGNAL_BITMAP_BPP == 4 ? 0x90 : 0x8c;
    map_background = NO_SIGNAL_BITMAP_BPP == 4 ? 0 : 0x10;
    palette_count = NO_SIGNAL_PALETTE_COLORS;
    tiles = no_signal_tiles;
    colors = &no_signal_palette[0][0];
    tile_bytes = sizeof(no_signal_tiles);
  } else {
    columns = (SPLASH_BITMAP_WIDTH + 11u) / 12u;
    rows = (SPLASH_BITMAP_HEIGHT + 17u) / 18u;
    map_mode = SPLASH_BITMAP_BPP == 4 ? 0x90 : 0x8c;
    map_background = SPLASH_BITMAP_BPP == 4 ? 0 : 0x10;
    palette_count = SPLASH_PALETTE_COLORS;
    tiles = splash_tiles;
    colors = &splash_palette[0][0];
    tile_bytes = sizeof(splash_tiles);
  }
  active_width = columns * 48u;
  active_height = rows * 72u;
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
  for (row = 0; row < rows; ++row) {
    write_word(OSD_SRAM | row, 0x83, (uint8_t)(17u << 3), columns);
  }
  write_word(OSD_SRAM | rows, 0, 0, 0);
  for (tile = 0; tile < columns * rows; ++tile) {
    /* LUT mode has seven-bit selectors; palette index zero is transparent. */
    write_word(OSD_SRAM | (OSD_MAP_BASE + tile), map_mode, tile, map_background);
  }

  select_word(OSD_ALL_BYTES | OSD_SRAM | OSD_FONT_BASE);
  /* Build-time conversion provides complete planes, low palette bit first,
   * with each 24-bit SRAM word already in low/middle/high byte-lane order.
   * Upload directly: the 8051 does no image decoding or per-pixel packing.
   */
  for (byte = 0; byte < tile_bytes; ++byte) {
    rtd_write(0, 0x92, tiles[byte]);
  }

  /* Palette zero is the transparent background in both tile modes. */
  rtd_write(0, 0x6e, 0x80);
  for (color = 0; color < palette_count; ++color) {
    for (channel = 0; channel < 3; ++channel) {
      rtd_write(0, 0x6f, *colors++);
    }
  }
  rtd_write(0, 0x6e, 0);
}

static void show_bitmap(void) {
  if (active_width > panel.width || active_height > panel.height) {
    return;
  }
  /* Global 2x width/height scales the already doubled character rows. */
  write_word(3, 0, 0x03, 0);
  set_frame(1);
  rtd_update(0, 0x6c, 0x01, 0x01);
}

void osd_show_splash(void) {
  load_bitmap(0);
  show_bitmap();
}

void osd_show_no_signal(void) {
  load_bitmap(1);
  show_bitmap();
}
