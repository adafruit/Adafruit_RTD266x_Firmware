// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rtd/io.h"
#include "rtd/panel.h"
#include "rtd/video.h"

/* Host model covers register encoding and mode rejection. It cannot model
 * analog PLL lock, the filter's physical tap mapping, or panel image quality.
 */
const panel_t panel = {800, 480, 1000, 525, 88, 32, 48, 3, 31500000UL};
static uint8_t registers[7][256];
static uint8_t ports[7][256][256];
static uint8_t measurement[2][8];
static uint8_t coefficients[128];
static unsigned coefficient_count;
static uint32_t now;
static uint8_t stalled;

uint8_t rtd_read(uint8_t page, uint8_t reg) {
  return registers[page][reg];
}

void rtd_write(uint8_t page, uint8_t reg, uint8_t value) {
  registers[page][reg] = value;
  if (page == 0 && reg == 0x52 && (value & 0x60) && !(value & stalled)) {
    memcpy(&registers[0][0x52], measurement[registers[0][0x47] & 1], 8);
  }
  if (page == 0 && reg == 0x36) {
    assert(coefficient_count < sizeof(coefficients));
    coefficients[coefficient_count++] = value;
  }
  if (page == 0 && reg == 0x34) {
    assert(registers[0][0x33] & 0x80);
    ports[0][0x33][registers[0][0x33]++] = value;
  }
}

void rtd_update(uint8_t page, uint8_t reg, uint8_t mask, uint8_t value) {
  rtd_write(page, reg, (rtd_read(page, reg) & (uint8_t)~mask) | (value & mask));
}

uint8_t rtd_indirect_read(uint8_t page, uint8_t reg, uint8_t index) {
  return ports[page][reg][index];
}

void rtd_indirect_write(uint8_t page, uint8_t reg, uint8_t index, uint8_t value) {
  ports[page][reg][index] = value;
}

void rtd_indirect_update(uint8_t page, uint8_t reg, uint8_t index,
                         uint8_t mask, uint8_t value) {
  rtd_indirect_write(page, reg, index,
      (rtd_indirect_read(page, reg, index) & (uint8_t)~mask) | (value & mask));
}

uint32_t platform_millis(void) { return now++; }
void platform_delay_ms(uint16_t ms) { now += ms; }

static uint16_t word(uint8_t reg) {
  return ((uint16_t)(registers[0][reg] & 7) << 8) | registers[0][reg + 1];
}

static uint16_t timing(uint8_t index) {
  return ((uint16_t)ports[0][0x2a][index] << 8) | ports[0][0x2a][index + 1];
}

static uint32_t factor(uint8_t axis) {
  uint8_t index = 0x80 + axis * 3;
  return ((uint32_t)ports[0][0x33][index] << 16) |
         ((uint32_t)ports[0][0x33][index + 1] << 8) |
         ports[0][0x33][index + 2];
}

static void fixture(uint16_t width, uint16_t period) {
  uint16_t count = width == 640 ? 799 : 999;
  memset(measurement, 0, sizeof(measurement));
  measurement[1][0] = (uint8_t)(count >> 8);
  measurement[1][1] = (uint8_t)count;
  measurement[1][2] = 1;
  measurement[1][3] = 223; /* 479 active lines. */
  count = width - 1;
  measurement[1][4] = (uint8_t)((count >> 8) << 4);
  measurement[1][5] = (uint8_t)count;
  measurement[0][0] = (uint8_t)(period >> 12);
  measurement[0][1] = (uint8_t)(period >> 4);
  measurement[0][2] = 2;
  measurement[0][3] = 12; /* 524 total lines, accepted counter endpoint. */
  measurement[0][4] = period & 15;
  stalled = 0;
}

static void reject_cases(void) {
  video_signal_t signal;
  static const uint8_t errors[] = {
    VIDEO_GEOMETRY, VIDEO_GEOMETRY, VIDEO_DIGITAL_TOTAL,
    VIDEO_VERTICAL_TOTAL, VIDEO_POLARITY, VIDEO_POLARITY,
    VIDEO_ANALOG_OVERFLOW, VIDEO_ANALOG_TIMEOUT, VIDEO_ZERO_PERIOD,
    VIDEO_DIGITAL_OVERFLOW
  };
  unsigned i;
  for (i = 0; i < sizeof(errors); ++i) {
    fixture(800, 13714);
    switch (i) {
      case 0: fixture(720, 13714); break;
      case 1: measurement[1][3] = 224; break;
      case 2: measurement[1][1] = 0; break;
      case 3: measurement[0][3] = 11; break;
      case 4: measurement[0][2] |= 0x40; break;
      case 5: measurement[0][2] |= 0x80; break;
      case 6: measurement[0][0] |= 0x10; break;
      case 7: measurement[0][2] |= 0x20; break;
      case 8: fixture(800, 0); break;
      case 9: measurement[1][0] |= 0x10; break;
    }
    assert(!video_measure(&signal));
    assert(!signal.width && !signal.height && !signal.output_clock_hz);
    assert(signal.error == errors[i]);
    if (i == 0) {
      assert(signal.detail[0] == 1000 && signal.detail[1] == 720 &&
             signal.detail[2] == 480);
    } else if (i == 4 || i == 5) {
      assert(signal.detail[0] == 13714 && signal.detail[1] == 524);
      assert(signal.detail[2] == (i == 4 ? 1 : 2));
    }
  }
  fixture(800, 16000); /* 27 kHz: outside the admitted line-rate range. */
  assert(!video_measure(&signal));
  assert(signal.error == VIDEO_LINE_RATE && signal.detail[0] == 16000);
  fixture(800, 13714);
  stalled = 0x20;
  now = UINT32_MAX - 20;
  assert(!video_measure(&signal)); /* timeout remains bounded across wrap. */
  assert(signal.error == VIDEO_DIGITAL_TIMEOUT);
  assert(!(registers[0][0x52] & 0x20));
  fixture(800, 13714);
  stalled = 0x40;
  assert(!video_measure(&signal)); /* Completed measurement, stalled pop-up. */
  assert(signal.error == VIDEO_DIGITAL_TIMEOUT);
  assert(!(registers[0][0x52] & 0x40));
  assert(!video_measure(NULL));
  assert(!video_apply(NULL));
}

