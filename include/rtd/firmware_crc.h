// SPDX-License-Identifier: MIT
#ifndef RTD_FIRMWARE_CRC_H
#define RTD_FIRMWARE_CRC_H

#include <stdint.h>

/* CRC-32/ISO-HDLC of the complete 64 KiB code bank, including padding.
 * No embedded expected value: the host computes it from the built image.
 * Service work is bounded so video, audio and DDC remain responsive. */
enum { FIRMWARE_CRC_BANK0 = 1, FIRMWARE_CRC_VENDOR_PROBE = 2 };
/* The read-only vendor probe covers 0x010000..0x011fff. */
uint8_t firmware_crc_start(uint8_t region);
void firmware_crc_service(void);
/* F6 status: 0 idle, 1 busy, 2 ready; F7/F8 low/high CRC words (ready only);
 * F9 progress in completed 256-byte pages (256 bank0, 32 vendor probe);
 * FA selected region, zero before the first job. */
uint8_t firmware_crc_get(uint8_t code, uint16_t *maximum, uint16_t *value);

#endif
