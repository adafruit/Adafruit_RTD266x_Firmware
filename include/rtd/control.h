// SPDX-License-Identifier: MIT
#ifndef RTD_CONTROL_H
#define RTD_CONTROL_H
#include <stdint.h>

#ifndef RTD_SETTINGS
#define RTD_SETTINGS 1
#endif
#ifndef RTD_EEPROM_DIAGNOSTICS
#define RTD_EEPROM_DIAGNOSTICS 0
#endif

enum {
  MENU_CLOSED,
  MENU_MAIN,
  MENU_PICTURE,
  MENU_AUDIO,
  MENU_DISPLAY,
  MENU_SETTINGS,
  MENU_SIGNAL,
  MENU_COLOR,
  MENU_OSD,
  MENU_SYSTEM,
  MENU_RESET
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
  SET_VOLUME,
  SET_MUTE,
  SET_RED,
  SET_GREEN,
  SET_BLUE,
  SET_SATURATION,
  SET_SHARPNESS,
  SET_OSD_X,
  SET_OSD_Y,
  SET_OSD_ALPHA,
  SET_SLEEP_MINUTES,
  SET_LANGUAGE, /* Reserved at English=0 until translated glyphs are supported. */
  SET_COUNT
};

void control_init(void);
uint8_t control_get(uint8_t code, uint16_t *maximum, uint16_t *value);
uint8_t control_set(uint8_t code, uint16_t value);
void control_key(uint8_t key);
void control_service(uint32_t now);
uint8_t control_setting(uint8_t setting);
uint8_t control_power(void);
/* Return and clear soft-power transitions and wake requests, including off then
 * on between monitor polls. An on request also wakes no-signal backlight sleep
 * when soft power was already on; repeated off requests do not set the flag. */
uint8_t control_power_changed(void);
/* Burn-in is a transient panel color test; reset or power off cancels it. */
uint8_t control_burn_in(void);
uint8_t control_menu_open(void);
/* Return and clear notification that the application overlay needs restoring.
 */
uint8_t control_overlay_changed(void);
uint32_t control_signal_timeout_ms(void);
#endif
