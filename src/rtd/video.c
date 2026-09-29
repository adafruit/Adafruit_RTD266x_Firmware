// SPDX-License-Identifier: MIT
#include "rtd/video.h"
#include "rtd/ddcci.h"

#include "rtd/board.h"
#include "rtd/io.h"
#include "rtd/panel.h"
#include "rtd/platform.h"

/* Common-page register addresses from the RTD2660 register manual. */
enum {
  HOST = 0x01,
  INPUT = 0x10,
  INPUT_POLARITY = 0x11,
  CAPTURE_X = 0x14,
  CAPTURE_WIDTH = 0x16,
  CAPTURE_Y = 0x18,
  CAPTURE_HEIGHT = 0x1a,
  CAPTURE_VDELAY = 0x1c,
  CAPTURE_HDELAY = 0x1d,
  CAPTURE_DELAY_HIGH = 0x1e,
  DISPLAY = 0x28,
  DISPLAY_POLARITY = 0x29,
  TIMING_PORT = 0x2a,
  FIFO_PORT = 0x30,
  SCALE = 0x32,
  SCALE_PORT = 0x33,
  FILTER = 0x35,
  FILTER_DATA = 0x36,
  FRAME_LINES = 0x40,
  FRAME_CLOCKS = 0x41,
  FRAME_CONTROL = 0x43,
  MEASURE_SOURCE = 0x47,
  SYNC_SOURCE = 0x49,
  MEASURE_H = 0x52,
  MEASURE_V = 0x54,
  MEASURE_ACTIVE = 0x56,
  MEASURE_SELECT = 0x58,
  COLOR_CONTROL = 0x62,
  PICTURE_ACCESS = 0x64,
  PICTURE_DATA = 0x65,
  GAMMA = 0x67,
  DITHER = 0x6a,
  OVERLAY = 0x6c,
  BACKGROUND = 0x6d,
  OUTPUT_PORT = 0x8b
};

/* Page 1: output clock. Page 2: TMDS receiver. */
enum {
  PLL_M = 0xbf,
  PLL_N = 0xc0,
  PLL_CURRENT = 0xc1,
  PLL_CONTROL = 0xc2,
  PLL_OFFSET = 0xc4,
  PLL_OFFSET_LOW = 0xc5,
  PLL_LATCH = 0xc6,
  LAST_LINE_HIGH = 0xc7,
  LAST_LINE_V = 0xc8,
  LAST_LINE_H = 0xc9,
  LAST_LINE_CONTROL = 0xca,
  M2_POWER = 0xe4,
  TMDS_PORT = 0xa2,
  TMDS_OUTPUT = 0xa6,
  TMDS_POWER = 0xa7,
  TMDS_ANALOG = 0xab,
  TMDS_IMPEDANCE = 0xac,
  TMDS_PLL = 0xad,
  TMDS_TRACKING = 0xb5,
  HDCP_PORT = 0xc2,
  HDMI_PORT = 0xc9,
  HDMI_STATUS = 0xcb,
  HDMI_AV_CONTROL = 0x30,
  HDMI_WATCHDOG = 0x31
};

#ifdef __SDCC_mcs51
#define VIDEO_CODE __code
#else
#define VIDEO_CODE
#endif

/* Qualified source timings, not a general mode solver. CVT timing is from
 * PicoDVI's 800x480p60 definition; all active heights are 480 lines. */
typedef struct {
  uint16_t width, htotal, vtotal;
  uint16_t capture_x, capture_y, display_y;
  uint16_t minimum_line_hz, maximum_line_hz;
  uint8_t polarity, frame_lines, frame_clocks;
} input_mode_t;

static const VIDEO_CODE input_mode_t input_modes[] = {
  {640, 800, 525, 142, 35, 32, 31300, 31700, 0, 5, 44},
  {800, 1000, 525, 86, 32, 32, 31300, 31700, 0, 2, 40},
  {800, 992, 500, 166, 17, 10, 29500, 30000, 2, 9, 40}
};
static uint16_t display_vstart;
static uint16_t picture_width;
static uint8_t aspect_fill;

