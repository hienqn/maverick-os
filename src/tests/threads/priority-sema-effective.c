/* Tests that sema_up selects threads based on effective priority
   (including priority donations) rather than base priority. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

struct test_data {
  struct lock lock;
  struct semaphore sema;
  struct semaphore ready_sema;
  struct semaphore blocked_sema;
};

static thread_func thread_a_func;
static thread_func thread_b_func;
static thread_func thread_c_func;

void test_priority_sema_effective_priority(void) {
  struct test_data data;

  /* This test does not work with the MLFQS. */
  ASSERT(active_sched_policy == SCHED_PRIO);

  /* Make sure our priority is the default. */
  ASSERT(thread_get_priority() == PRI_DEFAULT);

  lock_init(&data.lock);
  sema_init(&data.sema, 0);
  sema_init(&data.ready_sema, 0);
  sema_init(&data.blocked_sema, 0);

  /* Create Thread A with base priority 30 */
  thread_create("thread-a", PRI_DEFAULT - 1, thread_a_func, &data);
  sema_down(&data.ready_sema); /* Wait for Thread A to acquire lock */

  /* Create Thread C with priority 50 to donate to Thread A */
  thread_create("thread-c", PRI_DEFAULT + 19, thread_c_func, &data);
  sema_down(&data.ready_sema); /* Wait for Thread C to block on lock */

  /* Create Thread B with base priority 40 */
  thread_set_priority(PRI_DEFAULT +
                      10); /* Raise main thread priority so Thread B doesn't preempt */
  thread_create("thread-b", PRI_DEFAULT + 9, thread_b_func, &data);
  sema_down(&data.blocked_sema); /* Wait for Thread B to signal it has blocked on sema */

  /* At this point:
     - Thread A: base priority 30, eff_priority 50 (due to donation from Thread C)
     - Thread B: base priority 40, eff_priority 40
     - Both Thread A and Thread B are waiting on data.sema
     - Thread C is waiting on data.lock (held by Thread A)
  */

  msg("Calling sema_up - should wake Thread A (eff_priority 50) not Thread B (eff_priority 40)");
  sema_up(&data.sema);

  /* Wait for first thread to wake and finish */
  sema_down(&data.ready_sema); /* First thread to wake signals here */

  /* Wake the remaining thread so test can complete */
  sema_up(&data.sema);
  sema_down(&data.ready_sema); /* Second thread signals here */
  sema_down(&data.ready_sema); /* Thread C signals when done */

  msg("Test completed.");
}

static void thread_a_func(void* data_) {
  struct test_data* data = data_;

  lock_acquire(&data->lock);
  msg("Thread A acquired lock (base priority 30, should have eff_priority 50 after donation)");
  sema_up(&data->ready_sema); /* Signal that lock is acquired */

  /* Block on semaphore while holding the lock */
  sema_down(&data->sema);
  msg("Thread A woke up from semaphore (this is CORRECT - eff_priority 50 > 40)");

  lock_release(&data->lock);
  sema_up(&data->ready_sema);
}

static void thread_b_func(void* data_) {
  struct test_data* data = data_;

  /* Signal that we're about to block, then block on semaphore */
  sema_up(&data->blocked_sema);
  sema_down(&data->sema);
  msg("Thread B woke up from semaphore (this is WRONG - base priority 40 < eff_priority 50)");
  sema_up(&data->ready_sema);
}

static void thread_c_func(void* data_) {
  struct test_data* data = data_;

  sema_up(&data->ready_sema); /* Signal that we're about to acquire lock */

  /* Try to acquire lock, which will donate priority to Thread A */
  lock_acquire(&data->lock);
  msg("Thread C acquired lock (priority 50)");

  lock_release(&data->lock);
  sema_up(&data->ready_sema);
}
