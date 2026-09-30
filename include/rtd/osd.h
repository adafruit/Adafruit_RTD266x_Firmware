// SPDX-License-Identifier: MIT
#ifndef RTD_OSD_H
#define RTD_OSD_H

#include "rtd/video.h"

/* Upload and show a centered bitmap. Each call replaces the OSD SRAM and
 * palette, so call on screen transitions, not every poll. The app owns the
 * background and duration; display timing must already be running.
 */
void osd_show_splash(void);
void osd_show_no_signal(void);
/* Replace the bitmap with measured input information at the top left. */
void osd_show_input(const video_signal_t *signal);
/* Static artwork review; no settings or button actions are attached. */
enum {
  OSD_PREVIEW_MAIN, OSD_PREVIEW_PICTURE, OSD_PREVIEW_AUDIO,
  OSD_PREVIEW_DISPLAY, OSD_PREVIEW_MENU, OSD_PREVIEW_COUNT
};
/* Variants 0, 1, 2 exercise low/middle/high sample values and selection rows. */
void osd_show_menu_preview(uint8_t page, uint8_t variant);
/* Restore the overlay port if a hardware background transition cleared it. */
void osd_service(void);
/* Apply on the next live-menu draw only. Position is 0..100 inside the
 * visible panel, with 50 centered (four-pixel horizontal steps). Transparency
 * 0..100 maps to eight background-blending levels, opaque through 7/8 video;
 * foreground text stays opaque. Out-of-range arguments clamp to 100.
 */
void osd_set_menu_style(uint8_t x, uint8_t y, uint8_t transparency);
/* Framed live menu; caller supplies up to five items between title/footer. */
/* Tab 0..3 selects Picture/Audio/Display/Settings; rail_focus highlights
 * category navigation instead of a setting in the right pane. */
void osd_menu_begin(const char *title, uint8_t tab, uint8_t rail_focus);
void osd_menu_row(const char *label, const char *choice, uint8_t value,
                  uint8_t percent, uint8_t selected, uint8_t available);
void osd_menu_end(const char *footer);
void osd_hide(void);

#endif
