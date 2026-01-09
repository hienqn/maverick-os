#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <round.h>
#include <stdint.h>

/*
 * TIMER DRIVER
 * ============
 * Driver for the 8254 Programmable Interval Timer (PIT). The timer generates
 * periodic hardware interrupts that drive the OS's notion of time.
 *
 * TIMER FREQUENCY:
 * ----------------
 * TIMER_FREQ defines how many times per second the timer fires (default: 100 Hz).
 * Each interrupt is called a "tick" (10 ms at 100 Hz). Valid range: 19-1000 Hz.
 * Higher frequency = finer time granularity but more interrupt overhead.
 *
 * TIMER INTERRUPT RESPONSIBILITIES:
 * ---------------------------------
 * Each timer interrupt (100 times/second by default):
 *   1. Increments global tick count
 *   2. Wakes sleeping threads whose wake time has passed
 *   3. Updates scheduler statistics (time slice accounting)
 *   4. For MLFQS scheduler: updates recent_cpu, priorities, load_avg
 *
 * SLEEP vs DELAY (IMPORTANT DISTINCTION):
 * ---------------------------------------
 * Sleep functions (timer_sleep, timer_msleep, timer_usleep, timer_nsleep):
 *   - Yield CPU to other threads while waiting
 *   - Require interrupts to be ON
 *   - Use these for normal delays in user/kernel code
 *
 * Delay functions (timer_mdelay, timer_udelay, timer_ndelay):
 *   - Busy-wait (spin) without yielding CPU
 *   - Work with interrupts ON or OFF
 *   - Use only for hardware timing where sleeping isn't safe
 *   - WARNING: Long delays with interrupts OFF will lose timer ticks
 *
 * USAGE EXAMPLE:
 * --------------
 *   timer_msleep(100);        // Sleep for 100ms, yield CPU
 *   timer_udelay(50);         // Busy-wait 50us (hardware timing)
 *   int64_t start = timer_ticks();
 *   ... do work ...
 *   int64_t elapsed = timer_elapsed(start);  // Ticks since start
 */

/* Number of timer interrupts per second (100 Hz = 10ms per tick).
   Valid range: 19-1000. Higher values give finer granularity but more overhead. */
#define TIMER_FREQ 100

void timer_init(void);
void timer_calibrate(void);

int64_t timer_ticks(void);
int64_t timer_elapsed(int64_t);

/* Sleep and yield the CPU to other threads. */
void timer_sleep(int64_t ticks);
void timer_msleep(int64_t milliseconds);
void timer_usleep(int64_t microseconds);
void timer_nsleep(int64_t nanoseconds);

/* Busy waits. */
void timer_mdelay(int64_t milliseconds);
void timer_udelay(int64_t microseconds);
void timer_ndelay(int64_t nanoseconds);

void timer_print_stats(void);

#endif /* devices/timer.h */
