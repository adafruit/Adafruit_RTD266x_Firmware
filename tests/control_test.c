// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rtd/control.h"
#include "rtd/board.h"
#include "rtd/audio.h"
#include "rtd/osd.h"
#include "rtd/platform.h"
#include "rtd/video.h"
#include "rtd/storage.h"

/* Exercise real application policy with observable hardware and OSD calls.
 * Analog image quality and physical key debounce belong to board tests. */
static uint32_t clock_ms;
static uint8_t keys, backlight_available, backlight_ok, muted, volume;
static uint8_t volume_available;
static uint8_t saved[SET_COUNT], saved_present, storage_state, storage_ok;
static uint8_t saved_count;
static uint8_t change_during_save;
static unsigned saves;
static uint8_t brightness, contrast, fill, backlight;
static uint8_t aspect_requested, aspect_source;
static uint8_t red, green, blue, saturation, sharpness, osd_x, osd_y, osd_alpha;
static unsigned color_writes, sharpness_writes, style_writes;
static unsigned picture_writes, aspect_writes, backlight_writes;
static unsigned mute_writes, stops, hides, renders, row_count;
static const char *title, *footer;
static uint8_t active_tab, rail_focus;
static uint8_t drawing, inject_at, inject_key;
static unsigned injected;
static struct {
  const char *label, *choice;
  uint8_t value, percent, selected, available;
} rows[5];

#define TEST_ASPECT_MAX (RTD_ASPECT_16_9 ? 3 : RTD_ASPECT_4_3 ? 2 : 1)

uint32_t platform_millis(void) { return clock_ms; }
uint8_t store_load(uint8_t *values, uint8_t count) {
  assert(count == SET_COUNT);
  assert(!saved_present || saved_count == count);
  storage_state = saved_present ? STORE_LOADED : STORE_EMPTY;
  if (saved_present) memcpy(values, saved, count);
  return saved_present;
}
uint8_t store_load_compatible(uint8_t *values, uint8_t count,
                              uint8_t previous_count) {
  assert(count == SET_COUNT && previous_count == 11);
  assert(!saved_present || saved_count == count || saved_count == previous_count);
  storage_state = saved_present ? STORE_LOADED : STORE_EMPTY;
  if (saved_present) memcpy(values, saved, saved_count);
  return saved_present;
}
uint8_t store_save(const uint8_t *values, uint8_t count) {
  uint16_t maximum, status;
  assert(count == SET_COUNT);
  ++saves;
  assert(control_get(0xeb, &maximum, &status) && (status & 0x100));
  /* Mirror storage's entry snapshot before servicing a DDC callback. */
  if (storage_ok) memcpy(saved, values, count);
  if (change_during_save) {
    change_during_save = 0;
    clock_ms += 100;
    assert(control_set(0xe2, 77));
  }
  storage_state = storage_ok ? STORE_LOADED : STORE_ERROR;
  if (!storage_ok) return 0;
  saved_present = 1;
  saved_count = count;
  return 1;
}
uint8_t store_status(void) { return storage_state; }
uint8_t board_eeprom_read(uint16_t address, uint8_t *data, uint8_t count) {
  uint8_t i;
  if (address >= 2048 || count > 2048 - address) return 0;
  for (i = 0; i < count; ++i) data[i] = (uint8_t)(address + i);
  return 1;
}
uint16_t board_eeprom_diagnostic(void) { return 0x0053; }
uint8_t board_buttons(void) { return keys; }
uint8_t board_backlight_available(void) { return backlight_available; }
uint8_t board_backlight_power(uint8_t on) { backlight = on ? 100 : 0; return 1; }
uint8_t board_backlight_set(uint8_t percent) {
  if (!backlight_available || !backlight_ok || percent > 100) return 0;
  backlight = percent;
  ++backlight_writes;
  return 1;
}
uint8_t audio_get_mute(void) { return muted; }
uint8_t audio_volume_available(void) { return volume_available; }
uint8_t audio_get_volume(void) { return volume; }
uint8_t audio_set_volume(uint8_t percent) {
  if (!volume_available || percent > 100) return 0;
  volume = percent;
  return 1;
}
void audio_set_mute(uint8_t value) { muted = value != 0; ++mute_writes; }
void audio_stop(void) { ++stops; }
void video_set_picture(uint8_t b, uint8_t c) {
  brightness = b; contrast = c; ++picture_writes;
}
uint8_t video_aspect_available(uint8_t mode) {
  if (mode <= VIDEO_ASPECT_FILL) return 1;
  if (mode == VIDEO_ASPECT_4_3) return RTD_ASPECT_4_3 != 0;
  return mode == VIDEO_ASPECT_16_9 && RTD_ASPECT_16_9 &&
         (aspect_source == VIDEO_MODE_VGA || aspect_source == VIDEO_MODE_PANEL);
}
uint8_t video_aspect_current(void) {
  fill = video_aspect_available(aspect_requested) ? aspect_requested : VIDEO_ASPECT_KEEP;
  return fill;
}
void video_set_aspect(uint8_t value) {
  if (!video_aspect_available(value) &&
      !(RTD_ASPECT_16_9 && value == VIDEO_ASPECT_16_9)) return;
  aspect_requested = value;
  video_aspect_current();
  ++aspect_writes;
}
void video_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t s) {
  red = r; green = g; blue = b; saturation = s; ++color_writes;
}
void video_set_sharpness(uint8_t value) { sharpness = value; ++sharpness_writes; }
void osd_set_menu_style(uint8_t x, uint8_t y, uint8_t alpha) {
  osd_x = x; osd_y = y; osd_alpha = alpha; ++style_writes;
}
void osd_hide(void) { assert(!drawing); ++hides; }

