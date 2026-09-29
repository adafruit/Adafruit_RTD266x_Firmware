// SPDX-License-Identifier: MIT
#ifndef RTD_EDID_H
#define RTD_EDID_H
#include <stdint.h>

void edid_build(uint8_t bytes[128]);
void edid_build_audio_extension(uint8_t bytes[128]);
void edid_publish(void);

#endif
