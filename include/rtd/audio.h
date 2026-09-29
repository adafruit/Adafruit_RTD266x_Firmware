// SPDX-License-Identifier: MIT
#ifndef RTD_AUDIO_H
#define RTD_AUDIO_H
#include <stdint.h>

enum {
  AUDIO_OFF, AUDIO_MEASURE, AUDIO_PLL_START, AUDIO_PLL_VERIFY,
  AUDIO_SETTLE, AUDIO_FIFO_CHECK, AUDIO_PLAYING, AUDIO_RATE_CHECK, AUDIO_RETRY
};

/* First UC-586 audio profile: 48 kHz LPCM, stereo SD0, 256fs master clock.
 * Foreground only. Service about every 10 ms; video_valid must go false on
 * signal loss or unsupported timing. Stop before changing video clocks.
 */
void audio_init(void);
void audio_stop(void);
void audio_service(uint32_t now, uint8_t video_valid);
/* User mute survives signal loss. Unmute waits for the existing audio guards. */
void audio_set_mute(uint8_t muted);
uint8_t audio_get_mute(void);
/* Digital gain encoding is unverified; this board currently offers mute only. */
uint8_t audio_volume_available(void);
uint8_t audio_state(void);
uint32_t audio_sample_rate(void);
#endif
