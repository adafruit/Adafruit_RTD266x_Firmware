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

enum {
  VIDEO_MEASURE_GEOMETRY = 1,
  VIDEO_MEASURE_TIMING = 2
};

enum {
  VIDEO_MODE_NONE = 0,
  VIDEO_MODE_VGA,
  VIDEO_MODE_PANEL,
  VIDEO_MODE_CVT
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
  /* Independent measurements, retained even when the mode is unsupported.
   * Check measured flags before use; failed measurement fields stay zero.
   */
  uint16_t input_width;
  uint16_t input_height;
  uint16_t htotal;
  uint16_t vtotal;
  uint32_t line_hz;
  uint8_t measured;
  uint8_t polarity; /* bit0=positive H, bit1=positive V */
  uint8_t mode; /* Qualified timing profile, or VIDEO_MODE_NONE. */
} video_signal_t;

void video_init(void);
/* Returns zero for absent, timed-out, or unsupported input. */
uint8_t video_measure(video_signal_t *signal);
uint8_t video_apply(const video_signal_t *signal);
/* Actual output origin, including the shorter blanking of the CVT profile. */
uint16_t video_display_vstart(void);
/* A blanked display free-runs without input; unblank only after video_apply. */
void video_blank(uint8_t blank);
void video_background(uint8_t red, uint8_t green, uint8_t blue);

#endif
