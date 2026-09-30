// SPDX-License-Identifier: MIT
#include "rtd/control.h"
#include "rtd/audio.h"
#include "rtd/board.h"
#include "rtd/osd.h"
#include "rtd/platform.h"
#include "rtd/video.h"
#include "rtd/storage.h"

/* Values and controls have one owner, shared by physical keys and DDC/CI.
 * Persistent preferences use the board EEPROM, never the program flash. */
static uint8_t settings[SET_COUNT];
static uint8_t page, selection, editing, dirty, overlay_changed, powered;
static uint8_t power_changed;
static uint8_t hide_requested;
static uint8_t previous_keys;
static uint32_t last_key;
static uint32_t sleep_started;
static uint8_t burn_in;
static uint8_t render_first;

#ifdef __SDCC_mcs51
#define CONTROL_CODE __code
#else
#define CONTROL_CODE
#endif

/* The first eleven settings retain the v39 EEPROM layout. */
static const CONTROL_CODE uint8_t setting_codes[SET_COUNT] = {
  0xe2, 0x12, 0x10, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0x62, 0x8d,
  0x16, 0x18, 0x1a, 0x8a, 0x87, 0xf0, 0xf1, 0xf2, 0xf3, 0xf5
};
static const CONTROL_CODE uint8_t setting_maximum[SET_COUNT] = {
  100, 100, 100, RTD_ASPECT_16_9 ? 3 : RTD_ASPECT_4_3 ? 2 : 1, 1, 1, 2, 9, 3, 100, 1,
  100, 100, 100, 100, 100, 100, 100, 100, 120, 0
};
static const CONTROL_CODE uint8_t setting_defaults[SET_COUNT] = {
  50, 50, 100, 0, RTD_SPLASH ? 1 : 0, 1, 2, 0, 2, 100, 0,
  50, 50, 50, 50, 50, 50, 50, 0, 0, 0
};
static const CONTROL_CODE uint8_t signal_seconds[] = {
  0, 1, 2, 5, 10, 20, 30, 40, 50, 60
};
#if RTD_SETTINGS
static uint8_t save_pending, saving;
static uint32_t changed_at;
#endif
#if RTD_EEPROM_DIAGNOSTICS
static uint16_t eeprom_address;
#endif

static void defaults(void) {
  uint8_t i;
  for (i = 0; i < SET_COUNT; ++i) settings[i] = setting_defaults[i];
}

#if RTD_SETTINGS
static uint8_t settings_valid(void) {
  uint8_t i;
  for (i = 0; i < SET_COUNT; ++i)
    /* Keep known preferences when changing experimental build flags. The
     * video driver falls back to Keep for modes this build cannot apply. */
    if (settings[i] > (i == SET_ASPECT ? VIDEO_ASPECT_16_9 : setting_maximum[i]))
      return 0;
  return 1;
}
#endif

static void apply_picture(void) {
  video_set_picture(settings[SET_BRIGHTNESS], settings[SET_CONTRAST]);
  video_set_color(settings[SET_RED], settings[SET_GREEN], settings[SET_BLUE],
                  settings[SET_SATURATION]);
}

static void apply_settings(void) {
  apply_picture();
  video_set_sharpness(settings[SET_SHARPNESS]);
  video_set_aspect(settings[SET_ASPECT]);
  audio_set_mute(settings[SET_MUTE]);
  audio_set_volume(settings[SET_VOLUME]);
  osd_set_menu_style(settings[SET_OSD_X], settings[SET_OSD_Y],
                     settings[SET_OSD_ALPHA]);
}

static uint8_t item_count(void) {
  switch (page) {
  case MENU_MAIN: case MENU_SYSTEM: case MENU_OSD: return 4;
  case MENU_AUDIO: case MENU_SIGNAL: return 3;
  case MENU_SETTINGS: return 7;
  case MENU_RESET: return 2;
  default: return 5;
  }
}

static uint8_t selected_code(void) {
  switch (page) {
  case MENU_PICTURE:
    return selection == 0 ? 0xe2 : selection == 1 ? 0x12 :
           selection == 3 ? 0x87 : 0;
  case MENU_COLOR:
    return selection < 3 ? 0x16 + 2 * selection : 0x8a;
  case MENU_AUDIO: return selection == 0 ? 0x62 : 0x8d;
  case MENU_DISPLAY:
    return selection == 0 ? 0x10 : selection == 1 ? 0xe3 : 0;
  case MENU_SETTINGS:
    return selection < 2 ? 0xe4 + selection : selection == 2 ? 0xe8 : 0;
  case MENU_SIGNAL: return 0xe6 + selection;
  case MENU_OSD: return selection < 3 ? 0xf0 + selection : 0;
  case MENU_SYSTEM: return selection < 2 ? 0xf3 + selection : 0;
  default: return 0;
  }
}