static void clock_range(void) {
  video_signal_t signal;
  uint16_t period;
  for (period = 13620; period < 13810; ++period) {
    uint32_t expected = (uint32_t)(432000000000ULL / period);
    uint8_t admitted;
    fixture(800, period);
    admitted = video_measure(&signal);
    assert(admitted == (expected >= 31300000UL && expected <= 31700000UL));
    if (admitted) {
      uint32_t base;
      uint32_t estimate;
      uint16_t offset;
      assert(signal.output_clock_hz == expected);
      assert(video_apply(&signal));
      assert(registers[1][0xc0] == 0x26);
      assert(registers[1][0xc1] == 0x81);
      base = 421875UL * (registers[1][0xbf] + 2);
      offset = ((uint16_t)registers[1][0xc4] << 8) | registers[1][0xc5];
      assert(offset <= 4095);
      estimate = base + (uint32_t)((uint64_t)base * offset / 32768);
      assert((estimate > expected ? estimate - expected : expected - estimate)
             < 1000); /* Fine-tune quantization and denominator rounding. */
    }
  }
}

int main(void) {
  video_signal_t signal;
  unsigned phase;
  unsigned tap;
  unsigned sum;

  video_init();
  assert(registers[2][0xa7] == 0x6f); /* UC-586 differential and R/B swaps. */
  assert(timing(0) + 4 == panel.htotal);
  assert(timing(5) + 10 == panel.hstart);
  assert(timing(7) - timing(5) == panel.width);
  assert(timing(0x12) - timing(0x10) == panel.height);
  assert(registers[0][0x29] == 6);
  assert((registers[0][0x28] & 0xa8) == 0xa0); /* Forced background, free-run. */
  assert(!(registers[0][0x28] & 0x14)); /* 24-bit single-port output. */
  assert(coefficient_count == 128);
  for (phase = 0; phase < 16; ++phase) {
    sum = 0;
    for (tap = 0; tap < 4; ++tap) {
      unsigned offset = 2 * (16 * tap + phase);
      sum += coefficients[offset] | ((unsigned)coefficients[offset + 1] << 8);
    }
    assert(sum == 1024); /* Every phase preserves a uniform image. */
  }

  fixture(800, 13714);
  assert(video_measure(&signal));
  assert(signal.error == VIDEO_OK && signal.detail[0] == 13714 &&
         signal.detail[1] == 524 && signal.detail[2] == 0);
  assert(signal.width == 800 && signal.height == 480);
  assert(signal.output_clock_hz == (uint32_t)(432000000000ULL / 13714));
  assert(video_apply(&signal));
  assert(word(0x14) == 86 && word(0x18) == 32);
  assert(word(0x16) == 800 && word(0x1a) == 480);
  assert(registers[0][0x16] & 8); /* Capture-width write preserves TMDS path. */
  assert(factor(0) == 0xfffff && factor(1) == 0xfffff);
  assert((registers[0][0x32] & 0x13) == 0x10);
  assert(registers[0][0x40] == 2 && registers[0][0x41] == 40);
  assert((registers[0][0x28] & 0xa8) == 0x88); /* Input video, frame sync. */
  video_blank(1);
  assert((registers[0][0x28] & 0xa8) == 0xa0); /* Signal loss needs no IVS. */
  video_blank(0);
  assert((registers[0][0x28] & 0xa8) == 0x88);

  fixture(640, 13714);
  measurement[0][3] = 13; /* The alternate 525-line endpoint also works. */
  assert(video_measure(&signal) && video_apply(&signal));
  assert(word(0x14) == 142 && word(0x18) == 35);
  assert(word(0x16) == 640 && factor(0) == 0xccccd);
  assert((registers[0][0x32] & 0x13) == 0x11);
  assert(registers[0][0x40] == 5 && registers[0][0x41] == 44);

  signal.output_clock_hz = UINT32_MAX;
  assert(!video_apply(&signal));
  clock_range();
  reject_cases();
  puts("video: panel encoding, scaling, mode validation and timeout checks passed");
  return 0;
}
