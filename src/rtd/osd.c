// SPDX-License-Identifier: MIT
#include "rtd/io.h"
#include "rtd/board.h"
#include "rtd/osd.h"
#include "rtd/panel.h"
#include "rtd/ddcci.h"

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
#define OSD_FONT_BASE 0x0140
#define OSD_SRAM 0x1000
#define OSD_ALL_BYTES 0xc000
/* At most 16 columns x 6 rows: maps fit before word 0x140, selectors stay
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
static uint8_t visible, text_loaded;

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
  /* DDC callbacks defer OSD work; a completed word is safe to yield. */
  ddcci_service();
}

static void set_frame(uint8_t enabled) {
  /* Global 2x zoom also doubles both frame delays. Horizontal delay counts
   * groups of four pixels before zoom; vertical delay counts lines.
   * Center in the active raster, including blanking and the board's measured
   * horizontal OSD correction. The OSD origin differs from the video origin.
   */
  uint16_t x = (panel.hstart + (panel.width - active_width) / 2 -
                BOARD_OSD_X_CORRECTION) / 8;
  uint16_t y = (video_display_vstart() + (panel.height - active_height) / 2) / 2;
  write_word(0, (uint8_t)(y >> 3), (uint8_t)(x >> 2),
             (uint8_t)(((x & 3) << 6) | ((y & 7) << 3) | enabled));
}

void osd_hide(void) {
  visible = 0;
  rtd_update(0, 0x6c, 0x01, 0);
  write_word(0, 0, 0, 0);
  /* Manual p383 requires global double width off when OSD is inactive. */
  write_word(3, 0, 0, 0);
}

void osd_service(void) {
  /* CR6C.0 is cleared by automatic background switching (manual p65).
   * Preserve the requested overlay across capture/frame-sync transitions.
   */
  if (visible && !(rtd_read(0, 0x6c) & 1)) {
    rtd_update(0, 0x6c, 1, 1);
  }
}

