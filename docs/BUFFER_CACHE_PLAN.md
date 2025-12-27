# Buffer Cache Implementation Plan

## My Understanding of the Problem

### Why do we need a buffer cache?

Every time we call `block_read()`, we talk to disk, and disk is *slow*. It takes roughly 10ms for an operation on average while accessing memory is much less, about 10 microseconds.

It's expensive mainly because of the 3 main latencies for a typical hard disk drive: seek time, rotation time, and transfer time. For a solid-state drive, it's a lot better but it's still in the order of milliseconds.

The buffer cache acts as a "middleman" between the inode code and the block device. It stores recently-accessed disk sectors in memory so we don't have to hit the disk every time.

### What the cache stores

- The block sector data (512 bytes per sector)
- Metadata: sector number (so we know which disk sector this entry corresponds to)

## Key Questions to Answer

1. **How to find a sector in the cache?**
   - Could use a hash table with sector number as key for O(1) lookup
   - Or simple linear scan over 64 entries (acceptable for small cache)

2. **When to write dirty data back to disk?**
   - TODO: Define write-back policy

3. **What synchronization primitive for "wait for loading"?**
   - Condition variables (from `threads/synch.h`)

## Design Decisions

### Cache Entry States

| State | Meaning |
|-------|---------|
| `INVALID` | Slot is empty, available for use |
| `LOADING` | Slot is reserved, someone is actively loading data from disk |
| `VALID` | Slot contains valid data, ready to use |

### Lookup Decision Matrix

| Entry State | Sector Match? | Action |
|-------------|---------------|--------|
| VALID | Yes | **Use it!** (cache hit) |
| VALID | No | Skip, keep searching |
| LOADING | Yes | **Wait** for it to finish, then use |
| LOADING | No | Skip, keep searching (slot is reserved) |
| INVALID | n/a | Remember this slot in case we need to load |

### After Scanning All 64 Entries

| Result | Action |
|--------|--------|
| Found VALID entry with my sector | Return it (cache hit!) |
| Found LOADING entry with my sector | Wait for it, then return it |
| Didn't find my sector, found INVALID slot(s) | Pick one, set to LOADING, load from disk |
| Didn't find my sector, all slots VALID or LOADING | **Evict** a VALID entry (write back if dirty), then load |

---

## Data Structures I'll Need

```c
enum cache_state {
  CACHE_INVALID,   // Slot is empty
  CACHE_LOADING,   // Slot is reserved, loading in progress
  CACHE_VALID      // Slot has valid data
};

struct cache_entry {
  block_sector_t sector;           // Which disk sector this caches
  enum cache_state state;          // INVALID, LOADING, or VALID
  bool dirty;                      // Has this been modified? Needs write-back?
  uint8_t data[BLOCK_SECTOR_SIZE]; // The actual 512 bytes of data
  
  struct lock entry_lock;          // Per-entry lock for data access
  struct condition loading_done;   // For threads waiting on LOADING state
  
  // For eviction algorithm (Clock or LRU):
  bool accessed;                   // Clock algorithm: recently accessed?
  // OR: uint64_t last_access_time; // LRU: timestamp of last access
};

// Global cache structure
struct cache_entry cache[64];      // Array of 64 cache entries
struct lock cache_global_lock;     // Protects search/slot-selection phase
int clock_hand;                    // For clock eviction algorithm
```

---

## Algorithm for Cache Read

```
cache_read(sector, buffer):
    acquire(cache_global_lock)
    
    entry = NULL
    empty_slot = NULL
    
    // Phase 1: Search the cache
    for each slot in cache:
        if slot.sector == sector:
            if slot.state == VALID:
                entry = slot
                break
            else if slot.state == LOADING:
                // Someone is loading what we need - wait for it
                release(cache_global_lock)
                acquire(slot.entry_lock)
                while slot.state == LOADING:
                    cond_wait(slot.loading_done, slot.entry_lock)
                // Now it's VALID, copy data
                memcpy(buffer, slot.data, 512)
                slot.accessed = true
                release(slot.entry_lock)
                return
        else if slot.state == INVALID and empty_slot == NULL:
            empty_slot = slot
    
    if entry != NULL:
        // Cache hit - copy data
        acquire(entry.entry_lock)
        release(cache_global_lock)
        memcpy(buffer, entry.data, 512)
        entry.accessed = true
        release(entry.entry_lock)
        return
    
    // Cache miss - need to load from disk
    if empty_slot == NULL:
        empty_slot = evict_one()  // Find victim, write back if dirty
    
    empty_slot.state = LOADING
    empty_slot.sector = sector
    release(cache_global_lock)   // Release before slow I/O!
    
    // Do the slow disk read
    block_read(fs_device, sector, empty_slot.data)
    
    // Mark as valid and wake up waiters
    acquire(empty_slot.entry_lock)
    empty_slot.state = VALID
    empty_slot.dirty = false
    empty_slot.accessed = true
    cond_broadcast(empty_slot.loading_done, empty_slot.entry_lock)
    release(empty_slot.entry_lock)
    
    memcpy(buffer, empty_slot.data, 512)
```

---

## Algorithm for Eviction

**Choice: Clock Algorithm** (simpler than true LRU)

```
evict_one():
    // Must be called while holding cache_global_lock
    // Returns a slot that has been cleared (and written back if dirty)
    
    while true:
        entry = cache[clock_hand]
        clock_hand = (clock_hand + 1) % 64
        
        if entry.state != VALID:
            continue   // Can only evict VALID entries, not LOADING!
        
        if entry.accessed:
            entry.accessed = false  // Give it a second chance
            continue
        
        // Found our victim
        if entry.dirty:
            block_write(fs_device, entry.sector, entry.data)
        
        entry.state = INVALID
        return entry
```

