/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                   SYNCHRONIZATION IMPLEMENTATION                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  This file implements synchronization primitives for the PintOS kernel.  ║
 * ║  All primitives are built on top of the fundamental semaphore.           ║
 * ║                                                                          ║
 * ║  ATOMICITY MODEL:                                                        ║
 * ║  ─────────────────                                                       ║
 * ║  • All operations achieve atomicity by disabling interrupts              ║
 * ║  • This is sufficient on uniprocessor systems (like PintOS)              ║
 * ║  • On SMP systems, would need spinlocks + interrupt disable              ║
 * ║                                                                          ║
 * ║  PRIORITY SCHEDULING INTEGRATION:                                        ║
 * ║  ─────────────────────────────────                                       ║
 * ║  When SCHED_PRIO is active:                                              ║
 * ║  • sema_up() wakes the highest-priority waiting thread                   ║
 * ║  • lock_acquire() donates priority to lock holder                        ║
 * ║  • lock_release() recalculates priority from remaining donations         ║
 * ║  • cond_signal() wakes the highest-priority waiting thread               ║
 * ║                                                                          ║
 * ║  PRIORITY DONATION (LOCK):                                               ║
 * ║  ──────────────────────────                                              ║
 * ║                                                                          ║
 * ║    Thread H (pri=60)         Thread L (pri=20)                           ║
 * ║         │                         │                                      ║
 * ║         │                    holds lock                                  ║
 * ║         │                         │                                      ║
 * ║    lock_acquire ─────────────────►│                                      ║
 * ║         │                         │                                      ║
 * ║         │   DONATE: L.eff_priority = 60                                  ║
 * ║         │                         │                                      ║
 * ║       BLOCK                   RUNS (at pri 60)                           ║
 * ║         │                         │                                      ║
 * ║         │                    lock_release                                ║
 * ║         │                    L.eff_priority = 20                         ║
 * ║         │                         │                                      ║
 * ║      UNBLOCK ◄────────────────────┘                                      ║
 * ║         │                                                                ║
 * ║      RUNS (highest priority now)                                         ║
 * ║                                                                          ║
 * ║  CONDITION VARIABLE PATTERN:                                             ║
 * ║  ────────────────────────────                                            ║
 * ║  Unlike semaphores where multiple threads share one waiters list,        ║
 * ║  condition variables use "one semaphore per waiter" pattern:             ║
 * ║                                                                          ║
 * ║    cond->waiters: [ sema_elem_A, sema_elem_B, sema_elem_C ]               ║
 * ║                         │             │             │                    ║
 * ║                         ▼             ▼             ▼                    ║
 * ║                    sema(0)       sema(0)       sema(0)                   ║
 * ║                         │             │             │                    ║
 * ║                    thread_A      thread_B      thread_C                  ║
 * ║                                                                          ║
 * ║  This allows cond_signal to wake a specific thread (e.g., highest        ║
 * ║  priority) rather than just the first in queue.                          ║
 * ║                                                                          ║
 * ║  Originally derived from Nachos instructional OS.                        ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 *
 * Copyright (c) 1992-1996 The Regents of the University of California.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose, without fee, and
 * without written agreement is hereby granted, provided that the
 * above copyright notice and the following two paragraphs appear
 * in all copies of this software.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
 * ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
 * AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
 * HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
 * BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
 * MODIFICATIONS.
 */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * SEMAPHORE IMPLEMENTATION
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void sema_init(struct semaphore* sema, unsigned value) {
  ASSERT(sema != NULL);

  sema->value = value;
  list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void sema_down(struct semaphore* sema) {
  enum intr_level old_level;

  ASSERT(sema != NULL);
  ASSERT(!intr_context());

  old_level = intr_disable();
  while (sema->value == 0) {
    list_push_back(&sema->waiters, &thread_current()->elem);
    thread_block();
  }
  sema->value--;
  intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore* sema) {
  enum intr_level old_level;
  bool success;

  ASSERT(sema != NULL);

  old_level = intr_disable();
  if (sema->value > 0) {
    sema->value--;
    success = true;
  } else
    success = false;
  intr_set_level(old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore* sema) {
  enum intr_level old_level;
  struct thread* woken = NULL;

  ASSERT(sema != NULL);

  old_level = intr_disable();
  if (!list_empty(&sema->waiters)) {
    struct thread* t;
    if (active_sched_policy == SCHED_PRIO) {
      struct list_elem* max_elem = list_max(&sema->waiters, thread_priority_less, NULL);
      list_remove(max_elem);
      t = list_entry(max_elem, struct thread, elem);
    } else {
      t = list_entry(list_pop_front(&sema->waiters), struct thread, elem);
    }
    thread_unblock(t);
    woken = t;
  }
  sema->value++;
  intr_set_level(old_level);

  /* Only yield if NOT in interrupt context */
  if (!intr_context() && woken != NULL && woken->eff_priority > thread_current()->eff_priority)
    thread_yield();
}

static void sema_test_helper(void* sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void) {
  struct semaphore sema[2];
  int i;

  printf("Testing semaphores...");
  sema_init(&sema[0], 0);
  sema_init(&sema[1], 0);
  thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) {
    sema_up(&sema[0]);
    sema_down(&sema[1]);
  }
  printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void sema_test_helper(void* sema_) {
  struct semaphore* sema = sema_;
  int i;

  for (i = 0; i < 10; i++) {
    sema_down(&sema[0]);
    sema_up(&sema[1]);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * LOCK IMPLEMENTATION
 * ─────────────────────────────────────────────────────────────────────────────
 * A lock provides mutual exclusion with owner tracking and priority donation.
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock* lock) {
  ASSERT(lock != NULL);

  lock->holder = NULL;
  lock->max_donation = 0;
  sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock* lock) {
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(!lock_held_by_current_thread(lock));

  if (active_sched_policy == SCHED_PRIO) {
    /* Set waiting_lock before attempting to acquire */
    thread_current()->waiting_lock = lock;

    if (lock->holder != NULL) {
      /* Donate priority to the HOLDER (not waiters!) */
      struct thread* holder = lock->holder;
      if (thread_current()->eff_priority > holder->eff_priority) {
        enum intr_level old_level = intr_disable();
        holder->eff_priority = thread_current()->eff_priority;

        /* If holder is in ready queue, re-insert at correct position */
        if (holder->status == THREAD_READY) {
          thread_reinsert_ready(holder);
        }

        /* Chain: if holder is waiting for another lock, propagate */
        struct lock* next_lock = holder->waiting_lock;
        while (next_lock != NULL && next_lock->holder != NULL) {
          struct thread* next_holder = next_lock->holder;
          if (holder->eff_priority > next_holder->eff_priority) {
            next_holder->eff_priority = holder->eff_priority;
            /* Re-sort if in ready queue */
            if (next_holder->status == THREAD_READY) {
              thread_reinsert_ready(next_holder);
            }
          }
          holder = next_holder;
          next_lock = holder->waiting_lock;
        }
        intr_set_level(old_level);
      }
    }
  }

  sema_down(&lock->semaphore);

  /* We now hold the lock */
  lock->holder = thread_current();
  if (active_sched_policy == SCHED_PRIO) {
    thread_current()->waiting_lock = NULL;
    list_push_back(&thread_current()->held_locks, &lock->elem);
  }
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock* lock) {
  bool success;

  ASSERT(lock != NULL);
  ASSERT(!lock_held_by_current_thread(lock));

  success = sema_try_down(&lock->semaphore);
  if (success) {
    lock->holder = thread_current();
    if (active_sched_policy == SCHED_PRIO) {
      list_push_back(&thread_current()->held_locks, &lock->elem);
    }
  }
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock* lock) {
  ASSERT(lock != NULL);
  ASSERT(lock_held_by_current_thread(lock));

  if (active_sched_policy == SCHED_PRIO) {
    struct thread* cur = thread_current();

    /* Remove this lock from our held_locks list */
    list_remove(&lock->elem);

    /* Recalculate eff_priority from base priority + remaining held locks */
    int max_prio = cur->priority;

    for (struct list_elem* e = list_begin(&cur->held_locks); e != list_end(&cur->held_locks);
         e = list_next(e)) {
      struct lock* held = list_entry(e, struct lock, elem);

      /* Find max priority among this lock's waiters */
      if (!list_empty(&held->semaphore.waiters)) {
        struct list_elem* max_waiter =
            list_max(&held->semaphore.waiters, thread_priority_less, NULL);
        struct thread* t = list_entry(max_waiter, struct thread, elem);
        if (t->eff_priority > max_prio)
          max_prio = t->eff_priority;
      }
    }

    cur->eff_priority = max_prio;
  }

  lock->holder = NULL;
  sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock* lock) {
  ASSERT(lock != NULL);

  return lock->holder == thread_current();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * READERS-WRITERS LOCK IMPLEMENTATION
 * ─────────────────────────────────────────────────────────────────────────────
 * Writer-preferring RW lock using monitor pattern (lock + 2 condition vars).
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Initializes a readers-writers lock. */
void rw_lock_init(struct rw_lock* rw_lock) {
  lock_init(&rw_lock->lock);
  cond_init(&rw_lock->read);
  cond_init(&rw_lock->write);
  rw_lock->AR = rw_lock->WR = rw_lock->AW = rw_lock->WW = 0;
}

/* Acquire a writer-centric readers-writers lock */
void rw_lock_acquire(struct rw_lock* rw_lock, bool reader) {
  /* Must hold the guard lock the entire time */
  lock_acquire(&rw_lock->lock);

  if (reader) {
    /* Reader code: Block while there are waiting or active writers */
    while ((rw_lock->AW + rw_lock->WW) > 0) {
      rw_lock->WR++;
      cond_wait(&rw_lock->read, &rw_lock->lock);
      rw_lock->WR--;
    }
    rw_lock->AR++;
  } else {
    /* Writer code: Block while there are any active readers/writers in the system */
    while ((rw_lock->AR + rw_lock->AW) > 0) {
      rw_lock->WW++;
      cond_wait(&rw_lock->write, &rw_lock->lock);
      rw_lock->WW--;
    }
    rw_lock->AW++;
  }

  /* Release guard lock */
  lock_release(&rw_lock->lock);
}

/* Release a writer-centric readers-writers lock */
void rw_lock_release(struct rw_lock* rw_lock, bool reader) {
  /* Must hold the guard lock the entire time */
  lock_acquire(&rw_lock->lock);

  if (reader) {
    /* Reader code: Wake any waiting writers if we are the last reader */
    rw_lock->AR--;
    if (rw_lock->AR == 0 && rw_lock->WW > 0)
      cond_signal(&rw_lock->write, &rw_lock->lock);
  } else {
    /* Writer code: First try to wake a waiting writer, otherwise all waiting readers */
    rw_lock->AW--;
    if (rw_lock->WW > 0)
      cond_signal(&rw_lock->write, &rw_lock->lock);
    else if (rw_lock->WR > 0)
      cond_broadcast(&rw_lock->read, &rw_lock->lock);
  }

  /* Release guard lock */
  lock_release(&rw_lock->lock);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CONDITION VARIABLE IMPLEMENTATION
 * ─────────────────────────────────────────────────────────────────────────────
 * Mesa-style condition variables using "one semaphore per waiter" pattern.
 * ═══════════════════════════════════════════════════════════════════════════*/

/* One semaphore per waiting thread. 
   This pattern allows us to selectively wake specific waiters (e.g., highest priority)
   rather than just the first in queue. Each waiter allocates this on their stack. */
struct semaphore_elem {
  struct list_elem elem;      /* Element in condition's waiters list. */
  struct semaphore semaphore; /* This waiter's personal semaphore (initialized to 0). */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition* cond) {
  ASSERT(cond != NULL);

  list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition* cond, struct lock* lock) {
  struct semaphore_elem waiter;

  ASSERT(cond != NULL);
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(lock_held_by_current_thread(lock));

  sema_init(&waiter.semaphore, 0);
  list_push_back(&cond->waiters, &waiter.elem);
  lock_release(lock);
  sema_down(&waiter.semaphore);
  lock_acquire(lock);
}

/* Compares priority of threads waiting on condition variable semaphores.
   Returns true if a's thread has lower priority than b's thread.
   
   Condition Variable Waiter Pattern:
   ─────────────────────────────────
   Unlike a regular semaphore where multiple threads share one wait queue,
   condition variables use a "one semaphore per waiter" pattern:
   
   Each thread calling cond_wait():
   1. Creates its own semaphore_elem on the stack
   2. Initializes its own personal semaphore (value=0)
   3. Adds the semaphore_elem to cond->waiters
   4. Blocks on its own personal semaphore via sema_down()
   
   Therefore, each semaphore_elem in cond->waiters has EXACTLY ONE thread
   waiting on its semaphore, making list_front(&sema.waiters) always valid
   and always returning that single waiting thread.
   
   This pattern allows cond_signal to selectively wake any waiter
   (e.g., highest priority) rather than just the first in queue. */
static bool cond_sema_priority_less(const struct list_elem* a, const struct list_elem* b,
                                    void* aux UNUSED) {
  struct semaphore_elem* sa = list_entry(a, struct semaphore_elem, elem);
  struct semaphore_elem* sb = list_entry(b, struct semaphore_elem, elem);

  /* Each semaphore has exactly one waiter (the thread waiting on this cond) */
  struct thread* ta = list_entry(list_front(&sa->semaphore.waiters), struct thread, elem);
  struct thread* tb = list_entry(list_front(&sb->semaphore.waiters), struct thread, elem);

  return ta->eff_priority < tb->eff_priority;
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition* cond, struct lock* lock UNUSED) {
  ASSERT(cond != NULL);
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(lock_held_by_current_thread(lock));

  if (!list_empty(&cond->waiters)) {
    if (active_sched_policy == SCHED_PRIO) {
      /* Wake highest priority waiter */
      struct list_elem* max_elem = list_max(&cond->waiters, cond_sema_priority_less, NULL);
      list_remove(max_elem);
      sema_up(&list_entry(max_elem, struct semaphore_elem, elem)->semaphore);
    } else {
      /* FIFO: wake front waiter */
      sema_up(&list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem)->semaphore);
    }
  }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition* cond, struct lock* lock) {
  ASSERT(cond != NULL);
  ASSERT(lock != NULL);

  while (!list_empty(&cond->waiters))
    cond_signal(cond, lock);
}
