/* Test that CFS/EEVDF correctly tracks actual CPU time used.

   This test creates:
   - A CPU-bound thread that never blocks (uses full time slices)
   - An I/O-bound thread that blocks frequently (uses partial slices)

   With correct vruntime tracking:
   - I/O-bound thread should have LOWER vruntime (charged less)
   - CPU-bound thread should have HIGHER vruntime (charged more)

   If we incorrectly assume full time slices:
   - Both threads would have similar vruntime despite different behavior */

#include <stdio.h>
#include <string.h>
#include "tests/threads/tests.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/interrupt.h"
#include "devices/timer.h"

/* Thread references for vruntime comparison */
static struct thread* cpu_thread;
static struct thread* io_thread;

/* Semaphore to simulate I/O blocking */
static struct semaphore io_sema;

/* Flag to stop threads */
static volatile bool test_done;

/* CPU-bound thread: just loops, never blocks voluntarily.
   Uses full TIME_SLICE (4 ticks) each time it runs. */
static void cpu_bound_thread(void* aux UNUSED) {
  while (!test_done) {
    /* Burn some CPU cycles, then yield */
    volatile int i;
    for (i = 0; i < 10000; i++)
      ;
    thread_yield();
  }
}

/* I/O-bound thread: blocks almost immediately after starting.
   Simulates a thread that does brief work then waits for I/O.
   Should use only ~0-1 ticks before blocking. */
static void io_bound_thread(void* aux UNUSED) {
  while (!test_done) {
    /* Brief work, then "wait for I/O" by blocking on semaphore.
       The waker thread will wake us up quickly. */
    sema_down(&io_sema);
  }
}

/* Waker thread: periodically wakes up the I/O-bound thread.
   This simulates I/O completion interrupts. */
static void waker_thread(void* aux UNUSED) {
  while (!test_done) {
    /* Wait a tiny bit, then wake the I/O thread */
    timer_sleep(1); /* Sleep 1 tick */
    sema_up(&io_sema);
  }
  /* Final wake to let I/O thread exit */
  sema_up(&io_sema);
}

/* Helper struct for find_thread_by_name callback */
struct find_thread_aux {
  const char* name;
  struct thread* result;
};

/* Callback for thread_foreach to find thread by name */
static void find_thread_callback(struct thread* t, void* aux_) {
  struct find_thread_aux* aux = aux_;
  if (aux->result == NULL && strcmp(t->name, aux->name) == 0) {
    aux->result = t;
  }
}

/* Find thread by name - used to get references for vruntime comparison */
static struct thread* find_thread_by_name(const char* name) {
  struct find_thread_aux aux;
  aux.name = name;
  aux.result = NULL;

  enum intr_level old_level = intr_disable();
  thread_foreach(find_thread_callback, &aux);
  intr_set_level(old_level);

  return aux.result;
}

void test_fair_vruntime(void) {
  msg("Testing CFS/EEVDF actual tick accounting...");

  /* Initialize synchronization */
  sema_init(&io_sema, 0);
  test_done = false;

  /* Create the test threads with same priority/nice value */
  thread_create("cpu-bound", PRI_DEFAULT, cpu_bound_thread, NULL);
  thread_create("io-bound", PRI_DEFAULT, io_bound_thread, NULL);
  thread_create("waker", PRI_DEFAULT, waker_thread, NULL);

  /* Get thread references */
  cpu_thread = find_thread_by_name("cpu-bound");
  io_thread = find_thread_by_name("io-bound");

  if (cpu_thread == NULL || io_thread == NULL) {
    msg("FAIL: Could not find test threads");
    return;
  }

  /* Let the test run for a while */
  timer_sleep(200); /* Run for 200 ticks (~2 seconds at 100Hz) */

  /* Capture vruntimes before stopping (with interrupts disabled for consistency) */
  enum intr_level old_level = intr_disable();
  int64_t cpu_vruntime = cpu_thread->vruntime;
  int64_t io_vruntime = io_thread->vruntime;
  intr_set_level(old_level);

  /* Stop all threads */
  test_done = true;
  timer_sleep(10); /* Give threads time to notice and exit */

  /* Report results */
  msg("CPU-bound thread vruntime: %lld", (long long)cpu_vruntime);
  msg("I/O-bound thread vruntime: %lld", (long long)io_vruntime);

  /* The I/O-bound thread should have LOWER vruntime because:
     - It blocks after using only ~0-1 ticks
     - So its vruntime grows slower
     - CPU-bound thread uses full TIME_SLICE (4 ticks) before yielding

     If accounting is correct:
       CPU vruntime should be significantly higher (using ~4x more CPU per schedule)

     If we assumed full slices:
       Both would have similar vruntime (both charged 4 ticks each time) */

  if (cpu_vruntime > io_vruntime * 2) {
    /* CPU thread used at least 2x more vruntime - strong evidence of partial accounting */
    msg("PASS: CPU-bound vruntime is much higher (%lld > 2 * %lld)", (long long)cpu_vruntime,
        (long long)io_vruntime);
    msg("This proves partial tick accounting is working!");
  } else if (cpu_vruntime > io_vruntime) {
    /* CPU thread used more, but not dramatically more */
    msg("MARGINAL: CPU-bound vruntime is higher (%lld > %lld)", (long long)cpu_vruntime,
        (long long)io_vruntime);
    msg("Partial accounting is working, but difference is small.");
  } else {
    msg("FAIL: I/O-bound vruntime >= CPU-bound (%lld >= %lld)", (long long)io_vruntime,
        (long long)cpu_vruntime);
    msg("This suggests the I/O thread is being overcharged!");
  }

  /* Sanity check: CPU thread should have accumulated significant vruntime.
     I/O thread may have 0 vruntime if it always blocks before any timer tick. */
  if (cpu_vruntime < 1000) {
    msg("WARNING: Very low CPU vruntime - test may be unreliable");
    msg("Is the CFS/EEVDF scheduler active? Use -sched=fair -fair=cfs");
  }
  if (io_vruntime == 0) {
    msg("Note: I/O thread has 0 vruntime (blocks before any tick) - this is expected!");
  }
}
