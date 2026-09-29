// SPDX-License-Identifier: MIT
#ifndef RTD_OSD_H
#define RTD_OSD_H

/* Call after the scaler reset. Uploads original glyphs with overlay disabled. */
void osd_init(void);
/* Display timing must already be running. Experimental until bench verified. */
void osd_show_splash(void);
void osd_hide(void);

#endif
