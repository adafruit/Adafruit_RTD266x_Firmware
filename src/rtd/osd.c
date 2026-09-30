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
#define MENU_FONT_CODE OSD_CODE
#include "rtd/menu_font.h"
#include "rtd/menu_icons.h"

/* Realtek RTD2660 register manual, pp. 64-65, 81, 358-359, 383-398.
 * All accesses below are to the common scaler page. The OSD address selects
 * a three-byte word, unlike the scaler's byte-addressed register space.
 */
#define OSD_MAP_BASE 0x0010
#define OSD_FONT_BASE 0x0180
#define OSD_SRAM 0x1000
#define OSD_ALL_BYTES 0xc000
/* At most 16 columns x 6 rows: maps fit before word 0x180, selectors stay
 * below 128, and even 4-bpp fonts fit in SRAM words 0x000..0xEFF. */
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
  /* This layout stays in the documented SRAM words 0x000..0xEFF. */
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

static void set_frame(uint8_t doubled) {
  /* Global zoom also scales both frame delays. Horizontal delay counts
   * groups of four pixels before zoom; vertical delay counts lines.
   * Center in the active raster, including blanking and the board's measured
   * horizontal OSD correction. The OSD origin differs from the video origin.
   */
  uint16_t x = (panel.hstart + (panel.width - active_width) / 2 -
                BOARD_OSD_X_CORRECTION) / 4;
  uint16_t y = video_display_vstart() + (panel.height - active_height) / 2;
  if (doubled) {
    x /= 2;
    y /= 2;
  }
  write_word(0, (uint8_t)(y >> 3), (uint8_t)(x >> 2),
             (uint8_t)(((x & 3) << 6) | ((y & 7) << 3) | 1));
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

  /* Font-select base 0x010; font base 0x180 (both word units). */
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

static void show_centered(uint8_t doubled) {
  if (active_width > panel.width || active_height > panel.height) {
    return;
  }
  /* Bitmap rows add their own 2x scale; live text uses native-size rows. */
  write_word(3, 0, doubled ? 0x03 : 0, 0);
  set_frame(doubled);
  visible = 1;
  rtd_update(0, 0x6c, 0x01, 0x01);
}

void osd_show_splash(void) {
  load_bitmap(0);
  show_centered(1);
}

void osd_show_no_signal(void) {
  load_bitmap(1);
  show_centered(1);
}

/* The font is rasterized from the bundled OFL Roboto Mono source.
 * Native 12x18, two-bit glyphs retain four coverage levels.
 * Four original 24x36 icons use four consecutive character cells each.
 */
enum {
  GLYPH_ICON = MENU_FONT_COUNT,
  GLYPH_HORIZONTAL = GLYPH_ICON + MENU_ICON_COUNT * MENU_ICON_GLYPHS,
  GLYPH_VERTICAL, GLYPH_TOP_LEFT, GLYPH_TOP_RIGHT,
  GLYPH_BOTTOM_LEFT, GLYPH_BOTTOM_RIGHT, GLYPH_JOIN_LEFT, GLYPH_JOIN_RIGHT,
  GLYPH_SELECT, GLYPH_BACK, GLYPH_HOME, TEXT_GLYPHS
};

static uint16_t menu_scanline(uint8_t glyph, uint8_t y) {
  uint8_t offset;
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
  case GLYPH_HOME:
    if ((y >= 3 && y <= 7) || (y >= 10 && y <= 14))
      return y == 3 || y == 7 || y == 10 || y == 14 ? 0x79e : 0x492;
    return 0;
  default: return 0;
  }
}

#define TEXT_COLUMNS 30
#define TEXT_ROWS 5
#define MENU_ROWS 7
#define LIVE_MENU_ROWS 12
_Static_assert(MENU_FONT_WIDTH == 12 && MENU_FONT_HEIGHT == 18 &&
               MENU_FONT_BYTES == 54 && MENU_ICON_BYTES == 54,
               "Text renderer requires 12x18 two-bit glyphs");
_Static_assert(OSD_MAP_BASE + TEXT_COLUMNS * LIVE_MENU_ROWS <= OSD_FONT_BASE &&
               OSD_FONT_BASE + TEXT_GLYPHS * 18 <= 0xf00,
               "Menu map and fonts must fit without overlap");
static uint8_t text_row, text_column, text_color;
static uint8_t menu_slider, menu_slider_value;

