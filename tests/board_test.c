// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rtd/board.h"
#include "rtd/io.h"

volatile uint8_t P1, P3_3, P3_4, P3_5, P3_6;
static uint8_t registers[256], last_reg;
static unsigned writes;

uint8_t mcu_read(uint8_t reg) { return registers[reg]; }

void mcu_write(uint8_t reg, uint8_t value) {
  registers[reg] = value;
  last_reg = reg;
  ++writes;
}

void mcu_update(uint8_t reg, uint8_t mask, uint8_t value) {
  mcu_write(reg, (mcu_read(reg) & (uint8_t)~mask) | (value & mask));
}

int main(void) {
  unsigned value, before;
  memset(registers, 0xa5, sizeof(registers));
  registers[0xcb] = 0xa4;
  P1 = 0xff;
  board_init();
  assert(!board_backlight_available());
  assert(P1 == 0x7f && P3_3 == 0 && P3_5 == 0 && P3_4 == 1 && P3_6 == 1);
  assert(registers[0x9c] == 0xa4); /* Existing pin54 push-pull mux. */
  assert(registers[0xa0] == 0x32); /* Other stock pin selections stay intact. */
  assert(registers[0xc0] == 0xad); /* P6 samples pins; other read modes retained. */
  assert(registers[0xcb] == 0xa5); /* Default on, preserving all other bits. */
  assert(registers[0x3a] == 0xa5 && registers[0x3b] == 0xa5);
  for (value = 0x46; value <= 0x4c; ++value)
    assert(registers[value] == 0xa5); /* No PWM setup or duty writes. */
  before = writes;
  for (value = 0; value <= 255; ++value)
    assert(!board_backlight_set((uint8_t)value));
  assert(writes == before);
  for (value = 0; value <= 255; ++value) {
    registers[0xcb] = (uint8_t)value;
    before = writes;
    assert(board_backlight_power(0));
    assert(registers[0xcb] == (value & 0xfe));
    assert(writes == before + 1 && last_reg == 0xcb);
    assert(board_backlight_power(1));
    assert(registers[0xcb] == (value | 1));
    assert(writes == before + 2 && last_reg == 0xcb);
  }
  for (value = 0; value <= 255; ++value) {
    registers[0xc0] = (uint8_t)value;
    board_init();
    assert(registers[0xc0] == (value | 0x08));
    assert(registers[0x9a] == 0x05); /* EEPROM SCL/SDA remain open drain. */
  }
  puts("UC586 gate bit preservation, default on and unsupported brightness passed");
  return 0;
}
