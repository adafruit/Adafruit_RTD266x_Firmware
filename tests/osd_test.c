// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rtd/io.h"
#include "rtd/board.h"
#include "rtd/osd.h"
#include "rtd/panel.h"
#include "rtd/ddcci.h"

#define OSD_CODE
#include "splash_bitmap.h"
#include "no_signal_bitmap.h"

/* Model the documented three byte lanes and auto-incrementing word port,
 * independently of the driver's bitmap packing code. */
static uint8_t regs[256], frame[16][3], sram[4096][3];
static uint8_t written[4096][3], palette[48];
static uint16_t address;
static unsigned lane, palette_bytes, palette_index;
static uint16_t runtime_vstart = 32;
static uint8_t font_snapshot[59 * 9][3];
static unsigned font_cached, font_seen;
static unsigned writes_since_poll, ddcci_polls, upload_polls;

uint16_t video_display_vstart(void) {
  return runtime_vstart;
}

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
    assert(++writes_since_poll <= 27); /* Bounded service during uploads. */
    assert((address & 0xc000) == 0xc000); /* All three byte lanes. */
    assert(!(regs[0x93] & 0x08));       /* Lower SRAM bank. */
    word = address & 0x0fff;
    if (address & 0x1000) {
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
  } else if (reg == 0x6e) {
    palette_index = value & 0x3f;
  } else if (reg == 0x6f) {
    assert((regs[0x6e] & 0x80) && palette_index < sizeof(palette));
    palette[palette_index++] = value;
    ++palette_bytes;
  }
}

void rtd_update(uint8_t page, uint8_t reg, uint8_t mask, uint8_t value) {
  rtd_write(page, reg, (rtd_read(page, reg) & (uint8_t)~mask) | (value & mask));
}

void ddcci_service(void) {
  uint16_t saved_address = address;
  assert(lane == 0); /* Never yield between lanes of an OSD word. */
  assert(writes_since_poll == 3 || writes_since_poll == 27);
  if (writes_since_poll == 27) ++upload_polls;
  writes_since_poll = 0;
  ++ddcci_polls;
  /* Live commands may touch unrelated scaler registers, but must defer OSD. */
  rtd_update(0, 0x62, 3, rtd_read(0, 0x62) & 3);
  assert(address == saved_address && lane == 0);
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
  unsigned polls_before = ddcci_polls, uploads_before = upload_polls;

  assert(bpp == 1 || bpp == 4);
  assert(colors <= 16);
  /* Preserve SRAM, palette and frame state between shows. Only reset the
   * transaction tracking, so missing writes cannot masquerade as clean RAM. */
  memset(written, 0, sizeof(written));
  palette_bytes = 0;
  show();
  assert(!writes_since_poll && ddcci_polls > polls_before);
  assert(upload_polls - uploads_before == rows * columns * bpp);
  font_cached = 0; /* Bitmap tiles replace text-font SRAM. */
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
  assert(y_delay * 2 <= runtime_vstart + (panel.height - height * 4) / 2);
  assert(runtime_vstart + (panel.height - height * 4) / 2 - y_delay * 2 < 2);

  printf("OSD %s %u-bpp bitmap, palette, padding, layout and position passed "
         "(%u differing scanline comparisons)\n", name, bpp, differing_pairs);
}

static unsigned glyph_pixel(unsigned glyph, unsigned x, unsigned y) {
  unsigned word = 0x100 + glyph * 9 + y / 2;
  uint32_t pixels = (uint32_t)sram[word][0] |
                    ((uint32_t)sram[word][1] << 8) |
                    ((uint32_t)sram[word][2] << 16);
  return (pixels >> ((y % 2 ? 11 : 23) - x)) & 1;
}

static void check_text_writes(unsigned rows) {
  unsigned word, byte;
  assert(!writes_since_poll);
  assert(rows + 1 <= 0x10 && 0x10 + rows * 30 <= 0x100);
  assert(0x100 + 59 * 9 <= 4096);
  for (word = 0; word < 4096; ++word) {
    unsigned used = word <= rows ||
        (word >= 0x10 && word < 0x10 + rows * 30) ||
        (!font_cached && word >= 0x100 && word < 0x100 + 59 * 9);
    for (byte = 0; byte < 3; ++byte) assert(written[word][byte] == used);
  }
  /* Both cached glyphs and uploads after bitmap use must exactly match the
   * original font, whose pixels are independently decoded by check_input. */
  if (font_seen) assert(!memcmp(font_snapshot, &sram[0x100], sizeof font_snapshot));
  else {
    memcpy(font_snapshot, &sram[0x100], sizeof font_snapshot);
    font_seen = 1;
  }
  font_cached = 1;
}