/* Colors 0..7 are opaque foreground/background choices (except zero).
 * Colors 8..15 are the two intermediate coverages for four color pairs.
 * The two AA colors share their high palette bit, as required by p397.
 */
static const OSD_CODE uint8_t menu_palette[16][3] = {
  {0,0,0}, {232,238,246}, {12,20,36}, {24,84,148},
  {72,216,192}, {132,148,168}, {52,76,104}, {12,20,36},
  {85,93,106}, {159,165,176}, {93,135,181}, {163,187,213},
  {52,63,80}, {92,105,124}, {32,85,88}, {52,151,140}
};
static const OSD_CODE uint8_t diagnostic_palette[16][3] = {
  {0,0,0}, {255,255,255}, {0,0,0}, {16,64,160},
  {0,220,120}, {96,96,96}, {52,76,104}, {0,0,0},
  {85,85,85}, {170,170,170}, {96,128,192}, {175,191,223},
  {32,32,32}, {64,64,64}, {0,73,40}, {0,147,80}
};

static void text_palette(uint8_t menu) {
  const OSD_CODE uint8_t *colors = menu ? &menu_palette[0][0] :
                                        &diagnostic_palette[0][0];
  uint8_t i;
  rtd_write(0, 0x6e, 0x80);
  for (i = 0; i < 48; ++i) rtd_write(0, 0x6f, colors[i]);
  rtd_write(0, 0x6e, 0);
}

static void text_glyph(uint8_t glyph) {
  uint8_t fg = text_color >> 4, bg = text_color & 15;
  uint8_t aa1 = 8, aa2;
  if (bg == 3) aa1 = 10;
  else if (fg == 5) aa1 = 12;
  else if (fg == 4) aa1 = 14;
  aa2 = aa1 + 1;
  if (bg == 3 && fg == 5) {
    /* Disabled selected rows have no dedicated palette pair. Use their
     * exact endpoints for intermediate pixels instead of a dark halo. */
    aa1 = bg;
    aa2 = fg;
  }
  if (text_column < TEXT_COLUMNS) {
    /* 2-bpp map: bit7+bit5 enable the mode; four palette indices map
     * the low-bit-first font planes to background, AA1, AA2, foreground.
     * Foreground/background must share their palette high bit.
     */
    write_word(OSD_SRAM | (OSD_MAP_BASE + text_row * TEXT_COLUMNS +
                           text_column++),
               0xa0 | ((fg & 8) << 3) | ((aa1 & 8) << 1) |
               ((fg & 7) << 1) | ((bg & 4) >> 2), glyph,
               ((bg & 3) << 6) | ((aa2 & 7) << 3) | (aa1 & 7));
  }
}

static void text_put(char character) {
  if (character < MENU_FONT_FIRST || character >= MENU_FONT_FIRST + MENU_FONT_COUNT)
    character = ' ';
  text_glyph(character - MENU_FONT_FIRST);
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
  uint8_t row, glyph, y, plane, byte;
  uint16_t top, bottom;
  const OSD_CODE uint8_t *data;
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
    for (glyph = 0; glyph < GLYPH_HORIZONTAL; ++glyph) {
      if (glyph < MENU_FONT_COUNT) data = menu_font[glyph];
      else data = menu_icons[(glyph - GLYPH_ICON) / 4][(glyph - GLYPH_ICON) % 4];
      for (byte = 0; byte < MENU_FONT_BYTES; ++byte) {
        rtd_write(0, 0x92, data[byte]);
        if (byte == 26 || byte == 53) ddcci_service();
      }
    }
    for (; glyph < TEXT_GLYPHS; ++glyph) {
      /* Both planes carry the same binary rules/arrows: coverage 0 or 3. */
      for (plane = 0; plane < 2; ++plane) {
        for (y = 0; y < 18; y += 2) {
          top = menu_scanline(glyph, y);
          bottom = menu_scanline(glyph, y + 1);
          rtd_write(0, 0x92, (uint8_t)bottom);
          rtd_write(0, 0x92, (uint8_t)((top << 4) | (bottom >> 8)));
          rtd_write(0, 0x92, (uint8_t)(top >> 4));
        }
        ddcci_service();
      }
    }
    text_loaded = 1;
  }
  text_palette(0);
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
  show_centered(1);
}

static void menu_position(uint8_t row, uint8_t column) {
  text_row = row;
  text_column = column;
}

