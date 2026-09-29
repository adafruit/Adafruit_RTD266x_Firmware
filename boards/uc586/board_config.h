// SPDX-License-Identifier: MIT
#ifndef UC586_BOARD_CONFIG_H
#define UC586_BOARD_CONFIG_H

#define BOARD_CRYSTAL_HZ 27000000UL
/* UC-586 routes port 0 with reversed differential polarity and R/B lanes. */
#define BOARD_TMDS_SWAP_PN 1
#define BOARD_TMDS_SWAP_RB 1
/* Measured horizontal OSD shift on the UC-586 at 4x zoom, in panel pixels.
 * Subtract this from the frame delay to center artwork in the active raster. */
#define BOARD_OSD_X_CORRECTION 32u

#endif
