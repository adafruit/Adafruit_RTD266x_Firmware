// SPDX-License-Identifier: MIT
#ifndef RTD_AUDIO_H
#define RTD_AUDIO_H
#include <stdint.h>

/* UC-586 digital attenuation is qualified by analog loopback measurements. */
#ifndef RTD_AUDIO_VOLUME
#define RTD_AUDIO_VOLUME 1
#endif

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
/* Volume and mute are independent and survive signal loss. Zero volume gates
 * output; a nonzero setting never bypasses the existing acquisition guards.
 * Values are linear amplitude percentages, with 100% an exact unity bypass.
 * Return zero for unavailable hardware or values outside 0..100.
 */
uint8_t audio_volume_available(void);
uint8_t audio_set_volume(uint8_t percent);
uint8_t audio_get_volume(void);
uint8_t audio_state(void);
uint32_t audio_sample_rate(void);
#endif
