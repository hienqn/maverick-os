#include "filesys/cache_prefetch.h"
#include "filesys/cache.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* Size of the circular prefetch queue. */
#define PREFETCH_QUEUE_SIZE 16

/* Circular queue for prefetch requests. */
static block_sector_t prefetch_queue[PREFETCH_QUEUE_SIZE];
static int prefetch_head = 0;  /* Next slot to dequeue from. */
static int prefetch_tail = 0;  /* Next slot to enqueue to. */

/* Synchronization for prefetch queue. */
static struct lock prefetch_lock;
static struct condition prefetch_ready;

/* Flag to signal the prefetcher thread to stop. */
static bool prefetcher_running = false;

/* The prefetcher thread function. */
static void cache_prefetcher_thread(void *aux UNUSED) {
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