**Why can't we evict LOADING entries?**
1. Threads waiting for that entry would be stuck forever (no one signals them)
2. The thread doing the load would corrupt data (two block_reads into same buffer)

---

## Write-Back Policy

### When Does Data Get Written to Disk?

| Trigger | What Happens |
|---------|--------------|
| **Eviction** | If evicting a dirty entry, `block_write()` before reusing the slot |
| **Shutdown** | Flush ALL dirty entries to disk via `cache_flush()` |
| **Optional: Periodic flush** | Background thread calls `cache_flush()` every N seconds |

### Algorithm: cache_flush()

Called on filesystem shutdown (`filesys_done()`) to ensure no data is lost.

```
cache_flush():
    acquire(cache_global_lock)
    
    for i = 0 to 63:
        entry = cache[i]
        
        if entry.state == VALID and entry.dirty:
            // Write dirty entry back to disk
            block_write(fs_device, entry.sector, entry.data)
            entry.dirty = false
        
        // Note: Skip LOADING entries - they're still being set up
        // Note: Skip INVALID entries - nothing to flush
    
    release(cache_global_lock)
```

### Important: Crash Safety

If the system crashes before `cache_flush()`:
- All dirty (unwritten) data is **lost**
- This is the tradeoff of write-back: performance vs durability

For better crash safety (optional):
- Flush dirty entries periodically (every few seconds)
- Use a write-behind thread with `timer_sleep()`

---

## Concurrency Considerations

### The Race Condition Problem

Without synchronization, two threads could:
1. Both search the cache and not find their sector
2. Both pick the same empty slot
3. Both call `block_read()` into the same buffer → **data corruption**

The final buffer contents could be a random interleaving of bytes from both sectors!

### Solution: Two-Level Locking

| Lock | Protects | When Held |
|------|----------|-----------|
| `cache_global_lock` | Search phase, slot selection | Short time only! Released before disk I/O |
| `entry.entry_lock` | Individual entry's data and state | During data access/modification |

**Critical Rule:** Never hold `cache_global_lock` during `block_read()` or `block_write()` because I/O takes ~10ms and would block all other cache operations!

### The LOADING State

The `LOADING` state reserves a slot so that:
- Other threads looking for the same sector **wait** instead of duplicating the load
- Other threads looking for different sectors **skip** this slot (it's taken)
- The eviction algorithm **skips** this slot (can't evict something being loaded)

### Design Decision: Uniform LOADING State for All Operations

**Decision:** Use `LOADING` state for both reads AND full writes (even though writes don't do disk I/O).

**Rationale:** The speed difference is negligible (memcpy takes nanoseconds), and having one uniform code path means fewer bugs and easier debugging. Code simplicity > micro-optimization.

---

## Functions to Modify

| File | Function | Change |
|------|----------|--------|
| `inode.c` | `inode_read_at()` | Replace `block_read()` with `cache_read()` |
| `inode.c` | `inode_write_at()` | Replace `block_write()` with `cache_write()` |
| `filesys.c` | `filesys_done()` | Flush all dirty cache entries to disk |

### New Files to Create

- `filesys/cache.c` - Buffer cache implementation
- `filesys/cache.h` - Buffer cache interface

---

## Testing Strategy

TODO: Define test cases
- Basic read/write through cache
- Cache hit vs cache miss performance
- Eviction behavior when cache is full
- Concurrent access from multiple threads
- Dirty write-back on eviction and shutdown

---

## Notes & Open Questions

1. **Write policy:** When `inode_write_at()` writes data:
   - Write to cache entry and mark dirty (NOT directly to disk)
   - If writing a partial sector, must load the sector first to preserve other bytes
   - If writing a full sector, can overwrite without loading first

2. **When do dirty entries get written back?** (See "Write-Back Policy" section above)
   - On eviction (must write before overwriting with new data)
   - On filesystem shutdown (`filesys_done()` calls `cache_flush()`)
   - Optional: periodic write-behind (background flushing)

3. **Lookup approach:** Linear scan over 64 entries is acceptable. With only 64 entries, a hash table adds complexity without significant benefit. Linear scan is O(64) = O(1) in practice.

4. **Cache Write Algorithm:**

```
cache_write(sector, buffer, sector_ofs, chunk_size):
    acquire(cache_global_lock)
    
    entry = search_for_sector(sector)
    
    if entry != NULL and entry.state == VALID:
        // Cache hit
        acquire(entry.entry_lock)
        release(cache_global_lock)
        memcpy(entry.data + sector_ofs, buffer, chunk_size)
        entry.dirty = true
        entry.accessed = true
        release(entry.entry_lock)
        return
    
    if entry != NULL and entry.state == LOADING:
        // Wait for it to finish loading
        release(cache_global_lock)
        acquire(entry.entry_lock)
        while entry.state == LOADING:
            cond_wait(entry.loading_done, entry.entry_lock)
        memcpy(entry.data + sector_ofs, buffer, chunk_size)
        entry.dirty = true
        entry.accessed = true
        release(entry.entry_lock)
        return
    
    // Cache miss - need to allocate slot
    slot = find_empty_or_evict()
    slot.state = LOADING
    slot.sector = sector
    release(cache_global_lock)
    
    // Partial write: must load existing data first
    // Full write: can skip the load
    if sector_ofs != 0 or chunk_size != 512:
        block_read(fs_device, sector, slot.data)
    
    // Now write our data
    acquire(slot.entry_lock)
    memcpy(slot.data + sector_ofs, buffer, chunk_size)
    slot.state = VALID
    slot.dirty = true
    slot.accessed = true
    cond_broadcast(slot.loading_done, slot.entry_lock)
    release(slot.entry_lock)
```