/* Return to the exact row that opened each nested page. */
static void back(void) {
  if (page == MENU_COLOR) { page = MENU_PICTURE; selection = 2; }
  else if (page == MENU_SIGNAL) { page = MENU_SETTINGS; selection = 3; }
  else if (page == MENU_OSD) { page = MENU_SETTINGS; selection = 4; }
  else if (page == MENU_SYSTEM) { page = MENU_SETTINGS; selection = 5; }
  else if (page == MENU_RESET) { page = MENU_SYSTEM; selection = 2; }
  else if (page != MENU_MAIN) {
    selection = page - MENU_PICTURE; page = MENU_MAIN;
  } else {
    page = selection = 0; overlay_changed = hide_requested = 1;
  }
  editing = 0;
}

void control_init(void) {
  defaults();
#if RTD_SETTINGS
  if (!store_load_compatible(settings, SET_COUNT, 11) || !settings_valid()) defaults();
  save_pending = saving = 0;
  changed_at = 0;
#endif
  page = selection = editing = dirty = overlay_changed = previous_keys = 0;
  hide_requested = 0;
  powered = 1;
  power_changed = 0;
  burn_in = 0;
  sleep_started = platform_millis();
  apply_settings();
}

uint8_t control_setting(uint8_t setting) {
  return setting < SET_COUNT ? settings[setting] : 0;
}
uint8_t control_power(void) { return powered; }
uint8_t control_power_changed(void) {
  uint8_t changed = power_changed;
  power_changed = 0;
  return changed;
}
uint8_t control_burn_in(void) { return burn_in; }
uint8_t control_menu_open(void) { return page != MENU_CLOSED; }
uint8_t control_overlay_changed(void) {
  uint8_t changed = overlay_changed;
  overlay_changed = 0;
  return changed;
}
uint32_t control_signal_timeout_ms(void) {
  return (uint32_t)signal_seconds[settings[SET_SIGNAL_TIMEOUT]] * 1000;
}

static uint8_t setting_index(uint8_t code) {
  uint8_t i;
  for (i = 0; i < SET_COUNT; ++i)
    if (setting_codes[i] == code) return i;
  return SET_COUNT;
}

uint8_t control_get(uint8_t code, uint16_t *maximum, uint16_t *value) {
  uint8_t index = setting_index(code);
  if (index < SET_COUNT) {
    if (index == SET_BACKLIGHT && !board_backlight_available()) return 0;
    if (index == SET_VOLUME && !audio_volume_available()) return 0;
    *maximum = setting_maximum[index];
    *value = index == SET_ASPECT ? video_aspect_current() : settings[index];
    if (index == SET_MUTE) { *maximum = 2; *value = settings[index] ? 1 : 2; }
    return 1;
  }
  switch (code) {
  case 0x04: *maximum = 1; *value = 0; break;
  case 0xd6: *maximum = 4; *value = powered ? 1 : 4; break;
  case 0xdf: *maximum = *value = 0x202; break;
  case 0xe0: *maximum = 16; *value = 0; break;
  case 0xe1:
    *maximum = 0xffff;
    *value = ((uint16_t)page << 8) | (selection << 1) | editing;
    break;
  case 0xf4: *maximum = 1; *value = burn_in; break;
  case 0xeb:
    *maximum = 0x103;
#if RTD_SETTINGS
    *value = store_status() | ((save_pending || saving) ? 0x100 : 0);
#else
    *value = STORE_UNAVAILABLE;
#endif
    break;
#if RTD_EEPROM_DIAGNOSTICS
  case 0xec: *maximum = 0xffff; *value = board_eeprom_diagnostic(); break;
  case 0xe9: *maximum = 2046; *value = eeprom_address; break;
  case 0xea: {
    uint8_t bytes[2];
    if (!board_eeprom_read(eeprom_address, bytes, 2)) return 0;
    *maximum = 0xffff;
    *value = ((uint16_t)bytes[0] << 8) | bytes[1];
    eeprom_address = eeprom_address < 2045 ? eeprom_address + 2 : 0;
    break;
  }
#endif
  default: return 0;
  }
  return 1;
}