uint16_t video_display_vstart(void) {
  return display_vstart;
}

void video_service(void) {
  uint8_t status = rtd_read(2, HDMI_STATUS);
  uint8_t enable = (status & 0x41) == 0x41 ? 0 : 0x08;
  uint8_t control = rtd_indirect_read(2, HDMI_PORT, HDMI_AV_CONTROL);
  uint8_t watchdog;
  if ((control & 0x08) == enable) return;
  /* Set_AVMute can clear video enable as well as audio enable. Restore video
   * independently of audio rate support when HDMI clears AVMute or DVI returns.
   */
  watchdog = rtd_indirect_read(2, HDMI_PORT, HDMI_WATCHDOG);
  if (watchdog & 0x80)
    rtd_indirect_write(2, HDMI_PORT, HDMI_WATCHDOG, watchdog & 0x7f);
  rtd_indirect_write(2, HDMI_PORT, HDMI_AV_CONTROL,
                     (control & (uint8_t)~0x08) | enable);
  if (watchdog & 0x80)
    rtd_indirect_write(2, HDMI_PORT, HDMI_WATCHDOG, watchdog);
}

void video_set_picture(uint8_t brightness, uint8_t contrast) {
  uint8_t channel;
  uint8_t access = rtd_read(0, PICTURE_ACCESS);
  if (brightness > 100) brightness = 100;
  if (contrast > 100) contrast = 100;
  brightness = (uint8_t)(((uint16_t)brightness * 255u + 50u) / 100u);
  contrast = (uint8_t)(((uint16_t)contrast * 255u + 50u) / 100u);
  /* Manual pp60-62: Set A applies to the full picture without a highlight
   * window. Each control has three RGB coefficients, with 128 neutral.
   */
  rtd_update(0, COLOR_CONTROL, 3, 0);
  rtd_write(0, PICTURE_ACCESS, (access & 0x70) | 0x80);
  for (channel = 0; channel < 3; ++channel)
    rtd_write(0, PICTURE_DATA, brightness);
  for (channel = 0; channel < 3; ++channel)
    rtd_write(0, PICTURE_DATA, contrast);
  rtd_write(0, PICTURE_ACCESS, access & 0x7f);
  rtd_update(0, COLOR_CONTROL, 3, 3);
}

static void timing_word(uint8_t index, uint16_t value) {
  rtd_indirect_write(0, TIMING_PORT, index, (uint8_t)(value >> 8));
  rtd_indirect_write(0, TIMING_PORT, index + 1, (uint8_t)value);
}

/* This driver supports the 29.5..31.7 MHz output-clock families below.
 * The UC-586 measurements require an additional divide-by-two relative to
 * the RTD2660 manual's PLL example. N=8 and output divisor=4 therefore give
 * one M step of 27 MHz / 64 = 421875 Hz. See docs/video-registers.md.
 */
static uint8_t output_clock(uint32_t hz) {
  uint8_t multiplier;
  uint8_t current;
  uint16_t correction;
  uint32_t base;

  if (hz < 29500000UL || hz > 33000000UL)
    return 0;
  multiplier = (uint8_t)(hz / 421875UL);
  base = 421875UL * multiplier;
  /* Offset has 15 fractional bits. Divide first to fit 32-bit arithmetic;
   * base>>15 loses less than 1 Hz in the correction denominator.
   */
  correction = (uint16_t)((hz - base) / (base >> 15));
  if (correction > 4095)
    return 0;
  current = (uint8_t)(((uint16_t)multiplier * 100U) / 3667U);
  if (current == 0)
    current = 1;

  rtd_write(1, PLL_M, multiplier - 2);
  rtd_write(1, PLL_N, 0x26); /* N=8, divisor=4, running. */
  rtd_write(1, PLL_CURRENT, 0x80 | (current - 1));
  rtd_update(1, PLL_CONTROL, 0x02, 0x02);
  rtd_update(1, PLL_CONTROL, 0x01, 0x01);
  rtd_write(1, PLL_OFFSET, (uint8_t)(correction >> 8));
  rtd_write(1, PLL_OFFSET_LOW, (uint8_t)correction);
  rtd_update(1, PLL_LATCH, 0x04, 0x04);
  rtd_update(1, LAST_LINE_CONTROL, 0x01, 0x01);
  return 1;
}

