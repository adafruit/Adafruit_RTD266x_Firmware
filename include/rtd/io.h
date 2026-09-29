// SPDX-License-Identifier: MIT
#ifndef RTD_IO_H
#define RTD_IO_H

#include <stdint.h>

/* All scaler transactions name their page. No caller-owned page state. */
uint8_t rtd_read(uint8_t page, uint8_t reg);
void rtd_write(uint8_t page, uint8_t reg, uint8_t value);
void rtd_update(uint8_t page, uint8_t reg, uint8_t mask, uint8_t value);
uint8_t rtd_indirect_read(uint8_t page, uint8_t address_reg, uint8_t index);
void rtd_indirect_write(uint8_t page, uint8_t address_reg, uint8_t index,
                        uint8_t value);
void rtd_indirect_update(uint8_t page, uint8_t address_reg, uint8_t index,
                         uint8_t mask, uint8_t value);
uint8_t mcu_read(uint8_t reg);
void mcu_write(uint8_t reg, uint8_t value);
void mcu_update(uint8_t reg, uint8_t mask, uint8_t value);

#endif
