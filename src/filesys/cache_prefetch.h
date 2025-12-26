#ifndef FILESYS_CACHE_PREFETCH_H
#define FILESYS_CACHE_PREFETCH_H

#include "devices/block.h"

/*
 * Prefetch Subsystem
 * ==================
 * 
 * Provides asynchronous read-ahead for the buffer cache. After reading
 * sector N, the cache automatically requests prefetch of sector N+1,
 * anticipating sequential access patterns. A background thread processes
 * the prefetch queue, loading sectors before they're explicitly needed.
 *
 * Key properties:
 *   - Non-blocking: requests return immediately
 *   - Best-effort: queue overflow silently drops requests
 *   - Low priority: prefetched data is evicted before accessed data
 *
 * See cache_prefetch.c for detailed documentation.
 */

/* Initialize the prefetch subsystem. Call from cache_init(). */
void cache_prefetch_init(void);

/* Shutdown the prefetch subsystem. Call from cache_shutdown(). */
void cache_prefetch_shutdown(void);

/* Queue a sector for asynchronous prefetching.
   Best-effort: silently drops request if queue is full. */
void cache_request_prefetch(block_sector_t sector);

#endif /* filesys/cache_prefetch.h */

