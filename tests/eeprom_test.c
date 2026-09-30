// SPDX-License-Identifier: MIT
#include "rtd/storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t scl = 1, sda = 1, stuck_clock;
static uint8_t samples[512], clocks[512];
static unsigned sample_count, sample_index, clock_count, starts, stops, reads;
static uint32_t now;

static void reset(void) {
  scl = sda = 1;
  stuck_clock = 0;
  sample_count = sample_index = clock_count = starts = stops = reads = 0;
  now = 0;
}

static void sample(uint8_t value) { samples[sample_count++] = value; }

static void byte_samples(uint8_t value) {
  uint8_t bit;
  for (bit = 0; bit < 8; ++bit)
    sample((value >> (7 - bit)) & 1);
}

void mcu_write(uint8_t reg, uint8_t value) {
  assert((reg == 0xcd || reg == 0xce) && value <= 1);
  if (reg == 0xcd) {
    if (!scl && value) {
      assert(clock_count < sizeof clocks);
      clocks[clock_count++] = sda;
    }
    scl = value;
  } else {
    if (scl && sda != value) {
      if (value) ++stops;
      else ++starts;
    }
    sda = value;
  }
}

uint8_t mcu_read(uint8_t reg) {
  ++reads;
  assert(reads < 10000);
  if (reg == 0xcd)
    return stuck_clock ? 0 : scl;
  assert(reg == 0xce && sample_index < sample_count);
  return samples[sample_index++];
}

uint32_t platform_millis(void) { return now++; }

static void expect_byte(unsigned offset, uint8_t value) {
  uint8_t bit;
  for (bit = 0; bit < 8; ++bit)
    assert(clocks[offset + bit] == ((value >> (7 - bit)) & 1));
}

int main(void) {
  uint8_t data[3] = {0x12, 0x34, 0x56};

  /* Random read at 0x7c0: block111 addresses AE/AF, word C0, repeated START,
   * then ACK the first byte and NACK the final byte. Stop releases both pins. */
  reset();
  sample(1); sample(0); sample(0); sample(1); sample(0);
  byte_samples(0x5a); byte_samples(0x81);
  assert(board_eeprom_read(0x7c0, data, 2));
  assert(data[0] == 0x5a && data[1] == 0x81);
  assert(sample_index == sample_count && starts == 2 && stops == 1);
  assert(scl && sda);
  expect_byte(0, 0xae);
  expect_byte(9, 0xc0);
  expect_byte(19, 0xaf);
  assert(clocks[36] == 0 && clocks[45] == 1);

  /* Three-byte page write, followed by one busy NACK and one ready ACK. */
  reset();
  sample(1); sample(0); sample(0);
  sample(0); sample(0); sample(0);
  sample(1); sample(1); sample(1); sample(0);
  assert(board_eeprom_write_page(0x7e0, data, 3));
  assert(sample_index == sample_count && starts == 3 && stops == 3);
  expect_byte(0, 0xae); expect_byte(9, 0xe0);
  expect_byte(18, data[0]); expect_byte(27, data[1]);
  expect_byte(36, data[2]);

  reset();
  assert(!board_eeprom_write_page(0x7ef, data, 2));
  assert(!board_eeprom_write_page(2048, data, 1));
  assert(!board_eeprom_write_page(0x7c0, data, 0));
  assert(!board_eeprom_read(2047, data, 2));
  assert(!board_eeprom_read(0, data, 0));
  assert(starts == 0 && reads == 0);

  reset();
  sample(1); sample(1); /* Device absent. */
  assert(!board_eeprom_read(0, data, 1));
  assert(starts == 1 && stops == 1 && scl && sda);

  reset();
  stuck_clock = 1;
  assert(!board_eeprom_read(0, data, 1));
  assert(reads == 510 && scl && sda);

  reset();
  sample(1); sample(0); sample(0); sample(0);
  for (unsigned i = 0; i < 10; ++i) { sample(1); sample(1); }
  assert(!board_eeprom_write_page(0x7c0, data, 1));
  assert(now == 11 && scl && sda);

  puts("EEPROM: addressing, repeated START, ACK/NACK, page limits, bounded failures passed");
  return 0;
}
