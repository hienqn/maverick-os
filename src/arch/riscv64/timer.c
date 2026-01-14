/* arch/riscv64/timer.c - RISC-V timer implementation.
 *
 * Uses SBI timer extension to schedule timer interrupts.
 * The timer frequency is set to 100 Hz (TIMER_FREQ).
 *
 * TIMER SLEEP:
 * timer_sleep() uses an efficient sleep list instead of busy-waiting:
 *   1. Thread sets wake_up_tick and adds itself to sleeping_threads
 *   2. Thread blocks
 *   3. Timer interrupt checks list and wakes threads whose time is up
 */

#include "arch/riscv64/timer.h"
#include "arch/riscv64/sbi.h"
#include "arch/riscv64/csr.h"
#include "arch/riscv64/intr.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"
#include <debug.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

#define UNUSED __attribute__((unused))

/* Timer state */
static volatile int64_t ticks;
static uint64_t timebase_freq;

/* Sleeping threads list - ordered by wake_up_tick */
static struct list sleeping_threads;

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

  /* Initialize sleeping threads list */
  list_init(&sleeping_threads);

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
 * Wakes up sleeping threads whose time has come.
 */
void timer_interrupt(struct intr_frame* f UNUSED) {
  /* Increment tick counter */
  ticks++;

  /* Schedule next timer interrupt */
  set_next_timer();

  /* Notify thread subsystem of timer tick (for preemptive scheduling) */
  thread_tick();

  /* Wake up sleeping threads whose time has come */
  while (!list_empty(&sleeping_threads)) {
    struct thread* t = list_entry(list_front(&sleeping_threads), struct thread, elem);
    if (t->wake_up_tick > ticks)
      break;
    list_pop_front(&sleeping_threads);
    thread_unblock(t);
  }
}

/*
 * timer_ticks - Get current tick count.
 */
int64_t timer_ticks(void) {
  /* Disable interrupts to get consistent read */
  enum intr_level old = intr_set_level(INTR_OFF);
  int64_t t = ticks;
  intr_set_level(old);
  return t;
}

/*
 * timer_elapsed - Returns the number of ticks since THEN.
 *
 * Used by tests to measure elapsed time.
 */
int64_t timer_elapsed(int64_t then) { return timer_ticks() - then; }

/*
 * timer_ms - Get time in milliseconds since boot.
 */
uint64_t timer_ms(void) { return (uint64_t)timer_ticks() * 1000 / TIMER_FREQ; }

/*
 * timer_sleep - Sleep for the given number of ticks.
 *
 * Adds the current thread to the sleeping threads list and blocks.
 * The timer interrupt will wake the thread when its time comes.
 * Interrupts must be turned on.
 */
void timer_sleep(int64_t sleep_ticks) {
  int64_t start = timer_ticks();

  ASSERT(intr_get_level() == INTR_ON);

  if (sleep_ticks <= 0)
    return;

  enum intr_level old_level = intr_disable();

  struct thread* current_thread = thread_current();
  /* Wake up time is the current tick plus the duration */
  current_thread->wake_up_tick = start + sleep_ticks;

  /* Insert this thread into the sleeping_threads list, ordered by wake_up_tick */
  struct list_elem* e = list_begin(&sleeping_threads);
  while (e != list_end(&sleeping_threads)) {
    struct thread* t = list_entry(e, struct thread, elem);
    if (current_thread->wake_up_tick < t->wake_up_tick) {
      break;
    }
    e = list_next(e);
  }
  list_insert(e, &current_thread->elem);

  /* Block this thread - timer interrupt will wake it */
  thread_block();

  intr_set_level(old_level);
}

/*
 * timer_msleep - Sleep for the given number of milliseconds.
 */
void timer_msleep(int64_t ms) {
  int64_t ticks_to_sleep = ms * TIMER_FREQ / 1000;
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

/*
 * timer_print_stats - Print timer statistics.
 *
 * Prints the total number of timer ticks since boot.
 * Required for test framework compatibility.
 */
void timer_print_stats(void) { printf("Timer: %" PRId64 " ticks\n", timer_ticks()); }
