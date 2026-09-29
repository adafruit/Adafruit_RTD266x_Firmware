// SPDX-License-Identifier: MIT
#ifndef RTD_BOARD_H
#define RTD_BOARD_H

#include <stdint.h>

#include "board_config.h"

enum {
  BOARD_KEY_MENU = 1,
  BOARD_KEY_BACK = 2,
  BOARD_KEY_INCREASE = 4,
  BOARD_KEY_DECREASE = 8,
  BOARD_KEY_POWER = 16
};

void board_init(void);
/* Logical key bitmap; hardware decoding belongs to the board implementation. */
uint8_t board_buttons(void);
/* Whether this board implements adjustable backlight brightness. */
uint8_t board_backlight_available(void);
/* 0..100%; return zero for unsupported hardware or an out-of-range value. */
uint8_t board_backlight_set(uint8_t percent);
/* Separate on/off gate; return zero if unavailable. The UC586 P6.4 gate
 * is bench-verified; adjustable brightness remains unavailable.
 */
uint8_t board_backlight_power(uint8_t on);

#endif
