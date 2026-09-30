// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rtd/ddcci.h"
#include "rtd/io.h"
#include "rtd/platform.h"
#include "rtd/audio.h"

/* Packet tests use the public decoder without hardware. The transport model
 * separately models the documented shared FIFO; physical DDC validation is
 * still required for latch/STOP timing and FIFO direction turnaround.
 */
static uint8_t registers[256], fifo[16], fifo_count, fifo_head;
static uint32_t now;
static unsigned gets, sets;
static uint8_t last_code;
static uint16_t last_value;
static const uint8_t *queued_request;
static uint8_t queued_count;

static void host_write(const uint8_t *request, uint8_t count);

uint8_t control_get(uint8_t code, uint16_t *maximum, uint16_t *value) {
  ++gets;
  last_code = code;
  if (code == 0x12) {
    *maximum = 100;
    *value = 73;
    return 1;
  }
  if (code == 0xe1) {
    *maximum = 0xffff;
    *value = 0x0311;
    return 1;
  }
  return 0;
}

uint8_t control_set(uint8_t code, uint16_t value) {
  ++sets;
  last_code = code;
  last_value = value;
  if (queued_count) {
    /* A second complete transaction arrives while a slow reset handler is
     * still applying its settings, before the first service call returns. */
    now += 201;
    host_write(queued_request, queued_count);
    queued_count = 0;
  }
  return (code == 0xe0 && value <= 31) || (code == 0x04 && value == 1);
}

uint8_t mcu_read(uint8_t reg) {
  if (reg == 0x29) {
    return (registers[reg] & 0xf9) | (fifo_count ? 0 : 2) |
           (fifo_count == 16 ? 4 : 0);
  }
  if (reg == 0x25) {
    assert(fifo_count && !(registers[0x2a] & 0x20));
    --fifo_count;
    return fifo[fifo_head++];
  }
  return registers[reg];
}

void mcu_write(uint8_t reg, uint8_t value) {
  if (reg == 0x26) {
    assert((registers[0x2a] & 0x20) && fifo_count < 16);
    fifo[fifo_count++] = value;
  } else if (reg == 0x27) {
    registers[reg] &= value; /* Latched flags clear on zero. */
  } else if (reg == 0x2a) {
    registers[reg] = value & (uint8_t)~0x40;
    if (value & 0x40) fifo_count = fifo_head = 0;
  } else {
    registers[reg] = value;
  }
}

void mcu_update(uint8_t reg, uint8_t mask, uint8_t value) {
  mcu_write(reg, (mcu_read(reg) & (uint8_t)~mask) | (value & mask));
}

uint32_t platform_millis(void) { return now; }

static void checksum(uint8_t *packet, uint8_t count, uint8_t seed) {
  uint8_t i;
  for (i = 0; i < count - 1; ++i) seed ^= packet[i];
  packet[count - 1] = seed;
}

static void response_valid(const uint8_t *packet, uint8_t count) {
  uint8_t i, sum = 0x50;
  assert(count >= 3 && count <= DDCCI_PACKET_BYTES);
  assert(packet[0] == 0x6e && (packet[1] & 0x80));
  assert((packet[1] & 0x7f) == count - 3);
  for (i = 0; i < count; ++i) sum ^= packet[i];
  assert(!sum);
}

static void test_get_set(void) {
  uint8_t get[] = {0x51, 0x82, 0x01, 0x12, 0};
  uint8_t set[] = {0x51, 0x84, 0x03, 0xe0, 0, 4, 0};
  uint8_t output[DDCCI_PACKET_BYTES], count;
  gets = sets = 0;
  checksum(get, sizeof get, 0x6e);
  count = ddcci_packet(get, sizeof get, output);
  assert(count == 11 && gets == 1);
  response_valid(output, count);
  assert(output[2] == 2 && output[3] == 0 && output[4] == 0x12);
  assert(output[6] == 0 && output[7] == 100);
  assert(output[8] == 0 && output[9] == 73);
  get[3] = 0x62; /* This test controller deliberately rejects the requested code. */
  checksum(get, sizeof get, 0x6e);
  count = ddcci_packet(get, sizeof get, output);
  response_valid(output, count);
  assert(output[3] == 1 && output[4] == 0x62);
  assert(!(output[6] | output[7] | output[8] | output[9]));
  get[3] = 0xe1;
  checksum(get, sizeof get, 0x6e);
  memcpy(output, get, sizeof get);
  count = ddcci_packet(output, sizeof get, output); /* Shared RX/TX storage. */
  assert(output[8] == 3 && output[9] == 0x11);
  response_valid(output, count);
  checksum(set, sizeof set, 0x6e);
  assert(!ddcci_packet(set, sizeof set, output));
  assert(sets == 1 && last_code == 0xe0 && last_value == 4);
  set[4] = 0x12;
  set[5] = 0x34;
  checksum(set, sizeof set, 0x6e);
  assert(!ddcci_packet(set, sizeof set, output));
  assert(sets == 2 && last_value == 0x1234); /* Policy belongs to control_set. */
}

static void test_invalid(void) {
  uint8_t request[16] = {0x51, 0x84, 3, 0xe0, 0, 1, 0};
  uint8_t output[16], i;
  unsigned before = sets;
  checksum(request, 7, 0x6e);
  for (i = 0; i < 7; ++i) {
    request[i] ^= 1;
    assert(!ddcci_packet(request, 7, output));
    assert(sets == before);
    request[i] ^= 1;
  }
  for (i = 0; i < 7; ++i) assert(!ddcci_packet(request, i, output));
  assert(!ddcci_packet(request, 17, output));
  request[1] = 4; /* High length bit is required, even with valid checksum. */
  checksum(request, 7, 0x6e);
  assert(!ddcci_packet(request, 7, output));
  request[1] = 0x83;
  checksum(request, 6, 0x6e);
  assert(!ddcci_packet(request, 6, output)); /* Truncated Set payload. */
  assert(sets == before);
  request[1] = 0x81;
  request[2] = 0x77;
  checksum(request, 4, 0x6e);
  assert(ddcci_packet(request, 4, output) == 3);
  response_valid(output, 3);
  assert(output[1] == 0x80);
}

