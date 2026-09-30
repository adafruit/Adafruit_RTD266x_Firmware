// SPDX-License-Identifier: MIT
#ifndef RTD_PLATFORM_H
#define RTD_PLATFORM_H

#include <stdint.h>

void platform_init(void);
uint32_t platform_millis(void);
void platform_delay_ms(uint16_t duration);
/* Read code space with MOVC, never the XDATA/register window. Bank zero is
 * fixed for this firmware; no bank selector is changed during a checksum. */
uint8_t platform_code_read(uint16_t address);
/* Diagnostic flash reads through the existing bank1 XDATA mapping. No bank
 * selector is changed; offsets are limited to 0x0000..0x1fff by the caller. */
uint8_t platform_vendor_probe_available(void);
uint8_t platform_vendor_probe_read(uint16_t offset);

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
