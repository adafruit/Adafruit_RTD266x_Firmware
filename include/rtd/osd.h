// SPDX-License-Identifier: MIT
#ifndef RTD_OSD_H
#define RTD_OSD_H

/* Upload and show a centered bitmap. Each call replaces the OSD SRAM and
 * palette, so call on screen transitions, not every poll. The app owns the
 * background and duration; display timing must already be running.
 */
void osd_show_splash(void);
void osd_show_no_signal(void);
void osd_hide(void);

#endif
