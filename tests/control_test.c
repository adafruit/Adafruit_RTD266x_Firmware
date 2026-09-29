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

/* Exercise real application policy with observable hardware and OSD calls.
 * Analog image quality and physical key debounce belong to board tests. */
static uint32_t clock_ms;
static uint8_t keys, backlight_available, backlight_ok, muted;
static uint8_t brightness, contrast, fill, backlight;
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

uint32_t platform_millis(void) { return clock_ms; }
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
uint8_t audio_volume_available(void) { return 0; }
void audio_set_mute(uint8_t value) { muted = value != 0; ++mute_writes; }
void audio_stop(void) { ++stops; }
void video_set_picture(uint8_t b, uint8_t c) {
  brightness = b; contrast = c; ++picture_writes;
}
void video_set_aspect(uint8_t value) { fill = value; ++aspect_writes; }
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
  backlight_available = backlight_ok = 1;
  picture_writes = aspect_writes = backlight_writes = mute_writes = 0;
  stops = hides = renders = row_count = 0;
  title = footer = 0;
  active_tab = rail_focus = 0;
  drawing = inject_at = inject_key = 0;
  injected = 0;
  control_init();
  assert(brightness == 50 && contrast == 50 && picture_writes == 1);
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
  uint8_t i, j;
  fixture();
  event(BOARD_KEY_MENU);
  assert(!strcmp(title, "Picture") && row_count == 3);
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
    assert(row_count == (i < 2 ? 3u : 5u));
    for (j = 0; j < row_count; ++j) assert(!rows[j].selected);
    event(BOARD_KEY_MENU);
    state(MENU_PICTURE + i, 0, 0);
    assert(!rail_focus && active_tab == i && rows[0].selected);
    assert(!strcmp(title, titles[i]));
    assert(row_count == (i < 2 ? 3u : 5u));
    assert(!strcmp(footer, "Menu: edit  Back: tabs"));
    event(BOARD_KEY_BACK);
    state(MENU_MAIN, i, 0); /* Back key retains the category. */
    assert(rail_focus && active_tab == i);
    event(BOARD_KEY_MENU);
    down(row_count - 1);
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
  assert(fill == 1 && value(0xe3, 1) == 1);
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

static void test_settings_and_signal(void) {
  static const uint16_t seconds[] = {0, 1, 2, 5, 10, 20};
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
  assert(value(0xe7, 5) == 1 && control_signal_timeout_ms() == 1000);
  for (i = 0; i < 6; ++i) {
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
    {0xe3, 2}, {0xe4, 2}, {0xe5, 2}, {0xe6, 3}, {0xe7, 6}, {0xe8, 4},
    {0x8d, 0}, {0x8d, 3}, {0xd6, 0}, {0xd6, 2}, {0xd6, 3}, {0xd6, 5},
    {0xe0, 0}, {0xe0, 3}, {0xe0, 17}, {0x62, 50}, {0xdf, 0x202},
    {0xe1, 0}, {0xff, 0}
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
  assert(renders == 2 && !strcmp(title, "Picture") && row_count == 3);
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
}

int main(void) {
  test_navigation();
  test_rail_previews_settings();
  test_shared_picture_settings();
  test_audio_and_display();
  test_settings_and_signal();
  test_vcp_rejection_and_power();
  test_timeouts_and_physical_edges();
  test_callbacks_during_render();
  puts("Control: menu navigation, real setting calls, VCP bounds, power and timeouts pass");
  return 0;
}
