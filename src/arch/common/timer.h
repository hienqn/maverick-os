/* arch/common/timer.h - Timer interface.
 *
 * Architecture-neutral interface for hardware timer.
 * i386: Uses 8254 PIT (Programmable Interval Timer)
 * RISC-V: Uses CLINT timer via SBI calls
 */

#ifndef ARCH_COMMON_TIMER_H
#define ARCH_COMMON_TIMER_H

#include <stdint.h>

/* Initialize the hardware timer to fire at the given frequency (Hz).
 * Called during kernel initialization. */
void arch_timer_init(unsigned frequency);

/* Read current timer tick count (monotonic, 64-bit).
 * This counter increments at the timer frequency. */
uint64_t arch_timer_ticks(void);

/* Get the timer frequency in Hz. */
uint64_t arch_timer_frequency(void);

/* Acknowledge timer interrupt.
 * Called by the timer interrupt handler. Some architectures
 * require explicit acknowledgment to clear the interrupt. */
void arch_timer_ack(void);

/* Calibrate timer loops per tick.
 * Used for busy-wait timing calibration. */
void arch_timer_calibrate(void);

#endif /* ARCH_COMMON_TIMER_H */