static void poll_virtual_key(uint8_t point) {
  if (inject_at == point) {
    inject_at = 0; /* A single DDC event at this OSD polling boundary. */
    ++injected;
    assert(control_set(0xe0, inject_key));
  }
}

void osd_menu_begin(const char *text, uint8_t tab, uint8_t focus) {
  assert(!drawing);
  assert(tab < 4);
  drawing = 1;
  active_tab = tab;
  rail_focus = focus;
  title = text; row_count = 0; ++renders;
  poll_virtual_key(1);
}
void osd_menu_row(const char *label, const char *choice, uint8_t value,
                  uint8_t percent, uint8_t selected, uint8_t available) {
  assert(row_count < 5);
  rows[row_count].label = label;
  rows[row_count].choice = choice;
  rows[row_count].value = value;
  rows[row_count].percent = percent;
  rows[row_count].selected = selected;
  rows[row_count++].available = available;
  poll_virtual_key(2);
}
void osd_menu_end(const char *text) {
  assert(strlen(text) <= 22);
  footer = text;
  drawing = 0;
}

static void fixture(void) {
  clock_ms = 0;
  keys = muted = fill = 0;
  aspect_requested = VIDEO_ASPECT_KEEP;
  aspect_source = VIDEO_MODE_VGA;
  volume = 100;
  volume_available = 0;
  saved_present = saves = 0;
  saved_count = SET_COUNT;
  storage_ok = 1;
  change_during_save = 0;
  backlight_available = backlight_ok = 1;
  picture_writes = aspect_writes = backlight_writes = mute_writes = 0;
  color_writes = sharpness_writes = style_writes = 0;
  stops = hides = renders = row_count = 0;
  title = footer = 0;
  active_tab = rail_focus = 0;
  drawing = inject_at = inject_key = 0;
  injected = 0;
  control_init();
  assert(brightness == 50 && contrast == 50 && picture_writes == 1);
  assert(red == 50 && green == 50 && blue == 50 && saturation == 50);
  assert(sharpness == 50 && color_writes == 1 && sharpness_writes == 1);
  assert(osd_x == 50 && osd_y == 50 && osd_alpha == 0 && style_writes == 1);
  assert(!fill && !muted && volume == 100);
  aspect_writes = mute_writes = 0;
}

static uint16_t value(uint8_t code, uint16_t expected_maximum) {
  uint16_t maximum = 0, result = 0;
  assert(control_get(code, &maximum, &result));
  assert(maximum == expected_maximum);
  return result;
}

static void state(uint8_t page, uint8_t selection, uint8_t editing) {
  assert(value(0xe1, 0xffff) ==
         (((uint16_t)page << 8) | (selection << 1) | editing));
  assert(control_menu_open() == (page != MENU_CLOSED));
}

static void event(uint8_t key) {
  control_key(key);
  control_service(clock_ms);
}

static void down(uint8_t count) {
  while (count--) event(BOARD_KEY_DECREASE);
}

static void enter(uint8_t main_item) {
  event(BOARD_KEY_MENU);
  down(main_item);
  event(BOARD_KEY_MENU);
}

static void test_navigation(void) {
  static const char *titles[] = {"Picture", "Audio", "Display", "Menu settings"};
  static const char *first_labels[] = {"Image brightness", "Volume",
                                      "LED backlight", "Startup splash"};
  static const uint8_t item_counts[] = {5, 3, 5, 7};
  uint8_t i, j;
  fixture();
  event(BOARD_KEY_MENU);
  assert(!strcmp(title, "Picture") && row_count == 5);
  assert(rail_focus && active_tab == 0 && !rows[0].selected);
  assert(!strcmp(footer, "Menu: open  Back: exit"));
  event(BOARD_KEY_INCREASE);
  state(MENU_MAIN, 3, 0);
  assert(rail_focus && active_tab == 3 && !strcmp(title, "Menu settings"));
  event(BOARD_KEY_DECREASE);
  state(MENU_MAIN, 0, 0);
  down(4);
  state(MENU_MAIN, 0, 0); /* The rail has four categories, with no Exit row. */
  assert(picture_writes == 1 && !aspect_writes && !mute_writes && !backlight_writes);
  event(BOARD_KEY_BACK);
  state(MENU_CLOSED, 0, 0);
  assert(hides == 1 && control_overlay_changed());
  assert(!control_overlay_changed());
  for (i = 0; i < 4; ++i) {
    fixture();
    event(BOARD_KEY_MENU);
    down(i);
    state(MENU_MAIN, i, 0);
    assert(rail_focus && active_tab == i);
    assert(!strcmp(title, titles[i]) && !strcmp(rows[0].label, first_labels[i]));
    assert(row_count == (i == 1 ? 3u : 5u));
    for (j = 0; j < row_count; ++j) assert(!rows[j].selected);
    event(BOARD_KEY_MENU);
    state(MENU_PICTURE + i, 0, 0);
    assert(!rail_focus && active_tab == i && rows[0].selected);
    assert(!strcmp(title, titles[i]));
    assert(row_count == (i == 1 ? 3u : 5u));
    assert(!strcmp(footer, i == 3 ? "More: +/- Back: tabs" :
                                  "Menu: edit  Back: tabs"));
    event(BOARD_KEY_BACK);
    state(MENU_MAIN, i, 0); /* Back key retains the category. */
    assert(rail_focus && active_tab == i);
    event(BOARD_KEY_MENU);
    down(item_counts[i] - 1);
    event(BOARD_KEY_MENU); /* Explicit Back row also retains the category. */
    state(MENU_MAIN, i, 0);
    assert(rail_focus && active_tab == i);
    for (j = 0; j < row_count; ++j) assert(!rows[j].selected);
    event(BOARD_KEY_BACK);
    assert(!control_menu_open() && hides == 1);
  }
}

