#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>
#include <string.h>

/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                        BUFFER CACHE OVERVIEW                             ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  Cache Structure:                                                        ║
 * ║  ┌────────┬────────┬────────┬─────┬────────┐                             ║
 * ║  │ Slot 0 │ Slot 1 │ Slot 2 │ ... │Slot 63 │  (64 entries, 512B each)    ║
 * ║  └────────┴────────┴────────┴─────┴────────┘                             ║
 * ║       ↑                                                                  ║
 * ║    clock_hand (for eviction)                                             ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  Entry State Machine:                                                    ║
 * ║                                                                          ║
 * ║                    ┌─────────────────────────────────┐                   ║
 * ║                    │                                 │                   ║
 * ║                    ▼                                 │ (eviction)        ║
 * ║              ┌──────────┐                            │                   ║
 * ║              │ INVALID  │  ← empty slot              │                   ║
 * ║              └────┬─────┘                            │                   ║
 * ║                   │ allocate slot                    │                   ║
 * ║                   ▼                                  │                   ║
 * ║              ┌──────────┐                            │                   ║
 * ║              │ LOADING  │  ← reserved, I/O in progress                   ║
 * ║              └────┬─────┘    (other threads wait or skip)                ║
 * ║                   │ I/O complete                     │                   ║
 * ║                   ▼                                  │                   ║
 * ║              ┌──────────┐                            │                   ║
 * ║              │  VALID   │  ← data ready to use ──────┘                   ║
 * ║              └──────────┘                                                ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  Two-Lock Protocol (Hand-over-Hand):                                     ║
 * ║                                                                          ║
 * ║  global_lock:  ████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░             ║
 * ║  entry_lock:   ░░░░░░░░░░░░████████████████████████░░░░░░░░░             ║
 * ║                            ↑                                             ║
 * ║                     overlap (baton pass)                                 ║
 * ║                                                                          ║
 * ║  • global_lock: protects search + slot selection (short duration)       ║
 * ║  • entry_lock:  protects data access (can hold during I/O wait)         ║
 * ║  • LOADING state: reserves slot without blocking other operations       ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

/* The buffer cache: array of 64 entries. */
static struct cache_entry cache[CACHE_SIZE];

/* Global lock for cache search and slot allocation. */
static struct lock cache_global_lock;

/* Clock hand for eviction algorithm. */
static int clock_hand;

/* ============ Debug Statistics ============ */
static int cache_hits = 0;
static int cache_misses = 0;
static int cache_evictions = 0;
static int cache_writebacks = 0;

/* ============ Periodic Flusher Thread ============ */

/* How often to flush dirty entries (in timer ticks). 
   Default: 30 seconds (assuming TIMER_FREQ = 100). */
#define CACHE_FLUSH_INTERVAL (30 * TIMER_FREQ)

/* Flag to signal the flusher thread to stop. */
static bool flusher_running = false;

/* The periodic flusher thread function. */
static void cache_flusher_thread(void *aux UNUSED) {
  while (flusher_running) {
    timer_sleep(CACHE_FLUSH_INTERVAL);
    
    if (!flusher_running)
      break;
    
    /* Flush all dirty entries to disk. */
    cache_flush();
  }
}

/* ============ Replacement Policy Indirection ============ */

/* Replacement policy options. */
enum cache_replacement_policy {
  CACHE_REPLACE_CLOCK,
  CACHE_REPLACE_FIFO,
  // Add more policies here
};

/* Current policy (change this to switch strategies). */
static enum cache_replacement_policy replacement_policy = CACHE_REPLACE_CLOCK;

/* Forward declarations for policy implementations. */
static struct cache_entry *evict_clock(void);
static struct cache_entry *evict_fifo(void);

/* Select victim using current policy. Returns a VALID entry to evict. */
static struct cache_entry *select_victim(void) {
  switch (replacement_policy) {
    case CACHE_REPLACE_CLOCK:
      return evict_clock();
    case CACHE_REPLACE_FIFO:
      return evict_fifo();
    default:
      PANIC("Unknown replacement policy");
  }
}

/* ============ Policy Implementations ============ */

