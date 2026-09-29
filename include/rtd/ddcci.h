// SPDX-License-Identifier: MIT
#ifndef RTD_DDCCI_H
#define RTD_DDCCI_H
#include <stdint.h>

#define DDCCI_PACKET_BYTES 16

/* Live DDC2 slave at 7-bit address 0x37. Foreground polling, about every
 * 10 ms; no ISP or interrupt ownership. Host waits at least 40 ms between
 * request and reply, and 10 ms after a read before sending another request.
 */
void ddcci_init(void);
void ddcci_service(void);

/* Decode a complete request beginning with source 0x51. Response storage
 * must hold DDCCI_PACKET_BYTES bytes. Returns response length, or zero for
 * a write-only command or invalid request. Input/output may share storage.
 */
uint8_t ddcci_packet(const uint8_t *request, uint8_t count, uint8_t *response);

/* Implemented by the application. Return nonzero only for supported values.
 * E0 sets logical keys: Menu=1, Back=2, Increase=4, Decrease=8, Power=16.
 * E1 reads the application's menu state. Standard VCP codes use this same
 * checked dispatch; this transport does not expose raw register writes.
 */
uint8_t control_get(uint8_t code, uint16_t *maximum, uint16_t *value);
uint8_t control_set(uint8_t code, uint16_t value);
#endif
