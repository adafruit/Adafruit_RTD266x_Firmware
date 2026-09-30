// SPDX-License-Identifier: MIT
#include "rtd/board.h"
#include "rtd/io.h"

#ifdef __SDCC_mcs51
#include <8052.h>
#else
/* Host register-model tests provide these five 8051 port latches. */
extern volatile uint8_t P1, P3_3, P3_4, P3_5, P3_6;
#endif

void board_init(void) {
  uint8_t i;
  /* Empirical UC-586 pin settings from the preserved stock image at
   * function 0xcc8f, also exercised by the previous SDCC bench build.
   * These are board facts, not a generic RTD2660H pin configuration.
   * FF9F/FFA1/FFA2 route MCLK, SCLK, LRCK and SD0 to the CS4334 DAC.
   * Other GPIO roles beyond the TTL/DDC interface still need a wiring audit.
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

  /* Manual p340: P6 reads must sample the pins, not the output latches.
   * Otherwise a released EEPROM SDA always reads 1 and every ACK is missed.
   * Stock startup also sets this bit. Preserve the other ports' read modes. */
  mcu_update(0xc0, 0x08, 0x08);

  board_backlight_power(1);
}

uint8_t board_buttons(void) {
  /* Stock uses P6.3; physical key sampling is pending bench verification. */
  return 0;
}

uint8_t board_backlight_available(void) { return 0; }

uint8_t board_backlight_set(uint8_t percent) {
  /* PWM1 changes produced no visible brightness change on the UC-586. */
  (void)percent;
  return 0;
}

uint8_t board_backlight_power(uint8_t on) {
  /* Stock's button path toggles P6.4/pin54 (FFCB bit0), configured as
   * push-pull by the existing pin setup. Active-high backlight on/off and
   * wake after signal loss were camera-verified with this firmware.
   * Limor's continuity check confirmed pin54 connects to the boost IC's EN.
   */
  mcu_update(0xcb, 0x01, on ? 1 : 0);
  return 1;
}