static void panel_timing(uint16_t vtotal, uint16_t vstart) {
  uint16_t left = panel.hstart - 10;
  uint16_t right = left + panel.width;
  uint16_t bottom = vstart + panel.height;

  display_vstart = vstart;

  /* Manual pp31-34: total counts four early; horizontal edges ten early. */
  timing_word(0x00, panel.htotal - 4);
  rtd_indirect_write(0, TIMING_PORT, 0x02, panel.hsync);
  timing_word(0x03, left);
  timing_word(0x05, left);
  timing_word(0x07, right);
  timing_word(0x09, right);
  timing_word(0x0b, vtotal);
  rtd_indirect_write(0, TIMING_PORT, 0x0d, panel.vsync);
  timing_word(0x0e, vstart);
  timing_word(0x10, vstart);
  timing_word(0x12, bottom);
  timing_word(0x14, bottom);

  rtd_write(0, DISPLAY_POLARITY, 0x06); /* Negative HS/VS, positive DE. */
  rtd_write(0, DISPLAY, 0xa3); /* Single RGB888, background, free-running. */
  rtd_write(1, LAST_LINE_HIGH,
            (uint8_t)(((panel.htotal >> 8) << 4) | (vtotal >> 8)));
  rtd_write(1, LAST_LINE_V, (uint8_t)vtotal);
  rtd_write(1, LAST_LINE_H, (uint8_t)panel.htotal);

  rtd_indirect_write(0, OUTPUT_PORT, 0x00, 0x00); /* TTL output. */
  rtd_indirect_write(0, TIMING_PORT, 0x20, 0x02); /* DCLK output on. */
  rtd_indirect_write(0, OUTPUT_PORT, 0xa0, 0x30); /* Output power state. */
}

static void receiver_init(void) {
  rtd_update(0, CAPTURE_WIDTH, 0x08, 0x08); /* TMDS capture path. */
  /* 00 is TMDS on this silicon; older RTD2660 documentation says reserved. */
  rtd_update(0, SYNC_SOURCE, 0x03, 0);
  rtd_update(0, INPUT, 0x0f, 0x07);
  video_blank(1); /* Keep the startup raster independent of input sync. */
  rtd_update(0, INPUT_POLARITY, 0x0c, 0x0c);

  rtd_write(2, TMDS_IMPEDANCE, 0xe3);
  rtd_update(2, TMDS_OUTPUT, 0x78, 0x78);
  /* These two receiver analog settings are retained as observed hardware
   * requirements, not claimed as fully understood tuning choices.
   */
  rtd_update(2, TMDS_ANALOG, 0x03, 0x03);
  rtd_update(2, TMDS_TRACKING, 0x80, 0x80);
  rtd_update(2, TMDS_PLL, 0x1c, 0x04);
  /* Port 0, HS/VS capture; differential and lane swaps follow PCB routing. */
  rtd_write(2, TMDS_POWER, 0x0f | (BOARD_TMDS_SWAP_PN ? 0x40 : 0) |
            (BOARD_TMDS_SWAP_RB ? 0x20 : 0));
  rtd_update(2, TMDS_PORT, 0x02, 0);
  rtd_update(2, HDCP_PORT, 0x02, 0);
  rtd_indirect_update(2, HDMI_PORT, 0x00, 0x03, 0x02); /* Auto HDMI/DVI. */
  rtd_indirect_update(2, HDMI_PORT, 0x30, 0x08, 0x08); /* Video output. */
}

