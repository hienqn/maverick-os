#ifndef FILESYS_CACHE_PREFETCH_H
#define FILESYS_CACHE_PREFETCH_H

#include "devices/block.h"

/* Initialize the prefetch subsystem. Call from cache_init(). */
void cache_prefetch_init(void);

/* Shutdown the prefetch subsystem. Call from cache_shutdown(). */
void cache_prefetch_shutdown(void);

/* Queue a sector for asynchronous prefetching.
   Best-effort: silently drops request if queue is full. */
void cache_request_prefetch(block_sector_t sector);

#endif /* filesys/cache_prefetch.h */