static void check_input(const video_signal_t *signal,
                        const char *const expected[5], char text[5][31]) {
  unsigned row, column, glyph, x, y, ink, x_delay, y_delay;
  memset(written, 0, sizeof(written));
  palette_bytes = 0;
  osd_show_input(signal);
  assert((regs[0x6c] & 1) && (frame[0][2] & 1));
  assert(frame[3][1] == 3);
  assert(frame[4][0] == 0x10 && frame[4][1] == 0 && frame[4][2] == 0x10);
  assert(palette_bytes >= 9 && !(regs[0x6e] & 0x80));
  for (column = 0; column < 3; ++column) {
    assert(palette[3 + column] == 255); /* White on opaque black. */
    assert(palette[6 + column] == 0);
  }
  for (row = 0; row < 5; ++row) {
    assert(sram[row][0] == 0x80 && sram[row][1] == 0x88 && sram[row][2] == 30);
    for (column = 0; column < 30; ++column) {
      unsigned entry = 0x10 + row * 30 + column;
      assert(sram[entry][0] == 0x8c && sram[entry][2] == 0x12);
      assert(sram[entry][1] < 59);
      text[row][column] = (char)(sram[entry][1] + 32);
      if (expected && expected[row]) {
        char wanted = column < strlen(expected[row]) ? expected[row][column] : ' ';
        assert(text[row][column] == wanted);
      }
    }
    text[row][30] = '\0';
  }
  assert(sram[5][0] == 0 && sram[5][1] == 0 && sram[5][2] == 0);
  check_text_writes(5);
  /* Font geometry is independently decoded from SRAM: blank space, one
   * pixel side margins, two line top/bottom margins and doubled 5x7 pixels. */
  for (glyph = 0; glyph < 59; ++glyph) {
    ink = 0;
    for (y = 0; y < 18; ++y) {
      for (x = 0; x < 12; ++x) {
        unsigned pixel = glyph_pixel(glyph, x, y);
        ink += pixel;
        if (x == 0 || x == 11 || y < 2 || y >= 16) {
          assert(pixel == 0);
        } else {
          assert(pixel == glyph_pixel(glyph, 1 + ((x - 1) / 2) * 2,
                                     2 + ((y - 2) / 2) * 2));
        }
      }
    }
    if (glyph == 0) {
      assert(ink == 0);
    } else if (glyph == 'A' - 32 || glyph == '%' - 32 || (glyph >= '0' - 32 && glyph <= '9' - 32)) {
      assert(ink > 0);
    }
  }
  x_delay = ((unsigned)frame[0][1] << 2) | (frame[0][2] >> 6);
  y_delay = ((unsigned)frame[0][0] << 3) | ((frame[0][2] >> 3) & 7);
  assert(x_delay == (panel.hstart + 16u - BOARD_OSD_X_CORRECTION) / 8u);
  assert(y_delay == (runtime_vstart + 16u) / 2u);
}

