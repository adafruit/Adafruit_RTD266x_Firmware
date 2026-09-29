// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rtd/audio.h"
#include "rtd/io.h"

/* Model W1C FIFO faults and delayed PLL acceptance. This verifies the mute
 * contract and bounded recovery, not physical PLL lock or analog sound. */
static uint8_t direct[256], audio[256], pll_accept;
static unsigned output_enables;

uint8_t rtd_read(uint8_t page, uint8_t reg) {
  assert(page == 2);
  return direct[reg];
}

void rtd_write(uint8_t page, uint8_t reg, uint8_t value) {
  assert(page == 2);
  if (reg == 0xcb) {
    assert(value == 0x06); /* Other status bits must never be cleared. */
    direct[reg] &= (uint8_t)~value;
  } else {
    direct[reg] = value;
  }
}

void rtd_update(uint8_t page, uint8_t reg, uint8_t mask, uint8_t value) {
  rtd_write(page, reg, (rtd_read(page, reg) & (uint8_t)~mask) | (value & mask));
}

uint8_t rtd_indirect_read(uint8_t page, uint8_t reg, uint8_t index) {
  assert(page == 2 && reg == 0xc9 && !(direct[0xc8] & 1));
  return audio[index];
}

void rtd_indirect_write(uint8_t page, uint8_t reg, uint8_t index,
                        uint8_t value) {
  assert(page == 2 && reg == 0xc9 && !(direct[0xc8] & 1));
  audio[index] = value;
  if (index == 0x2d && value == 2 && pll_accept) {
    audio[0x3c] = 0x1e;
    audio[0x3d] = 0x2f;
  }
  if (index == 0x62 && value) {
    assert(value == 0x0f); /* SPDIF stays disabled. */
    assert((direct[0xcb] & 0x57) == 1);
    assert((audio[0x31] & 0x86) == 0x86 && (audio[0x32] & 0x80));
    ++output_enables;
  }
}

void rtd_indirect_update(uint8_t page, uint8_t reg, uint8_t index,
                         uint8_t mask, uint8_t value) {
  rtd_indirect_write(page, reg, index,
      (rtd_indirect_read(page, reg, index) & (uint8_t)~mask) | (value & mask));
}

static void acr(uint32_t n, uint32_t cts, uint16_t count) {
  audio[0x52] = (uint8_t)(cts >> 12);
  audio[0x53] = (uint8_t)(cts >> 4);
  audio[0x54] = (uint8_t)((cts << 4) | (n >> 16));
  audio[0x55] = (uint8_t)(n >> 8);
  audio[0x56] = (uint8_t)n;
  audio[0x28] = (uint8_t)(count >> 8);
  audio[0x29] = (uint8_t)count;
}

static void fixture(void) {
  memset(direct, 0, sizeof direct);
  memset(audio, 0, sizeof audio);
  direct[0xc8] = 0xa5;
  direct[0xcb] = 1;
  audio[0x30] = 0x9b; /* Video enabled, unrelated controls nonzero. */
  audio[0x31] = 0x19;
  audio[0x32] = 0x35;
  audio[0x51] = 0x44;
  pll_accept = 1;
  output_enables = 0;
  audio_init();
  acr(6144, 25200, 1097); /* 25.2 MHz HSTX source, 48 kHz PCM. */
  assert(audio_state() == AUDIO_OFF && !audio[0x62]);
  assert(direct[0xc8] == 0xa4);
}

static void service(uint32_t now, uint8_t valid) {
  audio_service(now, valid);
  assert((audio[0x30] & 0x9f) == 0x9b); /* Preserve video and other AV bits. */
  assert((audio[0x31] & 0x19) == 0x19);
  assert((audio[0x32] & 0x7f) == 0x35);
  assert((audio[0x51] & 0xfd) == 0x44);
}

static void muted(void) {
  assert(!audio[0x62] && !output_enables);
}

static void start(uint32_t base) {
  service(base, 1);
  service(base + 1, 1);
  assert(audio_state() == AUDIO_MEASURE);
  service(base + 2, 1);
  assert(audio_state() == AUDIO_PLL_START);
  assert(audio_sample_rate() >= 47600 && audio_sample_rate() <= 48400);
  service(base + 3, 1);
  service(base + 4, 1);
  assert(audio_state() == AUDIO_SETTLE);
  muted();
}