static void test_rail_previews_settings(void) {
  fixture();
  assert(control_set(0xe2, 65) && control_set(0x12, 40));
  assert(control_set(0x8d, 1) && control_set(0xe3, 1));
  assert(control_set(0xe4, 0) && control_set(0xe8, 0));
  backlight_available = 0;
  event(BOARD_KEY_MENU);
  assert(rows[0].value == 65 && rows[1].value == 40);
  assert(rows[0].percent && rows[1].percent && !rows[0].selected);
  assert(control_set(0xe2, 70));
  control_service(clock_ms);
  assert(rows[0].value == 70 && rail_focus);
  down(1);
  assert(!rows[0].available && !strcmp(rows[1].choice, "On"));
  down(1);
  assert(!rows[0].available && !rows[2].available && !rows[3].available);
  assert(!strcmp(rows[1].choice, "Fill"));
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_MENU);
  state(MENU_DISPLAY, 0, 0); /* Disabled LED level cannot enter adjustment. */
  event(BOARD_KEY_BACK);
  down(1);
  assert(!strcmp(rows[0].choice, "Off") && !strcmp(rows[2].choice, "Never"));
  assert(picture_writes == 4 && mute_writes == 1 && aspect_writes == 1);
  assert(!backlight_writes);
}

static void test_shared_picture_settings(void) {
  fixture();
  enter(0);
  event(BOARD_KEY_MENU);
  state(MENU_PICTURE, 0, 1);
  assert(!strcmp(footer, "Adjust +/-  Menu: done"));
  event(BOARD_KEY_INCREASE);
  assert(brightness == 55 && contrast == 50 && picture_writes == 2);
  assert(value(0xe2, 100) == 55 && rows[0].value == 55 && rows[0].percent);
  event(BOARD_KEY_BACK); /* Cancel editing, not the submenu. */
  state(MENU_PICTURE, 0, 0);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_DECREASE);
  assert(brightness == 55 && contrast == 45);
  event(BOARD_KEY_MENU);
  state(MENU_PICTURE, 1, 0);
  fixture();
  assert(control_set(0xe2, 55) && control_set(0x12, 45));
  assert(brightness == 55 && contrast == 45 && picture_writes == 3);
  assert(control_set(0xe2, 100));
  enter(0);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_INCREASE);
  assert(brightness == 100);
  assert(control_set(0xe2, 0));
  event(BOARD_KEY_DECREASE);
  assert(brightness == 0);
}

static void test_audio_and_display(void) {
  uint16_t maximum, current;
  fixture();
  enter(1);
  assert(!rows[0].available && !control_get(0x62, &maximum, &current));
  event(BOARD_KEY_MENU);
  state(MENU_AUDIO, 0, 0); /* Unsupported volume cannot enter edit mode. */
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_INCREASE);
  assert(muted && value(0x8d, 2) == 1 && !strcmp(rows[1].choice, "On"));
  event(BOARD_KEY_DECREASE);
  assert(!muted && value(0x8d, 2) == 2);
  assert(control_set(0x8d, 1) && muted);
  fixture();
  enter(2);
  assert(rows[0].available && !rows[2].available && !rows[3].available);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_DECREASE);
  assert(backlight == 95 && value(0x10, 100) == 95);
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_INCREASE);
  assert(fill == 1 && value(0xe3, TEST_ASPECT_MAX) == 1);
  assert(control_set(0xe3, 0) && !fill);
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  state(MENU_DISPLAY, 2, 0);
  down(1);
  event(BOARD_KEY_MENU);
  state(MENU_DISPLAY, 3, 0);
  assert(aspect_writes == 2 && backlight_writes == 1);
  backlight_ok = 0;
  assert(!control_set(0x10, 25) && value(0x10, 100) == 95);
  backlight_available = 0;
  assert(!control_get(0x10, &maximum, &current) && !control_set(0x10, 25));
}

