// SPDX-License-Identifier: MIT
#include "rtd/board.h"
#include "rtd/edid.h"
#include "rtd/diagnostics.h"
#include "rtd/io.h"
#include "rtd/osd.h"
#include "rtd/platform.h"
#include "rtd/video.h"

#ifndef RTD_SPLASH
#define RTD_SPLASH 0
#endif

#define SPLASH_DURATION_MS 1000
#define INPUT_INFO_DURATION_MS 3000

static video_signal_t shown_signal;

/* Ignore small measurement jitter while keeping changed rejected settings
 * visible. Compare measured fields, never the possibly stale trace details. */
static uint8_t input_info_changed(const video_signal_t *signal) {
  uint32_t difference = signal->line_hz > shown_signal.line_hz ?
      signal->line_hz - shown_signal.line_hz :
      shown_signal.line_hz - signal->line_hz;
  return signal->error != shown_signal.error ||
         signal->measured != shown_signal.measured ||
         signal->input_width != shown_signal.input_width ||
         signal->input_height != shown_signal.input_height ||
         signal->htotal != shown_signal.htotal ||
         signal->vtotal != shown_signal.vtotal ||
         signal->polarity != shown_signal.polarity || difference > 50;
}

/* Application policy lives here; register setup belongs to the drivers.
 * Two matching samples acquire a mode. Signal loss blanks immediately.
 * Same-width rate changes without loss are intentionally not tracked yet.
 */
void main(void) {
  video_signal_t signal;
  uint16_t displayed_width = 0, candidate_width = 0;
  uint8_t matching_samples = 0, screen = 0;
  uint32_t info_started = 0;

  platform_init();
  mcu_write(0x19, 'N'); /* New firmware; scratch register, not flash. */
  mcu_write(0xf2, 1);
  edid_publish();
  video_init();
  board_init();
  video_background(0, 0, 0);
  mcu_write(0xf2, 2);
#if RTD_SPLASH
  /* The startup screen owns the entire raster. Start video acquisition only
   * after it ends, so incoming pixels cannot appear behind the bitmap.
   */
  video_background(0, 0, 0);
  osd_show_splash();
  platform_delay_ms(SPLASH_DURATION_MS);
  osd_hide();
#endif

  for (;;) {
    if (!video_measure(&signal)) {
      video_blank(1);
      if (signal.error == VIDEO_DIGITAL_TIMEOUT) {
        if (screen != 1) {
          osd_show_no_signal();
          screen = 1;
        }
      } else if (screen != 2 || input_info_changed(&signal)) {
        osd_show_input(&signal);
        shown_signal = signal;
        screen = 2;
      }
      displayed_width = candidate_width = 0;
      matching_samples = 0;
      mcu_write(0xf2, 3);
    } else if (signal.width != displayed_width) {
      if (signal.width != candidate_width) {
        candidate_width = signal.width;
        matching_samples = 1;
      } else if (++matching_samples >= 2) {
        if (video_apply(&signal)) {
          osd_show_input(&signal);
          info_started = platform_millis();
          screen = 3;
          displayed_width = signal.width;
          mcu_write(0xf2, 4);
        }
        matching_samples = 0;
      }
    } else {
      candidate_width = matching_samples = 0;
    }
    if (screen == 3 &&
        (uint32_t)(platform_millis() - info_started) >= INPUT_INFO_DURATION_MS) {
      osd_hide();
      screen = 0;
    }
#if RTD_TRACE
    diagnostics_measurement(signal.error, signal.detail[0], signal.detail[1],
                             signal.detail[2]);
#endif
    platform_delay_ms(250);
  }
}