static void acquire(uint32_t base) {
  start(base);
  service(base + 503, 1);
  assert(audio_state() == AUDIO_SETTLE);
  muted();
  service(base + 504, 1);
  assert(audio_state() == AUDIO_FIFO_CHECK);
  service(base + 1003, 1);
  muted();
  service(base + 1004, 1);
  assert(audio_state() == AUDIO_PLAYING && audio[0x62] == 15);
  assert(output_enables == 1);
}

static void test_acquisition(void) {
  fixture();
  acquire(0);
  service(1104, 1);
  assert(audio_state() == AUDIO_RATE_CHECK && audio[0x62] == 15);
  service(1106, 1);
  assert(audio_state() == AUDIO_PLAYING && output_enables == 1);
  audio_stop();
  assert(audio_state() == AUDIO_OFF && !audio_sample_rate());
  assert(!audio[0x62] && !(audio[0x30] & 0x20));
  fixture();
  acquire(UINT32_MAX - 20); /* All deadlines cross millis rollover. */
}

static void test_rates(void) {
  static const uint32_t bad[][3] = {
    {0, 25200, 1097}, {6144, 0, 1097}, {6144, 25200, 0},
    {4096, 25200, 1097}, /* 32 kHz */
    {6272, 28000, 1097}, /* 44.1 kHz */
    {12288, 25200, 1097}, /* 96 kHz */
    {0xfffff, 0xfffff, 2047}, {1, 0xfffff, 2047},
    {0xfffff, 1, 1} /* Division gives zero: must not divide by it. */
  };
  unsigned i;
  for (i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
    fixture();
    acr(bad[i][0], bad[i][1], (uint16_t)bad[i][2]);
    service(0, 1);
    service(2, 1);
    assert(audio_state() == AUDIO_RETRY && !audio_sample_rate());
    muted();
  }
  /* Maximum count*CTS multiplication, but a ratio corresponding to 48 kHz. */
  fixture();
  acr(477004, 0xfffff, 2047);
  acquire(0);
  acr(12288, 25200, 1097);
  service(1104, 1);
  service(1106, 1);
  assert(audio_state() == AUDIO_RETRY && !audio[0x62]);
}

static void test_pll_deadline(void) {
  fixture();
  pll_accept = 0;
  service(0, 1);
  service(2, 1);
  service(3, 1);
  service(4, 1);
  service(51, 1);
  service(52, 1);
  assert(audio_state() == AUDIO_RETRY);
  muted();
  service(301, 1);
  assert(audio_state() == AUDIO_RETRY);
  pll_accept = 1;
  acquire(302);
}

static void test_faults(void) {
  static const uint8_t faults[] = {0, 0x11, 0x41, 3, 5};
  unsigned i;
  for (i = 0; i < sizeof faults; ++i) {
    fixture();
    acquire(0);
    direct[0xcb] = faults[i];
    service(1005, 1);
    assert(!audio[0x62] && !audio_sample_rate());
  }
  fixture();
  acquire(0);
  service(1005, 0);
  assert(audio_state() == AUDIO_OFF && !audio[0x62]);
  fixture();
  acquire(0);
  audio[0x30] &= (uint8_t)~0x20; /* Hardware watchdog muted the engine. */
  service(1005, 1);
  assert(audio_state() == AUDIO_RETRY && !audio[0x62]);
}

static void test_fifo_settling(void) {
  fixture();
  start(0);
  direct[0xcb] |= 6; /* Startup fill errors are cleared after settling. */
  service(504, 1);
  assert(!(direct[0xcb] & 6));
  service(1004, 1);
  assert(audio_state() == AUDIO_PLAYING);
  fixture();
  start(0);
  service(504, 1);
  direct[0xcb] |= 2; /* A fault in the clean interval forbids output. */
  service(1003, 1);
  muted();
  service(1004, 1);
  assert(audio_state() == AUDIO_RETRY);
  muted();
}

int main(void) {
  test_acquisition();
  test_rates();
  test_pll_deadline();
  test_faults();
  test_fifo_settling();
  puts("Audio: rate validation, guarded unmute, faults, deadlines and rollover pass");
  return 0;
}
