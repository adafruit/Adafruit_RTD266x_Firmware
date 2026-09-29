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
  assert(bytes[18] == 1 && bytes[19] == 3 && bytes[126] == 1);
  assert((bytes[54] | (bytes[55] << 8)) == 3150);
  assert((bytes[56] | ((bytes[58] & 0xf0) << 4)) == 800);
  assert((bytes[57] | ((bytes[58] & 0x0f) << 8)) == 200);
  assert((bytes[59] | ((bytes[61] & 0xf0) << 4)) == 480);
  assert((bytes[60] | ((bytes[61] & 0x0f) << 8)) == 45);
  assert(bytes[62] == 112 && bytes[63] == 48 && bytes[64] == 0xd3);
  assert(bytes[71] == 0x18);
  assert(memcmp(bytes + 77, "Adafruit RTD", 12) == 0);
  assert(bytes[35] & 0x20);
  edid_build_audio_extension(bytes);
  sum = 0;
  for (i = 0; i < sizeof(bytes); ++i) sum += bytes[i];
  assert(sum == 0 && bytes[0] == 2 && bytes[1] == 3);
  assert(!(bytes[3] & 0x40)); /* Do not claim untested basic-audio rates. */
  {
    unsigned offset = 4, found = 0;
    while (offset < bytes[2]) {
      unsigned tag = bytes[offset] >> 5;
      unsigned length = bytes[offset++] & 31;
      assert(offset + length <= bytes[2]);
      if (tag == 1) {
        assert(length == 3 && (bytes[offset] >> 3) == 1);
        assert((bytes[offset] & 7) + 1 == 2);
        assert(bytes[offset + 1] == 4 && bytes[offset + 2] == 1);
        found |= 1;
      } else if (tag == 2) {
        assert(length == 1 && bytes[offset] == 1);
        found |= 2;
      } else if (tag == 3) {
        assert(length >= 5 && bytes[offset] == 3 && bytes[offset + 1] == 12);
        assert(bytes[offset + 2] == 0);
        found |= 4;
      } else if (tag == 4) {
        assert(length == 3 && bytes[offset] == 1);
        assert(bytes[offset + 1] == 0 && bytes[offset + 2] == 0);
        found |= 8;
      }
      offset += length;
    }
    assert(found == 15 && offset == bytes[2]);
  }
  puts("EDID checksum, native timing, sync polarity and identity pass");
  puts("CTA stereo 48 kHz LPCM, HDMI identity and checksum pass");
  return 0;
}