/* Clock algorithm victim selection. */
static struct cache_entry *evict_clock(void) {
  int scans = 0;
  while (true) {
    struct cache_entry *entry = &cache[clock_hand];

    // Only consider valid entries -- skip empty slots (INVALID/LOADING).
    if (entry->state == CACHE_VALID) {
      if (entry->accessed) {
        entry->accessed = false;  // Give a second chance.
      } else {
        // This entry has not been accessed recently, so choose it as victim.
        struct cache_entry *victim = entry;
        clock_hand = (clock_hand + 1) % CACHE_SIZE;
        return victim;
      }
    }
    clock_hand = (clock_hand + 1) % CACHE_SIZE;
    // (Optional: Prevent infinite loops, but this should never happen.)
    if (++scans > 2 * CACHE_SIZE)
      PANIC("Clock algorithm could not find a victim");
  }
}

/* FIFO victim selection. */
static struct cache_entry *evict_fifo(void) {
  static int fifo_hand = 0;
  int start = fifo_hand;
  while (true) {
    struct cache_entry *entry = &cache[fifo_hand];
    // Only consider valid entries.
    if (entry->state == CACHE_VALID) {
      struct cache_entry *victim = entry;
      fifo_hand = (fifo_hand + 1) % CACHE_SIZE;
      return victim;
    }
    fifo_hand = (fifo_hand + 1) % CACHE_SIZE;
    if (fifo_hand == start) {
      // We've scanned the whole cache and found no valid entries
      PANIC("FIFO algorithm could not find a victim");
    }
  }
}

/* ======================================================== */

/* Initialize the buffer cache. */
void cache_init(void) {
  lock_init(&cache_global_lock);
  clock_hand = 0;

  for (int i = 0; i < CACHE_SIZE; i++) {
    cache[i].state = CACHE_INVALID;
    cache[i].dirty = false;
    cache[i].accessed = false;
    lock_init(&cache[i].entry_lock);
    cond_init(&cache[i].loading_done);
  }

  /* Start the periodic flusher thread. */
  flusher_running = true;
  thread_create("cache_flusher", PRI_DEFAULT, cache_flusher_thread, NULL);
}

/* Find a sector in the cache. Returns entry or NULL if not found.
   Must be called while holding cache_global_lock. */
static struct cache_entry *cache_lookup(block_sector_t sector) {
  ASSERT(lock_held_by_current_thread(&cache_global_lock));

  for (size_t i = 0; i < CACHE_SIZE; i++) {
    if (cache[i].state != CACHE_INVALID && cache[i].sector == sector) {
      return &cache[i];
    }
  }

  return NULL;
}

/* Find an empty slot or evict one using selected policy.
   Must be called while holding cache_global_lock.
   Returns the slot to use (writes back dirty data if evicting). */
static struct cache_entry *cache_evict(void) {
  ASSERT(lock_held_by_current_thread(&cache_global_lock));
  // 1. First, look for an INVALID (empty) slot
  for (int i = 0; i < CACHE_SIZE; ++i) {
    if (cache[i].state == CACHE_INVALID) {
      return &cache[i];
    }
  }

  // 2. No empty slot - use policy to select victim
  struct cache_entry *victim = select_victim();

  ASSERT(victim != NULL);
  cache_evictions++;

  // 3. If victim is dirty, write it back to disk
  if (victim->dirty) {
    cache_writebacks++;
    lock_acquire(&victim->entry_lock);
    // Only flush if valid (should always be the case, but double check)
    if (victim->state == CACHE_VALID) {
      block_write(fs_device, victim->sector, victim->data);
      victim->dirty = false;
    }
    lock_release(&victim->entry_lock);
  }

  // 4. Return the slot
  return victim;
}

/* Copy sector data to buffer and mark entry as accessed.
   Caller must hold entry_lock. */
static void read_from_entry(struct cache_entry *ce, void *buffer) {
  ASSERT(ce != NULL);
  ASSERT(lock_held_by_current_thread(&ce->entry_lock));
  
  memcpy(buffer, ce->data, BLOCK_SECTOR_SIZE);
  ce->accessed = true;
}
  
/* Wait until entry transitions out of LOADING state.
   Caller must hold entry_lock. */
static void wait_for_loading(struct cache_entry *ce) {
  ASSERT(ce != NULL);
  ASSERT(lock_held_by_current_thread(&ce->entry_lock));
  ASSERT(ce->state == CACHE_LOADING);  // Only call when actually LOADING
  
  while (ce->state == CACHE_LOADING) {
    cond_wait(&ce->loading_done, &ce->entry_lock);
  }
}
  