static void test_capabilities(void) {
  uint8_t request[] = {0x51, 0x83, 0xf3, 0, 0, 0};
  uint8_t reply[16], count, chars;
  uint16_t offset = 0;
  char assembled[200];
  do {
    request[3] = offset >> 8;
    request[4] = offset;
    checksum(request, sizeof request, 0x6e);
    count = ddcci_packet(request, sizeof request, reply);
    response_valid(reply, count);
    assert(reply[2] == 0xe3 && reply[3] == request[3] && reply[4] == request[4]);
    chars = count - 6;
    assert(chars <= 10 && offset + chars < sizeof assembled);
    memcpy(assembled + offset, reply + 5, chars);
    offset += chars;
  } while (chars && assembled[offset - 1]);
  assert(strstr(assembled, "mccs_ver(2.2)"));
  assert(strstr(assembled, "8D D6 DF E0 E1 E2 E3 E4 E5 E6 E7 E8"));
#if RTD_AUDIO_VOLUME
  assert(strstr(assembled, "vcp(04 12 16 18 1A 87 8A 62 "));
#else
  assert(strstr(assembled, "vcp(04 12 16 18 1A 87 8A 8D "));
#endif
  request[3] = request[4] = 0xff;
  checksum(request, sizeof request, 0x6e);
  assert(ddcci_packet(request, sizeof request, reply) == 6);
  response_valid(reply, 6);
}

static void fixture(void) {
  memset(registers, 0, sizeof registers);
  registers[0x1e] = 3; /* Existing EDID and ISP settings must be preserved. */
  registers[0xec] = 0x4a;
  fifo_count = fifo_head = 0;
  queued_count = 0;
  now = 0;
  ddcci_init();
  assert(registers[0x23] == 0x6f && registers[0x2b] == 2);
  assert(!registers[0x28] && (registers[0x2a] & 0x3f) == 0);
  assert(registers[0x1e] == 3 && registers[0xec] == 0x4a);
}

static void host_write(const uint8_t *request, uint8_t count) {
  assert(!(registers[0x2a] & 0x20) && count <= 17);
  registers[0x24] = request[0];
  memcpy(fifo, request + 1, count - 1);
  fifo_head = 0;
  fifo_count = count - 1;
  registers[0x27] = 0x17;
}

static void test_transport(void) {
  uint8_t request[17] = {0x51, 0x82, 1, 0x12, 0};
  unsigned before;
  fixture();
  checksum(request, 5, 0x6e);
  host_write(request, 5);
  registers[0x27] &= (uint8_t)~0x10;
  before = gets;
  ddcci_service();
  assert(gets == before); /* Never execute an incomplete I2C transaction. */
  registers[0x27] |= 0x10;
  ddcci_service();
  assert(gets == before + 1 && fifo_count == 11);
  response_valid(fifo, fifo_count);
  assert(registers[0x2a] & 0x20);
  fifo_count = 0; /* Host read completed, followed by STOP. */
  registers[0x27] |= 0x10;
  ddcci_service();
  assert(!(registers[0x2a] & 0x20));
  host_write(request, 5);
  now = UINT32_MAX - 100;
  ddcci_service();
  now += 249;
  ddcci_service();
  assert(registers[0x2a] & 0x20);
  ++now;
  ddcci_service();
  assert(!(registers[0x2a] & 0x20)); /* Abandoned reply; rollover-safe. */
  before = gets;
  host_write(request, 5);
  registers[0x29] |= 0x20;
  ddcci_service();
  assert(gets == before && !(registers[0x2a] & 0x20));
  host_write(request, 17); /* More than one complete supported packet. */
  ddcci_service();
  assert(gets == before && !fifo_count);
  request[4] ^= 1;
  host_write(request, 5);
  ddcci_service();
  assert(gets == before && !fifo_count);
}

static void test_request_during_set(void) {
  uint8_t reset[] = {0x51, 0x84, 3, 0x04, 0, 1, 0};
  uint8_t get[] = {0x51, 0x82, 1, 0x12, 0};
  unsigned before_gets = gets, before_sets = sets;
  fixture();
  checksum(reset, sizeof reset, 0x6e);
  checksum(get, sizeof get, 0x6e);
  queued_request = get;
  queued_count = sizeof get;
  host_write(reset, sizeof reset);
  ddcci_service();
  assert(sets == before_sets + 1 && last_code == 0x04 && last_value == 1);
  assert(gets == before_gets && !queued_count && now == 201);
  assert(fifo_count == sizeof get - 1 && (registers[0x27] & 0x10));
  assert(!(registers[0x2a] & 0x20)); /* Set has no response; queued RX stays owned by host. */
  ddcci_service();
  assert(gets == before_gets + 1 && fifo_count == 11);
  assert(fifo[3] == 0 && fifo[4] == 0x12 && fifo[9] == 73);
  response_valid(fifo, fifo_count);
}

int main(void) {
  test_get_set();
  test_invalid();
  test_capabilities();
  test_transport();
  test_request_during_set();
  puts("DDC-CI: framing, dispatch, checksums, capabilities, queued reset read and FIFO recovery pass");
  return 0;
}
