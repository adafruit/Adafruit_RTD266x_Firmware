// SPDX-License-Identifier: MIT
#ifndef RTD_VIDEO_H
#define RTD_VIDEO_H

#include <stdint.h>

#ifdef __SDCC_mcs51
#define VIDEO_XDATA __xdata
#else
#define VIDEO_XDATA
#endif

/* UC-586 downscaler modes qualified with the supported 525-line inputs. */
#ifndef RTD_ASPECT_4_3
#define RTD_ASPECT_4_3 1
#endif
#ifndef RTD_ASPECT_16_9
#define RTD_ASPECT_16_9 1
#endif

enum {
  VIDEO_ASPECT_KEEP = 0,
  VIDEO_ASPECT_FILL = 1,
  VIDEO_ASPECT_4_3 = 2,
  VIDEO_ASPECT_16_9 = 3
};

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
/* Service HDMI AVMute while an accepted video mode is active. */
void video_service(void);
/* Percent controls clamp at 100; 50 gives neutral brightness and contrast. */
void video_set_picture(uint8_t brightness, uint8_t contrast);
/* RGB gains and saturation are 0..100, with 50 neutral. RGB gains multiply
 * contrast, with the resulting hardware coefficient clamped at its maximum. */
void video_set_color(uint8_t red, uint8_t green, uint8_t blue,
                     uint8_t saturation);
/* Horizontal softening/sharpening, including 1:1 input. 50 is the original
 * linear filter; the filter remains bypassed at 1:1 when neutral. A setter
 * queues/restarts the inactive-bank upload; 64 service calls apply it. */
void video_set_sharpness(uint8_t percent);
/* Call unconditionally every monitor tick, including while input is absent.
 * Uploads at most one coefficient and only selects a fully written bank. */
void video_controls_service(void);
/* Availability of the 16:9 mode depends on the current input.
 * Its saved preference falls back to Keep for incompatible input. Check
 * availability before accepting a user change; current reports the actual
 * displayed aspect. Other unsupported/invalid choices are ignored. */
uint8_t video_aspect_available(uint8_t mode);
uint8_t video_aspect_current(void);
void video_set_aspect(uint8_t mode);
/* Returns zero for absent, timed-out, or unsupported input. The result lives
 * in XRAM so SDCC can address fields directly without a large generic-pointer
 * stack frame underneath DDC callbacks. */
uint8_t video_measure(video_signal_t VIDEO_XDATA *signal);
uint8_t video_apply(const video_signal_t *signal);
/* Actual output origin, including the shorter blanking of the CVT profile. */
uint16_t video_display_vstart(void);
/* A blanked display free-runs without input; unblank only after video_apply. */
void video_blank(uint8_t blank);
void video_background(uint8_t red, uint8_t green, uint8_t blue);

#endif
