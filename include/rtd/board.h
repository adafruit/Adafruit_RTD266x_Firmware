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
uint8_t board_backlight_available(void);
/* Return zero if the board has no verified firmware-controlled backlight. */
uint8_t board_backlight_set(uint8_t percent);

#endif
