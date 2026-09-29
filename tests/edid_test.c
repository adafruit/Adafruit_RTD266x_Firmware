// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rtd/edid.h"

int main(void) {
  uint8_t bytes[128], sum = 0;
  unsigned i;
  edid_build(bytes);
  for (i = 0; i < sizeof(bytes); ++i) sum += bytes[i];
  assert(sum == 0);
  assert(bytes[0] == 0 && bytes[7] == 0 && bytes[1] == 255);
  assert(bytes[18] == 1 && bytes[19] == 3 && bytes[126] == 0);
  assert((bytes[54] | (bytes[55] << 8)) == 3150);
  assert((bytes[56] | ((bytes[58] & 0xf0) << 4)) == 800);
  assert((bytes[57] | ((bytes[58] & 0x0f) << 8)) == 200);
  assert((bytes[59] | ((bytes[61] & 0xf0) << 4)) == 480);
  assert((bytes[60] | ((bytes[61] & 0x0f) << 8)) == 45);
  assert(bytes[62] == 112 && bytes[63] == 48 && bytes[64] == 0xd3);
  assert(bytes[71] == 0x18);
  assert(memcmp(bytes + 77, "Adafruit RTD", 12) == 0);
  assert(bytes[35] & 0x20);
  puts("EDID checksum, native timing, sync polarity and identity pass");
  return 0;
}
