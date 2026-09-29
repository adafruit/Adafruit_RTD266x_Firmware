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
void osd_show_menu_preview(void);
/* Restore the overlay port if a hardware background transition cleared it. */
void osd_service(void);
void osd_hide(void);

#endif