static void test_aspect_availability(void) {
  static const uint8_t modes[] = {
    VIDEO_ASPECT_KEEP, VIDEO_ASPECT_FILL,
#if RTD_ASPECT_4_3
    VIDEO_ASPECT_4_3,
#endif
#if RTD_ASPECT_16_9
    VIDEO_ASPECT_16_9,
#endif
  };
  unsigned i;
  fixture();
  assert(value(0xe3, TEST_ASPECT_MAX) == VIDEO_ASPECT_KEEP);
#if !RTD_ASPECT_4_3
  assert(!control_set(0xe3, VIDEO_ASPECT_4_3));
#endif
#if !RTD_ASPECT_16_9
  assert(!control_set(0xe3, VIDEO_ASPECT_16_9));
#endif
  assert(!aspect_writes);
  enter(2);
  down(1);
  event(BOARD_KEY_MENU);
  for (i = 1; i < sizeof modes; ++i) {
    event(BOARD_KEY_INCREASE);
    assert(value(0xe3, TEST_ASPECT_MAX) == modes[i]);
    assert(control_setting(SET_ASPECT) == modes[i]);
    assert(!strcmp(rows[1].choice, modes[i] == VIDEO_ASPECT_FILL ? "Fill" :
                                  modes[i] == VIDEO_ASPECT_4_3 ? "4:3" : "16:9"));
  }
  event(BOARD_KEY_INCREASE);
  assert(value(0xe3, TEST_ASPECT_MAX) == modes[sizeof modes - 1]);
  for (i = sizeof modes - 1; i; --i) {
    event(BOARD_KEY_DECREASE);
    assert(value(0xe3, TEST_ASPECT_MAX) == modes[i - 1]);
  }
  event(BOARD_KEY_DECREASE);
  assert(value(0xe3, TEST_ASPECT_MAX) == VIDEO_ASPECT_KEEP);

#if RTD_SETTINGS
  /* Every compiled-in preference can survive a controller restart. */
  for (i = 1; i < sizeof modes; ++i) {
    assert(control_set(0xe3, modes[i]));
    clock_ms += 2000;
    control_service(clock_ms);
    assert(saved[SET_ASPECT] == modes[i]);
    aspect_requested = VIDEO_ASPECT_KEEP;
    control_init();
    assert(value(0xe3, TEST_ASPECT_MAX) == modes[i]);
    assert(control_setting(SET_ASPECT) == modes[i]);
  }
#endif

#if RTD_ASPECT_16_9
  fixture();
  assert(control_set(0xe3, VIDEO_ASPECT_16_9));
  clock_ms = 2000;
  control_service(clock_ms);
#if RTD_SETTINGS
  assert(saves == 1 && saved[SET_ASPECT] == VIDEO_ASPECT_16_9);
#endif
  aspect_source = VIDEO_MODE_CVT;
#if RTD_SETTINGS
  control_init(); /* Saved 16:9 is retained even when the source needs Keep. */
#endif
  assert(control_setting(SET_ASPECT) == VIDEO_ASPECT_16_9);
  assert(value(0xe3, TEST_ASPECT_MAX) == VIDEO_ASPECT_KEEP);
  assert(!control_set(0xe3, VIDEO_ASPECT_16_9));
  enter(2);
  assert(!strcmp(rows[1].choice, "Keep"));
  aspect_source = VIDEO_MODE_VGA;
  assert(value(0xe3, TEST_ASPECT_MAX) == VIDEO_ASPECT_16_9);
  aspect_source = VIDEO_MODE_CVT;
  assert(value(0xe3, TEST_ASPECT_MAX) == VIDEO_ASPECT_KEEP);
  assert(control_set(0xe3, VIDEO_ASPECT_KEEP));
  assert(control_setting(SET_ASPECT) == VIDEO_ASPECT_KEEP);
#if RTD_SETTINGS
  assert(value(0xeb, 0x103) & 0x100);
  clock_ms += 2000;
  control_service(clock_ms);
  assert(saves == 2 && saved[SET_ASPECT] == VIDEO_ASPECT_KEEP);
  aspect_source = VIDEO_MODE_VGA;
  control_init();
  assert(value(0xe3, TEST_ASPECT_MAX) == VIDEO_ASPECT_KEEP);
#endif

  /* With an incompatible source, navigation stops at the last usable mode.
   * The 16:9-only build also has a reserved hole at mode 2 to skip. */
  fixture();
  aspect_source = VIDEO_MODE_CVT;
  enter(2);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_INCREASE);
  event(BOARD_KEY_INCREASE);
  event(BOARD_KEY_INCREASE);
  assert(value(0xe3, TEST_ASPECT_MAX) ==
         (RTD_ASPECT_4_3 ? VIDEO_ASPECT_4_3 : VIDEO_ASPECT_FILL));
#endif
}

static void test_saved_aspect_build_compatibility(void) {
#if RTD_SETTINGS
  uint8_t i, mode;
  fixture();
  volume_available = 1;
  for (i = 0; i < SET_COUNT; ++i) saved[i] = control_setting(i);
  saved[SET_BRIGHTNESS] = 65;
  saved[SET_VOLUME] = 25;
  saved_present = 1;
  for (mode = VIDEO_ASPECT_4_3; mode <= VIDEO_ASPECT_16_9; ++mode) {
    saved[SET_ASPECT] = mode;
    /* A real boot initializes the video driver before loading preferences. */
    aspect_requested = VIDEO_ASPECT_KEEP;
    control_init();
    assert(brightness == 65 && volume == 25);
    assert(control_setting(SET_ASPECT) == mode);
    assert(value(0xe3, TEST_ASPECT_MAX) ==
           (video_aspect_available(mode) ? mode : VIDEO_ASPECT_KEEP));
  }
  saved[SET_ASPECT] = VIDEO_ASPECT_16_9 + 1;
  control_init();
  assert(brightness == 50 && volume == 100);
  assert(control_setting(SET_ASPECT) == VIDEO_ASPECT_KEEP);
#endif
}

static void test_settings_and_signal(void) {
  static const uint16_t seconds[] = {0, 1, 2, 5, 10, 20, 30, 40, 50, 60};
  uint8_t i;
  fixture();
  assert(control_setting(SET_SPLASH) == (RTD_SPLASH != 0));
  enter(3);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_DECREASE);
  assert(value(0xe4, 1) == 0);
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_DECREASE);
  assert(value(0xe5, 1) == 0);
  assert(control_overlay_changed() && !control_overlay_changed());
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_INCREASE);
  assert(value(0xe8, 3) == 3 && !strcmp(rows[2].choice, "20s"));
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  state(MENU_SIGNAL, 0, 0);
  assert(!strcmp(title, "No signal") && row_count == 3);
  assert(active_tab == 3 && !rail_focus);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_DECREASE);
  assert(value(0xe6, 2) == 1 && !strcmp(rows[0].choice, "Blue"));
  assert(control_overlay_changed() && !control_overlay_changed());
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_INCREASE);
  assert(value(0xe7, 9) == 1 && control_signal_timeout_ms() == 1000);
  for (i = 0; i < sizeof seconds / sizeof seconds[0]; ++i) {
    assert(control_set(0xe7, i));
    assert(control_signal_timeout_ms() == (uint32_t)seconds[i] * 1000);
  }
  event(BOARD_KEY_BACK);
  state(MENU_SIGNAL, 1, 0);
  event(BOARD_KEY_BACK);
  state(MENU_SETTINGS, 3, 0);
  assert(!strcmp(title, "Menu settings") && active_tab == 3 && !rail_focus);
  event(BOARD_KEY_MENU);
  state(MENU_SIGNAL, 0, 0);
  down(2);
  event(BOARD_KEY_MENU); /* Nested BACK row returns to the same parent item. */
  state(MENU_SETTINGS, 3, 0);
}

