// SPDX-License-Identifier: MIT
#include "rtd/audio.h"
#include "rtd/board.h"
#include "rtd/io.h"

/* Hardware facts are from the supplied RTD HDMI reference, since the public
 * register manual omits this block. This driver is a new fixed-rate state
 * machine; see docs/audio.md for provenance and bench limitations.
 */
#if BOARD_CRYSTAL_HZ != 27000000UL
#error The initial audio PLL profile requires the UC-586 27 MHz crystal
#endif

enum {
  HDMI_ACCESS = 0xc8, HDMI_PORT = 0xc9, HDMI_STATUS = 0xcb,
  FIFO_CONTROL = 0x03, CLOCK_COMMIT = 0x10,
  PLL_M = 0x11, PLL_S = 0x12, PLL_D_HIGH = 0x13, PLL_D_LOW = 0x14,
  TRACKING = 0x15, TREND_TIME = 0x1a, FIFO_BOUNDARY = 0x1b,
  TREND_GAIN = 0x21, BOUNDARY_I = 0x25, BOUNDARY_P = 0x27,
  TMDS_COUNT = 0x28, BOUNDARY_TIME = 0x2a, SIGMA_DELTA = 0x2d,
  AV_CONTROL = 0x30, WATCHDOG = 0x31, TMDS_WATCHDOG = 0x32,
  PLL_POWER = 0x38, PLL_CURRENT = 0x39, PLL_START = 0x3b,
  PLL_D_READ_HIGH = 0x3c, PLL_D_READ_LOW = 0x3d,
  ACR_CAPTURE = 0x51, ACR_BYTES = 0x52, OUTPUT_ENABLE = 0x62,
  STATUS_HDMI = 0x01, STATUS_FIFO = 0x06, STATUS_COMPRESSED = 0x10,
  STATUS_AVMUTE = 0x40, ENABLE_AUDIO = 0x20
};

#define PLL_D_48KHZ 0x1e2fu
#define PLL_DEADLINE_MS 50UL
#define FIFO_SETTLE_MS 500UL
#define RATE_CHECK_MS 100UL
#define RETRY_MS 250UL

static uint8_t state;
static uint8_t user_muted;
static uint32_t entered, pll_started, measured_rate;

static uint8_t read_audio(uint8_t index) {
  return rtd_indirect_read(2, HDMI_PORT, index);
}

static void write_audio(uint8_t index, uint8_t value) {
  rtd_indirect_write(2, HDMI_PORT, index, value);
}

static void update_audio(uint8_t index, uint8_t mask, uint8_t value) {
  rtd_indirect_update(2, HDMI_PORT, index, mask, value);
}

static void advance(uint8_t next, uint32_t now) {
  state = next;
  entered = now;
}

void audio_set_mute(uint8_t muted) {
  user_muted = muted != 0;
  /* Muting is immediate; unmuting waits for audio_service's signal, rate,
   * engine and FIFO checks. Keep PLL tracking active during a user mute.
   */
  if (user_muted) write_audio(OUTPUT_ENABLE, 0);
}

uint8_t audio_get_mute(void) { return user_muted; }

uint8_t audio_volume_available(void) {
  /* The CS4334 has no control interface. RTD gain registers 05/06 are named
   * in the reference, but their fields and gain encoding remain unverified.
   */
  return 0;
}

void audio_stop(void) {
  write_audio(OUTPUT_ENABLE, 0); /* Disable I2S and SPDIF outputs first. */
  update_audio(AV_CONTROL, ENABLE_AUDIO, 0); /* Preserve video bit 3. */
  update_audio(WATCHDOG, STATUS_FIFO, 0);
  update_audio(TMDS_WATCHDOG, 0x80, 0);
  write_audio(TRACKING, 0);
  state = AUDIO_OFF;
  measured_rate = 0;
}

void audio_init(void) {
  rtd_update(2, HDMI_ACCESS, 1, 0); /* Explicit-index HDMI accesses. */
  audio_stop();
  update_audio(AV_CONTROL, 0x60, 0x40);
  write_audio(FIFO_CONTROL, 0x06);
  entered = pll_started = 0;
  user_muted = 0;
}

static void retry(uint32_t now) {
  audio_stop();
  advance(AUDIO_RETRY, now);
}

static void capture_rate(void) {
  update_audio(ACR_CAPTURE, 2, 2);
  update_audio(TMDS_COUNT, 8, 8);
}

static uint8_t rate_is_48khz(void) {
  uint8_t shared;
  uint16_t count;
  uint32_t cts, n, period;
  cts = (uint32_t)read_audio(ACR_BYTES) << 12;
  cts |= (uint16_t)read_audio(ACR_BYTES + 1) << 4;
  shared = read_audio(ACR_BYTES + 2);
  cts |= shared >> 4;
  n = (uint32_t)(shared & 15) << 16;
  n |= (uint16_t)read_audio(ACR_BYTES + 3) << 8;
  n |= read_audio(ACR_BYTES + 4);
  count = ((uint16_t)(read_audio(TMDS_COUNT) & 7) << 8) |
          read_audio(TMDS_COUNT + 1);
  if (!n || !cts || !count) return 0;
  /* Fs = 8 * crystal * N / (count * CTS). The product fits uint32_t:
   * count <= 2047, CTS <= 1048575. Divide before multiplying to avoid
   * 64-bit arithmetic on the 8051; error near 48 kHz is under 11 Hz.
   */
  period = ((uint32_t)count * cts) / n;
  if (period < 4463 || period > 4537) return 0;
  measured_rate = 216000000UL / period;
  return 1;
}

