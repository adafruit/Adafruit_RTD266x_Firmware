// SPDX-License-Identifier: MIT
#ifndef RTD_CONTROL_H
#define RTD_CONTROL_H
#include <stdint.h>

enum {
  MENU_CLOSED,
  MENU_MAIN,
  MENU_PICTURE,
  MENU_AUDIO,
  MENU_DISPLAY,
  MENU_SETTINGS,
  MENU_SIGNAL
};
enum {
  SET_BRIGHTNESS,
  SET_CONTRAST,
  SET_BACKLIGHT,
  SET_ASPECT,
  SET_SPLASH,
  SET_POPUP,
  SET_NO_SIGNAL,
  SET_SIGNAL_TIMEOUT,
  SET_MENU_TIMEOUT,
  SET_COUNT
};

void control_init(void);
uint8_t control_get(uint8_t code, uint16_t *maximum, uint16_t *value);
uint8_t control_set(uint8_t code, uint16_t value);
void control_key(uint8_t key);
void control_service(uint32_t now);
uint8_t control_setting(uint8_t setting);
uint8_t control_power(void);
uint8_t control_menu_open(void);
/* Return and clear notification that the application overlay needs restoring.
 */
uint8_t control_overlay_changed(void);
uint32_t control_signal_timeout_ms(void);
#endif
