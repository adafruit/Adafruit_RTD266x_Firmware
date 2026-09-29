// SPDX-License-Identifier: MIT
#include <8052.h>
#include "rtd/board.h"
#include "rtd/io.h"
#include "rtd/platform.h"
#include "rtd/ddcci.h"

__sfr __at(0x8e) RTD_CKCON;

/* Timer0 uses crystal / 2 / 12. A two-millisecond tick leaves ample time
 * for foreground code. This is a control clock, not a precision timebase.
 */
#define TICK_COUNTS (BOARD_CRYSTAL_HZ / 12000UL)
#if BOARD_CRYSTAL_HZ % 12000UL || TICK_COUNTS > 65535UL
#error Unsupported crystal frequency for the control timer
#endif

static volatile uint32_t uptime_ms;

unsigned char __sdcc_external_startup(void) {
  IE = 0;
  *((volatile __xdata uint8_t *)0xffea) = 0;
  *((volatile __xdata uint8_t *)0xffea) = 0x40; /* watchdog disabled */
  *((volatile __xdata uint8_t *)0xfffe) = 0; /* XDATA flash bank */
  return 0; /* run SDCC data initialization */
}

void timer0_interrupt(void) __interrupt(1) {
  uint16_t counter;
  TR0 = 0;
  counter = ((uint16_t)TH0 << 8) | TL0;
  counter -= (uint16_t)TICK_COUNTS;
  TL0 = (uint8_t)counter;
  TH0 = (uint8_t)(counter >> 8);
  TF0 = 0;
  TR0 = 1;
  uptime_ms += 2;
}

/* Keep unexpected vectors out of code/data helper routines on warm restart. */
void external0_interrupt(void) __interrupt(0) { IE = 0; }
void external1_interrupt(void) __interrupt(2) { IE = 0; }
void timer1_interrupt(void) __interrupt(3) { IE = 0; }
void serial_interrupt(void) __interrupt(4) { IE = 0; }
void timer2_interrupt(void) __interrupt(5) { IE = 0; }

uint32_t platform_millis(void) {
  uint32_t snapshot;
  uint8_t enabled = EA;
  EA = 0;
  snapshot = uptime_ms;
  EA = enabled;
  return snapshot;
}

void platform_delay_ms(uint16_t duration) {
  uint32_t start = platform_millis();
  /* Extra tick covers starting partway through a timer period. */
  while ((uint32_t)(platform_millis() - start) < (uint32_t)duration + 2) {
    ddcci_service();
  }
}

void platform_init(void) {
  IE = 0;
  TCON = 0;
  /* Manual pp.350-351: crystal source, flash /2, MCU /1, peripherals
   * continue during instruction fetch. Preserve unrelated pin selection.
   */
  mcu_update(0xed, 0x3e, 0x08);
  mcu_update(0xee, 0x7f, 0x44);
  RTD_CKCON &= (uint8_t)~0x08;
  TMOD = (TMOD & 0xf0) | 1;
  uptime_ms = 0;
  TH0 = (uint8_t)((65536UL - TICK_COUNTS) >> 8);
  TL0 = (uint8_t)(65536UL - TICK_COUNTS);
  ET0 = 1;
  TR0 = 1;
  EA = 1;

  /* ISP restarts the MCU but does not reset the video pipeline. FFEE.0
   * explicitly resets the scaler and leaves the timer on the crystal.
   */
  mcu_update(0xee, 3, 1);
  platform_delay_ms(20);
  mcu_update(0xee, 3, 0);
  platform_delay_ms(20);
}
