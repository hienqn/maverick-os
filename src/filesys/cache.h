#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"
#include <stdbool.h>

/*
 * BUFFER CACHE
 * ============
 * A write-back cache for disk sectors, reducing disk I/O by keeping
 * frequently accessed sectors in memory. The cache holds up to 64 sectors
 * (32 KB total) and uses write-back policy (dirty data flushed lazily).
 *
 * EVICTION POLICY (Clock Algorithm / Second-Chance):
 * --------------------------------------------------
 * When the cache is full and a new sector is needed:
 *   1. A clock hand sweeps through cache entries
 *   2. If entry's 'accessed' bit is set, clear it (give second chance)
 *   3. If entry's 'accessed' bit is clear, evict it
 *   4. Dirty entries are written to disk before eviction
 *
 * This approximates LRU with O(1) overhead per access.
 *
 * ENTRY STATE MACHINE:
 * --------------------
 *   INVALID  -->  LOADING  -->  VALID  -->  (eviction) --> INVALID
 *      |                           ^
 *      +---------------------------+  (hit)
 *
 * THREAD SAFETY:
 * --------------
 * Uses two-lock protocol (hand-over-hand locking):
 *   - Global lock: short-duration lock for cache search and slot selection
 *   - Per-entry lock: held during I/O operations and data access
 *   - LOADING state: reserves slot without blocking other cache operations
 *
 * Multiple threads can read/write different cache entries concurrently.
 * Threads waiting for a LOADING entry block on a condition variable.
 *
 * BACKGROUND OPERATIONS:
 * ----------------------
 *   - Periodic flusher: writes dirty entries every 30 seconds
 *   - Prefetch thread: speculatively loads next sequential sector
 */

/* Maximum number of cache entries (32 KB total cache size). */
#define CACHE_SIZE 64

/* Cache entry states. */
enum cache_state {
  CACHE_INVALID, /* Slot is empty, available for use. */
  CACHE_LOADING, /* Slot is reserved, loading in progress. */
  CACHE_VALID    /* Slot contains valid data, ready to use. */
};

/* A single cache entry. */
struct cache_entry {
  block_sector_t sector;           /* Which disk sector this caches. */
  enum cache_state state;          /* INVALID, LOADING, or VALID. */
  bool dirty;                      /* Has this been modified? */
  bool accessed;                   /* Recently accessed? (for clock algorithm) */
  uint8_t data[BLOCK_SECTOR_SIZE]; /* The actual 512 bytes of data. */

  struct lock entry_lock;        /* Per-entry lock for data access. */
  struct condition loading_done; /* For threads waiting on LOADING state. */
};

/* Initialize the buffer cache. Call once at filesystem init. */
void cache_init(void);

/* Read a full sector from cache (or disk if not cached). */
void cache_read(block_sector_t sector, void* buffer);

/* Read partial sector data from cache into buffer.
   Reads chunk_size bytes starting at sector_ofs within the sector. */
void cache_read_at(block_sector_t sector, void* buffer, int sector_ofs, int chunk_size);

/* Write data to cache. Handles partial writes correctly. */
void cache_write(block_sector_t sector, const void* buffer, int sector_ofs, int chunk_size);

/* Flush all dirty entries to disk. */
void cache_flush(void);

/* Load a sector into cache for prefetching (called by prefetch subsystem). */
void cache_do_prefetch(block_sector_t sector);

/* Stop flusher thread and flush remaining entries. Call on shutdown. */
void cache_shutdown(void);

/* Print cache statistics (hits, misses, evictions, etc.). */
void cache_print_stats(void);

/* Reset cache statistics counters. */
void cache_reset_stats(void);

#endif /* filesys/cache.h */