static void test_vcp_rejection_and_power(void) {
  static const uint16_t invalid[][2] = {
    {0x10, 101}, {0x12, 101}, {0xe2, 101}, {0xe2, 0xffff},
    {0xe3, 4}, {0xe4, 2}, {0xe5, 2}, {0xe6, 3}, {0xe7, 10}, {0xe8, 4},
    {0x8d, 0}, {0x8d, 3}, {0xd6, 0}, {0xd6, 2}, {0xd6, 3}, {0xd6, 5},
    {0xe0, 0}, {0xe0, 3}, {0xe0, 17}, {0x62, 50}, {0xdf, 0x202},
    {0xe1, 0}, {0xff, 0}, {0x04, 0}, {0x04, 2}, {0x16, 101}, {0x18, 101},
    {0x1a, 101}, {0x8a, 101}, {0x87, 101}, {0xf0, 101}, {0xf1, 101},
    {0xf2, 101}, {0xf3, 121}, {0xf4, 2}, {0xf5, 1}
  };
  unsigned i;
  fixture();
  for (i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
    assert(!control_set((uint8_t)invalid[i][0], invalid[i][1]));
  assert(picture_writes == 1 && !aspect_writes && !backlight_writes);
  assert(!mute_writes && !stops && !hides && control_power());
  assert(control_setting(SET_COUNT) == 0);
  assert(value(0xdf, 0x202) == 0x202);
  assert(control_set(0xe0, BOARD_KEY_MENU));
  control_service(clock_ms);
  state(MENU_MAIN, 0, 0);
  assert(control_set(0xd6, 4));
  control_service(clock_ms);
  assert(!control_power() && !control_menu_open() && backlight == 0);
  assert(stops == 1 && hides == 1 && control_overlay_changed());
  event(BOARD_KEY_MENU);
  assert(!control_menu_open());
  assert(control_set(0x10, 75));
  assert(backlight == 0 && value(0x10, 100) == 75);
  event(BOARD_KEY_POWER);
  assert(control_power() && backlight == 75 && value(0xd6, 4) == 1);
}

static void test_timeouts_and_physical_edges(void) {
  uint32_t began = UINT32_MAX - 2000;
  fixture();
  clock_ms = began;
  event(BOARD_KEY_MENU);
  clock_ms = began + 9999;
  control_service(clock_ms);
  assert(control_menu_open());
  clock_ms = began + 10000;
  control_service(clock_ms);
  assert(!control_menu_open() && hides == 1 && control_overlay_changed());
  assert(control_set(0xe8, 0));
  event(BOARD_KEY_MENU);
  clock_ms += 1000000;
  control_service(clock_ms);
  assert(control_menu_open());

  fixture();
  clock_ms = 1002;
  keys = BOARD_KEY_MENU;
  control_service(1000); /* Timer advanced since caller captured now. */
  state(MENU_MAIN, 0, 0);
  control_service(clock_ms);
  state(MENU_MAIN, 0, 0); /* Held key is not another press. */
  keys = 0;
  control_service(clock_ms);
  keys = BOARD_KEY_MENU;
  control_service(clock_ms);
  state(MENU_PICTURE, 0, 0);
}

static void test_power_transition_latch(void) {
  fixture();
  assert(!control_power_changed());
  assert(control_set(0xd6, 1) && control_power_changed());
  assert(!control_power_changed());
  assert(!control_set(0xd6, 2) && !control_power_changed());
  assert(control_set(0xd6, 4) && control_power_changed());
  assert(!control_power_changed());
  assert(control_set(0xd6, 4) && !control_power_changed());
  assert(control_set(0xd6, 1) && control_power_changed());
  assert(!control_power_changed());

  /* Bitmap uploads can service both requests before the monitor next polls.
   * The final power value is unchanged, but transient state still needs reset. */
  assert(control_set(0xd6, 4));
  assert(control_set(0xd6, 1));
  assert(control_power() && control_power_changed());
  assert(!control_power_changed());
  /* Repeated on requests still wake the physical gate from no-signal sleep. */
  assert(control_set(0xd6, 1) && control_power_changed());
  assert(!control_power_changed());
  assert(control_set(0xd6, 4));
  control_init();
  assert(control_power() && !control_power_changed());
}

static void test_callbacks_during_render(void) {
  static const uint8_t closing_keys[] = {BOARD_KEY_BACK, BOARD_KEY_POWER};
  unsigned i;
  fixture();
  inject_at = 1;
  inject_key = BOARD_KEY_MENU;
  event(BOARD_KEY_MENU);
  state(MENU_PICTURE, 0, 0);
  assert(injected == 1 && renders == 1 && !drawing && !hides);
  /* Rail focus was chosen before the callback entered Picture. The subsequent
   * service must redraw the new focus, preserving dirty set during rendering. */
  assert(!strcmp(title, "Picture") && rail_focus);
  control_service(clock_ms);
  assert(renders == 2 && !strcmp(title, "Picture") && row_count == 5);
  assert(!rail_focus && active_tab == 0 && rows[0].selected);
  control_service(clock_ms);
  assert(renders == 2 && injected == 1);

  fixture();
  inject_at = 2;
  inject_key = BOARD_KEY_DECREASE;
  event(BOARD_KEY_MENU);
  state(MENU_MAIN, 1, 0);
  assert(injected == 1 && renders == 1 && !hides);
  assert(!strcmp(title, "Picture") && active_tab == 0 && rail_focus);
  control_service(clock_ms);
  assert(renders == 2 && !strcmp(title, "Audio") && active_tab == 1 && rail_focus);
  assert(!rows[0].selected && !rows[1].selected && !rows[2].selected);

  for (i = 0; i < sizeof closing_keys; ++i) {
    fixture();
    inject_at = i == 0 ? 2 : 1;
    inject_key = closing_keys[i];
    event(BOARD_KEY_MENU);
    assert(injected == 1 && !control_menu_open() && !drawing);
    assert(hides == 0 && renders == 1); /* No hide from inside OSD callbacks. */
    assert(control_power() == (closing_keys[i] != BOARD_KEY_POWER));
    control_service(clock_ms);
    assert(hides == 1 && renders == 1);
    control_service(clock_ms);
    assert(hides == 1 && renders == 1 && injected == 1);
  }

  /* A callback can cross the scrolling boundary while rows are being drawn.
   * Finish one five-row viewport, then redraw the new viewport next service. */
  for (i = 0; i < 2; ++i) {
    fixture();
    enter(3);
    down(i ? 5 : 4);
    assert(control_set(0xe2, 60)); /* Request another draw without moving focus. */
    inject_at = 2;
    inject_key = i ? BOARD_KEY_INCREASE : BOARD_KEY_DECREASE;
    control_service(clock_ms);
    state(MENU_SETTINGS, i ? 4 : 5, 0);
    assert(row_count == 5 && injected == 1);
    assert(!strcmp(rows[0].label, i ? "Menu timeout" : "Startup splash"));
    control_service(clock_ms);
    assert(row_count == 5);
    assert(!strcmp(rows[0].label, i ? "Startup splash" : "Menu timeout"));
    assert(rows[i ? 4 : 3].selected);
  }
}

static void test_volume_control(void) {
  fixture();
  volume_available = 1;
  enter(1);
  assert(rows[0].available && rows[0].value == 100);
  event(BOARD_KEY_MENU);
  state(MENU_AUDIO, 0, 1);
  event(BOARD_KEY_DECREASE);
  assert(value(0x62, 100) == 95 && rows[0].value == 95);
  assert(control_setting(SET_VOLUME) == 95);
  assert(control_set(0x62, 25) && volume == 25);
  assert(!control_set(0x62, 101) && volume == 25);
  assert(control_set(0x8d, 1) && volume == 25 && muted);
  assert(control_setting(SET_MUTE) == 1);
  assert(control_set(0x62, 0) && muted && volume == 0);
  assert(control_set(0x8d, 2) && !muted && volume == 0);
}

static void test_color_sharpness_and_style(void) {
  fixture();
  assert(value(0x16, 100) == 50 && value(0x18, 100) == 50);
  assert(value(0x1a, 100) == 50 && value(0x8a, 100) == 50);
  assert(value(0x87, 100) == 50 && value(0xf0, 100) == 50);
  assert(value(0xf1, 100) == 50 && value(0xf2, 100) == 0);
  assert(value(0xf5, 0) == 0 && !control_set(0xf5, 1));
  assert(control_set(0x16, 100) && red == 100 && green == 50 && blue == 50);
  assert(control_set(0x18, 0) && red == 100 && green == 0 && blue == 50);
  assert(control_set(0x1a, 25) && red == 100 && green == 0 && blue == 25);
  assert(control_set(0x8a, 75) && saturation == 75);
  assert(control_set(0x87, 0) && sharpness == 0);
  assert(control_set(0x87, 100) && sharpness == 100);
  assert(control_set(0xf0, 0) && osd_x == 0 && osd_y == 50 && osd_alpha == 0);
  assert(control_set(0xf1, 100) && osd_x == 0 && osd_y == 100 && osd_alpha == 0);
  assert(control_set(0xf2, 100) && osd_alpha == 100);
  assert(control_setting(SET_RED) == 100 && control_setting(SET_GREEN) == 0);
  assert(control_setting(SET_BLUE) == 25 && control_setting(SET_SATURATION) == 75);
  assert(control_setting(SET_SHARPNESS) == 100);

  fixture();
  enter(0);
  down(2);
  event(BOARD_KEY_MENU);
  state(MENU_COLOR, 0, 0);
  assert(row_count == 5 && active_tab == 0 && rows[0].percent);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_INCREASE);
  assert(red == 55 && green == 50 && blue == 50 && saturation == 50);
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_DECREASE);
  assert(green == 45);
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_INCREASE);
  assert(blue == 55);
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_DECREASE);
  assert(saturation == 45);
  event(BOARD_KEY_BACK);
  event(BOARD_KEY_BACK);
  state(MENU_PICTURE, 2, 0);
  event(BOARD_KEY_MENU);
  down(4);
  event(BOARD_KEY_MENU);
  state(MENU_PICTURE, 2, 0);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_DECREASE);
  assert(sharpness == 45);

  fixture();
  enter(3);
  down(4);
  event(BOARD_KEY_MENU);
  state(MENU_OSD, 0, 0);
  assert(row_count == 4 && active_tab == 3);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_DECREASE);
  assert(osd_x == 45 && osd_y == 50);
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_INCREASE);
  assert(osd_y == 55);
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_INCREASE);
  assert(osd_alpha == 5);
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  state(MENU_SETTINGS, 4, 0);
}

