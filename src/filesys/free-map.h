#ifndef FILESYS_FREE_MAP_H
#define FILESYS_FREE_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * FREE MAP (DISK SECTOR ALLOCATION)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * The free map tracks which disk sectors are allocated vs. available.
 * Implemented as a bitmap: bit N = 1 means sector N is in use.
 *
 * Disk Layout:
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │ Sector 0: Free map inode (always allocated)                            │
 * │ Sector 1: Root directory inode (always allocated)                      │
 * │ Sector 2-N: WAL (Write-Ahead Log) reserved sectors                     │
 * │ Sector N+: Free map data, then user data sectors                       │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * Persistence: The bitmap is stored in a file at sector 0. Changes are
 * written to disk immediately during allocate/release for crash safety.
 *
 * Thread Safety: A single lock protects all free map operations.
 *
 * Lifecycle:
 *   1. free_map_init()   - Create in-memory bitmap, mark reserved sectors
 *   2. free_map_create() - First boot: create free map file on disk
 *      OR free_map_open() - Subsequent boots: load from disk
 *   3. free_map_allocate/release() - Normal operation
 *   4. free_map_close()  - Shutdown: flush and close file
 * ═══════════════════════════════════════════════════════════════════════════
 */

/* ─────────────────────────────────────────────────────────────────────────
 * Initialization and Lifecycle
 * ───────────────────────────────────────────────────────────────────────── */

/* Initializes the in-memory free map bitmap.
   Marks sector 0 (free map), sector 1 (root dir), and WAL sectors as used.
   Must be called before any other free_map functions. */
void free_map_init(void);

/* Creates a new free map file on disk (for filesystem formatting).
   Writes the current in-memory bitmap to disk.
   PANICs on failure. */
void free_map_create(void);

/* Opens the existing free map file and loads the bitmap from disk.
   Used when mounting an existing filesystem.
   PANICs if the file cannot be opened or read. */
void free_map_open(void);

/* Flushes any pending changes and closes the free map file.
   Should be called during filesystem shutdown. */
void free_map_close(void);

/* ─────────────────────────────────────────────────────────────────────────
 * Sector Allocation
 * ───────────────────────────────────────────────────────────────────────── */

/* Allocates CNT consecutive free sectors.
   Searches for a contiguous run of CNT free sectors and marks them as used.
   @param cnt     Number of consecutive sectors needed
   @param sectorp Output: first sector of the allocated run
   @return true on success, false if not enough consecutive sectors available.
   Note: Writes bitmap to disk immediately for crash consistency. */
bool free_map_allocate(size_t cnt, block_sector_t* sectorp);

/* Allocates a single sector. Convenience wrapper for free_map_allocate(1, ...).
   @param sectorp  Output: the allocated sector number
   @return true on success, false if disk is full. */
bool free_map_allocate_one(block_sector_t* sectorp);

/* Releases CNT sectors starting at SECTOR back to the free pool.
   The sectors must currently be marked as allocated (asserted).
   @param sector  First sector to release
   @param cnt     Number of consecutive sectors to release.
   Note: Writes bitmap to disk immediately. */
void free_map_release(block_sector_t sector, size_t cnt);

#endif /* filesys/free-map.h */