static void queue_save(void) {
#if RTD_SETTINGS
  save_pending = 1;
  changed_at = platform_millis();
#endif
}

uint8_t control_set(uint8_t code, uint16_t value) {
  uint16_t maximum, old;
  uint8_t index = setting_index(code);
  if (!control_get(code, &maximum, &old) || value > maximum) return 0;
  if (index < SET_COUNT) {
    if (index == SET_ASPECT) {
      if (!video_aspect_available((uint8_t)value)) return 0;
      old = settings[index]; /* Effective fallback can differ from preference. */
    }
    if (index == SET_LANGUAGE) return 0; /* English only for this release. */
    if (index == SET_BACKLIGHT &&
        !board_backlight_set(powered ? (uint8_t)value : 0)) return 0;
    if (index == SET_VOLUME && !audio_set_volume((uint8_t)value)) return 0;
    if (index == SET_MUTE) {
      if (value != 1 && value != 2) return 0;
      settings[index] = value == 1;
      audio_set_mute(settings[index]);
    } else settings[index] = (uint8_t)value;
    if (index == SET_BRIGHTNESS || index == SET_CONTRAST ||
        (index >= SET_RED && index <= SET_SATURATION)) apply_picture();
    if (index == SET_SHARPNESS) video_set_sharpness((uint8_t)value);
    if (index == SET_ASPECT) video_set_aspect((uint8_t)value);
    if (index >= SET_OSD_X && index <= SET_OSD_ALPHA)
      osd_set_menu_style(settings[SET_OSD_X], settings[SET_OSD_Y],
                         settings[SET_OSD_ALPHA]);
    if (index == SET_SLEEP_MINUTES) sleep_started = platform_millis();
    if (index == SET_POPUP || index == SET_NO_SIGNAL || index == SET_SIGNAL_TIMEOUT)
      overlay_changed = 1;
    if (value != old) queue_save();
  } else switch (code) {
  case 0x04:
    if (value != 1) return 0;
    defaults();
    burn_in = 0;
    sleep_started = platform_millis();
    apply_settings();
    if (powered) board_backlight_set(settings[SET_BACKLIGHT]);
    overlay_changed = 1;
    queue_save();
    break;
  case 0xd6:
    if (value != 1 && value != 4) return 0;
    if (powered != (value == 1) || value == 1) power_changed = 1;
    powered = value == 1;
    burn_in = 0;
    sleep_started = platform_millis();
    page = selection = editing = 0;
    overlay_changed = 1;
    if (!powered) { audio_stop(); hide_requested = 1; }
    board_backlight_power(powered);
    if (powered) board_backlight_set(settings[SET_BACKLIGHT]);
    break;
  case 0xe0:
    if (value != 1 && value != 2 && value != 4 && value != 8 && value != 16) return 0;
    control_key((uint8_t)value);
    return 1;
  case 0xf4:
    burn_in = (uint8_t)value;
    overlay_changed = 1;
    break;
#if RTD_EEPROM_DIAGNOSTICS
  case 0xe9: eeprom_address = value; return 1;
#endif
  default: return 0;
  }
  dirty = page != 0;
  last_key = platform_millis();
  return 1;
}

