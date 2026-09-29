// SPDX-License-Identifier: MIT
#ifndef RTD_PANEL_H
#define RTD_PANEL_H

#include <stdint.h>

typedef struct {
  uint16_t width;
  uint16_t height;
  uint16_t htotal;
  uint16_t vtotal;
  uint16_t hstart;
  uint16_t vstart;
  uint8_t hsync;
  uint8_t vsync;
  uint32_t clock_hz;
} panel_t;

extern const panel_t panel;

#endif
