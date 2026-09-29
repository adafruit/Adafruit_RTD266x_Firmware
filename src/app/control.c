// SPDX-License-Identifier: MIT
#include "rtd/control.h"
#include "rtd/audio.h"
#include "rtd/board.h"
#include "rtd/osd.h"
#include "rtd/platform.h"
#include "rtd/video.h"

/* Session settings deliberately do not write the retained vendor flash tail.
 * Values and controls have one owner, shared by physical keys and DDC/CI. */
static uint8_t settings[SET_COUNT];
static uint8_t page, selection, editing, dirty, overlay_changed, powered;
static uint8_t hide_requested;
static uint8_t previous_keys;
static uint32_t last_key;

static uint8_t item_count(void) {
  switch (page) {
  case MENU_PICTURE:
  case MENU_AUDIO:
  case MENU_SIGNAL:
    return 3;
  default:
    return 5;
  }
}

static uint8_t selected_code(void) {
  switch (page) {
  case MENU_PICTURE:
    return selection == 0 ? 0xe2 : 0x12;
  case MENU_AUDIO:
    return selection == 0 ? 0x62 : 0x8d;
  case MENU_DISPLAY:
    if (selection == 0)
      return 0x10;
    if (selection == 1)
      return 0xe3;
    return 0; /* Rotation and mirror are not qualified on this board. */
  case MENU_SETTINGS:
    if (selection < 2)
      return 0xe4 + selection;
    return selection == 2 ? 0xe8 : 0;
  case MENU_SIGNAL:
    return 0xe6 + selection;
  default:
    return 0;
  }
}

void control_init(void) {
  settings[SET_BRIGHTNESS] = settings[SET_CONTRAST] = 50;
  settings[SET_BACKLIGHT] = 100;
  settings[SET_ASPECT] = 0;
  settings[SET_SPLASH] = RTD_SPLASH ? 1 : 0;
  settings[SET_POPUP] = 1;
  settings[SET_NO_SIGNAL] = 2;
  settings[SET_SIGNAL_TIMEOUT] = 0;
  settings[SET_MENU_TIMEOUT] = 2;
  page = selection = editing = dirty = overlay_changed = previous_keys = 0;
  hide_requested = 0;
  powered = 1;
  video_set_picture(50, 50);
}

uint8_t control_setting(uint8_t setting) {
  return setting < SET_COUNT ? settings[setting] : 0;
}
uint8_t control_power(void) { return powered; }
uint8_t control_menu_open(void) { return page != MENU_CLOSED; }
uint8_t control_overlay_changed(void) {
  uint8_t changed = overlay_changed;
  overlay_changed = 0;
  return changed;
}
uint32_t control_signal_timeout_ms(void) {
  static const uint8_t seconds[] = {0, 1, 2, 5, 10, 20};
  return (uint32_t)seconds[settings[SET_SIGNAL_TIMEOUT]] * 1000;
}

uint8_t control_get(uint8_t code, uint16_t *maximum, uint16_t *value) {
  *maximum = 100;
  switch (code) {
  case 0x10:
    if (!board_backlight_available())
      return 0;
    *value = settings[SET_BACKLIGHT];
    break;
  case 0x12:
    *value = settings[SET_CONTRAST];
    break;
  case 0x8d:
    *maximum = 2;
    *value = audio_get_mute() ? 1 : 2;
    break;
  case 0xd6:
    *maximum = 4;
    *value = powered ? 1 : 4;
    break;
  case 0xdf:
    *maximum = 0x202;
    *value = 0x202;
    break;
  case 0xe0:
    *maximum = 16;
    *value = 0;
    break;
  case 0xe1:
    *maximum = 0xffff;
    *value = ((uint16_t)page << 8) | (selection << 1) | editing;
    break;
  case 0xe2:
    *value = settings[SET_BRIGHTNESS];
    break;
  case 0xe3:
  case 0xe4:
  case 0xe5:
    *maximum = 1;
    *value = settings[SET_ASPECT + code - 0xe3];
    break;
  case 0xe6:
    *maximum = 2;
    *value = settings[SET_NO_SIGNAL];
    break;
  case 0xe7:
    *maximum = 5;
    *value = settings[SET_SIGNAL_TIMEOUT];
    break;
  case 0xe8:
    *maximum = 3;
    *value = settings[SET_MENU_TIMEOUT];
    break;
  default:
    return 0;
  }
  return 1;
}