static void linear_filter(void) {
  uint8_t coefficient;
  uint16_t weight;

  /* A newly generated triangular kernel; no inherited filter table.
   * 4 taps x 16 stored half-phases, mirrored to 32 phases by hardware.
   * Phase/layout and unity=1024 are inferred, pending image-quality checks.
   * p=(phase+0.5)/32 gives weights [0, 1024*p, 1024*(1-p), 0].
   */
  rtd_write(0, FILTER, 0x00);
  rtd_write(0, FILTER, 0xcc); /* Write inactive bank 2, both color paths. */
  for (coefficient = 0; coefficient < 64; ++coefficient) {
    if (coefficient < 16 || coefficient >= 48)
      weight = 0;
    else if (coefficient < 32)
      weight = 16U + 32U * (coefficient - 16U);
    else
      weight = 1008U - 32U * (coefficient - 32U);
    rtd_write(0, FILTER_DATA, (uint8_t)weight);
    rtd_write(0, FILTER_DATA, (uint8_t)(weight >> 8));
  }
  rtd_write(0, FILTER, 0x22); /* Horizontal bank 2; vertical stays bypassed. */
}

void video_init(void) {
  picture_width = 0;
  aspect_fill = 0;
  rtd_update(0, HOST, 0x01, 0x01);
  platform_delay_ms(20);
  rtd_update(0, HOST, 0x07, 0);
  rtd_write(1, M2_POWER, 0);
  output_clock(panel.clock_hz);
  panel_timing(panel.vtotal, panel.vstart);
  rtd_write(0, GAMMA, 0);
  rtd_write(0, DITHER, 0);
  video_set_picture(50, 50);
  linear_filter();
  receiver_init();
}

static uint8_t measurement_step(uint8_t bit, uint8_t timeout_ms) {
  uint32_t began = platform_millis();
  rtd_update(0, MEASURE_H, bit, bit);
  while (rtd_read(0, MEASURE_H) & bit) {
    ddcci_service();
    if ((uint32_t)(platform_millis() - began) >= timeout_ms) {
      rtd_update(0, MEASURE_H, bit, 0);
      return 0;
    }
  }
  return 1;
}

static uint8_t measure(uint8_t digital) {
  rtd_update(0, MEASURE_SOURCE, 0x01, digital);
  if (!measurement_step(0x20, 50) || !measurement_step(0x40, 60))
    return 1; /* Timeout. */
  if (rtd_read(0, MEASURE_V) & 0x20)
    return 1;
  if ((rtd_read(0, MEASURE_H) & 0x10) ||
      (rtd_read(0, MEASURE_V) & 0x10))
    return 2; /* Counter overflow. */
  return 0;
}

static uint16_t measured_count(uint8_t reg) {
  return ((uint16_t)(rtd_read(0, reg) & 0x0f) << 8) |
         rtd_read(0, reg + 1);
}

