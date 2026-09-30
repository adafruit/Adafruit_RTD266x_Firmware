// SPDX-License-Identifier: MIT
#include "rtd/io.h"
#include "rtd/platform.h"
#include "rtd/storage.h"

/* UC-586 stock I2C routines use P6.6/pin56 as SCL (FFCD) and P6.7/pin57 as
 * SDA (FFCE), confirmed by matching full EEPROM reads. board_init sets
 * FF9A=05 for open-drain GPIO and FFC0 bit3 for physical pin readback.
 * Microchip 24LC16B: 2048 bytes, 16-byte pages,
 * 5 ms maximum write cycle; bits10:8 of the address are in the control byte.
 */
#define SCL 0xcd
#define SDA 0xce
#if RTD_EEPROM_DIAGNOSTICS
static uint8_t read_stage = 0;
uint16_t board_eeprom_diagnostic(void) {
  return ((uint16_t)read_stage << 8) | (mcu_read(SCL) & 1) |
         ((mcu_read(SDA) & 1) << 1) | ((mcu_read(0x9a) & 15) << 4);
}
#define READ_STAGE(value) read_stage = value
#else
#define READ_STAGE(value) ((void)0)
#endif

static void settle(void) {
  volatile uint8_t i;
  for (i = 0; i < 8; ++i) { }
}

static uint8_t clock_high(void) {
  uint8_t attempts = 255;
  mcu_write(SCL, 1);
  do {
    settle();
    if (mcu_read(SCL) & 1)
      return 1;
  } while (--attempts);
  return 0;
}

static void stop(void) {
  mcu_write(SCL, 0);
  mcu_write(SDA, 0);
  settle();
  clock_high();
  mcu_write(SDA, 1);
  settle();
}

static uint8_t start(void) {
  mcu_write(SDA, 1);
  settle(); /* Allow the previous ACK to release before a repeated START. */
  if (!clock_high() || !(mcu_read(SDA) & 1))
    return 0;
  mcu_write(SDA, 0);
  settle();
  mcu_write(SCL, 0);
  return 1;
}

static uint8_t send(uint8_t value) {
  uint8_t bits = 8, acknowledged;
  do {
    mcu_write(SDA, (value & 0x80) ? 1 : 0);
    settle();
    if (!clock_high())
      return 0;
    mcu_write(SCL, 0);
    value <<= 1;
  } while (--bits);
  mcu_write(SDA, 1);
  settle();
  if (!clock_high())
    return 0;
  acknowledged = !(mcu_read(SDA) & 1);
  mcu_write(SCL, 0);
  return acknowledged;
}

static uint8_t receive(uint8_t *value, uint8_t last) {
  uint8_t bits = 8, data = 0;
  mcu_write(SDA, 1);
  do {
    settle();
    if (!clock_high())
      return 0;
    data = (data << 1) | (mcu_read(SDA) & 1);
    mcu_write(SCL, 0);
  } while (--bits);
  mcu_write(SDA, last ? 1 : 0);
  settle();
  if (!clock_high())
    return 0;
  mcu_write(SCL, 0);
  mcu_write(SDA, 1);
  *value = data;
  return 1;
}

uint8_t board_eeprom_read(uint16_t address, uint8_t *data, uint8_t count) {
  uint8_t device = 0xa0 | ((address >> 7) & 0x0e), ok = 0;
  if (!count || address >= 2048 || count > 2048u - address)
    return 0;
  READ_STAGE(1);
  if (!start()) goto done;
  READ_STAGE(2);
  if (!send(device)) goto done;
  READ_STAGE(3);
  if (!send((uint8_t)address)) goto done;
  READ_STAGE(4);
  if (!start()) goto done;
  READ_STAGE(5);
  if (!send(device | 1)) goto done;
  READ_STAGE(6);
  while (count) {
    if (!receive(data++, count == 1))
      goto done;
    --count;
  }
  ok = 1;
  READ_STAGE(0);
done:
  stop();
  return ok;
}

uint8_t board_eeprom_write_page(uint16_t address, const uint8_t *data,
                                uint8_t count) {
  uint8_t device = 0xa0 | ((address >> 7) & 0x0e), ok;
  uint32_t began;
  if (!count || count > 16 || address >= 2048 ||
      count > 16u - (address & 15))
    return 0;
  if (!start() || !send(device) || !send((uint8_t)address)) {
    stop();
    return 0;
  }
  while (count--) {
    if (!send(*data++)) {
      stop();
      return 0;
    }
  }
  stop();
  began = platform_millis();
  /* ACK polling is bounded and sends no data. DDC callbacks run only after
   * this complete transaction returns to the settings store. */
  do {
    ok = start() && send(device);
    stop();
    if (ok)
      return 1;
  } while ((uint32_t)(platform_millis() - began) < 10);
  return 0;
}