uint8_t control_set(uint8_t code, uint16_t value) {
  uint16_t maximum, old;
  if (!control_get(code, &maximum, &old) || value > maximum)
    return 0;
  switch (code) {
  case 0x10:
    if (!board_backlight_set(powered ? (uint8_t)value : 0))
      return 0;
    settings[SET_BACKLIGHT] = value;
    break;
  case 0x12:
    settings[SET_CONTRAST] = value;
    break;
  case 0x8d:
    if (value != 1 && value != 2)
      return 0;
    audio_set_mute(value == 1);
    break;
  case 0xd6:
    if (value != 1 && value != 4)
      return 0;
    powered = value == 1;
    page = selection = editing = 0;
    overlay_changed = 1;
    if (!powered) {
      audio_stop();
      hide_requested = 1;
    }
    board_backlight_power(powered);
    if (powered) board_backlight_set(settings[SET_BACKLIGHT]);
    break;
  case 0xe0:
    if (value != 1 && value != 2 && value != 4 && value != 8 && value != 16)
      return 0;
    control_key(value);
    return 1;
  case 0xe2:
    settings[SET_BRIGHTNESS] = value;
    break;
  case 0xe3:
    settings[SET_ASPECT] = value;
    video_set_aspect(value);
    break;
  case 0xe4:
  case 0xe5:
    settings[SET_ASPECT + code - 0xe3] = value;
    if (code == 0xe5 && !value)
      overlay_changed = 1;
    break;
  case 0xe6:
    settings[SET_NO_SIGNAL] = value;
    overlay_changed = 1;
    break;
  case 0xe7:
    settings[SET_SIGNAL_TIMEOUT] = value;
    overlay_changed = 1;
    break;
  case 0xe8:
    settings[SET_MENU_TIMEOUT] = value;
    break;
  default:
    return 0;
  }
  if (code == 0x12 || code == 0xe2)
    video_set_picture(settings[SET_BRIGHTNESS], settings[SET_CONTRAST]);
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
    if (editing)
      editing = 0;
    else if (page == MENU_SIGNAL) {
      page = MENU_SETTINGS;
      selection = 3;
    } else if (page != MENU_MAIN) {
      page = MENU_MAIN;
      selection = 0;
    } else {
      page = 0;
      overlay_changed = 1;
      hide_requested = 1;
    }
  } else if (key == BOARD_KEY_MENU) {
    if (selection == count - 1) {
      editing = 0;
      if (page == MENU_MAIN) {
        page = 0;
        overlay_changed = 1;
        hide_requested = 1;
      } else if (page == MENU_SIGNAL) {
        page = MENU_SETTINGS;
        selection = 3;
      } else {
        page = MENU_MAIN;
        selection = 0;
      }
    } else if (page == MENU_MAIN) {
      page = MENU_PICTURE + selection;
      selection = 0;
    } else if (page == MENU_SETTINGS && selection == 3) {
      page = MENU_SIGNAL;
      selection = 0;
    } else if (control_get(selected_code(), &maximum, &value)) {
      editing = !editing;
    }
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
  osd_menu_row(label, choice, (uint8_t)value, maximum == 100,
               selection == index, available);
}

static void render(void) {
  const char *title;
  switch (page) {
  case MENU_PICTURE:
    title = "PICTURE";
    break;
  case MENU_AUDIO:
    title = "AUDIO";
    break;
  case MENU_DISPLAY:
    title = "DISPLAY";
    break;
  case MENU_SETTINGS:
    title = "MENU SETTINGS";
    break;
  case MENU_SIGNAL:
    title = "NO SIGNAL";
    break;
  default:
    title = "ADAFRUIT MENU";
    break;
  }
  osd_menu_begin(title);
  switch (page) {
  case MENU_MAIN:
    row("PICTURE", "", 0, 0);
    row("AUDIO", "", 0, 1);
    row("DISPLAY", "", 0, 2);
    row("MENU SETTINGS", "", 0, 3);
    row("EXIT", "", 0, 4);
    break;
  case MENU_PICTURE:
    row("IMAGE BRIGHTNESS", 0, 0xe2, 0);
    row("CONTRAST", 0, 0x12, 1);
    row("BACK", "", 0, 2);
    break;
  case MENU_AUDIO:
    row("VOLUME", 0, 0x62, 0);
    row("MUTE", audio_get_mute() ? "ON" : "OFF", 0x8d, 1);
    row("BACK", "", 0, 2);
    break;
  case MENU_DISPLAY:
    row("LED BACKLIGHT", 0, 0x10, 0);
    row("ASPECT", settings[SET_ASPECT] ? "FILL" : "KEEP", 0xe3, 1);
    row("ROTATION", 0, 0, 2);
    row("MIRROR", 0, 0, 3);
    row("BACK", "", 0, 4);
    break;
  case MENU_SETTINGS:
    row("STARTUP SPLASH", settings[SET_SPLASH] ? "ON" : "OFF", 0xe4, 0);
    row("CONNECTION POPUP", settings[SET_POPUP] ? "ON" : "OFF", 0xe5, 1);
    row("MENU TIMEOUT",
        settings[SET_MENU_TIMEOUT] == 0   ? "NEVER"
        : settings[SET_MENU_TIMEOUT] == 1 ? "5S"
        : settings[SET_MENU_TIMEOUT] == 2 ? "10S"
                                          : "20S",
        0xe8, 2);
    row("NO SIGNAL", "", 0, 3);
    row("BACK", "", 0, 4);
    break;
  case MENU_SIGNAL:
    row("BACKGROUND",
        settings[SET_NO_SIGNAL] == 0   ? "BLACK"
        : settings[SET_NO_SIGNAL] == 1 ? "BLUE"
                                       : "TEST",
        0xe6, 0);
    row("SLEEP AFTER",
        settings[SET_SIGNAL_TIMEOUT] == 0   ? "NEVER"
        : settings[SET_SIGNAL_TIMEOUT] == 1 ? "1S"
        : settings[SET_SIGNAL_TIMEOUT] == 2 ? "2S"
        : settings[SET_SIGNAL_TIMEOUT] == 3 ? "5S"
        : settings[SET_SIGNAL_TIMEOUT] == 4 ? "10S"
                                            : "20S",
        0xe7, 1);
    row("BACK", "", 0, 2);
    break;
  }
  osd_menu_end(editing ? "ADJUST +/-  MENU TO FINISH"
                       : "MENU SELECT  BACK RETURN");
}

void control_service(uint32_t now) {
  static const uint8_t seconds[] = {0, 5, 10, 20};
  uint8_t keys = board_buttons(), pressed = keys & (uint8_t)~previous_keys;
  previous_keys = keys;
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
