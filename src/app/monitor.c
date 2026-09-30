// SPDX-License-Identifier: MIT
#include "rtd/audio.h"
#include "rtd/board.h"
#include "rtd/control.h"
#include "rtd/ddcci.h"
#include "rtd/diagnostics.h"
#include "rtd/edid.h"
#include "rtd/io.h"
#include "rtd/osd.h"
#include "rtd/platform.h"
#include "rtd/video.h"

#ifndef RTD_SPLASH
#define RTD_SPLASH 0
#endif

#ifndef RTD_MENU_PREVIEW
#define RTD_MENU_PREVIEW 0
#endif

#define SPLASH_DURATION_MS 1000
#define INPUT_INFO_DURATION_MS 3000

static video_signal_t shown_signal;

/* Ignore small measurement jitter while keeping changed rejected settings
 * visible. Compare measured fields, never the possibly stale trace details. */
static uint8_t input_info_changed(const video_signal_t *signal) {
  uint32_t difference = signal->line_hz > shown_signal.line_hz
                            ? signal->line_hz - shown_signal.line_hz
                            : shown_signal.line_hz - signal->line_hz;
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
 * Identify the timing profile as well as width when qualifying a source.
 */
void main(void) {
  /* This state lives for the entire program. Keep it in XRAM: DDC callbacks
   * can run inside video measurement, and the 8051 return stack is only 256
   * bytes including interrupt context and every active call's local data. */
  static video_signal_t signal;
  static uint8_t displayed_mode = VIDEO_MODE_NONE, candidate_mode = VIDEO_MODE_NONE;
  static uint8_t matching_samples = 0, screen = 0, audio_tick;
  static uint32_t info_started = 0;
  static uint32_t missing_started = 0, timeout;
  static uint8_t missing = 0, sleeping = 0, was_powered = 1;
#if RTD_MENU_PREVIEW
  static uint8_t preview_page, preview_variant;
#endif

  platform_init();
  mcu_write(0x19, 'N'); /* New firmware; scratch register, not flash. */
  mcu_write(0xf2, 1);
  edid_publish();
  video_init();
  board_init();
  audio_init();
  control_init();
  ddcci_init();
  video_background(0, 0, 0);
  mcu_write(0xf2, 2);
  /* The startup screen owns the entire raster. Start video acquisition only
   * after it ends, so incoming pixels cannot appear behind the bitmap.
   */
  if (control_setting(SET_SPLASH)) {
    video_background(0, 0, 0);
    osd_show_splash();
    platform_delay_ms(SPLASH_DURATION_MS);
    osd_hide();
  }
#if RTD_MENU_PREVIEW
  for (preview_page = 0; preview_page < OSD_PREVIEW_COUNT; ++preview_page) {
    for (preview_variant = 0; preview_variant < 3; ++preview_variant) {
      osd_show_menu_preview(preview_page, preview_variant);
      platform_delay_ms(1500);
    }
  }
  osd_hide();
#endif

  for (;;) {
    if (control_overlay_changed())
      screen = 0xff;
    if (!control_power()) {
      if (was_powered) {
        audio_stop();
        video_blank(1);
        video_background(0, 0, 0);
        osd_hide();
      }
      was_powered = 0;
      displayed_mode = candidate_mode = VIDEO_MODE_NONE;
      matching_samples = 0;
    } else {
      if (!was_powered) {
        was_powered = 1;
        if (control_setting(SET_SPLASH)) {
          osd_show_splash();
          platform_delay_ms(SPLASH_DURATION_MS);
          osd_hide();
        }
        screen = 0xff;
      }
      if (!video_measure(&signal)) {
        audio_stop();
        video_blank(1);
        if (!missing) {
          missing_started = platform_millis();
          missing = 1;
        }
        timeout = control_signal_timeout_ms();
        if (timeout &&
            (uint32_t)(platform_millis() - missing_started) >= timeout &&
            !control_menu_open()) {
          if (!sleeping) {
            board_backlight_power(0);
            sleeping = 1;
          }
        } else if (sleeping) {
          board_backlight_power(1);
          board_backlight_set(control_setting(SET_BACKLIGHT));
          sleeping = 0;
        }
        if (signal.error == VIDEO_DIGITAL_TIMEOUT) {
          if (screen != 1 && !control_menu_open()) {
            osd_hide();
            video_background(0, 0,
                             control_setting(SET_NO_SIGNAL) == 1 ? 255 : 0);
            if (control_setting(SET_NO_SIGNAL) == 2)
              osd_show_no_signal();
            screen = 1;
          }
        } else if (!control_menu_open() &&
                   (screen != 2 || input_info_changed(&signal))) {
          osd_show_input(&signal);
          shown_signal = signal;
          screen = 2;
        }
        displayed_mode = candidate_mode = VIDEO_MODE_NONE;
        matching_samples = 0;
        mcu_write(0xf2, 3);
      } else if (signal.mode != displayed_mode) {
        audio_stop();
        if (signal.mode != candidate_mode) {
          candidate_mode = signal.mode;
          matching_samples = 1;
        } else if (++matching_samples >= 2) {
          if (video_apply(&signal)) {
            missing = sleeping = 0;
            board_backlight_power(1);
            board_backlight_set(control_setting(SET_BACKLIGHT));
            if (!control_menu_open()) {
              if (control_setting(SET_POPUP))
                osd_show_input(&signal);
              else
                osd_hide();
            }
            info_started = platform_millis();
            screen = control_setting(SET_POPUP) ? 3 : 0;
            displayed_mode = signal.mode;
            mcu_write(0xf2, 4);
          }
          matching_samples = 0;
        }
      } else {
        candidate_mode = matching_samples = 0;
        if (screen == 0xff && !control_menu_open()) {
          osd_hide();
          screen = 0;
        }
      }
      if (screen == 3 && !control_menu_open() &&
          (uint32_t)(platform_millis() - info_started) >=
              INPUT_INFO_DURATION_MS) {
        osd_hide();
        screen = 0;
      }
    }
#if RTD_TRACE
    diagnostics_measurement(signal.error, signal.detail[0], signal.detail[1],
                            signal.detail[2]);
#endif
    /* Keep video qualification at its existing cadence while allowing the
     * audio PLL and mute watchdogs to progress without blocking video setup.
     */
    for (audio_tick = 0; audio_tick < 25; ++audio_tick) {
      ddcci_service();
      control_service(platform_millis());
      if (displayed_mode != VIDEO_MODE_NONE)
        video_service();
      osd_service();
      audio_service(platform_millis(),
                    control_power() && displayed_mode != VIDEO_MODE_NONE);
      platform_delay_ms(10);
    }
  }
}