uint8_t video_measure(video_signal_t *signal) {
  uint16_t width;
  uint16_t total;
  uint16_t period;
  uint32_t line_hz;
  uint8_t status;
  uint8_t polarity;
  uint8_t mode_index = 0;
  const VIDEO_CODE input_mode_t *mode = &input_modes[1];

  if (!signal)
    return 0;
  signal->width = signal->height = 0;
  signal->output_clock_hz = 0;
  signal->error = VIDEO_OK;
  signal->mode = VIDEO_MODE_NONE;
  signal->input_width = signal->input_height = 0;
  signal->htotal = signal->vtotal = 0;
  signal->line_hz = 0;
  signal->measured = signal->polarity = 0;
  status = measure(1);
  /* UC-586 digital counters are one short; analog periods are not. */
  rtd_update(0, MEASURE_SELECT, 0x01, 0);
  width = ((uint16_t)(rtd_read(0, MEASURE_ACTIVE) & 0xf0) << 4) |
          rtd_read(0, MEASURE_ACTIVE + 1);
  ++width;
  total = measured_count(MEASURE_H) + 1;
  signal->detail[0] = total;
  signal->detail[1] = width;
  signal->detail[2] = measured_count(MEASURE_V) + 1;
  if (status) {
    signal->error = status == 1 ? VIDEO_DIGITAL_TIMEOUT : VIDEO_DIGITAL_OVERFLOW;
    return 0;
  }
  signal->input_width = width;
  signal->input_height = signal->detail[2];
  signal->htotal = total;
  signal->measured = VIDEO_MEASURE_GEOMETRY;
  if ((width != 800 && width != 640) || signal->detail[2] != 480) {
    signal->error = VIDEO_GEOMETRY;
  } else {
    for (mode_index = 0; mode_index < 3; ++mode_index) {
      mode = &input_modes[mode_index];
      if (width == mode->width && total == mode->htotal)
        break;
    }
    if (mode_index == 3)
      signal->error = VIDEO_DIGITAL_TOTAL;
  }

  /* Unsupported geometry still deserves a useful measured timing report.
   * Keep its earlier error and digital detail if analog measurement fails.
   */
  status = measure(0);
  total = measured_count(MEASURE_V);
  period = (measured_count(MEASURE_H) << 4) |
           (rtd_read(0, MEASURE_ACTIVE) & 0x0f);
  polarity = rtd_read(0, MEASURE_V) >> 6;
  if (!signal->error) {
    signal->detail[0] = period;
    signal->detail[1] = total;
    signal->detail[2] = polarity;
  }
  if (status) {
    if (!signal->error)
      signal->error = status == 1 ? VIDEO_ANALOG_TIMEOUT : VIDEO_ANALOG_OVERFLOW;
    return 0;
  }
  signal->vtotal = total;
  signal->polarity = polarity;
  line_hz = period ? 432000000UL / period : 0;
  signal->line_hz = line_hz;
  signal->measured |= VIDEO_MEASURE_TIMING;
  if (signal->error)
    return 0;
  if (polarity != mode->polarity) {
    signal->error = VIDEO_POLARITY;
    return 0;
  }
  if (total != mode->vtotal - 1 && total != mode->vtotal) {
    signal->error = VIDEO_VERTICAL_TOTAL;
    return 0;
  }
  if (!period) {
    signal->error = VIDEO_ZERO_PERIOD;
    return 0;
  }
  /* 27 MHz crystal, 16-line average. */
  if (line_hz < mode->minimum_line_hz || line_hz > mode->maximum_line_hz) {
    signal->error = VIDEO_LINE_RATE;
    return 0;
  }

  /* Quotient/remainder preserves the fractional line rate without needing
   * a 432 MHz * 1000 intermediate, which exceeds 32 bits on the 8051.
   */
  signal->output_clock_hz = line_hz * panel.htotal +
      ((432000000UL % period) * panel.htotal) / period;
  if (signal->output_clock_hz < (uint32_t)mode->minimum_line_hz * panel.htotal ||
      signal->output_clock_hz > (uint32_t)mode->maximum_line_hz * panel.htotal) {
    signal->output_clock_hz = 0;
    signal->error = VIDEO_LINE_RATE;
    return 0;
  }
  signal->width = width;
  signal->height = 480;
  signal->mode = mode_index + 1;
  return 1;
}

static void capture_word(uint8_t reg, uint16_t value) {
  rtd_update(0, reg, 0x07, (uint8_t)(value >> 8));
  rtd_write(0, reg + 1, (uint8_t)value);
}

static void scale_factor(uint32_t factor) {
  rtd_write(0, SCALE_PORT + 1, (uint8_t)(factor >> 16));
  rtd_write(0, SCALE_PORT + 1, (uint8_t)(factor >> 8));
  rtd_write(0, SCALE_PORT + 1, (uint8_t)factor);
}

static void apply_aspect(void) {
  uint16_t width = aspect_fill ? panel.width : picture_width;
  uint16_t left = panel.hstart - 10 + (panel.width - width) / 2;
  uint8_t scale = width != picture_width;
  /* Only the picture window changes; full-panel DE/background and both
   * clock domains continue running. All admitted sources are 480 lines.
   */
  timing_word(0x05, left);
  timing_word(0x07, left + width);
  rtd_write(0, SCALE_PORT, 0x80);
  scale_factor(scale ? 0xccccdUL : 0xfffffUL); /* VGA 640 -> 800, or unity. */
  scale_factor(0xfffffUL);
  rtd_write(0, SCALE_PORT, 0);
  rtd_update(0, SCALE, 0x13, 0x10 | scale);
}

