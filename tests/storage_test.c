// SPDX-License-Identifier: MIT
#include "rtd/storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t memory[2048];
static unsigned writes, cut_after, fail_read;
static uint8_t write_protected;

uint8_t board_eeprom_read(uint16_t address, uint8_t *data, uint8_t count) {
  assert(address >= STORE_ADDRESS && address + count <= sizeof memory);
  if (fail_read)
    return 0;
  memcpy(data, memory + address, count);
  return 1;
}

uint8_t board_eeprom_write_page(uint16_t address, const uint8_t *data,
                                uint8_t count) {
  assert(address >= STORE_ADDRESS && address + count <= sizeof memory);
  assert(count && count <= 16 && (address & 15) + count <= 16);
  ++writes;
  if (cut_after && writes >= cut_after)
    return 0;
  if (!write_protected)
    memcpy(memory + address, data, count);
  return 1;
}

static void load_expect(const uint8_t *expected, uint8_t count) {
  uint8_t restored[STORE_PAYLOAD_MAX];
  memset(restored, 0, sizeof restored);
  assert(store_load(restored, count));
  assert(store_status() == STORE_LOADED);
  assert(!memcmp(restored, expected, count));
}

int main(void) {
  uint8_t settings[] = {50, 50, 100, 0, 1, 1, 2, 0, 2, 100, 0, 0, 0};
  uint8_t newer[sizeof settings], restored[sizeof settings], saved[64];
  unsigned before, fail, byte, bit;

  memset(memory, 0xff, sizeof memory);
  memset(restored, 0x55, sizeof restored);
  assert(!store_load(restored, sizeof restored));
  assert(store_status() == STORE_EMPTY && restored[0] == 0x55);
  assert(store_save(settings, sizeof settings));
  assert(writes == 4);
  load_expect(settings, sizeof settings);
  before = writes;
  assert(store_save(settings, sizeof settings) && writes == before);

  memcpy(newer, settings, sizeof newer);
  newer[0] = 75;
  assert(store_save(newer, sizeof newer));
  load_expect(newer, sizeof newer);
  memcpy(saved, memory + STORE_ADDRESS, sizeof saved);

  /* Every bit in the newest record is covered by magic/version/count/CRC or
   * commit checking. A corrupt newer record always falls back to the old one. */
  for (byte = 0; byte < STORE_SLOT_SIZE; ++byte) {
    for (bit = 0; bit < 8; ++bit) {
      memcpy(memory + STORE_ADDRESS, saved, sizeof saved);
      memory[STORE_ADDRESS + STORE_SLOT_SIZE + byte] ^= 1u << bit;
      load_expect(settings, sizeof settings);
    }
  }

  /* Interrupt before each physical page operation: invalidation, first page,
   * second page, and final commit. The previous complete record survives. */
  for (fail = 1; fail <= 4; ++fail) {
    memcpy(memory + STORE_ADDRESS, saved, sizeof saved);
    load_expect(newer, sizeof newer);
    before = writes;
    cut_after = writes + fail;
    assert(!store_save(settings, sizeof settings));
    assert(store_status() == STORE_ERROR);
    assert(writes - before == fail);
    cut_after = 0;
    load_expect(newer, sizeof newer);
    assert(store_save(settings, sizeof settings));
    load_expect(settings, sizeof settings);
  }

  /* First-ever save, including the initially blank commit-byte invalidation,
   * can be retried after reboot at each completed page boundary. */
  for (fail = 1; fail <= 4; ++fail) {
    memset(memory + STORE_ADDRESS, 0xff, sizeof saved);
    assert(!store_load(restored, sizeof restored));
    cut_after = writes + fail;
    assert(!store_save(settings, sizeof settings));
    cut_after = 0;
    assert(!store_load(restored, sizeof restored));
    assert(store_save(settings, sizeof settings));
    load_expect(settings, sizeof settings);
  }

  /* An interrupted page may have arbitrary bytes, not merely an old complete
   * page. A valid other slot establishes ownership for recovery. */
  memcpy(memory + STORE_ADDRESS, saved, sizeof saved);
  memset(memory + STORE_ADDRESS, 0x66, STORE_SLOT_SIZE);
  load_expect(newer, sizeof newer);
  assert(store_save(settings, sizeof settings));
  load_expect(settings, sizeof settings);

  /* A recognizable future schema is retained even beside our valid slot. */
  memcpy(memory + STORE_ADDRESS, saved, sizeof saved);
  memory[STORE_ADDRESS + 4] = 2;
  load_expect(newer, sizeof newer);
  before = writes;
  assert(!store_save(settings, sizeof settings) && writes == before);

  /* A write-protected chip can ACK a page but must fail readback; active data
   * still restores. No success is inferred merely from an ACK. */
  memcpy(memory + STORE_ADDRESS, saved, sizeof saved);
  load_expect(newer, sizeof newer);
  write_protected = 1;
  assert(!store_save(settings, sizeof settings));
  write_protected = 0;
  load_expect(newer, sizeof newer);

  /* EEPROM transport failure or occupied foreign data never requests writes. */
  fail_read = 1;
  assert(!store_load(restored, sizeof restored));
  before = writes;
  assert(!store_save(settings, sizeof settings) && writes == before);
  fail_read = 0;
  memset(memory + STORE_ADDRESS, 0x42, sizeof saved);
  assert(!store_load(restored, sizeof restored));
  assert(store_status() == STORE_UNAVAILABLE);
  assert(!store_save(settings, sizeof settings) && writes == before);
  assert(store_status() == STORE_ERROR);
  assert(!store_load(restored, STORE_PAYLOAD_MAX + 1));
  assert(!store_load(restored, 0));

  /* Sequence rollover chooses zero as newer than 65535. CRC still authenticates
   * each sequence, so exercise rollover through the public saving API. */
  memset(memory + STORE_ADDRESS, 0xff, sizeof saved);
  assert(!store_load(restored, sizeof restored));
  for (before = 0; before < 65538; ++before) {
    settings[0] = before & 1;
    assert(store_save(settings, sizeof settings));
  }
  load_expect(settings, sizeof settings);

  puts("settings storage: CRC, commit order, interrupted writes, protection, wear, wrap passed");
  return 0;
}
