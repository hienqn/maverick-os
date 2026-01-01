/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                           FREE MAP MODULE                                 ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  The free map tracks which disk sectors are in use vs. available.        ║
 * ║  It uses a bitmap where bit N=0 means sector N is free, bit N=1 means    ║
 * ║  sector N is allocated.                                                  ║
 * ║                                                                          ║
 * ║  STRUCTURE:                                                              ║
 * ║  ──────────                                                              ║
 * ║                                                                          ║
 * ║    Sector 0: Free map inode (always allocated)                           ║
 * ║    Sector 1: Root directory inode (always allocated)                     ║
 * ║    Sector 2+: Free map data, then user data                              ║
 * ║                                                                          ║
 * ║    ┌───────────────────────────────────────────────────────┐             ║
 * ║    │ Bit:    0   1   2   3   4   5   6   7   8   9  ...    │             ║
 * ║    │ Value:  1   1   1   1   0   0   1   0   1   0  ...    │             ║
 * ║    │         │   │   └───┴───┴───────────────────────────  │             ║
 * ║    │         │   │         Data sectors                    │             ║
 * ║    │         │   └── Root dir (always 1)                   │             ║
 * ║    │         └────── Free map (always 1)                   │             ║
 * ║    └───────────────────────────────────────────────────────┘             ║
 * ║                                                                          ║
 * ║  PERSISTENCE:                                                            ║
 * ║  ────────────                                                            ║
 * ║  The free map is stored as a file at sector 0. Changes are written      ║
 * ║  back to disk immediately during allocate/release to ensure crash       ║
 * ║  consistency (at the cost of performance).                              ║
 * ║                                                                          ║
 * ║  SYNCHRONIZATION:                                                        ║
 * ║  ────────────────                                                        ║
 * ║  A single lock (free_map_lock) protects all free map operations.        ║
 * ║  This ensures atomic allocate/release even with concurrent file         ║
 * ║  creation and deletion.                                                 ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/wal.h"
#include "threads/synch.h"

static struct file* free_map_file; /* Free map file (stored at sector 0). */
static struct bitmap* free_map;    /* In-memory free map, one bit per sector. */
static struct lock free_map_lock;  /* Lock for thread-safe free map access. */

/* Initializes the free map. */
void free_map_init(void) {
  lock_init(&free_map_lock);
  free_map = bitmap_create(block_size(fs_device));
  if (free_map == NULL)
    PANIC("bitmap creation failed--file system device is too large");
  bitmap_mark(free_map, FREE_MAP_SECTOR);
  bitmap_mark(free_map, ROOT_DIR_SECTOR);

  /* Reserve WAL sectors so filesystem doesn't allocate them for file data.
     WAL uses sectors 2 through WAL_METADATA_SECTOR (inclusive). */
  for (block_sector_t s = WAL_LOG_START_SECTOR; s <= WAL_METADATA_SECTOR; s++) {
    bitmap_mark(free_map, s);
  }
}

/* Allocates CNT consecutive sectors from the free map and stores
   the first into *SECTORP.
   Returns true if successful, false if not enough consecutive
   sectors were available or if the free_map file could not be
   written. */
bool free_map_allocate(size_t cnt, block_sector_t* sectorp) {
  lock_acquire(&free_map_lock);
  block_sector_t sector = bitmap_scan_and_flip(free_map, 0, cnt, false);
  if (sector != BITMAP_ERROR && free_map_file != NULL && !bitmap_write(free_map, free_map_file)) {
    bitmap_set_multiple(free_map, sector, cnt, false);
    sector = BITMAP_ERROR;
  }
  if (sector != BITMAP_ERROR)
    *sectorp = sector;
  lock_release(&free_map_lock);
  return sector != BITMAP_ERROR;
}

/* Allocates a single sector from the free map and stores it in *SECTORP.
   Returns true if successful, false if no sector was available. */
bool free_map_allocate_one(block_sector_t* sectorp) { return free_map_allocate(1, sectorp); }

/* Makes CNT sectors starting at SECTOR available for use. */
void free_map_release(block_sector_t sector, size_t cnt) {
  lock_acquire(&free_map_lock);
  ASSERT(bitmap_all(free_map, sector, cnt));
  bitmap_set_multiple(free_map, sector, cnt, false);
  bitmap_write(free_map, free_map_file);
  lock_release(&free_map_lock);
}

/* Opens the free map file and reads it from disk. */
void free_map_open(void) {
  free_map_file = file_open(inode_open(FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC("can't open free map");
  if (!bitmap_read(free_map, free_map_file))
    PANIC("can't read free map");
}

/* Writes the free map to disk and closes the free map file. */
void free_map_close(void) { file_close(free_map_file); }

/* Creates a new free map file on disk and writes the free map to
   it. */
void free_map_create(void) {
  /* Create inode. */
  if (!inode_create(FREE_MAP_SECTOR, bitmap_file_size(free_map)))
    PANIC("free map creation failed");

  /* Write bitmap to file. */
  free_map_file = file_open(inode_open(FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC("can't open free map");
  if (!bitmap_write(free_map, free_map_file))
    PANIC("can't write free map");
}