static void test_pagination_and_reset_confirmation(void) {
  fixture();
  assert(control_set(0xe2, 75));
  enter(3);
  down(4);
  assert(row_count == 5 && rows[4].selected);
  down(1);
  state(MENU_SETTINGS, 5, 0);
  assert(row_count == 5 && rows[3].selected);
  assert(!strcmp(rows[0].label, "Menu timeout") && !strcmp(rows[3].label, "System"));
  down(1);
  state(MENU_SETTINGS, 6, 0);
  assert(row_count == 5 && rows[4].selected);
  down(1);
  state(MENU_SETTINGS, 0, 0);
  assert(row_count == 5 && rows[0].selected);
  event(BOARD_KEY_INCREASE);
  state(MENU_SETTINGS, 6, 0);
  event(BOARD_KEY_INCREASE);
  event(BOARD_KEY_MENU);
  state(MENU_SYSTEM, 0, 0);
  assert(row_count == 4 && active_tab == 3);
  down(2);
  event(BOARD_KEY_MENU);
  state(MENU_RESET, 0, 0);
  assert(row_count == 2 && brightness == 75);
  event(BOARD_KEY_MENU); /* Cancel is selected when confirmation opens. */
  state(MENU_SYSTEM, 2, 0);
  assert(brightness == 75);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_BACK);
  state(MENU_SYSTEM, 2, 0);
  assert(brightness == 75);
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  assert(brightness == 50 && contrast == 50);
  state(MENU_SYSTEM, 2, 0);
  assert(value(0x04, 1) == 0);
}