void video_set_aspect(uint8_t fill) {
  aspect_fill = fill != 0;
  if (picture_width) apply_aspect();
}

uint8_t video_apply(const video_signal_t *signal) {
  const VIDEO_CODE input_mode_t *mode;

  if (!signal || signal->mode < VIDEO_MODE_VGA || signal->mode > VIDEO_MODE_CVT)
    return 0;
  mode = &input_modes[signal->mode - 1];
  if (signal->height != 480 || signal->width != mode->width ||
      signal->output_clock_hz < (uint32_t)mode->minimum_line_hz * panel.htotal ||
      signal->output_clock_hz > (uint32_t)mode->maximum_line_hz * panel.htotal)
    return 0;
  video_blank(1);
  if (!output_clock(signal->output_clock_hz))
    return 0;
  panel_timing(mode->vtotal, mode->display_y);
  video_background(0, 0, 0);

  rtd_update(0, INPUT, 0x02, 0); /* Sync-relative capture, not DE window. */
  /* Normalize each source's sync pulses before applying its capture window. */
  rtd_update(0, INPUT_POLARITY, 0x0c,
             (mode->polarity & 1 ? 0 : 0x04) | (mode->polarity & 2 ? 0 : 0x08));
  capture_word(CAPTURE_X, mode->capture_x);
  capture_word(CAPTURE_Y, mode->capture_y);
  capture_word(CAPTURE_WIDTH, signal->width);
  capture_word(CAPTURE_HEIGHT, signal->height);
  rtd_update(0, CAPTURE_DELAY_HIGH, 0x03, 0);
  rtd_write(0, CAPTURE_HDELAY, 0);
  rtd_write(0, CAPTURE_VDELAY, 0);
  rtd_indirect_write(0, FIFO_PORT, 0,
      (uint8_t)(((signal->width >> 8) << 4) | (signal->height >> 8)));
  rtd_indirect_write(0, FIFO_PORT, 1, (uint8_t)signal->width);
  rtd_indirect_write(0, FIFO_PORT, 2, (uint8_t)signal->height);

  picture_width = signal->width;
  apply_aspect();
  rtd_update(0, FRAME_CONTROL, 0x02, 0);
  /* Empirical register codes from aligned grid tests, not a general timing
   * solver. The manual's CR41 formula and earlier clock labels disagree.
   */
  rtd_write(0, FRAME_LINES, mode->frame_lines);
  rtd_write(0, FRAME_CLOCKS, mode->frame_clocks);
  rtd_update(6, 0xe3, 0x13, 0); /* Downscaler and extended buffer off. */
  rtd_update(6, 0xe4, 0x0c, 0); /* Downscaler buffer bypass. */
  video_blank(0);
  return 1;
}

void video_blank(uint8_t blank) {
  if (blank) picture_width = 0;
  /* CR28[3]=0 free-runs the panel; bit5 selects its full-screen background.
   * Once capture is configured, select frame sync and incoming video together.
   */
  rtd_update(0, DISPLAY, 0x28, blank ? 0x20 : 0x08);
  if (blank && display_vstart != panel.vstart) {
    /* Restore the full-height startup/no-signal raster once when leaving
     * short-blanking input. OSD positioning follows this output origin. */
    output_clock(panel.clock_hz);
    panel_timing(panel.vtotal, panel.vstart);
  }
}

void video_background(uint8_t red, uint8_t green, uint8_t blue) {
  rtd_update(0, OVERLAY, 0x20, 0x20);
  rtd_write(0, BACKGROUND, red);
  rtd_write(0, BACKGROUND, green);
  rtd_write(0, BACKGROUND, blue);
  rtd_update(0, OVERLAY, 0x20, 0);
}