static void check_input_messages(void) {
  video_signal_t signal = {0};
  char text[5][31];
  const char *valid[5] = {
    "HDMI 800X480 60.1HZ", "H 31.50KHZ H- V-", "TOTAL 1000X524", "", ""
  };
  const char *extreme[5] = {
    "HDMI 800X480 432000000.0HZ", "H 432000.00KHZ H- V-", "TOTAL 1000X1", "", ""
  };
  const char *rounded[5] = {
    "HDMI 800X480 60.0HZ", "H 31.49KHZ H- V-", "TOTAL 1000X525", "", ""
  };
  const char *geometry[5] = {
    "UNSUPPORTED INPUT", "1024X480 60.1HZ", "H 31.50KHZ H+ V+",
    "TOTAL 1000X524", "EXPECT 800/640X480"
  };
  const char *timing[5] = {
    "UNSUPPORTED INPUT", "800X480 60.1HZ", "H 31.50KHZ H+ V+",
    "TOTAL 999X524", "EXPECT HT 800/992/1000"
  };
  const uint8_t cvt_errors[] = {VIDEO_POLARITY, VIDEO_VERTICAL_TOTAL, VIDEO_LINE_RATE};
  const char *reasons[][2] = {
    {"EXPECT H- V-", "EXPECT H- V+"},
    {"EXPECT VT 524/525", "EXPECT VT 499/500"},
    {"EXPECT H 31.30-31.70KHZ", "EXPECT H 29.50-30.00KHZ"}
  };
  unsigned row, mode;
  signal.width = signal.input_width = 800;
  signal.height = signal.input_height = 480;
  signal.htotal = 1000;
  signal.vtotal = 524;
  signal.line_hz = 31500;
  signal.measured = VIDEO_MEASURE_GEOMETRY | VIDEO_MEASURE_TIMING;
  check_input(&signal, valid, text);
  runtime_vstart = 10;
  check_input(&signal, valid, text); /* Runtime CVT origin, not panel default32. */
  signal.line_hz = 432000000UL;
  signal.vtotal = 1;
  check_input(&signal, extreme, text); /* Multiplication by ten would overflow. */
  signal.line_hz = 31499;
  signal.vtotal = 525;
  check_input(&signal, rounded, text); /* 59.998 Hz carries into 60.0 Hz. */
  signal.line_hz = 31500;
  signal.vtotal = 524;
  signal.error = VIDEO_GEOMETRY;
  signal.input_width = 1024;
  signal.polarity = 3;
  check_input(&signal, geometry, text);
  signal.error = VIDEO_DIGITAL_TOTAL;
  signal.input_width = 800;
  signal.htotal = 999;
  check_input(&signal, timing, text);
  for (mode = 0; mode < 2; ++mode) {
    signal.htotal = mode ? 992 : 1000;
    for (row = 0; row < 3; ++row) {
      const char *expected[5] = {NULL, NULL, NULL, NULL, reasons[row][mode]};
      signal.error = cvt_errors[row];
      check_input(&signal, expected, text);
    }
  }
  signal.htotal = 999;

  /* Digital geometry can be fresh even when analog timing timed out. */
  signal.error = VIDEO_ANALOG_TIMEOUT;
  signal.measured = VIDEO_MEASURE_GEOMETRY;
  check_input(&signal, NULL, text);
  assert(strstr(text[1], "800X480") == text[1]);
  assert(strstr(text[1], "--") != NULL);
  for (row = 1; row < 4; ++row) {
    assert(!strstr(text[row], "60.1") && !strstr(text[row], "31.50"));
    assert(!strstr(text[row], "524"));
    assert(!strstr(text[row], "H+") && !strstr(text[row], "V+"));
  }

  /* Leave old numeric data populated, but mark it unmeasured. The new
   * screen must not show previous dimensions, rates, totals or polarities. */
  signal.error = VIDEO_DIGITAL_TIMEOUT;
  signal.measured = 0;
  check_input(&signal, NULL, text);
  assert(strstr(text[0], "UNSUPPORTED INPUT") == text[0]);
  assert(strstr(text[1], "--") != NULL);
  for (row = 1; row < 4; ++row) {
    assert(!strstr(text[row], "800") && !strstr(text[row], "480"));
    assert(!strstr(text[row], "999") && !strstr(text[row], "524"));
    assert(!strstr(text[row], "60.1") && !strstr(text[row], "31.50"));
    assert(!strstr(text[row], "H+") && !strstr(text[row], "V+"));
  }
  check_input(&signal, NULL, text); /* Repeat a status without leaking SRAM. */
  runtime_vstart = 32;
  puts("OSD input text, font, palette, rejection and stale-measurement checks passed");
}

