// SPDX-License-Identifier: MIT
#ifndef RTD_DIAGNOSTICS_H
#define RTD_DIAGNOSTICS_H
#include <stdint.h>

/* Bench-only ASCII EDID descriptor, not a display setting or normal mode. */
void diagnostics_measurement(uint8_t stage, uint16_t a, uint16_t b, uint16_t c);

#endif
