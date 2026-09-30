// SPDX-License-Identifier: MIT
#ifndef RTD_STORAGE_H
#define RTD_STORAGE_H

#include <stdint.h>

#define STORE_PAYLOAD_MAX 21
#define STORE_SLOT_SIZE 32u
#define STORE_ADDRESS 0x04c0u

enum { STORE_UNAVAILABLE, STORE_EMPTY, STORE_LOADED, STORE_ERROR };

/* UC-586's separate 24LC16B; these functions never access program flash.
 * A page write must fit in one aligned 16-byte EEPROM page. */
uint8_t board_eeprom_read(uint16_t address, uint8_t *data, uint8_t count);
uint8_t board_eeprom_write_page(uint16_t address, const uint8_t *data,
                                uint8_t count);
#if RTD_EEPROM_DIAGNOSTICS
uint16_t board_eeprom_diagnostic(void);
#endif

/* The caller supplies defaults, validates loaded setting ranges, and coalesces
 * changes before saving. An unknown occupied reservation is never overwritten.
 * Each save preserves the previous slot until the new CRC and commit byte pass
 * readback. Both operations are bounded; failures leave runtime settings alone.
 * Save snapshots the payload and services DDC between completed EEPROM bus
 * transactions. Call load/save only from the foreground, never DDC callbacks.
 */
uint8_t store_load(uint8_t *values, uint8_t count);
/* Explicitly accept one shorter, prefix-compatible payload at boot. The newest
 * valid recognized record wins; appended fields keep the caller's defaults.
 * Subsequent saves use count and migrate atomically. previous_count must be
 * smaller than count; zero is strict loading, as used by store_load(). */
uint8_t store_load_compatible(uint8_t *values, uint8_t count,
                              uint8_t previous_count);
uint8_t store_save(const uint8_t *values, uint8_t count);
uint8_t store_status(void);

#endif