static void check_menu_preview(void) {
  const char *titles[] = {" ADAFRUIT MENU PREVIEW", " PICTURE / PREVIEW",
      " AUDIO / PREVIEW", " DISPLAY / PREVIEW", " MENU SETTINGS / PREVIEW"};
  const char *first[] = {"PICTURE", "IMAGE BRIGHTNESS", "VOLUME", "LED BACKLIGHT", "TIMEOUT"};
  unsigned page, variant, row, column, x_delay, y_delay;
  char text[7][31];
  for (page = 0; page < OSD_PREVIEW_COUNT; ++page) {
    for (variant = 0; variant < 3; ++variant) {
      memset(written, 0, sizeof written);
      palette_bytes = 0;
      osd_show_menu_preview(page, variant);
      check_text_writes(7);
      assert((regs[0x6c] & 1) && (frame[0][2] & 1));
      assert(palette_bytes == 18 && frame[3][1] == 3);
      assert(palette[9] == 16 && palette[10] == 64 && palette[11] == 160);
      assert(palette[12] == 0 && palette[13] == 220 && palette[14] == 120);
      assert(palette[15] == 96 && palette[16] == 96 && palette[17] == 96);
      assert(0x10 + 7 * 30 <= 0x100); /* Map cannot overwrite glyphs. */
      for (row = 0; row < 7; ++row) {
        assert(sram[row][0] == 0x80 && sram[row][1] == 0x88);
        assert(sram[row][2] == 30);
        for (column = 0; column < 30; ++column) {
          unsigned entry = 0x10 + row * 30 + column;
          assert(sram[entry][0] == 0x8c && sram[entry][1] < 59);
          text[row][column] = (char)(sram[entry][1] + 32);
        }
        text[row][30] = '\0';
      }
      assert(sram[7][0] == 0 && sram[7][1] == 0 && sram[7][2] == 0);
      assert(strncmp(text[0], titles[page], strlen(titles[page])) == 0);
      assert(strncmp(text[1] + 1, first[page], strlen(first[page])) == 0);
      assert(sram[0x10][2] == 0x42);
      if (page == OSD_PREVIEW_MAIN) {
        assert(strstr(text[6], "SAMPLE VALUES - NO CHANGES"));
        row = variant == 2 ? 4 : variant + 1;
      } else {
        assert(strncmp(text[6], " BACK", 5) == 0);
        row = variant == 2 ? 6 : variant == 1 ? 3 : 1;
        if (page == OSD_PREVIEW_MENU) {
          assert(strstr(text[1], variant == 0 ? "5S" : variant == 1 ? "10S" : "15S"));
          assert(strstr(text[2], "CENTER") && strstr(text[5], "ENGLISH"));
        } else {
          assert(strstr(text[1], variant == 0 ? "0%" : variant == 1 ? "50%" : "100%"));
        }
      }
      assert(sram[0x10 + row * 30][2] == 0x13);
      for (column = 1; column < 7; ++column) {
        unsigned color = column == row ? 0x13 : 0x12;
        if (page == OSD_PREVIEW_MAIN && column == 6) color = 0x52;
        assert(sram[0x10 + column * 30][2] == color);
      }
      /* Decode slider tracks independently: exactly 0, 10 or 20 filled cells. */
      if (page != OSD_PREVIEW_MAIN) {
        unsigned track_row = page == OSD_PREVIEW_MENU ? 4 : 2;
        unsigned filled = 0;
        for (column = 2; column < 22; ++column) {
          unsigned entry = 0x10 + track_row * 30 + column;
          assert(sram[entry][1] == 0); /* Blank glyph with opaque cell color. */
          assert(sram[entry][2] == 0x14 || sram[entry][2] == 0x15);
          filled += sram[entry][2] == 0x14;
        }
        assert(filled == variant * 10);
      }
      if (page == OSD_PREVIEW_AUDIO) {
        assert(strstr(text[3], variant == 1 ? "ON" : "OFF"));
        assert(strstr(text[4], "48KHZ") && strstr(text[5], "STEREO"));
      }
      if (page == OSD_PREVIEW_DISPLAY) {
        assert(strstr(text[3], variant == 1 ? "FILL" : "KEEP"));
        assert(strstr(text[4], "--") && strstr(text[5], "--"));
        assert(sram[0x10 + 4 * 30 + 22][2] == 0x52);
        assert(sram[0x10 + 5 * 30 + 22][2] == 0x52);
      }
      x_delay = ((unsigned)frame[0][1] << 2) | (frame[0][2] >> 6);
      y_delay = ((unsigned)frame[0][0] << 3) | ((frame[0][2] >> 3) & 7);
      assert(x_delay * 8 + BOARD_OSD_X_CORRECTION - panel.hstart == 40);
      assert(y_delay * 2 - runtime_vstart == 114);
    }
  }
  puts("OSD all submenu pages, slider limits, selections and centering passed");
}

