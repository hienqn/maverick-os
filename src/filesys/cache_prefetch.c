/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                         PREFETCH SUBSYSTEM                               ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  PURPOSE:                                                                ║
 * ║  ────────                                                                ║
 * ║  Prefetching anticipates future disk reads by loading sectors into the  ║
 * ║  cache BEFORE they are explicitly requested. This hides disk latency    ║
 * ║  for sequential access patterns (e.g., reading a file from start to     ║
 * ║  end), turning cache misses into cache hits.                            ║
 * ║                                                                          ║
 * ║  HOW IT WORKS:                                                           ║
 * ║  ─────────────                                                           ║
 * ║                                                                          ║
 * ║    ┌──────────────┐      ┌─────────────────┐      ┌──────────────────┐  ║
 * ║    │  cache_read  │      │ Prefetch Queue  │      │ Prefetcher Thread│  ║
 * ║    │   (sector N) │      │  [N+1, ?, ?, ?] │      │   (background)   │  ║
 * ║    └──────┬───────┘      └────────┬────────┘      └────────┬─────────┘  ║
 * ║           │                       │                        │            ║
 * ║           │ 1. Read sector N      │                        │            ║
 * ║           │    from cache/disk    │                        │            ║
 * ║           │                       │                        │            ║
 * ║           │ 2. Request prefetch   │                        │            ║
 * ║           │    of sector N+1  ────┼──► enqueue(N+1)        │            ║
 * ║           │                       │                        │            ║
 * ║           │ 3. Return to caller   │    3. Wake up          │            ║
 * ║           ▼    (non-blocking)     │       (cond_signal) ───┼──►         ║
 * ║                                   │                        │            ║
 * ║                                   │    4. Dequeue N+1  ◄───┼────        ║
 * ║                                   │                        │            ║
 * ║                                   │    5. Load into cache  │            ║
 * ║                                   │       (cache_do_prefetch)           ║
 * ║                                   │                        ▼            ║
 * ║                                   │                                     ║
 * ║    Later: cache_read(N+1) ───────►│◄─── Already in cache! (HIT)        ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  KEY DESIGN DECISIONS:                                                   ║
 * ║  ─────────────────────                                                   ║
 * ║                                                                          ║
 * ║  1. ASYNCHRONOUS (Non-blocking)                                          ║
 * ║     - cache_request_prefetch() returns immediately                       ║
 * ║     - Disk I/O happens in background thread                              ║
 * ║     - Caller doesn't wait for prefetch to complete                       ║
 * ║                                                                          ║
 * ║  2. BEST-EFFORT (Graceful degradation)                                   ║
 * ║     - If queue is full, requests are silently dropped                    ║
 * ║     - If sector already cached, prefetch is skipped                      ║
 * ║     - System works correctly even if prefetch fails                      ║
 * ║                                                                          ║
 * ║  3. LOW PRIORITY                                                         ║
 * ║     - Prefetcher runs at PRI_DEFAULT - 1                                 ║
 * ║     - Explicit reads take priority over speculative prefetch             ║
 * ║     - Prefetched sectors have accessed=false (lower eviction priority)   ║
 * ║                                                                          ║
 * ║  4. SEQUENTIAL HEURISTIC                                                 ║
 * ║     - After reading sector N, prefetch sector N+1                        ║
 * ║     - Works well for sequential file reads                               ║
 * ║     - Could be extended with more sophisticated prediction               ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  CIRCULAR QUEUE:                                                         ║
 * ║  ───────────────                                                         ║
 * ║                                                                          ║
 * ║    prefetch_queue[16]:                                                   ║
 * ║    ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬───...   ║
 * ║    │ 42 │ 43 │ 44 │    │    │    │    │    │    │    │    │    │        ║
 * ║    └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴───...   ║
 * ║      ▲              ▲                                                    ║
 * ║      │              │                                                    ║
 * ║    head           tail                                                   ║
 * ║   (dequeue)      (enqueue)                                               ║
 * ║                                                                          ║
 * ║    Full when: (tail + 1) % SIZE == head                                  ║
 * ║    Empty when: head == tail                                              ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  SYNCHRONIZATION:                                                        ║
 * ║  ────────────────                                                        ║
 * ║                                                                          ║
 * ║    prefetch_lock:  Protects queue operations (enqueue/dequeue)           ║
 * ║    prefetch_ready: Condition variable to wake prefetcher thread          ║
 * ║                                                                          ║
 * ║    Producer (cache_request_prefetch):                                    ║
 * ║      lock → enqueue → signal → unlock                                    ║
 * ║                                                                          ║
 * ║    Consumer (prefetcher thread):                                         ║
 * ║      lock → wait (if empty) → dequeue → unlock → do I/O                  ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#include "filesys/cache_prefetch.h"
#include "filesys/cache.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* Size of the circular prefetch queue. */
#define PREFETCH_QUEUE_SIZE 16

/* Circular queue for prefetch requests. */
static block_sector_t prefetch_queue[PREFETCH_QUEUE_SIZE];
static int prefetch_head = 0; /* Next slot to dequeue from. */
static int prefetch_tail = 0; /* Next slot to enqueue to. */

/* Synchronization for prefetch queue. */
static struct lock prefetch_lock;
static struct condition prefetch_ready;

/* Flag to signal the prefetcher thread to stop. */
static bool prefetcher_running = false;

/* The prefetcher thread function. */
static void cache_prefetcher_thread(void* aux UNUSED) {
  while (prefetcher_running) {
    lock_acquire(&prefetch_lock);

    /* Wait for work (or shutdown signal). */
    while (prefetch_head == prefetch_tail && prefetcher_running) {
      cond_wait(&prefetch_ready, &prefetch_lock);
    }

    if (!prefetcher_running) {
      lock_release(&prefetch_lock);
      break;
    }

    /* Dequeue a sector to prefetch. */
    block_sector_t sector = prefetch_queue[prefetch_head];
    prefetch_head = (prefetch_head + 1) % PREFETCH_QUEUE_SIZE;
    lock_release(&prefetch_lock);

    /* Load sector into cache (if not already there). */
    cache_do_prefetch(sector);
  }
}

/* Initialize the prefetch subsystem. */
void cache_prefetch_init(void) {
  lock_init(&prefetch_lock);
  cond_init(&prefetch_ready);
  prefetch_head = 0;
  prefetch_tail = 0;
  prefetcher_running = true;
  thread_create("cache_prefetch", PRI_DEFAULT - 1, cache_prefetcher_thread, NULL);
}

/* Shutdown the prefetch subsystem. */
void cache_prefetch_shutdown(void) {
  lock_acquire(&prefetch_lock);
  prefetcher_running = false;
  cond_signal(&prefetch_ready, &prefetch_lock);
  lock_release(&prefetch_lock);
}

/* Queue a sector for asynchronous prefetching.
   Best-effort: silently drops request if queue is full. */
void cache_request_prefetch(block_sector_t sector) {
  lock_acquire(&prefetch_lock);

  int next_tail = (prefetch_tail + 1) % PREFETCH_QUEUE_SIZE;
  if (next_tail != prefetch_head) {
    /* Queue not full - add the request. */
    prefetch_queue[prefetch_tail] = sector;
    prefetch_tail = next_tail;
    cond_signal(&prefetch_ready, &prefetch_lock);
  }
  /* If queue is full, silently drop - prefetch is best-effort. */

  lock_release(&prefetch_lock);
}