static void test_sleep_and_burn_in(void) {
  uint32_t began = UINT32_MAX - 30000;
  fixture();
  enter(3);
  down(5);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_MENU);
  state(MENU_SYSTEM, 0, 1);
  event(BOARD_KEY_INCREASE);
  assert(value(0xf3, 120) == 1);
  event(BOARD_KEY_DECREASE);
  assert(value(0xf3, 120) == 0);
  event(BOARD_KEY_MENU);
  down(1);
  event(BOARD_KEY_MENU);
  event(BOARD_KEY_INCREASE);
  assert(control_burn_in());
  event(BOARD_KEY_DECREASE);
  assert(!control_burn_in());

  fixture();
  clock_ms = began;
  assert(value(0xf3, 120) == 0 && control_set(0xf3, 1));
  clock_ms = began + 59999;
  control_service(clock_ms);
  assert(control_power());
  ++clock_ms;
  control_service(clock_ms);
  assert(!control_power() && backlight == 0);
  assert(value(0xf3, 120) == 1);
  event(BOARD_KEY_POWER);
  assert(control_power());
  began = clock_ms;
  clock_ms = began + 59999;
  control_service(clock_ms);
  assert(control_power());
  ++clock_ms;
  control_service(clock_ms);
  assert(!control_power());
  event(BOARD_KEY_POWER);
  assert(control_set(0xf3, 0));
  clock_ms += 7200001;
  control_service(clock_ms);
  assert(control_power());
  assert(control_set(0xf3, 120) && value(0xf3, 120) == 120);

  fixture();
  assert(value(0xf4, 1) == 0 && !control_burn_in());
  assert(control_set(0xf4, 1) && control_burn_in());
  assert(control_overlay_changed() && !control_overlay_changed());
  clock_ms = 2000;
  control_service(clock_ms);
  assert(!saves); /* Test-pattern mode is never a persisted preference. */
  control_init();
  assert(!control_burn_in() && value(0xf4, 1) == 0);
  assert(control_set(0xf4, 1));
  assert(control_set(0xd6, 4) && !control_burn_in());
  assert(control_set(0xd6, 1) && !control_burn_in());
}

static void test_factory_reset_persistence(void) {
  fixture();
  volume_available = 1;
  assert(control_set(0xe2, 75) && control_set(0x12, 25));
  assert(control_set(0x16, 100) && control_set(0x18, 0));
  assert(control_set(0x1a, 25) && control_set(0x8a, 75));
  assert(control_set(0x87, 100) && control_set(0xf0, 0));
  assert(control_set(0xf1, 100) && control_set(0xf2, 100));
  assert(control_set(0xf3, 120) && control_set(0xf4, 1));
  assert(control_set(0x62, 25) && control_set(0x8d, 1));
  assert(control_set(0xe3, 1) && control_set(0xe4, 0));
  assert(control_set(0xe5, 0) && control_set(0xe6, 0));
  assert(control_set(0xe7, 9) && control_set(0xe8, 0));
  clock_ms += 2000;
  control_service(clock_ms);
#if RTD_SETTINGS
  assert(saves == 1);
#endif
  enter(2);
  assert(control_set(0x04, 1));
  control_service(clock_ms);
  state(MENU_DISPLAY, 0, 0); /* A DDC reset redraws the open menu in place. */
  assert(brightness == 50 && contrast == 50 && !fill);
  assert(red == 50 && green == 50 && blue == 50 && saturation == 50);
  assert(sharpness == 50 && osd_x == 50 && osd_y == 50 && osd_alpha == 0);
  assert(volume == 100 && !muted && !control_burn_in());
  assert(value(0xf3, 120) == 0 && control_power());
  assert(control_setting(SET_SPLASH) == (RTD_SPLASH != 0));
  assert(control_setting(SET_POPUP) == 1 && control_setting(SET_NO_SIGNAL) == 2);
  assert(control_setting(SET_SIGNAL_TIMEOUT) == 0 && control_setting(SET_MENU_TIMEOUT) == 2);
  clock_ms += 2000;
  control_service(clock_ms);
#if RTD_SETTINGS
  assert(saves == 2);
  brightness = red = sharpness = 0;
  control_init();
  assert(brightness == 50 && red == 50 && sharpness == 50);
  assert(value(0xf3, 120) == 0 && osd_alpha == 0 && !control_burn_in());
#endif
}

