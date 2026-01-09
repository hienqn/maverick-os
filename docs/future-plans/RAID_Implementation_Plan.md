# Software RAID Layer Implementation Plan

## Overview

Add a software RAID layer to Pintos that sits between the buffer cache and block devices. The infrastructure will be set up for you; you implement the core RAID read/write logic.

```
Buffer Cache (cache.c)
        |
        v
+------------------+
|   RAID Layer     |  <-- YOU IMPLEMENT raid_read/raid_write
|    (raid.c)      |
+------------------+
        |
   +----+----+
   v    v    v
 hdb  hdc  hdd    (physical disks)
```

## Files to Create

### 1. `src/devices/raid.h`

```c
#ifndef DEVICES_RAID_H
#define DEVICES_RAID_H

#include "devices/block.h"
#include <stdbool.h>

#define RAID_MAX_DISKS 4

enum raid_level {
  RAID_LEVEL_0,  /* Striping - no redundancy */
  RAID_LEVEL_1,  /* Mirroring - full redundancy */
};

struct raid_config {
  enum raid_level level;
  size_t num_disks;
  struct block* disks[RAID_MAX_DISKS];
  size_t stripe_size;              /* Sectors per stripe (RAID 0) */
  block_sector_t total_sectors;

  /* Statistics */
  unsigned long long reads;
  unsigned long long writes;
  unsigned long long disk_reads[RAID_MAX_DISKS];
  unsigned long long disk_writes[RAID_MAX_DISKS];
};

extern struct raid_config* raid_device;

/* Initialization */
void raid_init(enum raid_level level, size_t stripe_size);
void raid_shutdown(void);

/* Core I/O - YOU IMPLEMENT THESE */
void raid_read(block_sector_t sector, void* buffer);
void raid_write(block_sector_t sector, const void* buffer);

/* Utilities */
block_sector_t raid_size(void);
void raid_print_stats(void);
bool raid_is_initialized(void);

/* Helper functions for RAID 0 calculations */
size_t raid0_disk_for_sector(block_sector_t sector);
block_sector_t raid0_physical_sector(block_sector_t sector);

#endif
```

### 2. `src/devices/raid.c`

Infrastructure provided, with stubs for you to implement:

```c
#include "devices/raid.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

struct raid_config* raid_device = NULL;
static struct lock raid_lock;

/* ========== INITIALIZATION (PROVIDED) ========== */

void raid_init(enum raid_level level, size_t stripe_size) {
  /* Allocates raid_device, discovers disks, calculates capacity */
  /* ... infrastructure code ... */
}

/* ========== HELPER FUNCTIONS (PROVIDED) ========== */

size_t raid0_disk_for_sector(block_sector_t sector) {
  /* Returns: (sector / stripe_size) % num_disks */
}

block_sector_t raid0_physical_sector(block_sector_t sector) {
  /* Returns physical sector number on the target disk */
}

/* ========== YOUR IMPLEMENTATION ========== */

void raid_read(block_sector_t sector, void* buffer) {
  ASSERT(raid_device != NULL);
  ASSERT(sector < raid_device->total_sectors);

  switch (raid_device->level) {
    case RAID_LEVEL_0:
      /* TODO: Implement RAID 0 read
       * 1. Use raid0_disk_for_sector() to find target disk
       * 2. Use raid0_physical_sector() to get physical sector
       * 3. Call block_read(disks[disk_index], physical_sector, buffer)
       */
      PANIC("RAID 0 read not implemented!");
      break;

    case RAID_LEVEL_1:
      /* TODO: Implement RAID 1 read
       * Read from any disk (they all have same data)
       * Simple: always disk 0
       * Better: round-robin for load balancing
       */
      PANIC("RAID 1 read not implemented!");
      break;
  }
}

void raid_write(block_sector_t sector, const void* buffer) {
  ASSERT(raid_device != NULL);
  ASSERT(sector < raid_device->total_sectors);

  switch (raid_device->level) {
    case RAID_LEVEL_0:
      /* TODO: Implement RAID 0 write
       * Same calculation as read, write to ONE disk
       */
      PANIC("RAID 0 write not implemented!");
      break;

    case RAID_LEVEL_1:
      /* TODO: Implement RAID 1 write
       * Write to ALL disks (mirroring)
       * Loop through all disks and call block_write on each
       */
      PANIC("RAID 1 write not implemented!");
      break;
  }
}
```

## Files to Modify

### 1. `src/filesys/cache.c`

Add wrapper functions to route I/O through RAID:

```c
#include "devices/raid.h"

/* Add these helper functions */
static void cache_block_read(block_sector_t sector, void* buffer) {
  if (raid_is_initialized())
    raid_read(sector, buffer);
  else
    block_read(fs_device, sector, buffer);
}

static void cache_block_write(block_sector_t sector, const void* buffer) {
  if (raid_is_initialized())
    raid_write(sector, buffer);
  else
    block_write(fs_device, sector, buffer);
}
```

Then replace all `block_read(fs_device, ...)` calls with `cache_block_read(...)` and `block_write(fs_device, ...)` with `cache_block_write(...)`.

### 2. `src/threads/init.c`

Add RAID initialization after `ide_init()`:

```c
#include "devices/raid.h"

/* In main(), after ide_init() and before filesys_init(): */
#ifdef FILESYS
  ide_init();

  /* Uncomment ONE to enable RAID: */
  // raid_init(RAID_LEVEL_0, 1);  /* RAID 0 striping */
  // raid_init(RAID_LEVEL_1, 0);  /* RAID 1 mirroring */

  locate_block_devices();
  filesys_init(format_filesys);
#endif
```

### 3. `src/devices/Make.vars`

Add raid.c to the build:

```makefile
devices_SRC += devices/raid.c
```

## RAID Concepts

### RAID 0 (Striping)

Data is split across disks. With `stripe_size=2` and 3 disks:

```
Logical:   0  1 | 2  3 | 4  5 | 6  7 | 8  9 | ...
           -----+------+------+------+------+
Disk 0:    0  1                 6  7
Disk 1:          2  3                 8  9
Disk 2:                 4  5
```

**Formulas:**
- `disk_index = (sector / stripe_size) % num_disks`
- `physical_sector = (sector / stripe_size / num_disks) * stripe_size + (sector % stripe_size)`

### RAID 1 (Mirroring)

All disks have identical data:

```
Logical:   0  1  2  3  4  5 ...
Disk 0:    0  1  2  3  4  5 ...  (copy)
Disk 1:    0  1  2  3  4  5 ...  (copy)
Disk 2:    0  1  2  3  4  5 ...  (copy)
```

**Read:** From any disk (same data)
**Write:** To ALL disks

## Running with Multiple Disks

### Create disk images:
```bash
cd src/filesys/build
pintos-mkdisk disk2.dsk 2
pintos-mkdisk disk3.dsk 2
```

### Run with RAID:
```bash
# RAID 0
pintos --qemu --filesys-size=2 --disk=disk2.dsk --disk=disk3.dsk \
  -- -q -f run your-test

# RAID 1
pintos --qemu --filesys-size=2 --disk=disk2.dsk --disk=disk3.dsk \
  -- -q -f run your-test
```

## Testing Your Implementation

### Test 1: Basic read/write
```c
void test_raid_basic(void) {
  char write_buf[512], read_buf[512];
  memset(write_buf, 'A', 512);

  raid_write(0, write_buf);
  raid_read(0, read_buf);

  ASSERT(memcmp(write_buf, read_buf, 512) == 0);
}
```

### Test 2: Verify striping (RAID 0)
Write to multiple sectors, check `raid_print_stats()` shows balanced disk I/O.

### Test 3: Verify mirroring (RAID 1)
Write once, check all `disk_writes[]` incremented.

## Implementation Checklist

- [ ] Create `src/devices/raid.h`
- [ ] Create `src/devices/raid.c` with infrastructure
- [ ] Implement `raid_read()` for RAID 0
- [ ] Implement `raid_write()` for RAID 0
- [ ] Implement `raid_read()` for RAID 1
- [ ] Implement `raid_write()` for RAID 1
- [ ] Modify `cache.c` to use RAID layer
- [ ] Modify `init.c` to call `raid_init()`
- [ ] Add to build system
- [ ] Test with multiple disk images

## What I'll Set Up For You

1. **raid.h** - Complete header with all declarations
2. **raid.c** - Infrastructure: init, shutdown, helpers, stats, single-disk fallback
3. **cache.c modifications** - Wrapper functions to route through RAID
4. **init.c modifications** - RAID initialization call
5. **Build system** - Add raid.c to compilation

## What You Implement

1. **`raid_read()` body** - The switch cases for RAID 0 and RAID 1
2. **`raid_write()` body** - The switch cases for RAID 0 and RAID 1

The helper functions `raid0_disk_for_sector()` and `raid0_physical_sector()` are provided to make RAID 0 calculations easier.