void control_key(uint8_t key) {
  uint8_t count = item_count(), code, step;
  uint16_t maximum, value;
  last_key = platform_millis();
  if (key == BOARD_KEY_POWER) {
    control_set(0xd6, powered ? 4 : 1);
    return;
  }
  if (!powered)
    return;
  if (!page) {
    if (key == BOARD_KEY_MENU) {
      page = MENU_MAIN;
      selection = 0;
      dirty = 1;
    }
    return;
  }
  dirty = 1;
  if (key == BOARD_KEY_BACK) {
    if (editing) editing = 0;
    else back();
  } else if (key == BOARD_KEY_MENU) {
    if (page == MENU_MAIN) { page = MENU_PICTURE + selection; selection = 0; }
    else if (page == MENU_RESET) {
      if (selection == 1) control_set(0x04, 1);
      back();
    } else if (selection == count - 1) back();
    else if (page == MENU_PICTURE && selection == 2) { page = MENU_COLOR; selection = 0; }
    else if (page == MENU_SETTINGS && selection >= 3) {
      page = selection == 3 ? MENU_SIGNAL : selection == 4 ? MENU_OSD : MENU_SYSTEM;
      selection = 0;
    } else if (page == MENU_SYSTEM && selection == 2) { page = MENU_RESET; selection = 0; }
    else if (control_get(selected_code(), &maximum, &value)) editing = !editing;
  } else if (key == BOARD_KEY_INCREASE || key == BOARD_KEY_DECREASE) {
    if (editing) {
      code = selected_code();
      if (control_get(code, &maximum, &value)) {
        step = maximum == 100 ? 5 : 1;
        if (code == 0x8d)
          value = key == BOARD_KEY_INCREASE ? 1 : 2;
        else if (key == BOARD_KEY_INCREASE)
          value = value + step > maximum ? maximum : value + step;
        else
          value = value < step ? 0 : value - step;
        if (code == 0xe3) {
          while (!video_aspect_available((uint8_t)value)) {
            if (key == BOARD_KEY_INCREASE && value < maximum) ++value;
            else if (key == BOARD_KEY_DECREASE && value) --value;
            else break;
          }
        }
        control_set(code, value);
      }
    } else if (key == BOARD_KEY_DECREASE)
      selection = (selection + 1) % count;
    else
      selection = selection ? selection - 1 : count - 1;
  }
}

static void row(const char *label, const char *choice, uint8_t code,
                uint8_t index) {
  uint16_t maximum = 0, value = 0;
  uint8_t available = code ? control_get(code, &maximum, &value) : choice != 0;
  if (index < render_first || index >= render_first + 5) return;
  osd_menu_row(label, choice, (uint8_t)value, maximum == 100,
               page != MENU_MAIN && selection == index, available);
}

