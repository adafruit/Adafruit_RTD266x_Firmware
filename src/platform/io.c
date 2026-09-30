// SPDX-License-Identifier: MIT
#include "rtd/io.h"

/* RTD2660 manual pp.353-354: memory-mapped scaler address/data gateway.
 * Only foreground code accesses it; interrupts must not use this driver.
 */
uint8_t mcu_read(uint8_t reg) {
  return *((volatile __xdata uint8_t *)(0xff00u + reg));
}

void mcu_write(uint8_t reg, uint8_t value) {
  *((volatile __xdata uint8_t *)(0xff00u + reg)) = value;
}

void mcu_update(uint8_t reg, uint8_t mask, uint8_t value) {
  mcu_write(reg, (mcu_read(reg) & (uint8_t)~mask) | (value & mask));
}

static void select_register(uint8_t page, uint8_t reg) {
  /* Disable gateway auto-increment: each operation explicitly names a byte. */
  mcu_write(0xf3, 0x20);
  mcu_write(0xf4, 0x9f);
  mcu_write(0xf5, page);
  mcu_write(0xf4, reg);
}

uint8_t rtd_read(uint8_t page, uint8_t reg) {
  select_register(page, reg);
  return mcu_read(0xf5);
}

void rtd_write(uint8_t page, uint8_t reg, uint8_t value) {
  select_register(page, reg);
  mcu_write(0xf5, value);
}

void rtd_write_bytes(uint8_t page, uint8_t reg, const uint8_t *values,
                     uint8_t count) {
  if (!count) return;
  select_register(page, reg);
  do {
    mcu_write(0xf5, *values++);
  } while (--count);
}

void rtd_update(uint8_t page, uint8_t reg, uint8_t mask, uint8_t value) {
  rtd_write(page, reg, (rtd_read(page, reg) & (uint8_t)~mask) | (value & mask));
}

uint8_t rtd_indirect_read(uint8_t page, uint8_t address_reg, uint8_t index) {
  rtd_write(page, address_reg, index);
  return rtd_read(page, address_reg + 1);
}

void rtd_indirect_write(uint8_t page, uint8_t address_reg, uint8_t index,
                        uint8_t value) {
  rtd_write(page, address_reg, index);
  rtd_write(page, address_reg + 1, value);
}

void rtd_indirect_update(uint8_t page, uint8_t address_reg, uint8_t index,
                         uint8_t mask, uint8_t value) {
  uint8_t previous = rtd_indirect_read(page, address_reg, index);
  rtd_indirect_write(page, address_reg, index,
                     (previous & (uint8_t)~mask) | (value & mask));
}