static void test_persistence(void) {
#if RTD_SETTINGS
  uint8_t i;
  fixture();
  volume_available = 1;
  assert(control_set(0x12, 35));
  clock_ms = 1999;
  control_service(clock_ms);
  assert(!saves);
  assert(control_set(0xe2, 65));
  assert(control_set(0x62, 25) && control_set(0x8d, 1));
  assert(control_set(0xe4, 0) && control_set(0xe3, 1));
  assert(control_set(0xe5, 0) && control_set(0xe6, 1));
  assert(control_set(0xe7, 3) && control_set(0xe8, 3));
  assert(control_set(0x16, 80) && control_set(0x18, 40));
  assert(control_set(0x1a, 30) && control_set(0x8a, 60));
  assert(control_set(0x87, 70) && control_set(0xf0, 20));
  assert(control_set(0xf1, 80) && control_set(0xf2, 35));
  assert(control_set(0xf3, 7));
  clock_ms += 1999;
  control_service(clock_ms);
  assert(!saves && (value(0xeb, 0x103) & 0x100));
  ++clock_ms;
  control_service(clock_ms);
  assert(saves == 1 && value(0xeb, 0x103) == STORE_LOADED);
  muted = fill = 0; volume = 100;
  control_init();
  assert(brightness == 65 && contrast == 35 && volume == 25 && muted && fill);
  assert(!control_setting(SET_SPLASH) && !control_setting(SET_POPUP));
  assert(control_setting(SET_NO_SIGNAL) == 1 && control_signal_timeout_ms() == 5000);
  assert(control_setting(SET_MENU_TIMEOUT) == 3);
  assert(red == 80 && green == 40 && blue == 30 && saturation == 60);
  assert(sharpness == 70 && osd_x == 20 && osd_y == 80 && osd_alpha == 35);
  assert(value(0xf3, 120) == 7);
  assert(control_set(0x12, 35));
  clock_ms += 3000;
  control_service(clock_ms);
  assert(saves == 1); /* No wear for an unchanged preference. */
  storage_ok = 0;
  assert(control_set(0x12, 40));
  clock_ms += 2000;
  control_service(clock_ms);
  assert(saves == 2 && value(0xeb, 0x103) == STORE_ERROR && contrast == 40);
  clock_ms += 10000;
  control_service(clock_ms);
  assert(saves == 2); /* Do not keep retrying a failed EEPROM. */
  for (i = 0; i < SET_COUNT; ++i) {
    memset(saved, 0, sizeof saved);
    saved[i] = 255; saved_present = 1;
    fill = muted = 1; volume = 25;
    control_init();
    assert(brightness == 50 && contrast == 50);
    assert(control_setting(SET_VOLUME) == 100 && control_setting(SET_MUTE) == 0);
    assert(!fill && !muted && volume == 100);
  }
  fixture();
  clock_ms = UINT32_MAX - 1000;
  assert(control_set(0x12, 30));
  clock_ms += 2000;
  control_service(clock_ms);
  assert(saves == 1); /* Coalescing survives uptime rollover. */
  fixture();
  assert(control_set(0xe2, 65));
  clock_ms = 2000;
  change_during_save = 1;
  control_service(clock_ms);
  assert(saves == 1 && saved[SET_BRIGHTNESS] == 65 && brightness == 77);
  assert(value(0xeb, 0x103) == (STORE_LOADED | 0x100));
  clock_ms += 1999;
  control_service(clock_ms);
  assert(saves == 1); /* Callback starts a fresh coalescing deadline. */
  ++clock_ms;
  control_service(clock_ms);
  assert(saves == 2 && saved[SET_BRIGHTNESS] == 77);
  assert(value(0xeb, 0x103) == STORE_LOADED);
  control_init();
  assert(brightness == 77);

  /* Existing v39 EEPROM records restore their eleven fields while new fields
   * inherit defaults, then join the next atomic save as a 21-byte payload. */
  fixture();
  volume_available = 1;
  {
    static const uint8_t legacy[11] = {65, 35, 100, 1, 0, 0, 1, 3, 3, 25, 1};
    memset(saved, 0xff, sizeof saved);
    memcpy(saved, legacy, sizeof legacy);
  }
  saved_present = 1;
  saved_count = 11;
  control_init();
  assert(brightness == 65 && contrast == 35 && fill == 1 && muted && volume == 25);
  assert(red == 50 && green == 50 && blue == 50 && saturation == 50);
  assert(sharpness == 50 && osd_x == 50 && osd_y == 50 && osd_alpha == 0);
  assert(value(0xf3, 120) == 0 && value(0xf5, 0) == 0);
  assert(control_set(0xf2, 25));
  clock_ms = 2000;
  control_service(clock_ms);
  assert(saves == 1 && saved_count == SET_COUNT && saved[SET_OSD_ALPHA] == 25);
  control_init();
  assert(brightness == 65 && contrast == 35 && volume == 25 && osd_alpha == 25);
#endif
#if RTD_EEPROM_DIAGNOSTICS
  fixture();
  assert(control_set(0xe9, 0x7fc));
  assert(value(0xea, 0xffff) == 0xfcfd && value(0xea, 0xffff) == 0xfeff);
  assert(value(0xe9, 2046) == 0 && !control_set(0xe9, 2047));
  assert(!control_set(0xea, 0) && !control_set(0xeb, 0));
#endif
}

int main(void) {
  test_navigation();
  test_rail_previews_settings();
  test_shared_picture_settings();
  test_audio_and_display();
  test_aspect_availability();
  test_saved_aspect_build_compatibility();
  test_settings_and_signal();
  test_vcp_rejection_and_power();
  test_timeouts_and_physical_edges();
  test_power_transition_latch();
  test_callbacks_during_render();
  test_volume_control();
  test_color_sharpness_and_style();
  test_pagination_and_reset_confirmation();
  test_sleep_and_burn_in();
  test_factory_reset_persistence();
  test_persistence();
  puts("Control: menus, color/style, reset, VCP bounds, power/sleep, burn-in and persistence pass");
  return 0;
}
