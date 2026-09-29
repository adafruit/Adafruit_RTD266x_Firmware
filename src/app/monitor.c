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

/* Application policy lives here; register setup belongs to the drivers.
 * Two matching samples acquire a mode. Signal loss blanks immediately.
 * Same-width rate changes without loss are intentionally not tracked yet.
 */
void main(void) {
  video_signal_t signal;
  uint16_t displayed_width = 0, candidate_width = 0;
  uint8_t matching_samples = 0;

  platform_init();
  mcu_write(0x19, 'N'); /* New firmware; scratch register, not flash. */
  mcu_write(0xf2, 1);
  edid_publish();
  video_init();
  board_init();
  video_background(8, 8, 8);
  mcu_write(0xf2, 2);
#if RTD_SPLASH
  /* The startup screen owns the entire raster. Start video acquisition only
   * after it ends, so incoming pixels cannot appear behind the bitmap.
   */
  video_background(0, 32, 128);
  osd_init();
  osd_show_splash();
  platform_delay_ms(5000);
  osd_hide();
  video_background(8, 8, 8);
#endif

  for (;;) {
    if (!video_measure(&signal)) {
      video_blank(1);
      displayed_width = candidate_width = 0;
      matching_samples = 0;
      mcu_write(0xf2, 3);
    } else if (signal.width != displayed_width) {
      if (signal.width != candidate_width) {
        candidate_width = signal.width;
        matching_samples = 1;
      } else if (++matching_samples >= 2) {
        if (video_apply(&signal)) {
          displayed_width = signal.width;
          mcu_write(0xf2, 4);
        }
        matching_samples = 0;
      }
    } else {
      candidate_width = matching_samples = 0;
    }
#if RTD_TRACE
    diagnostics_measurement(signal.error, signal.detail[0], signal.detail[1],
                             signal.detail[2]);
#endif
    platform_delay_ms(250);
  }
}
