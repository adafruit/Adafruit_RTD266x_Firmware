// SPDX-License-Identifier: MIT
#include "rtd/edid.h"
#include "rtd/io.h"

void edid_publish(void) {
  uint16_t i;
  static uint8_t block[128]; /* XRAM; keep the 8051 return stack small. */
  volatile __xdata uint8_t *ram = (volatile __xdata uint8_t *)0xfd80;
  edid_build(block);
  /* Manual pp.289-293: MCU RAM 512 bytes, DDC1/2/3=128/256/128.
   * External DDC2 reads remain disabled until a complete EDID is ready.
   */
  mcu_update(0x1b, 1, 0);
  mcu_write(0x1e, 2);
  mcu_update(0x2c, 1, 0);
  mcu_write(0x21, 0x2b);
  for (i = 0; i < 256; ++i) ram[i] = i < 128 ? block[i] : 0;
  mcu_update(0xa4, 0xf0, 0); /* DDC2 pin function */
  mcu_write(0x1f, 6);
  mcu_write(0x1f, 4);
  mcu_update(0x20, 1, 0);
  mcu_write(0x1e, 3); /*0x50, debounce, read-only external access*/
}