static void render(void) {
  const char *title;
  uint8_t shown_page = page == MENU_MAIN ? MENU_PICTURE + selection : page;
  uint8_t tab = shown_page == MENU_COLOR ? 0 : shown_page >= MENU_SETTINGS ? 3 : shown_page - MENU_PICTURE;
  /* DDC can change focus while glyphs upload. Keep one viewport for this draw;
   * the queued redraw will show the new selection without duplicating rows. */
  render_first = page == MENU_SETTINGS && selection >= 5 ? 2 : 0;
  switch (shown_page) {
  case MENU_PICTURE:
    title = "Picture";
    break;
  case MENU_AUDIO:
    title = "Audio";
    break;
  case MENU_DISPLAY:
    title = "Display";
    break;
  case MENU_SETTINGS:
    title = "Menu settings";
    break;
  case MENU_SIGNAL: title = "No signal"; break;
  case MENU_COLOR: title = "Color"; break;
  case MENU_OSD: title = "OSD setup"; break;
  case MENU_SYSTEM: title = "System"; break;
  case MENU_RESET: title = "Reset settings?"; break;
  default:
    title = "Picture";
    break;
  }
  osd_menu_begin(title, tab, page == MENU_MAIN);
  switch (shown_page) {
  case MENU_PICTURE:
    row("Image brightness", 0, 0xe2, 0);
    row("Contrast", 0, 0x12, 1);
    row("Color", "", 0, 2);
    row("H sharpness", 0, 0x87, 3);
    row("Back", "", 0, 4);
    break;
  case MENU_COLOR:
    row("Red gain", 0, 0x16, 0);
    row("Green gain", 0, 0x18, 1);
    row("Blue gain", 0, 0x1a, 2);
    row("Saturation", 0, 0x8a, 3);
    row("Back", "", 0, 4);
    break;
  case MENU_AUDIO:
    row("Volume", 0, 0x62, 0);
    row("Mute", audio_get_mute() ? "On" : "Off", 0x8d, 1);
    row("Back", "", 0, 2);
    break;
  case MENU_DISPLAY:
    row("LED backlight", 0, 0x10, 0);
    row("Aspect", video_aspect_current() == VIDEO_ASPECT_FILL ? "Fill" :
        video_aspect_current() == VIDEO_ASPECT_4_3 ? "4:3" :
        video_aspect_current() == VIDEO_ASPECT_16_9 ? "16:9" : "Keep", 0xe3, 1);
    row("Rotation", 0, 0, 2);
    row("Mirror", 0, 0, 3);
    row("Back", "", 0, 4);
    break;
  case MENU_SETTINGS:
    row("Startup splash", settings[SET_SPLASH] ? "On" : "Off", 0xe4, 0);
    row("Connection popup", settings[SET_POPUP] ? "On" : "Off", 0xe5, 1);
    row("Menu timeout",
        settings[SET_MENU_TIMEOUT] == 0   ? "Never"
        : settings[SET_MENU_TIMEOUT] == 1 ? "5s"
        : settings[SET_MENU_TIMEOUT] == 2 ? "10s"
                                          : "20s",
        0xe8, 2);
    row("No signal", "", 0, 3);
    row("OSD setup", "", 0, 4);
    row("System", "", 0, 5);
    row("Back", "", 0, 6);
    break;
  case MENU_SIGNAL:
    row("Background",
        settings[SET_NO_SIGNAL] == 0   ? "Black"
        : settings[SET_NO_SIGNAL] == 1 ? "Blue"
                                       : "Test",
        0xe6, 0);
    row("Sleep after",
        settings[SET_SIGNAL_TIMEOUT] == 0   ? "Never"
        : settings[SET_SIGNAL_TIMEOUT] == 1 ? "1s"
        : settings[SET_SIGNAL_TIMEOUT] == 2 ? "2s"
        : settings[SET_SIGNAL_TIMEOUT] == 3 ? "5s"
        : settings[SET_SIGNAL_TIMEOUT] == 4 ? "10s"
        : settings[SET_SIGNAL_TIMEOUT] == 5 ? "20s"
        : settings[SET_SIGNAL_TIMEOUT] == 6 ? "30s"
        : settings[SET_SIGNAL_TIMEOUT] == 7 ? "40s"
        : settings[SET_SIGNAL_TIMEOUT] == 8 ? "50s" : "60s",
        0xe7, 1);
    row("Back", "", 0, 2);
    break;
  case MENU_OSD:
    row("H position", 0, 0xf0, 0);
    row("V position", 0, 0xf1, 1);
    row("Transparency", 0, 0xf2, 2);
    row("Back", "", 0, 3);
    break;
  case MENU_SYSTEM:
    row("Sleep (min)", settings[SET_SLEEP_MINUTES] ? 0 : "Off", 0xf3, 0);
    row("Burn-in", burn_in ? "On" : "Off", 0xf4, 1);
    row("Factory reset", "", 0, 2);
    row("Back", "", 0, 3);
    break;
  case MENU_RESET:
    row("Cancel", "", 0, 0);
    row("Reset", "", 0, 1);
    break;
  }
  osd_menu_end(page == MENU_MAIN ? "Menu: open  Back: exit"
               : editing ? "Adjust +/-  Menu: done"
                         : page == MENU_SETTINGS ? "More: +/- Back: tabs"
                         : "Menu: edit  Back: tabs");
}

void control_service(uint32_t now) {
  static const uint8_t seconds[] = {0, 5, 10, 20};
  uint8_t keys = board_buttons(), pressed = keys & (uint8_t)~previous_keys;
  previous_keys = keys;
#if RTD_SETTINGS
  /* Coalesce adjustments, and never write from a DDC callback while drawing.
   * A failed save is reported; another setting change permits a new attempt. */
  if (!saving && save_pending && (uint32_t)(now - changed_at) >= 2000) {
    save_pending = 0;
    saving = 1;
    store_save(settings, SET_COUNT);
    saving = 0;
    now = platform_millis();
  }
#endif
  if (powered && settings[SET_SLEEP_MINUTES] &&
      (uint32_t)(now - sleep_started) >= (uint32_t)settings[SET_SLEEP_MINUTES] * 60000UL)
    control_set(0xd6, 4);
  /* Board drivers debounce physical keys. Virtual keys are already events. */
  if (pressed) {
    control_key(pressed);
    now = platform_millis();
  }
  if (page && seconds[settings[SET_MENU_TIMEOUT]] &&
      (uint32_t)(now - last_key) >=
          (uint32_t)seconds[settings[SET_MENU_TIMEOUT]] * 1000) {
    page = selection = editing = 0;
    overlay_changed = 1;
    hide_requested = 1;
  }
  /* DDC is polled during drawing. Its callbacks must never alter the OSD
   * address port, and a key received during this render needs another redraw.
   */
  if (hide_requested) { hide_requested = 0; osd_hide(); }
  if (dirty && page) {
    dirty = 0;
    render();
  }
}
