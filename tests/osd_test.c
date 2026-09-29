// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rtd/io.h"
#include "rtd/board.h"
#include "rtd/osd.h"
#include "rtd/panel.h"

#define OSD_CODE
#include "splash_bitmap.h"
#include "no_signal_bitmap.h"

/* Model the documented three byte lanes and auto-incrementing word port,
 * independently of the driver's bitmap packing code. */
static uint8_t regs[256], frame[16][3], sram[4096][3];
static uint8_t written[4096][3], palette[48];
static uint16_t address;
static unsigned lane, palette_bytes;

uint8_t rtd_read(uint8_t page, uint8_t reg) {
  assert(page == 0);
  return regs[reg];
}

void rtd_write(uint8_t page, uint8_t reg, uint8_t value) {
  unsigned word;
  assert(page == 0);
  regs[reg] = value;
  if (reg == 0x90) {
    address = (address & 0xff) | ((uint16_t)value << 8);
  } else if (reg == 0x91) {
    address = (address & 0xff00) | value;
    lane = 0;
  } else if (reg == 0x92) {
    assert((address & 0xc000) == 0xc000); /* All three byte lanes. */
    assert(!(regs[0x93] & 0x08));       /* Lower SRAM bank. */
    word = address & 0x0fff;
    if (address & 0x1000) {
      assert(!written[word][lane]);
      sram[word][lane] = value;
      written[word][lane] = 1;
    } else {
      assert(word < 16);
      frame[word][lane] = value;
    }
    if (++lane == 3) {
      lane = 0;
      ++address;
    }
  } else if (reg == 0x6f) {
    assert(regs[0x6e] == 0x80 && palette_bytes < sizeof(palette));
    palette[palette_bytes++] = value;
  }
}

void rtd_update(uint8_t page, uint8_t reg, uint8_t mask, uint8_t value) {
  rtd_write(page, reg, (rtd_read(page, reg) & (uint8_t)~mask) | (value & mask));
}