static void load_bitmap(uint8_t missing_input) {
  const OSD_CODE uint8_t *tiles, *colors;
  uint16_t tile_bytes;
  uint8_t columns, rows, map_mode, map_background, palette_count;
  uint8_t tile, row, color, channel, uploaded = 0;
  uint16_t byte;
  text_loaded = 0;

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

  /* Font-select base 0x010; one-bit font base 0x140 (both word units). */
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
    if (++uploaded == 27) {
      uploaded = 0;
      ddcci_service(); /* Nine complete three-byte words per font plane. */
    }
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
  visible = 1;
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

/* Original five-column, seven-row diagnostic alphabet, doubled inside each
 * 12x18 tile. No vendor firmware font is required. Digits, then A..Z. */
static const OSD_CODE uint8_t text_font[][7] = {
  {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
  {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
  {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
  {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
  {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
  {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
  {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
  {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
  {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
  {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
  {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
  {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
  {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
  {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
  {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
  {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
  {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
  {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};

/* Original 12x18 menu symbols share the one-bit text font. Thin strokes
 * remain two panel pixels wide with global 2x zoom. No bitmap upload is
 * needed when changing pages; text and symbols are cached together.
 */
enum {
  GLYPH_HORIZONTAL = 59, GLYPH_VERTICAL, GLYPH_TOP_LEFT, GLYPH_TOP_RIGHT,
  GLYPH_BOTTOM_LEFT, GLYPH_BOTTOM_RIGHT, GLYPH_JOIN_LEFT, GLYPH_JOIN_RIGHT,
  GLYPH_SELECT, GLYPH_BACK, GLYPH_PICTURE, GLYPH_AUDIO, GLYPH_DISPLAY,
  GLYPH_SETTINGS, GLYPH_POWER, GLYPH_HOME, TEXT_GLYPHS
};

static uint16_t menu_scanline(uint8_t glyph, uint8_t y) {
  static const OSD_CODE uint16_t icons[6][18] = {
    /* Picture: framed mountain and sun. */
    {0,0,0x7fe,0x402,0x462,0x462,0x402,0x442,0x4e2,
     0x5f2,0x7ba,0x712,0x602,0x402,0x7fe,0,0,0},
    /* Audio: speaker cone and two sound waves. */
    {0,0,0,0x040,0x0c4,0x1c2,0x7d2,0x7d1,0x7d1,
     0x7d1,0x7d2,0x1c2,0x0c4,0x040,0,0,0,0},
    /* Display: monitor and pedestal. */
    {0,0,0x7fe,0x402,0x402,0x402,0x402,0x402,0x402,
     0x402,0x7fe,0x060,0x060,0x1f8,0,0,0,0},
    /* Settings: three slider tracks with staggered handles. */
    {0,0,0,0x180,0x7fe,0x180,0,0,0x030,
     0x7fe,0x030,0,0,0x0c0,0x7fe,0x0c0,0,0},
    /* Power: interrupted circle with a vertical switch mark. */
    {0,0,0x060,0x060,0x264,0x462,0x462,0x462,0x402,
     0x402,0x402,0x204,0x108,0x0f0,0,0,0,0},
    /* Home: four menu tiles. */
    {0,0,0x79e,0x492,0x492,0x492,0x79e,0,0,
     0x79e,0x492,0x492,0x492,0x79e,0,0,0,0}
  };
  uint8_t offset;
  if (glyph >= GLYPH_PICTURE && glyph <= GLYPH_HOME)
    return icons[glyph - GLYPH_PICTURE][y];
  switch (glyph) {
  case GLYPH_HORIZONTAL: return y == 8 ? 0xfff : 0;
  case GLYPH_VERTICAL: return 0x040;
  case GLYPH_TOP_LEFT: return y == 8 ? 0x07f : (y > 8 ? 0x040 : 0);
  case GLYPH_TOP_RIGHT: return y == 8 ? 0xfc0 : (y > 8 ? 0x040 : 0);
  case GLYPH_BOTTOM_LEFT: return y == 8 ? 0x07f : (y < 8 ? 0x040 : 0);
  case GLYPH_BOTTOM_RIGHT: return y == 8 ? 0xfc0 : (y < 8 ? 0x040 : 0);
  case GLYPH_JOIN_LEFT: return y == 8 ? 0x07f : 0x040;
  case GLYPH_JOIN_RIGHT: return y == 8 ? 0xfc0 : 0x040;
  case GLYPH_SELECT:
    if (y < 4 || y > 12) return 0;
    offset = y <= 8 ? y - 4 : 12 - y;
    return 0x180 >> offset;
  case GLYPH_BACK:
    if (y == 8) return 0x3fc;
    if (y < 4 || y > 12) return 0;
    offset = y <= 8 ? 8 - y : y - 8;
    return 0x300 >> offset;
  default: return 0;
  }
}

static uint16_t text_scanline(uint8_t character, uint8_t y) {
  uint8_t bits = 0, x;
  uint16_t pixels = 0;
  if (character > 'Z') return menu_scanline(character - ' ', y);
  if (y < 2 || y >= 16) {
    return 0;
  }
  y = (y - 2) / 2;
  if (character >= '0' && character <= '9') {
    bits = text_font[character - '0'][y];
  } else if (character >= 'A' && character <= 'Z') {
    bits = text_font[character - 'A' + 10][y];
  } else if (character == '-' && y == 3) {
    bits = 31;
  } else if (character == '+') {
    bits = y == 3 ? 31 : (y >= 1 && y <= 5 ? 4 : 0);
  } else if (character == '.' && y == 6) {
    bits = 4;
  } else if (character == ':' && (y == 2 || y == 5)) {
    bits = 4;
  } else if (character == '/') {
    bits = 1u << (y * 4 / 6);
  } else if (character == '%') {
    static const OSD_CODE uint8_t percent[7] = {25,26,2,4,8,11,19};
    bits = percent[y];
  }
  for (x = 0; x < 5; ++x) {
    if (bits & (16u >> x)) {
      pixels |= 3u << (9 - x * 2);
    }
  }
  return pixels;
}

#define TEXT_COLUMNS 30
#define TEXT_ROWS 5
#define MENU_ROWS 7
#define LIVE_MENU_ROWS 10
static uint8_t text_row, text_column, text_color;

static void text_glyph(uint8_t glyph) {
  if (text_column < TEXT_COLUMNS) {
    write_word(OSD_SRAM | (OSD_MAP_BASE + text_row * TEXT_COLUMNS +
                           text_column++), 0x8c, glyph, text_color);
  }
}

static void text_put(char character) {
  if (character < ' ' || character > 'Z') character = ' ';
  text_glyph(character - ' ');
}

static void text_literal(const char *text) {
  while (*text) {
    text_put(*text++);
  }
}

static void text_number(uint32_t number) {
  uint32_t place = 1000000000UL;
  uint8_t started = 0, digit;
  while (place) {
    digit = (uint8_t)(number / place);
    if (digit || started || place == 1) {
      text_put('0' + digit);
      started = 1;
    }
    number %= place;
    place /= 10;
  }
}

static void text_next_row(void) {
  while (text_column < TEXT_COLUMNS) {
    text_put(' ');
  }
  ++text_row;
  text_column = 0;
}

static void text_geometry(const video_signal_t *signal) {
  uint32_t whole;
  uint8_t fraction;
  if (signal->measured & VIDEO_MEASURE_GEOMETRY) {
    text_number(signal->input_width);
    text_put('X');
    text_number(signal->input_height);
  } else {
    text_literal("--X--");
  }
  text_put(' ');
  if ((signal->measured & VIDEO_MEASURE_TIMING) && signal->vtotal &&
      signal->line_hz) {
    /* Keep the integer and fraction separate: even a rejected one-count
     * period must not overflow while we explain its measured settings. */
    whole = signal->line_hz / signal->vtotal;
    fraction = (uint8_t)(((signal->line_hz % signal->vtotal) * 10 +
                          signal->vtotal / 2) / signal->vtotal);
    if (fraction == 10) {
      ++whole;
      fraction = 0;
    }
    text_number(whole);
    text_put('.');
    text_number(fraction);
  } else {
    text_literal("--");
  }
  text_literal("HZ");
  text_next_row();
}

static void text_begin(uint8_t rows) {
  uint8_t row, character, y;
  uint16_t top, bottom;
  osd_hide();
  write_word(5, 0, 0, 0);
  write_word(8, 0, 0, 0);
  rtd_update(0, 0x6c, 0x1f, 0);
  write_word(4, OSD_MAP_BASE & 0xff,
             ((OSD_MAP_BASE >> 4) & 0xf0) | (OSD_FONT_BASE & 0x0f),
             OSD_FONT_BASE >> 4);
  for (row = 0; row < rows; ++row) {
    write_word(OSD_SRAM | row, 0x80, 17u << 3, TEXT_COLUMNS);
  }
  write_word(OSD_SRAM | rows, 0, 0, 0);
  if (!text_loaded) {
    select_word(OSD_ALL_BYTES | OSD_SRAM | OSD_FONT_BASE);
    for (character = ' '; character < ' ' + TEXT_GLYPHS; ++character) {
      for (y = 0; y < 18; y += 2) {
        top = text_scanline(character, y);
        bottom = text_scanline(character, y + 1);
        rtd_write(0, 0x92, (uint8_t)bottom);
        rtd_write(0, 0x92, (uint8_t)((top << 4) | (bottom >> 8)));
        rtd_write(0, 0x92, (uint8_t)(top >> 4));
      }
      ddcci_service(); /* The complete 27-byte glyph is now in SRAM. */
    }
    text_loaded = 1;
  }
  /* Index zero remains transparent; index two is opaque black. */
  rtd_write(0, 0x6e, 0x80);
  for (character = 0; character < 9; ++character) {
    rtd_write(0, 0x6f, character >= 3 && character < 6 ? 255 : 0);
  }
  rtd_write(0, 0x6e, 0);
  text_row = text_column = 0;
  text_color = 0x12;
}

void osd_show_input(const video_signal_t *signal) {
  uint16_t x = (panel.hstart + 16 - BOARD_OSD_X_CORRECTION) / 8;
  uint16_t position_y = (video_display_vstart() + 16) / 2;
  if (!signal) {
    return;
  }
  text_begin(TEXT_ROWS);
  if (signal->error) {
    text_literal("UNSUPPORTED INPUT");
    text_next_row();
  } else {
    text_literal("HDMI ");
  }
  text_geometry(signal);
  text_literal("H ");
  if ((signal->measured & VIDEO_MEASURE_TIMING) && signal->line_hz) {
    text_number(signal->line_hz / 1000);
    text_put('.');
    text_put('0' + (signal->line_hz / 100) % 10);
    text_put('0' + (signal->line_hz / 10) % 10);
    text_literal("KHZ H");
    text_put(signal->polarity & 1 ? '+' : '-');
    text_literal(" V");
    text_put(signal->polarity & 2 ? '+' : '-');
  } else {
    text_literal("--KHZ H-- V--");
  }
  text_next_row();
  text_literal("TOTAL ");
  if (signal->measured & VIDEO_MEASURE_GEOMETRY) {
    text_number(signal->htotal);
  } else {
    text_literal("--");
  }
  text_put('X');
  if (signal->measured & VIDEO_MEASURE_TIMING) {
    text_number(signal->vtotal);
  } else {
    text_literal("--");
  }
  text_next_row();
  switch (signal->error) {
    case VIDEO_GEOMETRY: text_literal("EXPECT 800/640X480"); break;
    case VIDEO_DIGITAL_TOTAL: text_literal("EXPECT HT 800/992/1000"); break;
    case VIDEO_POLARITY:
      text_literal(signal->htotal == 992 ? "EXPECT H- V+" : "EXPECT H- V-");
      break;
    case VIDEO_VERTICAL_TOTAL:
      text_literal(signal->htotal == 992 ? "EXPECT VT 499/500" : "EXPECT VT 524/525");
      break;
    case VIDEO_LINE_RATE:
      text_literal(signal->htotal == 992 ? "EXPECT H 29.50-30.00KHZ" :
                                          "EXPECT H 31.30-31.70KHZ");
      break;
    case VIDEO_ZERO_PERIOD: text_literal("ZERO SYNC PERIOD"); break;
    case VIDEO_ANALOG_TIMEOUT: text_literal("SYNC MEASUREMENT TIMEOUT"); break;
    case VIDEO_ANALOG_OVERFLOW: text_literal("SYNC COUNTER OVERFLOW"); break;
    case VIDEO_DIGITAL_OVERFLOW: text_literal("PIXEL COUNTER OVERFLOW"); break;
    case VIDEO_DIGITAL_TIMEOUT: text_literal("PIXEL MEASUREMENT TIMEOUT"); break;
  }
  while (text_row < TEXT_ROWS) {
    text_next_row();
  }
  write_word(3, 0, 3, 0);
  write_word(0, (uint8_t)(position_y >> 3), (uint8_t)(x >> 2),
             (uint8_t)(((x & 3) << 6) | ((position_y & 7) << 3) | 1));
  visible = 1;
  rtd_update(0, 0x6c, 1, 1);
}

static void preview_label(const char *label, uint8_t selected) {
  text_color = selected ? 0x13 : 0x12;
  text_put(' ');
  text_literal(label);
  while (text_column < 22) text_put(' ');
}

static void preview_number(const char *label, uint8_t number, const char *unit,
                           uint8_t selected) {
  preview_label(label, selected);
  text_number(number);
  text_literal(unit);
  text_next_row();
}

static void preview_choice(const char *label, const char *value,
                           uint8_t selected, uint8_t unavailable) {
  preview_label(label, selected);
  if (unavailable) text_color = 0x52;
  text_literal(value);
  text_next_row();
}

static void preview_slider(uint8_t value) {
  uint8_t cell;
  text_color = 0x12;
  text_literal("  ");
  for (cell = 0; cell < 20; ++cell) {
    text_color = cell < value / 5 ? 0x14 : 0x15;
    text_put(' '); /* Opaque background cells form the filled/empty track. */
  }
  text_color = 0x12;
  text_next_row();
}

void osd_show_menu_preview(uint8_t page, uint8_t variant) {
  uint8_t value;
  if (page >= OSD_PREVIEW_COUNT) page = OSD_PREVIEW_MAIN;
  if (variant > 2) variant = 2;
  value = variant * 50;
  text_begin(MENU_ROWS);
  /* Palette 3: selection blue; 4: green title/slider; 5: empty/disabled gray. */
  rtd_write(0, 0x6e, 0x89);
  rtd_write(0, 0x6f, 16);
  rtd_write(0, 0x6f, 64);
  rtd_write(0, 0x6f, 160);
  rtd_write(0, 0x6f, 0);
  rtd_write(0, 0x6f, 220);
  rtd_write(0, 0x6f, 120);
  rtd_write(0, 0x6f, 96);
  rtd_write(0, 0x6f, 96);
  rtd_write(0, 0x6f, 96);
  rtd_write(0, 0x6e, 0);
  text_color = 0x42;
  switch (page) {
  case OSD_PREVIEW_PICTURE: text_literal(" PICTURE / PREVIEW"); break;
  case OSD_PREVIEW_AUDIO: text_literal(" AUDIO / PREVIEW"); break;
  case OSD_PREVIEW_DISPLAY: text_literal(" DISPLAY / PREVIEW"); break;
  case OSD_PREVIEW_MENU: text_literal(" MENU SETTINGS / PREVIEW"); break;
  default: text_literal(" ADAFRUIT MENU PREVIEW"); break;
  }
  text_next_row();
  switch (page) {
  case OSD_PREVIEW_PICTURE:
    preview_number("IMAGE BRIGHTNESS", value, "%", variant == 0);
    preview_slider(value);
    preview_number("CONTRAST", 100 - value, "%", variant == 1);
    preview_slider(100 - value);
    preview_choice("COLOR TEMPERATURE", "6500K", 0, 0);
    break;
  case OSD_PREVIEW_AUDIO:
    preview_number("VOLUME", value, "%", variant == 0);
    preview_slider(value);
    preview_choice("MUTE", variant == 1 ? "ON" : "OFF", variant == 1, 0);
    preview_choice("FORMAT", "48KHZ", 0, 0);
    preview_choice("CHANNELS", "STEREO", 0, 0);
    break;
  case OSD_PREVIEW_DISPLAY:
    preview_number("LED BACKLIGHT", value, "%", variant == 0);
    preview_slider(value);
    preview_choice("ASPECT", variant == 1 ? "FILL" : "KEEP", variant == 1, 0);
    preview_choice("ROTATION", "--", 0, 1);
    preview_choice("MIRROR", "--", 0, 1);
    break;
  case OSD_PREVIEW_MENU:
    preview_number("TIMEOUT", 5 + variant * 5, "S", variant == 0);
    preview_choice("POSITION", "CENTER", 0, 0);
    preview_number("OPACITY", value, "%", variant == 1);
    preview_slider(value);
    preview_choice("LANGUAGE", "ENGLISH", 0, 0);
    break;
  default:
    preview_choice("PICTURE", "", variant == 0, 0);
    preview_choice("AUDIO", "", variant == 1, 0);
    preview_choice("DISPLAY", "", 0, 0);
    preview_choice("MENU SETTINGS", "", variant == 2, 0);
    text_color = 0x12;
    text_next_row();
    break;
  }
  if (page == OSD_PREVIEW_MAIN) {
    text_color = 0x52;
    text_literal(" SAMPLE VALUES - NO CHANGES");
    text_next_row();
  } else {
    preview_choice("BACK", "", variant == 2, 0);
  }
  active_width = TEXT_COLUMNS * 24u;
  active_height = MENU_ROWS * 36u;
  show_bitmap();
}

static void menu_edge(void) {
  text_color = 0x62;
  text_glyph(GLYPH_VERTICAL);
}

static void menu_finish_row(void) {
  while (text_column < TEXT_COLUMNS - 1) text_put(' ');
  menu_edge();
  text_next_row();
}

static void menu_rule(uint8_t left, uint8_t right) {
  text_color = 0x62;
  text_glyph(left);
  while (text_column < TEXT_COLUMNS - 1) text_glyph(GLYPH_HORIZONTAL);
  text_glyph(right);
  text_next_row();
}

static uint8_t menu_icon(const char *title) {
  if (title[0] == 'P' || title[0] == 'N') return GLYPH_PICTURE;
  if (title[0] == 'A' && title[1] == 'U') return GLYPH_AUDIO;
  if (title[0] == 'D') return GLYPH_DISPLAY;
  if (title[0] == 'M') return GLYPH_SETTINGS;
  return GLYPH_HOME;
}

void osd_menu_begin(const char *title) {
  static const OSD_CODE uint8_t colors[][3] = {
    {12,20,36}, {24,84,148}, {72,216,192},
    {132,148,168}, {52,76,104}, {20,36,56}
  };
  uint8_t color, channel;
  text_begin(LIVE_MENU_ROWS);
  rtd_write(0, 0x6e, 0x86);
  for (color = 0; color < 6; ++color)
    for (channel = 0; channel < 3; ++channel)
      rtd_write(0, 0x6f, colors[color][channel]);
  rtd_write(0, 0x6e, 0);
  menu_rule(GLYPH_TOP_LEFT, GLYPH_TOP_RIGHT);
  menu_edge();
  text_color = 0x47;
  text_glyph(menu_icon(title));
  text_put(' ');
  text_color = 0x17;
  while (*title && text_column < TEXT_COLUMNS - 1) text_put(*title++);
  menu_finish_row();
  menu_rule(GLYPH_JOIN_LEFT, GLYPH_JOIN_RIGHT);
}

void osd_menu_row(const char *label, const char *choice, uint8_t value,
                  uint8_t percent, uint8_t selected, uint8_t available) {
  uint8_t length = 0, start, background = selected ? 3 : 2;
  const char *cursor;
  if (text_row >= LIVE_MENU_ROWS - 2) return;
  if (!available) choice = "--";
  if (choice) {
    for (cursor = choice; *cursor && length < 7; ++cursor) ++length;
  } else {
    length = (value >= 100 ? 3 : value >= 10 ? 2 : 1) + (percent != 0);
  }
  start = TEXT_COLUMNS - 1 - length;
  menu_edge();
  text_color = 0x10 | background;
  if (selected) text_glyph(GLYPH_SELECT);
  else text_put(' ');
  text_put(' ');
  text_color = (available ? 0x10 : 0x50) | background;
  while (*label && text_column < 22) text_put(*label++);
  while (text_column < start) text_put(' ');
  if (choice) {
    while (*choice && text_column < TEXT_COLUMNS - 1) text_put(*choice++);
  } else {
    text_number(value);
    if (percent) text_put('%');
  }
  menu_finish_row();
}

void osd_menu_end(const char *footer) {
  while (text_row < LIVE_MENU_ROWS - 2) {
    menu_edge();
    text_color = 0x12;
    menu_finish_row();
  }
  menu_edge();
  text_color = footer[0] == 'A' ? 0x42 : 0x52;
  text_put(' ');
  while (*footer && text_column < TEXT_COLUMNS - 1) text_put(*footer++);
  menu_finish_row();
  menu_rule(GLYPH_BOTTOM_LEFT, GLYPH_BOTTOM_RIGHT);
  active_width = TEXT_COLUMNS * 24u;
  active_height = LIVE_MENU_ROWS * 36u;
  show_bitmap();
}