static void menu_rule(uint8_t left, uint8_t right) {
  text_color = 0x62;
  text_glyph(left);
  while (text_column < TEXT_COLUMNS - 1) text_glyph(GLYPH_HORIZONTAL);
  text_glyph(right);
  text_next_row();
}

void osd_menu_begin(const char *title, uint8_t tab, uint8_t rail_focus) {
  uint8_t row, icon, half;
  if (tab >= MENU_ICON_COUNT) tab = 0;
  text_begin(LIVE_MENU_ROWS);
  text_palette(1);
  menu_slider = 0;
  menu_rule(GLYPH_TOP_LEFT, GLYPH_TOP_RIGHT);
  /* Clear every cell so shorter pages cannot leave stale text or colors. */
  for (row = 1; row < LIVE_MENU_ROWS - 1; ++row) {
    text_color = 0x62;
    text_glyph(GLYPH_VERTICAL);
    text_color = 0x12;
    while (text_column < TEXT_COLUMNS - 1) text_put(' ');
    text_color = 0x62;
    text_glyph(GLYPH_VERTICAL);
    text_next_row();
  }
  menu_rule(GLYPH_BOTTOM_LEFT, GLYPH_BOTTOM_RIGHT);
  menu_position(1, 2);
  text_color = 0x42;
  text_glyph(GLYPH_HOME);
  menu_position(1, 7);
  text_color = 0x12;
  while (*title && text_column < TEXT_COLUMNS - 1) text_put(*title++);
  menu_position(2, 0);
  menu_rule(GLYPH_JOIN_LEFT, GLYPH_JOIN_RIGHT);

  for (icon = 0; icon < MENU_ICON_COUNT; ++icon) {
    for (half = 0; half < 2; ++half) {
      menu_position(3 + icon * 2 + half, 1);
      text_color = icon == tab && rail_focus ? 0x13 :
                   icon == tab ? 0x42 : 0x52;
      text_put(' ');
      text_glyph(GLYPH_ICON + icon * 4 + half * 2);
      text_glyph(GLYPH_ICON + icon * 4 + half * 2 + 1);
      text_put(' ');
      text_color = 0x62;
      text_glyph(GLYPH_VERTICAL);
      if (icon == tab && !rail_focus) {
        menu_position(3 + icon * 2 + half, 1);
        text_color = 0x42;
        text_glyph(GLYPH_VERTICAL);
      }
    }
  }
  /* The caller fills only the right pane. Navigation never erases the rail. */
  menu_position(3, 6);
}

void osd_menu_row(const char *label, const char *choice, uint8_t value,
                  uint8_t percent, uint8_t selected, uint8_t available) {
  uint8_t length = 0, start, background = selected ? 3 : 2;
  const char *cursor;
  if (text_row >= 8) return;
  if (!available) choice = "--";
  if (choice) {
    for (cursor = choice; *cursor && length < 7; ++cursor) ++length;
  } else {
    length = (value >= 100 ? 3 : value >= 10 ? 2 : 1) + (percent != 0);
  }
  start = TEXT_COLUMNS - 1 - length;
  text_color = 0x10 | background;
  if (selected) text_glyph(GLYPH_SELECT);
  else text_put(' ');
  text_color = (available ? 0x10 : 0x50) | background;
  while (*label && text_column < start - (length != 0)) text_put(*label++);
  while (text_column < start) text_put(' ');
  if (choice) {
    while (*choice && text_column < TEXT_COLUMNS - 1) text_put(*choice++);
  } else {
    text_number(value);
    if (percent) text_put('%');
  }
  if (selected && available && percent && !choice) {
    menu_slider = 1;
    menu_slider_value = value > 100 ? 100 : value;
  }
  ++text_row;
  text_column = 6;
}

void osd_menu_end(const char *footer) {
  uint8_t cell;
  if (menu_slider) {
    menu_position(9, 7);
    for (cell = 0; cell < 20; ++cell) {
      text_color = cell < menu_slider_value / 5 ? 0x42 : 0x62;
      text_glyph(GLYPH_HORIZONTAL);
    }
  }
  menu_position(10, 7);
  text_color = footer[0] == 'A' ? 0x42 : 0x52;
  while (*footer && text_column < TEXT_COLUMNS - 1) text_put(*footer++);
  active_width = TEXT_COLUMNS * 12u;
  active_height = LIVE_MENU_ROWS * 18u;
  show_centered(0);
}
