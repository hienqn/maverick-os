/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                     SYNCHRONIZATION PRIMITIVES                            ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  This module provides the fundamental synchronization primitives used    ║
 * ║  throughout PintOS: semaphores, locks, condition variables, and          ║
 * ║  readers-writers locks.                                                  ║
 * ║                                                                          ║
 * ║  PRIMITIVE HIERARCHY:                                                    ║
 * ║  ────────────────────                                                    ║
 * ║                                                                          ║
 * ║    ┌─────────────────────────────────────────────────────────────────┐   ║
 * ║    │                    Higher-Level Constructs                      │   ║
 * ║    │    ┌──────────────────┐        ┌───────────────────────────┐    │   ║
 * ║    │    │ Readers-Writers  │        │   Monitors (lock + cond)  │    │   ║
 * ║    │    │      Lock        │        │                           │    │   ║
 * ║    │    └────────┬─────────┘        └─────────────┬─────────────┘    │   ║
 * ║    └─────────────┼────────────────────────────────┼──────────────────┘   ║
 * ║                  │                                │                      ║
 * ║    ┌─────────────┼────────────────────────────────┼──────────────────┐   ║
 * ║    │             ▼                                ▼                  │   ║
 * ║    │    ┌──────────────┐    ┌──────────────┐   ┌──────────────┐      │   ║
 * ║    │    │     Lock     │◄───│  Condition   │   │   RW Lock    │      │   ║
 * ║    │    │  (mutex)     │    │   Variable   │   │  (guard +    │      │   ║
 * ║    │    └──────┬───────┘    └──────┬───────┘   │   2 conds)   │      │   ║
 * ║    │           │                   │           └──────────────┘      │   ║
 * ║    └───────────┼───────────────────┼─────────────────────────────────┘   ║
 * ║                │                   │                                     ║
 * ║    ┌───────────┼───────────────────┼─────────────────────────────────┐   ║
 * ║    │           ▼                   ▼                                 │   ║
 * ║    │    ┌────────────────────────────────────────────────────────┐   │   ║
 * ║    │    │                    Semaphore                           │   │   ║
 * ║    │    │   (fundamental counting primitive)                     │   │   ║
 * ║    │    └────────────────────────────────────────────────────────┘   │   ║
 * ║    │                                                                 │   ║
 * ║    │                      Foundation Layer                           │   ║
 * ║    └─────────────────────────────────────────────────────────────────┘   ║
 * ║                                                                          ║
 * ║  WHEN TO USE EACH:                                                       ║
 * ║  ─────────────────                                                       ║
 * ║                                                                          ║
 * ║  Semaphore: General counting, producer-consumer, thread join             ║
 * ║  Lock:      Mutual exclusion, protecting shared data structures          ║
 * ║  Condition: Wait for arbitrary condition (with lock held)                ║
 * ║  RW Lock:   Many readers OR one writer (shared-exclusive access)         ║
 * ║                                                                          ║
 * ║  INTERRUPT SAFETY:                                                       ║
 * ║  ─────────────────                                                       ║
 * ║  • sema_up(), sema_try_down() — safe in interrupt context                ║
 * ║  • sema_down(), lock_acquire(), cond_wait() — NOT safe (may sleep)       ║
 * ║  • All operations disable interrupts briefly for atomicity               ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * SEMAPHORE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * A counting semaphore with two atomic operations:
 *   • DOWN (P, wait): Decrement if positive, else block until positive
 *   • UP (V, signal): Increment and wake one waiting thread
 *
 * Classic uses:
 *   • Binary semaphore (value 0 or 1): Mutual exclusion
 *   • Counting semaphore: Resource pool, bounded buffer
 *   • Signaling (initial value 0): Thread join, event notification
 *
 * Example - Producer/Consumer:
 *   struct semaphore items, spaces;
 *   sema_init(&items, 0);         // 0 items initially
 *   sema_init(&spaces, BUFFER_SIZE);
 *
 *   Producer:              Consumer:
 *   sema_down(&spaces);    sema_down(&items);
 *   add_to_buffer();       remove_from_buffer();
 *   sema_up(&items);       sema_up(&spaces);
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

struct semaphore {
  unsigned value;      /* Current value (never negative). */
  struct list waiters; /* List of threads blocked on this semaphore. */
};

