/* arch/riscv64/timer.c - RISC-V timer implementation.
 *
 * Uses SBI timer extension to schedule timer interrupts.
 * The timer frequency is set to 100 Hz (TIMER_FREQ).
 */

#include "arch/riscv64/timer.h"
#include "arch/riscv64/sbi.h"
#include "arch/riscv64/csr.h"
#include "arch/riscv64/intr.h"
#include <stdint.h>

#define UNUSED __attribute__((unused))

/* Timer state */
static volatile uint64_t ticks;
static uint64_t timebase_freq;

/* Forward declaration */
static uint64_t get_time(void);
static void set_next_timer(void);

/*
 * timer_init - Initialize the timer subsystem.
 *
 * Determines the timebase frequency and schedules the first timer interrupt.
 */
void timer_init(void) {
  /* Initialize tick counter */
  ticks = 0;

  /* Get timebase frequency from SBI.
   * For QEMU virt machine, this is typically 10MHz. */
  timebase_freq = 10000000; /* Default to 10MHz */

  /* Enable supervisor timer interrupt */
  csr_set(sie, SIE_STIE);

  /* Schedule first timer interrupt */
  set_next_timer();
}

/*
 * get_time - Read current time from the time CSR.
 *
 * The time CSR is a read-only shadow of mtime.
 */
static uint64_t get_time(void) {
  uint64_t time;
  asm volatile("rdtime %0" : "=r"(time));
  return time;
}

/*
 * set_next_timer - Schedule the next timer interrupt.
 *
 * Uses SBI to set stimecmp for the next tick.
 */
static void set_next_timer(void) {
  uint64_t next = get_time() + timebase_freq / TIMER_FREQ;
  sbi_set_timer(next);
}

/*
 * timer_interrupt - Timer interrupt handler.
 *
 * Called from intr.c when a supervisor timer interrupt occurs.
 */
void timer_interrupt(struct intr_frame* f UNUSED) {
  /* Increment tick counter */
  ticks++;

  /* Schedule next timer interrupt */
  set_next_timer();

  /* TODO: Call thread_tick() for scheduling */
  /* thread_tick(); */
}

/*
 * timer_ticks - Get current tick count.
 */
uint64_t timer_ticks(void) {
  /* Disable interrupts to get consistent read */
  enum intr_level old = intr_set_level(INTR_OFF);
  uint64_t t = ticks;
  intr_set_level(old);
  return t;
}

/*
 * timer_ms - Get time in milliseconds since boot.
 */
uint64_t timer_ms(void) { return timer_ticks() * 1000 / TIMER_FREQ; }

/*
 * timer_sleep - Sleep for the given number of ticks.
 */
void timer_sleep(uint64_t duration) {
  uint64_t start = timer_ticks();

  /* Busy wait until enough ticks have passed */
  while (timer_ticks() - start < duration) {
    /* TODO: Use thread_yield() when available */
    asm volatile("wfi");
  }
}

/*
 * timer_msleep - Sleep for the given number of milliseconds.
 */
void timer_msleep(uint64_t ms) {
  uint64_t ticks_to_sleep = ms * TIMER_FREQ / 1000;
  if (ticks_to_sleep == 0)
    ticks_to_sleep = 1;
  timer_sleep(ticks_to_sleep);
}

/*
 * timer_read_time - Read the raw time counter.
 *
 * Returns the current value of the time CSR.
 */
uint64_t timer_read_time(void) { return get_time(); }
