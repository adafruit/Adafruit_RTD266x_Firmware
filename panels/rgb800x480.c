// SPDX-License-Identifier: MIT
#include "rtd/panel.h"

/* Physical pixels/lines, before register-specific offsets.
 * KD50G21-40NT-A1 datasheet p14, with extended H front porch for 60 Hz.
 * The UC-586 panel is provisionally treated as this generic RGB888 class.
 */
const panel_t panel = {
  800, 480, 1000, 525, 88, 32, 48, 3, 31500000UL
};
