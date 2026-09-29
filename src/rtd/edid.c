// SPDX-License-Identifier: MIT
#include "rtd/edid.h"
#include "rtd/panel.h"

static void text_descriptor(uint8_t *bytes, uint8_t type, const char *text) {
  uint8_t i = 0;
  bytes[3] = type;
  while (text[i] && i < 13) {
    bytes[5 + i] = text[i];
    ++i;
  }
  if (i < 13) bytes[5 + i++] = '\n';
  while (i < 13) bytes[5 + i++] = ' ';
}

/* EDID1.3 base block, one detailed native mode and no audio advertisement.
 * Timings are generated from the panel profile rather than an opaque dump.
 */
void edid_build(uint8_t bytes[128]) {
  uint8_t i, sum = 0;
  uint8_t *timing = bytes + 54;
  uint16_t hblank = panel.htotal - panel.width;
  uint16_t vblank = panel.vtotal - panel.height;
  uint16_t hfront = panel.htotal - panel.hstart - panel.width;
  uint16_t vfront = panel.vtotal - panel.vstart - panel.height;
  uint16_t clock = (uint16_t)(panel.clock_hz / 10000UL);
  for (i = 0; i < 128; ++i) bytes[i] = 0;
  for (i = 1; i < 7; ++i) bytes[i] = 0xff;
  bytes[8] = 0x04; /* ADA manufacturer identity */
  bytes[9] = 0x81;
  bytes[10] = 0x60;
  bytes[11] = 0x26;
  bytes[17] = 36; /*2026*/
  bytes[18] = 1;
  bytes[19] = 3;
  bytes[20] = 0x80; /* digital RGB */
  bytes[21] = 11;
  bytes[22] = 7;
  bytes[23] = 120; /* nominal gamma2.2 */
  bytes[24] = 0x0a; /* RGB display, preferred timing */
  /* Nominal generic RGB chromaticities, not a measured panel calibration. */
  bytes[25] = 0xee;
  bytes[26] = 0x91;
  bytes[27] = 0xa3;
  bytes[28] = 0x54;
  bytes[29] = 0x4c;
  bytes[30] = 0x99;
  bytes[31] = 0x26;
  bytes[32] = 0x0f;
  bytes[33] = 0x50;
  bytes[34] = 0x54;
  bytes[35] = 0x20; /*640x480@60, supported by this bring-up profile*/
  for (i = 38; i < 54; ++i) bytes[i] = 1; /* unused standard timings */
  timing[0] = (uint8_t)clock;
  timing[1] = (uint8_t)(clock >> 8);
  timing[2] = (uint8_t)panel.width;
  timing[3] = (uint8_t)hblank;
  timing[4] = ((panel.width >> 8) << 4) | (hblank >> 8);
  timing[5] = (uint8_t)panel.height;
  timing[6] = (uint8_t)vblank;
  timing[7] = ((panel.height >> 8) << 4) | (vblank >> 8);
  timing[8] = (uint8_t)hfront;
  timing[9] = panel.hsync;
  timing[10] = (uint8_t)((vfront << 4) | (panel.vsync & 15));
  timing[11] = ((hfront >> 8) << 6) | ((vfront >> 4) << 2) |
               (panel.vsync >> 4);
  timing[12] = 110; /* nominal active dimensions, millimeters */
  timing[13] = 70;
  timing[17] = 0x18; /* separate digital sync, negative HS/VS */
  text_descriptor(bytes + 72, 0xfc, "Adafruit RTD");
  text_descriptor(bytes + 90, 0xfe, "UC586 SDCC");
  bytes[111] = 0x10; /* unused descriptor */
  for (i = 0; i < 127; ++i) sum += bytes[i];
  bytes[127] = (uint8_t)(0 - sum);
}
