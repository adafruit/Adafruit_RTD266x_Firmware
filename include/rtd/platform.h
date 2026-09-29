// SPDX-License-Identifier: MIT
#ifndef RTD_PLATFORM_H
#define RTD_PLATFORM_H

#include <stdint.h>

void platform_init(void);
uint32_t platform_millis(void);
void platform_delay_ms(uint16_t duration);

/* SDCC emits vectors in the translation unit containing main(). Keep these
 * declarations visible there even though their definitions live separately.
 */
#ifdef __SDCC_mcs51
void external0_interrupt(void) __interrupt(0);
void timer0_interrupt(void) __interrupt(1);
void external1_interrupt(void) __interrupt(2);
void timer1_interrupt(void) __interrupt(3);
void serial_interrupt(void) __interrupt(4);
void timer2_interrupt(void) __interrupt(5);
#endif

#endif
