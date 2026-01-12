/* arch/riscv64/timer.h - RISC-V timer interface.
 *
 * The RISC-V timer uses the SBI timer extension to set timer interrupts.
 * Timer interrupts are delivered as supervisor timer interrupts (STI).
 */

#ifndef ARCH_RISCV64_TIMER_H
#define ARCH_RISCV64_TIMER_H

#include <stdint.h>

struct intr_frame;

/* Timer frequency (ticks per second) */
#define TIMER_FREQ 100

/* Initialize the timer */
void timer_init(void);

/* Timer interrupt handler (called from intr.c) */
void timer_interrupt(struct intr_frame* f);

/* Get current timer ticks since boot */
uint64_t timer_ticks(void);

/* Get time in milliseconds since boot */
uint64_t timer_ms(void);

/* Sleep for given number of ticks */
void timer_sleep(uint64_t ticks);

/* Sleep for given number of milliseconds */
void timer_msleep(uint64_t ms);

/* Read the raw time counter (mtime equivalent) */
uint64_t timer_read_time(void);

#endif /* ARCH_RISCV64_TIMER_H */
