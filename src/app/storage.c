// SPDX-License-Identifier: MIT
#include "rtd/storage.h"
#include <string.h>

/* Two records in the proposed final 64 EEPROM bytes. Qualify and back up
 * this reservation before enabling RTD_SETTINGS on a physical board.
 * 0..3 magic, 4 schema version, 5 payload length, 6..7 sequence (little endian),
 * 8..28 payload/zero padding, 29..30 CRC-16/CCITT-FALSE, 31 commit marker.
 * A new record is invalidated first, then written, then committed last.
 */
static uint8_t record[STORE_SLOT_SIZE];
static uint8_t state, current_slot, writable, loaded_count;
static uint16_t sequence;

static uint16_t crc16(void) {
  uint16_t crc = 0xffff;
  uint8_t i, bit;
  for (i = 0; i < 29; ++i) {
    crc ^= (uint16_t)record[i] << 8;
    for (bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

static uint8_t ours(void) {
  return record[0] == 'A' && record[1] == 'R' && record[2] == 'T' &&
         record[3] == 'D';
}

static uint8_t blank(void) {
  uint8_t i;
  /* A first-ever save may lose power after clearing only the commit marker. */
  for (i = 0; i < STORE_SLOT_SIZE - 1; ++i)
    if (record[i] != 0xff)
      return 0;
  return record[31] == 0xff || record[31] == 0;
}

static uint8_t valid(uint8_t count) {
  uint16_t crc = crc16();
  return ours() && record[4] == 1 && record[5] == count &&
         record[29] == (uint8_t)crc && record[30] == (uint8_t)(crc >> 8) &&
         record[31] == 0xa5;
}

uint8_t store_status(void) { return state; }

uint8_t store_load(uint8_t *values, uint8_t count) {
  uint8_t slot, found = 0, foreign_schema = 0;
  uint16_t candidate;
  writable = 1;
  current_slot = 1;
  sequence = 0xffff;
  loaded_count = count;
  state = STORE_UNAVAILABLE;
  if (!count || count > STORE_PAYLOAD_MAX) {
    writable = 0;
    return 0;
  }
  for (slot = 0; slot < 2; ++slot) {
    if (!board_eeprom_read(STORE_ADDRESS + slot * STORE_SLOT_SIZE,
                           record, STORE_SLOT_SIZE)) {
      writable = 0;
      return 0;
    }
    /* Unknown versions are retained for a newer firmware, even with a valid
     * magic. Do not turn downgrading firmware into a destructive migration. */
    if (!blank() && !(ours() && record[4] == 1 && record[5] == count))
      writable = 0;
    if (ours() && (record[4] != 1 || record[5] != count))
      foreign_schema = 1;
    if (!valid(count))
      continue;
    candidate = (uint16_t)record[6] | ((uint16_t)record[7] << 8);
    if (!found || (uint16_t)(candidate - sequence) < 0x8000u) {
      current_slot = slot;
      sequence = candidate;
      found = 1;
    }
  }
  /* A valid record proves we already own this reservation. Recover a torn
   * inactive record on the next save, but preserve recognizable new schemas. */
  if (found && !foreign_schema)
    writable = 1;
  if (found) {
    if (!board_eeprom_read(STORE_ADDRESS + current_slot * STORE_SLOT_SIZE,
                           record, STORE_SLOT_SIZE) || !valid(count)) {
      writable = 0;
      return 0;
    }
    memcpy(values, record + 8, count);
  }
  state = found ? STORE_LOADED : writable ? STORE_EMPTY : STORE_UNAVAILABLE;
  return found;
}

uint8_t store_save(const uint8_t *values, uint8_t count) {
  uint8_t next = current_slot ^ 1, i, marker = 0, equal, check[8];
  uint16_t address = STORE_ADDRESS + next * STORE_SLOT_SIZE, crc;
  if (!writable || !count || count != loaded_count || count > STORE_PAYLOAD_MAX) {
    state = STORE_ERROR;
    return 0;
  }
  /* Suppress writes even when a caller reports an unchanged preference. */
  if (state == STORE_LOADED &&
      board_eeprom_read(STORE_ADDRESS + current_slot * STORE_SLOT_SIZE,
                         record, STORE_SLOT_SIZE) && valid(count) &&
      !memcmp(values, record + 8, count))
    return 1;
  memset(record, 0, sizeof record);
  record[0] = 'A'; record[1] = 'R'; record[2] = 'T'; record[3] = 'D';
  record[4] = 1;
  record[5] = count;
  record[6] = (uint8_t)(sequence + 1);
  record[7] = (uint8_t)((sequence + 1) >> 8);
  memcpy(record + 8, values, count);
  crc = crc16();
  record[29] = (uint8_t)crc;
  record[30] = (uint8_t)(crc >> 8);
  if (!board_eeprom_write_page(address + 31, &marker, 1) ||
      !board_eeprom_write_page(address, record, 16) ||
      !board_eeprom_write_page(address + 16, record + 16, 16))
    goto failed;
  /* Validate the body before the final commit, using small stack storage. */
  for (i = 0; i < STORE_SLOT_SIZE; i += sizeof check) {
    if (!board_eeprom_read(address + i, check, sizeof check) ||
        memcmp(check, record + i, sizeof check))
      goto failed;
  }
  marker = 0xa5;
  if (!board_eeprom_write_page(address + 31, &marker, 1) ||
      !board_eeprom_read(address, record, STORE_SLOT_SIZE))
    goto failed;
  equal = valid(count) && !memcmp(values, record + 8, count);
  if (!equal)
    goto failed;
  sequence = (uint16_t)record[6] | ((uint16_t)record[7] << 8);
  current_slot = next;
  state = STORE_LOADED;
  return 1;
failed:
  state = STORE_ERROR;
  return 0;
}