static void check_asset(void (*show)(void), const char *name,
                        unsigned bitmap_width, unsigned bitmap_height,
                        unsigned bpp, unsigned colors,
                        const uint8_t expected_palette[][3],
                        const uint8_t *bitmap) {
  const unsigned columns = (bitmap_width + 11) / 12;
  const unsigned rows = (bitmap_height + 17) / 18;
  const unsigned width = columns * 12, height = rows * 18;
  const unsigned pad_x = (width - bitmap_width) / 2;
  const unsigned pad_y = (height - bitmap_height) / 2;
  unsigned map, fonts, row, column, x, y, differing_pairs = 0;
  unsigned x_delay, y_delay;

  assert(bpp == 1 || bpp == 4);
  assert(colors <= 16);
  /* Preserve SRAM, palette and frame state between shows. Only reset the
   * transaction tracking, so missing writes cannot masquerade as clean RAM. */
  memset(written, 0, sizeof(written));
  palette_bytes = 0;
  show();
  assert((regs[0x6c] & 1) && (frame[0][2] & 1));
  assert(frame[3][1] == 3);
  assert(palette_bytes == colors * 3);
  for (row = 0; row < colors; ++row) {
    for (column = 0; column < 3; ++column) {
      assert(palette[row * 3 + column] == expected_palette[row][column]);
    }
  }
  assert(!(regs[0x6e] & 0x80));

  map = frame[4][0] | ((unsigned)(frame[4][1] & 0xf0) << 4);
  fonts = (frame[4][1] & 0x0f) | ((unsigned)frame[4][2] << 4);
  assert(rows + 1 <= map);
  assert(map + rows * columns <= fonts);
  assert(fonts + rows * columns * 9 * bpp <= 4096);
  assert(rows * columns <= (bpp == 4 ? 128 : 256));
  for (row = 0; row < rows; ++row) {
    assert(sram[row][0] == 0x83); /* Enabled, row width/height both 2x. */
    assert((sram[row][1] >> 3) + 1u == 18);
    assert((sram[row][1] & 7) == 0);
    assert(sram[row][2] == columns);
    for (column = 0; column < columns; ++column) {
      unsigned entry = map + row * columns + column;
      assert(sram[entry][0] == (bpp == 1 ? 0x8c : 0x90));
      assert(sram[entry][1] == row * columns + column);
      assert(sram[entry][2] == (bpp == 1 ? 0x10 : 0));
    }
  }
  assert(sram[rows][0] == 0 && sram[rows][1] == 0 && sram[rows][2] == 0);

  /* Recover indices through the map and 24-bit words. Multicolor tiles
   * contain complete 27-byte planes, starting with palette-index bit zero. */
  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      unsigned entry = map + (y / 18) * columns + x / 12;
      unsigned bit = (y % 2 ? 11 : 23) - x % 12;
      unsigned expected = 0, actual = 0, plane;
      for (plane = 0; plane < bpp; ++plane) {
        unsigned word = fonts + sram[entry][1] * 9 * bpp +
                        plane * 9 + (y % 18) / 2;
        uint32_t pixels = (uint32_t)sram[word][0] |
                          ((uint32_t)sram[word][1] << 8) |
                          ((uint32_t)sram[word][2] << 16);
        actual |= ((pixels >> bit) & 1) << plane;
        if (!(y % 2) && ((pixels >> 12) & 0xfff) != (pixels & 0xfff)) {
          ++differing_pairs;
        }
      }
      if (x >= pad_x && x < pad_x + bitmap_width &&
          y >= pad_y && y < pad_y + bitmap_height) {
        unsigned source_x = x - pad_x, source_y = y - pad_y;
        unsigned source_bit = source_x * bpp;
        unsigned byte = source_y * ((bitmap_width * bpp + 7) / 8) +
                        source_bit / 8;
        expected = (bitmap[byte] >> (8 - bpp - source_bit % 8)) &
                   ((1u << bpp) - 1);
      }
      assert(actual == expected);
      assert(actual < colors);
    }
  }
  /* Every expected word has all lanes; unused SRAM was not overwritten. */
  for (row = 0; row < 4096; ++row) {
    unsigned used = row <= rows ||
                    (row >= map && row < map + rows * columns) ||
                    (row >= fonts && row < fonts + rows * columns * 9 * bpp);
    for (column = 0; column < 3; ++column) {
      assert(written[row][column] == used);
    }
  }

  assert((regs[0x6c] & 1) && (frame[0][2] & 1));
  assert(frame[3][1] == 3); /* Global 2x combines with row 2x for 4x. */
  x_delay = ((unsigned)frame[0][1] << 2) | (frame[0][2] >> 6);
  y_delay = ((unsigned)frame[0][0] << 3) | ((frame[0][2] >> 3) & 7);
  assert(x_delay * 8 + BOARD_OSD_X_CORRECTION <=
         panel.hstart + (panel.width - width * 4) / 2);
  assert(panel.hstart + (panel.width - width * 4) / 2 -
         (x_delay * 8 + BOARD_OSD_X_CORRECTION) < 8);
  assert(y_delay * 2 <= panel.vstart + (panel.height - height * 4) / 2);
  assert(panel.vstart + (panel.height - height * 4) / 2 - y_delay * 2 < 2);

  printf("OSD %s %u-bpp bitmap, palette, padding, layout and position passed "
         "(%u differing scanline comparisons)\n", name, bpp, differing_pairs);
}

int main(void) {
  check_asset(osd_show_splash, "splash", SPLASH_BITMAP_WIDTH,
              SPLASH_BITMAP_HEIGHT, SPLASH_BITMAP_BPP, SPLASH_PALETTE_COLORS,
              splash_palette, splash_bitmap);
  check_asset(osd_show_no_signal, "no signal", NO_SIGNAL_BITMAP_WIDTH,
              NO_SIGNAL_BITMAP_HEIGHT, NO_SIGNAL_BITMAP_BPP,
              NO_SIGNAL_PALETTE_COLORS, no_signal_palette, no_signal_bitmap);
  check_asset(osd_show_splash, "splash again", SPLASH_BITMAP_WIDTH,
              SPLASH_BITMAP_HEIGHT, SPLASH_BITMAP_BPP, SPLASH_PALETTE_COLORS,
              splash_palette, splash_bitmap);
  osd_hide();
  assert(!(regs[0x6c] & 1) && !(frame[0][2] & 1));
  assert(frame[3][0] == 0 && frame[3][1] == 0 && frame[3][2] == 0);
  puts("OSD asset switching and hide passed");
  return 0;
}
