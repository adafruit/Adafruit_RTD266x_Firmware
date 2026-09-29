// SPDX-License-Identifier: MIT
#ifndef RTD_VIDEO_H
#define RTD_VIDEO_H

#include <stdint.h>

enum {
  VIDEO_OK = 0,
  VIDEO_DIGITAL_TIMEOUT = 1,
  VIDEO_DIGITAL_OVERFLOW = 2,
  VIDEO_GEOMETRY = 3,
  VIDEO_DIGITAL_TOTAL = 4,
  VIDEO_ANALOG_TIMEOUT = 5,
  VIDEO_ANALOG_OVERFLOW = 6,
  VIDEO_POLARITY = 7,
  VIDEO_VERTICAL_TOTAL = 8,
  VIDEO_ZERO_PERIOD = 9,
  VIDEO_LINE_RATE = 10
};

typedef struct {
  uint16_t width;
  uint16_t height;
  uint32_t output_clock_hz;
  uint8_t error;
  /* Codes 1..4: digital [horizontal total, active width, active height].
   * Codes 5..10 and success: analog [period16, vertical total, polarity].
   * Polarity bit0=positive H, bit1=positive V. Timeout data may be stale.
   */
  uint16_t detail[3];
} video_signal_t;

void video_init(void);
/* Returns zero for absent, timed-out, or unsupported input. */
uint8_t video_measure(video_signal_t *signal);
uint8_t video_apply(const video_signal_t *signal);
void video_blank(uint8_t blank);
void video_background(uint8_t red, uint8_t green, uint8_t blue);

#endif