static void program_clock(void) {
  write_audio(TRACKING, 0);
  write_audio(SIGMA_DELTA, 0x08);
  /* 256fs MCLK=12.288 MHz, VCO target=196.608 MHz. With a 27 MHz
   * crystal: M=13, S=0x84, D=7727 including the reference's +100 bias.
   */
  write_audio(PLL_M, 13);
  write_audio(PLL_S, 0x84);
  write_audio(PLL_D_HIGH, PLL_D_48KHZ >> 8);
  write_audio(PLL_D_LOW, (uint8_t)PLL_D_48KHZ);
  update_audio(PLL_POWER, 0x30, 0x10);
  write_audio(PLL_CURRENT, 0x82);
  write_audio(PLL_START, 3);
  write_audio(CLOCK_COMMIT, 0x50);
  update_audio(PLL_POWER, 0xc0, 0); /* Power on, release freeze. */
}

static void start_tracking(void) {
  /* Temporarily suspend AVMute watchdog while enabling the audio engine.
   * Physical I2S outputs remain muted until both settling intervals pass.
   */
  update_audio(WATCHDOG, 0x80, 0);
  update_audio(AV_CONTROL, ENABLE_AUDIO, ENABLE_AUDIO);
  update_audio(WATCHDOG, 0x80, 0x80);
  write_audio(FIFO_CONTROL, 0x26);
  write_audio(TRACKING, 0x04);
  write_audio(BOUNDARY_I, 1);
  write_audio(BOUNDARY_P, 1);
  write_audio(BOUNDARY_TIME, 0x80);
  write_audio(SIGMA_DELTA, 0xc2);
  write_audio(FIFO_BOUNDARY, 0xe2);
  update_audio(WATCHDOG, 0x20, 0x20);
  write_audio(CLOCK_COMMIT, 0x50);
  rtd_write(2, HDMI_STATUS, STATUS_FIFO); /* W1C, never read/modify/write. */
  write_audio(TRACKING, 0xec);
  write_audio(TREND_TIME, 3);
  write_audio(TREND_GAIN, 7);
  write_audio(CLOCK_COMMIT, 0x50);
}

void audio_service(uint32_t now, uint8_t video_valid) {
  uint8_t status = rtd_read(2, HDMI_STATUS);
  uint16_t accepted_d;
  uint32_t elapsed = now - entered;
  if (!video_valid || !(status & STATUS_HDMI) ||
      (status & (STATUS_COMPRESSED | STATUS_AVMUTE))) {
    if (state != AUDIO_OFF) audio_stop();
    return;
  }
  if (state >= AUDIO_SETTLE && state <= AUDIO_RATE_CHECK &&
      !(read_audio(AV_CONTROL) & ENABLE_AUDIO)) {
    retry(now);
    return;
  }
  if ((state == AUDIO_PLAYING || state == AUDIO_RATE_CHECK) &&
      (status & STATUS_FIFO)) {
    retry(now);
    return;
  }
  switch (state) {
  case AUDIO_OFF:
  case AUDIO_RETRY:
    if (state == AUDIO_RETRY && elapsed < RETRY_MS) break;
    write_audio(TRACKING, 0);
    write_audio(CLOCK_COMMIT, 0x50);
    rtd_write(2, HDMI_STATUS, STATUS_FIFO);
    capture_rate();
    advance(AUDIO_MEASURE, now);
    break;
  case AUDIO_MEASURE:
  case AUDIO_RATE_CHECK:
    if (elapsed < 2) break;
    if (!rate_is_48khz()) {
      retry(now);
    } else if (state == AUDIO_RATE_CHECK) {
      advance(AUDIO_PLAYING, now);
    } else {
      program_clock();
      pll_started = now;
      advance(AUDIO_PLL_START, now);
    }
    break;
  case AUDIO_PLL_START:
    if (elapsed < 1) break;
    write_audio(SIGMA_DELTA, 0);
    write_audio(SIGMA_DELTA, 2);
    advance(AUDIO_PLL_VERIFY, now);
    break;
  case AUDIO_PLL_VERIFY:
    if (elapsed < 1) break;
    accepted_d = ((uint16_t)read_audio(PLL_D_READ_HIGH) << 8) |
                 read_audio(PLL_D_READ_LOW);
    if (accepted_d == PLL_D_48KHZ) {
      start_tracking();
      advance(AUDIO_SETTLE, now);
    } else if ((uint32_t)(now - pll_started) >= PLL_DEADLINE_MS) {
      retry(now);
    } else {
      advance(AUDIO_PLL_START, now);
    }
    break;
  case AUDIO_SETTLE:
    if (elapsed < FIFO_SETTLE_MS) break;
    rtd_write(2, HDMI_STATUS, STATUS_FIFO);
    advance(AUDIO_FIFO_CHECK, now);
    break;
  case AUDIO_FIFO_CHECK:
    if (elapsed < FIFO_SETTLE_MS) break;
    if (status & STATUS_FIFO) {
      retry(now);
      break;
    }
    update_audio(WATCHDOG, STATUS_FIFO, STATUS_FIFO);
    update_audio(TMDS_WATCHDOG, 0x80, 0x80);
    advance(AUDIO_PLAYING, now);
    break;
  case AUDIO_PLAYING:
    if (elapsed < RATE_CHECK_MS) break;
    capture_rate();
    advance(AUDIO_RATE_CHECK, now);
    break;
  }
  if (state == AUDIO_PLAYING) {
    uint8_t outputs = user_muted ? 0 : 0x0f; /* I2S only; SPDIF stays off. */
    if (read_audio(OUTPUT_ENABLE) != outputs)
      write_audio(OUTPUT_ENABLE, outputs);
  }
}

uint8_t audio_state(void) { return state; }
uint32_t audio_sample_rate(void) { return measured_rate; }