void cache_read(block_sector_t sector, void *buffer) {
  ASSERT(buffer != NULL);
  
  lock_acquire(&cache_global_lock);
  struct cache_entry *ce = cache_lookup(sector);
  
  /* Case 1: Cache hit - data is ready */
  if (ce != NULL && ce->state == CACHE_VALID) {
    cache_hits++;
    lock_acquire(&ce->entry_lock);
    lock_release(&cache_global_lock);
    
    read_from_entry(ce, buffer);
    
    lock_release(&ce->entry_lock);
    return;
  }
  
  /* Case 2: Someone else is loading this sector - wait for them */
  if (ce != NULL && ce->state == CACHE_LOADING) {
    lock_acquire(&ce->entry_lock);
    lock_release(&cache_global_lock);
    
    wait_for_loading(ce);
    read_from_entry(ce, buffer);
    
    lock_release(&ce->entry_lock);
    return;
  }
  
  /* Case 3: Cache miss - we must load from disk */
  ASSERT(ce == NULL);
  cache_misses++;
  
  struct cache_entry *slot = cache_evict();
  ASSERT(slot != NULL);
  
  /* Reserve slot and release global lock before slow I/O */
  slot->state = CACHE_LOADING;
  slot->sector = sector;
  slot->dirty = false;
  lock_release(&cache_global_lock);
  
  /* Perform disk I/O without holding any locks */
  block_read(fs_device, sector, slot->data);
  
  /* Finalize: mark valid and notify waiters */
  lock_acquire(&slot->entry_lock);
  slot->state = CACHE_VALID;
  slot->accessed = true;
  cond_broadcast(&slot->loading_done, &slot->entry_lock);
  
  memcpy(buffer, slot->data, BLOCK_SECTOR_SIZE);
  
  lock_release(&slot->entry_lock);
}

/* Read partial sector data from cache at given offset.
   Copies chunk_size bytes starting at sector_ofs into buffer. */
void cache_read_at(block_sector_t sector, void *buffer,
                   int sector_ofs, int chunk_size) {
  /* Validate parameters */
  ASSERT(buffer != NULL);
  ASSERT(sector_ofs >= 0);
  ASSERT(sector_ofs < BLOCK_SECTOR_SIZE);
  ASSERT(chunk_size > 0);
  ASSERT(sector_ofs + chunk_size <= BLOCK_SECTOR_SIZE);
  
  lock_acquire(&cache_global_lock);
  struct cache_entry *ce = cache_lookup(sector);
  
  /* Case 1: Cache hit - data is ready */
  if (ce != NULL && ce->state == CACHE_VALID) {
    cache_hits++;
    lock_acquire(&ce->entry_lock);
    lock_release(&cache_global_lock);
    
    memcpy(buffer, (uint8_t *)ce->data + sector_ofs, chunk_size);
    ce->accessed = true;
    
    lock_release(&ce->entry_lock);
    return;
  }
  
  /* Case 2: Someone else is loading this sector - wait for them */
  if (ce != NULL && ce->state == CACHE_LOADING) {
    lock_acquire(&ce->entry_lock);
    lock_release(&cache_global_lock);
    
    wait_for_loading(ce);
    memcpy(buffer, (uint8_t *)ce->data + sector_ofs, chunk_size);
    ce->accessed = true;
    
    lock_release(&ce->entry_lock);
    return;
  }
  
  /* Case 3: Cache miss - we must load from disk */
  ASSERT(ce == NULL);
  cache_misses++;
  
  struct cache_entry *slot = cache_evict();
  ASSERT(slot != NULL);
  
  /* Reserve slot and release global lock before slow I/O */
  slot->state = CACHE_LOADING;
  slot->sector = sector;
  slot->dirty = false;
  lock_release(&cache_global_lock);
  
  /* Perform disk I/O without holding any locks */
  block_read(fs_device, sector, slot->data);
  
  /* Finalize: mark valid and notify waiters */
  lock_acquire(&slot->entry_lock);
  slot->state = CACHE_VALID;
  slot->accessed = true;
  cond_broadcast(&slot->loading_done, &slot->entry_lock);
  
  memcpy(buffer, (uint8_t *)slot->data + sector_ofs, chunk_size);
  
  lock_release(&slot->entry_lock);
}

