// SPDX-License-Identifier: MIT
#include "rtd/diagnostics.h"
#include "rtd/io.h"

#if RTD_TRACE
void diagnostics_measurement(uint8_t stage, uint16_t a, uint16_t b, uint16_t c) {
  volatile __xdata uint8_t *edid = (volatile __xdata uint8_t *)0xfd80;
  uint8_t byte, field, digit, sum = 0;
  uint16_t word;
  mcu_write(0x19, stage);
  /* Stop external reads while changing the ASCII descriptor and checksum. */
  mcu_write(0x1e, 2);
  edid[95] = stage < 10 ? '0' + stage : 'A' + stage - 10;
  for (field = 0; field < 3; ++field) {
    word = field == 0 ? a : (field == 1 ? b : c);
    for (byte = 0; byte < 4; ++byte) {
      digit = (uint8_t)((word >> 12) & 15);
      edid[96 + 4 * field + byte] = digit < 10 ? '0' + digit : 'A' + digit - 10;
      word <<= 4;
    }
  }
  for (byte = 0; byte < 127; ++byte) sum += edid[byte];
  edid[127] = (uint8_t)(0 - sum);
  mcu_write(0xf2, edid[95]); /* Confirm the MCU sees its descriptor write. */
  mcu_write(0x1e, 3);
}
#endif