void sema_init(struct semaphore*, unsigned value);
void sema_down(struct semaphore*);     /* May block - not interrupt-safe. */
bool sema_try_down(struct semaphore*); /* Non-blocking - interrupt-safe. */
void sema_up(struct semaphore*);       /* Never blocks - interrupt-safe. */
void sema_self_test(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * LOCK (MUTEX)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * A lock provides mutual exclusion - at most one thread can hold it.
 * Built on a binary semaphore but adds:
 *   • Owner tracking (for debugging and assertions)
 *   • Priority donation support (prevents priority inversion)
 *   • Non-recursive (acquiring twice = error)
 *
 * Priority Donation:
 * When a high-priority thread H waits for a lock held by low-priority L,
 * L temporarily inherits H's priority to prevent priority inversion.
 * The held_locks list and max_donation field support this.
 *
 * Lock vs Semaphore:
 *   • Lock: Same thread must acquire and release
 *   • Semaphore: Any thread can up/down (useful for signaling)
 *
 * Example:
 *   struct lock data_lock;
 *   lock_init(&data_lock);
 *
 *   lock_acquire(&data_lock);
 *   // Critical section - exclusive access
 *   modify_shared_data();
 *   lock_release(&data_lock);
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

struct lock {
  int max_donation;           /* Max priority donated through this lock. */
  struct thread* holder;      /* Thread holding lock (NULL if unlocked). */
  struct semaphore semaphore; /* Binary semaphore (0 = locked, 1 = unlocked). */
  struct list_elem elem;      /* Element in holder's held_locks list. */
};

void lock_init(struct lock*);
void lock_acquire(struct lock*);     /* May block - not interrupt-safe. */
bool lock_try_acquire(struct lock*); /* Non-blocking - interrupt-safe. */
void lock_release(struct lock*);
bool lock_held_by_current_thread(const struct lock*);

/* ═══════════════════════════════════════════════════════════════════════════
 * CONDITION VARIABLE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * A condition variable allows threads to wait for an arbitrary condition
 * to become true. Always used with an associated lock (forming a "monitor").
 *
 * Mesa-style semantics:
 *   • After cond_signal, signaled thread is made ready but may not run next
 *   • Signaled thread must re-check condition in a loop (spurious wakeups)
 *
 * Pattern:
 *   lock_acquire(&lock);
 *   while (!condition)           // Loop, not if!
 *     cond_wait(&cond, &lock);   // Releases lock, blocks, reacquires lock
 *   // Condition is now true
 *   lock_release(&lock);
 *
 * Signaling:
 *   lock_acquire(&lock);
 *   make_condition_true();
 *   cond_signal(&cond, &lock);   // Wake one waiter
 *   // or: cond_broadcast(&cond, &lock);  // Wake all waiters
 *   lock_release(&lock);
 *
 * Implementation Note:
 * Each waiting thread creates its own semaphore (on the stack). This allows
 * cond_signal to selectively wake the highest-priority waiter instead of
 * just the first in queue.
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

struct condition {
  struct list waiters; /* List of semaphore_elem, one per waiting thread. */
};

void cond_init(struct condition*);
void cond_wait(struct condition*, struct lock*);      /* Releases and reacquires lock. */
void cond_signal(struct condition*, struct lock*);    /* Wake one waiter. */
void cond_broadcast(struct condition*, struct lock*); /* Wake all waiters. */

/* ═══════════════════════════════════════════════════════════════════════════
 * READERS-WRITERS LOCK
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Allows either:
 *   • Multiple concurrent readers (shared access), OR
 *   • Exactly one writer (exclusive access)
 *
 * This implementation is WRITER-PREFERRING:
 *   • New readers block if any writers are waiting
 *   • Prevents writer starvation when readers are frequent
 *
 * State variables:
 *   AR = Active Readers    (currently reading)
 *   WR = Waiting Readers   (blocked, waiting to read)
 *   AW = Active Writers    (currently writing, 0 or 1)
 *   WW = Waiting Writers   (blocked, waiting to write)
 *
 * Reader entry:  while (AW + WW > 0) wait;  AR++;
 * Reader exit:   AR--; if (AR == 0 && WW > 0) signal writer;
 * Writer entry:  while (AR + AW > 0) wait;  AW++;
 * Writer exit:   AW--; prefer signaling writers, else broadcast readers;
 *
 * Example:
 *   struct rw_lock cache_lock;
 *   rw_lock_init(&cache_lock);
 *
 *   // Reader:
 *   rw_lock_acquire(&cache_lock, true);  // true = reader
 *   read_from_cache();
 *   rw_lock_release(&cache_lock, true);
 *
 *   // Writer:
 *   rw_lock_acquire(&cache_lock, false); // false = writer
 *   update_cache();
 *   rw_lock_release(&cache_lock, false);
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Convenience constants for reader/writer parameter. */
#define RW_READER 1
#define RW_WRITER 0

struct rw_lock {
  struct lock lock;       /* Guard lock for state variables. */
  struct condition read;  /* Condition for waiting readers. */
  struct condition write; /* Condition for waiting writers. */
  int AR, WR, AW, WW;     /* Active/Waiting Readers/Writers counts. */
};

void rw_lock_init(struct rw_lock*);
void rw_lock_acquire(struct rw_lock*, bool reader);
void rw_lock_release(struct rw_lock*, bool reader);

/* ═══════════════════════════════════════════════════════════════════════════
 * OPTIMIZATION BARRIER
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * The compiler may reorder memory operations for optimization. This macro
 * prevents reordering across it, ensuring operations before the barrier
 * happen before operations after it (from the compiler's perspective).
 *
 * Note: This is a COMPILER barrier, not a CPU memory barrier. On x86,
 * the CPU provides strong ordering guarantees, so this is usually sufficient.
 *
 * Example use: Implementing lock-free data structures, spin loops.
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

#define barrier() asm volatile("" : : : "memory")

#endif /* threads/synch.h */