/* Write data to cache at given offset within sector. */
void cache_write(block_sector_t sector, const void *buffer,
                 int sector_ofs, int chunk_size) {
  /* Validate parameters */
  ASSERT(buffer != NULL);
  ASSERT(sector_ofs >= 0);
  ASSERT(sector_ofs < BLOCK_SECTOR_SIZE);
  ASSERT(chunk_size > 0);
  ASSERT(sector_ofs + chunk_size <= BLOCK_SECTOR_SIZE);
  
  lock_acquire(&cache_global_lock);
  struct cache_entry *ce = cache_lookup(sector);               
  
  /* Case 1: Sector is in the cache and it's valid */
  if (ce != NULL && ce->state == CACHE_VALID) {
    cache_hits++;
    lock_acquire(&ce->entry_lock);
    lock_release(&cache_global_lock);
    memcpy((uint8_t *)ce->data + sector_ofs, buffer, chunk_size);
    ce->dirty = true;
    ce->accessed = true;
    lock_release(&ce->entry_lock);
    return;
  }

  /* Case 2: Sector is in the cache, but it's being loaded*/
  if (ce != NULL && ce->state == CACHE_LOADING) {
    lock_acquire(&ce->entry_lock);
    lock_release(&cache_global_lock);

    wait_for_loading(ce);

    // Now the sector should be VALID. Write as usual.
    memcpy((uint8_t *)ce->data + sector_ofs, buffer, chunk_size);
    ce->dirty = true;
    ce->accessed = true;
    lock_release(&ce->entry_lock);
    return;
  }

  /* Case 3: Sector is not in the cache - must allocate and possibly load */
  ASSERT(ce == NULL);
  cache_misses++;
  
  struct cache_entry *slot = cache_evict();
  ASSERT(slot != NULL);

  // We do not need to acquire the slot (entry) lock within this block.
  // Reason: At this point, we still hold the cache_global_lock and the cache_eviction function 
  // returns a slot that is exclusively reserved for us (never concurrently accessed by others 
  // until we finish initializing and change its state from CACHE_LOADING to CACHE_VALID).
  //
  // Only after we release the global lock (and the slot might become visible to others), 
  // do other threads contend for the per-slot entry_lock. Before that, we have sole access.
  //
  // So: lock_acquire(&slot->entry_lock) is NOT needed here.
  
  /* Reserve slot before releasing global lock */
  slot->state = CACHE_LOADING;
  slot->sector = sector;
  lock_release(&cache_global_lock);
  
  /* If partial write, must load existing data first to preserve other bytes.
     If full write, skip the read - we're overwriting everything anyway. */
  bool is_full_write = (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE);
  if (!is_full_write) {
    block_read(fs_device, sector, slot->data);
  }
  
  /* Write our data into the slot */
  lock_acquire(&slot->entry_lock);
  memcpy((uint8_t *)slot->data + sector_ofs, buffer, chunk_size);
  slot->state = CACHE_VALID;
  // We set dirty to true because this cache entry now holds data 
  // that has been written to by the caller, but not yet flushed to disk.
  slot->dirty = true;
  // We set accessed to true so the clock eviction algorithm knows this
  // slot was recently used (helps avoid evicting actively-used cache lines).
  slot->accessed = true;
  cond_broadcast(&slot->loading_done, &slot->entry_lock);
  
  lock_release(&slot->entry_lock);
}

/* Flush all dirty cache entries to disk. */
void cache_flush(void) {
  lock_acquire(&cache_global_lock);
  
  for (int i = 0; i < CACHE_SIZE; i++) {
    struct cache_entry *ce = &cache[i];
    if (ce->state == CACHE_VALID && ce->dirty) {
      lock_acquire(&ce->entry_lock);
      block_write(fs_device, ce->sector, ce->data);
      ce->dirty = false;
      lock_release(&ce->entry_lock);
    }
  }
  
  lock_release(&cache_global_lock);
}

/* Stop the periodic flusher thread and flush remaining dirty entries. */
void cache_shutdown(void) {
  /* Signal the flusher thread to stop. */
  flusher_running = false;
  
  /* Final flush to ensure all dirty data is written. */
  cache_flush();
}

/* Print cache statistics for debugging. */
void cache_print_stats(void) {
  int total = cache_hits + cache_misses;
  int hit_rate = total > 0 ? (cache_hits * 100 / total) : 0;
  
  printf("Buffer Cache Statistics:\n");
  printf("  Hits:       %d\n", cache_hits);
  printf("  Misses:     %d\n", cache_misses);
  printf("  Hit Rate:   %d%%\n", hit_rate);
  printf("  Evictions:  %d\n", cache_evictions);
  printf("  Writebacks: %d\n", cache_writebacks);
}

/* Reset cache statistics (useful for testing). */
void cache_reset_stats(void) {
  cache_hits = 0;
  cache_misses = 0;
  cache_evictions = 0;
  cache_writebacks = 0;
}

