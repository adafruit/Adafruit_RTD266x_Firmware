// SPDX-License-Identifier: MIT
#ifndef RTD_OSD_H
#define RTD_OSD_H

/* Call after the scaler reset. Uploads original glyphs with overlay disabled. */
void osd_init(void);
/* Draw the large centered title. The app owns the background and duration;
 * display timing must already be running. Designed for an 800x480 panel.
 */
void osd_show_splash(void);
void osd_hide(void);

#endif
