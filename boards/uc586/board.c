// SPDX-License-Identifier: MIT
#include <8052.h>
#include "rtd/board.h"
#include "rtd/io.h"

void board_init(void) {
  uint8_t i;
  /* Empirical UC-586 pin settings from the preserved stock image at
   * function 0xcc8f, also exercised by the previous SDCC bench build.
   * These are board facts, not a generic RTD2660H pin configuration.
   * GPIO roles beyond the TTL/DDC interface still need a wiring audit.
   */
  static const uint8_t pin_modes[] = {
    0x8a, 0x92, 0x02, 0x05, 0x20, 0xa4, 0x1b,
    0x01, 0x1c, 0x32, 0x55, 0x51, 0x61, 0x00
  };
  P1 &= (uint8_t)~0x80;
  /* Set only the four known board signals, leaving other latches alone. */
  P3_3 = 0;
  P3_5 = 0;
  P3_4 = 1;
  P3_6 = 1;
  mcu_write(0xc9, 1);
  mcu_update(0x96, 7, 2);
  for (i = 0; i < sizeof(pin_modes); ++i) mcu_write(0x97 + i, pin_modes[i]);
}

uint8_t board_buttons(void) {
  /* Stock uses P6.3; physical key sampling is pending bench verification. */
  return 0;
}

uint8_t board_backlight_available(void) { return 0; }

uint8_t board_backlight_set(uint8_t percent) {
  (void)percent;
  return 0;
}
