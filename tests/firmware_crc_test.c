// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "rtd/firmware_crc.h"
#include "rtd/platform.h"

static uint8_t pattern;
static uint32_t reads;
static uint32_t vendor_reads;
static uint8_t vendor_available;

uint8_t platform_vendor_probe_available(void) { return vendor_available; }
uint8_t platform_vendor_probe_read(uint16_t offset) {
  assert(vendor_reads < 8192 && offset == vendor_reads);
  ++vendor_reads;
  return (uint8_t)offset;
}

uint8_t platform_code_read(uint16_t address) {
  assert(reads < 65536 && address == reads);
  ++reads;
  if (pattern == 0) return 0;
  if (pattern == 1) return 255;
  if (pattern == 3 && address == 65535) return 254;
  return (uint8_t)address;
}

static uint16_t get(uint8_t code, uint16_t expected_maximum) {
  uint16_t maximum, value;
  assert(firmware_crc_get(code, &maximum, &value));
  assert(maximum == expected_maximum);
  return value;
}

int main(void) {
  /* Independent golden CRCs from Python zlib.crc32, all 65536 bytes. */
  static const uint32_t expected[] = {
    0xd7978eebUL, 0xdeab7e4eUL, 0xb11de6a1UL, 0xc61ad637UL
  };
  uint16_t maximum, value;
  assert(get(0xf6, 2) == 0 && get(0xf9, 256) == 0);
  assert(get(0xfa, 2) == 0);
  assert(!firmware_crc_start(0) && !firmware_crc_start(3));
  assert(!firmware_crc_start(FIRMWARE_CRC_VENDOR_PROBE));
  firmware_crc_service();
  assert(!reads && !firmware_crc_get(0xf7, &maximum, &value));
  assert(!firmware_crc_get(0xf8, &maximum, &value));
  assert(!firmware_crc_get(0xff, &maximum, &value));
  for (pattern = 0; pattern < 4; ++pattern) {
    uint16_t calls;
    uint32_t actual;
    reads = 0;
    assert(firmware_crc_start(FIRMWARE_CRC_BANK0));
    assert(get(0xfa, 2) == FIRMWARE_CRC_BANK0);
    assert(!firmware_crc_start(FIRMWARE_CRC_BANK0)); /* Busy jobs cannot restart. */
    assert(!firmware_crc_start(FIRMWARE_CRC_VENDOR_PROBE));
    assert(!firmware_crc_get(0xf7, &maximum, &value));
    for (calls = 0; calls < 1024; ++calls) {
      assert(get(0xf6, 2) == 1);
      assert(get(0xf9, 256) == reads / 256);
      firmware_crc_service();
      assert(reads == (uint32_t)(calls + 1) * 64);
    }
    assert(get(0xf6, 2) == 2 && get(0xf9, 256) == 256);
    actual = get(0xf7, 65535);
    actual |= (uint32_t)get(0xf8, 65535) << 16;
    assert(actual == expected[pattern]);
    firmware_crc_service();
    assert(reads == 65536 && get(0xf6, 2) == 2);
  }
  assert(!vendor_reads);
  assert(!firmware_crc_start(FIRMWARE_CRC_VENDOR_PROBE));
  assert(get(0xfa, 2) == FIRMWARE_CRC_BANK0);
  assert(get(0xf7, 65535) == 0xd637); /* Rejection preserves the last result. */
  vendor_available = 1;
  assert(firmware_crc_start(FIRMWARE_CRC_VENDOR_PROBE));
  assert(get(0xfa, 2) == FIRMWARE_CRC_VENDOR_PROBE);
  while (vendor_reads < 8192) {
    uint32_t before = vendor_reads;
    assert(get(0xf6, 2) == 1);
    assert(get(0xf9, 32) == vendor_reads / 256);
    firmware_crc_service();
    assert(vendor_reads == before + 64);
  }
  assert(get(0xf6, 2) == 2 && get(0xf9, 32) == 32);
  assert(get(0xf7, 65535) == 0x5307 && get(0xf8, 65535) == 0xb667);
  firmware_crc_service();
  assert(vendor_reads == 8192 && reads == 65536);
  puts("Firmware CRC: zlib goldens, bounded bank/sector reads, region guards and progress pass");
  return 0;
}