static void check_live_menu(void) {
  unsigned pass, row, column, x_delay, y_delay;
  char text[7][31];
  for (pass = 0; pass < 2; ++pass) {
    memset(written, 0, sizeof written);
    palette_bytes = 0;
    osd_menu_begin("LIVE MENU");
    osd_menu_row("CONTRAST", NULL, pass ? 5 : 75, 1, !pass, 1);
    osd_menu_row("ROTATION", NULL, 0, 0, 0, 0);
    if (!pass) {
      osd_menu_row("ASPECT", "KEEP", 0, 0, 0, 1);
      osd_menu_row("MUTE", "OFF", 0, 0, 0, 1);
      osd_menu_row("BACK", "", 0, 0, 0, 1);
      osd_menu_row("MUST NOT FIT", "", 0, 0, 0, 1);
    }
    osd_menu_end(pass ? "ADJUST +/-" : "MENU SELECT");
    check_text_writes(7);
    assert((regs[0x6c] & 1) && (frame[0][2] & 1));
    assert(palette_bytes == 18 && frame[3][1] == 3);
    assert(palette[9] == 16 && palette[10] == 64 && palette[11] == 160);
    for (row = 0; row < 7; ++row) {
      assert(sram[row][0] == 0x80 && sram[row][1] == 0x88 && sram[row][2] == 30);
      for (column = 0; column < 30; ++column) {
        unsigned entry = 0x10 + row * 30 + column;
        assert(sram[entry][0] == 0x8c && sram[entry][1] < 59);
        text[row][column] = (char)(sram[entry][1] + 32);
      }
      text[row][30] = 0;
    }
    assert(sram[7][0] == 0 && sram[7][1] == 0 && sram[7][2] == 0);
    assert(!strncmp(text[0], " LIVE MENU", 10));
    assert(!strncmp(text[1], " CONTRAST", 9));
    assert(!strncmp(text[1] + 22, pass ? "5% " : "75%", 3));
    assert(sram[0x10 + 30][2] == (pass ? 0x12 : 0x13));
    assert(sram[0x10 + 30 + 22][2] == (pass ? 0x12 : 0x13));
    assert(!strncmp(text[2], " ROTATION", 9));
    assert(!strncmp(text[2] + 22, "--", 2));
    assert(sram[0x10 + 60 + 22][2] == 0x52);
    assert(sram[0x10 + 180][2] == 0x52);
    assert(strstr(text[6], pass ? "ADJUST +/-" : "MENU SELECT"));
    if (pass) {
      for (row = 3; row < 6; ++row)
        for (column = 0; column < 30; ++column) assert(text[row][column] == ' ');
    } else {
      assert(!strncmp(text[5], " BACK", 5));
      assert(!strstr(text[6], "MUST NOT FIT"));
    }
    x_delay = ((unsigned)frame[0][1] << 2) | (frame[0][2] >> 6);
    y_delay = ((unsigned)frame[0][0] << 3) | ((frame[0][2] >> 3) & 7);
    assert(x_delay * 8 + BOARD_OSD_X_CORRECTION - panel.hstart == 40);
    assert(y_delay * 2 - runtime_vstart == 114);
  }
  puts("OSD live menu selections, disabled rows, stale cells and font cache passed");
}

int main(void) {
  check_asset(osd_show_splash, "splash", SPLASH_BITMAP_WIDTH,
              SPLASH_BITMAP_HEIGHT, SPLASH_BITMAP_BPP, SPLASH_PALETTE_COLORS,
              splash_palette, splash_bitmap);
  check_asset(osd_show_no_signal, "no signal", NO_SIGNAL_BITMAP_WIDTH,
              NO_SIGNAL_BITMAP_HEIGHT, NO_SIGNAL_BITMAP_BPP,
              NO_SIGNAL_PALETTE_COLORS, no_signal_palette, no_signal_bitmap);
  check_menu_preview();
  check_input_messages();
  check_live_menu();
  regs[0x6c] = 0x20; /* Hardware background transition cleared the port. */
  osd_service();
  assert(regs[0x6c] == 0x21);
  check_asset(osd_show_splash, "splash again", SPLASH_BITMAP_WIDTH,
              SPLASH_BITMAP_HEIGHT, SPLASH_BITMAP_BPP, SPLASH_PALETTE_COLORS,
              splash_palette, splash_bitmap);
  check_live_menu(); /* Bitmap invalidation forces exactly one new font upload. */
  osd_hide();
  osd_service(); /* An expired overlay must not be resurrected. */
  assert(!(regs[0x6c] & 1) && !(frame[0][2] & 1));
  assert(frame[3][0] == 0 && frame[3][1] == 0 && frame[3][2] == 0);
  puts("OSD asset switching and hide passed");
  return 0;
}
