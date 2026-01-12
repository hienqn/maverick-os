#ifndef DEVICES_RAID_H
#define DEVICES_RAID_H

/**
 * @file devices/raid.h
 * @brief Software RAID (Redundant Array of Independent Disks) layer.
 *
 * This module provides a software RAID implementation that sits between
 * the buffer cache and physical block devices, combining multiple disks
 * into a single logical volume with optional redundancy.
 *
 * SUPPORTED RAID LEVELS:
 *
 *   RAID 0 (Striping):
 *     - Data is split across all disks
 *     - Capacity: 100% (all disks combined)
 *     - Performance: Best (parallel I/O)
 *     - Fault tolerance: None (any disk failure = total data loss)
 *
 *   RAID 1 (Mirroring):
 *     - Identical data on all disks
 *     - Capacity: 1 disk (others are mirrors)
 *     - Performance: Good reads, slower writes
 *     - Fault tolerance: Survives N-1 disk failures
 *
 *   RAID 5 (Striping with Distributed Parity):
 *     - Data striped with rotating parity blocks
 *     - Capacity: (N-1) disks
 *     - Performance: Good reads, slower writes (read-modify-write)
 *     - Fault tolerance: Survives 1 disk failure
 *
 * ARCHITECTURE:
 *
 *   +-----------------+
 *   |  Buffer Cache   |
 *   +--------+--------+
 *            |
 *            v
 *   +--------+--------+
 *   |   RAID Layer    |  <-- This module
 *   +--------+--------+
 *            |
 *      +-----+-----+
 *      v     v     v
 *    [hdb] [hdc] [hdd]   (physical disks)
 *
 * USAGE:
 *   1. In init.c, uncomment one raid_init() call before locate_block_devices()
 *   2. Provide additional disk images when running Pintos:
 *      pintos --disk=disk2.dsk --disk=disk3.dsk -- ...
 *   3. RAID will automatically discover BLOCK_RAW disks
 *
 * NOTE: RAID is disabled by default. Enable by uncommenting raid_init() in init.c.
 */

#include "devices/block.h"
#include <stdbool.h>

/** Maximum number of disks in a RAID array. */
#define RAID_MAX_DISKS 4

/** RAID levels supported by this implementation. */
enum raid_level {
  RAID_LEVEL_0, /**< Striping - max performance, no redundancy */
  RAID_LEVEL_1, /**< Mirroring - full redundancy, 1/N capacity */
  RAID_LEVEL_5, /**< Distributed parity - 1 disk fault tolerance */
};

/** RAID array configuration and state. */
struct raid_config {
  enum raid_level level;               /**< RAID level (0, 1, or 5) */
  size_t num_disks;                    /**< Number of disks in array */
  struct block* disks[RAID_MAX_DISKS]; /**< Physical disk devices */
  size_t stripe_size;                  /**< Sectors per stripe unit (RAID 0/5) */
  block_sector_t total_sectors;        /**< Total usable sectors */
};

/** Global RAID device (NULL if not initialized). */
extern struct raid_config* raid_device;

/*
 * Core RAID Functions
 */

/** Initialize RAID with given level and stripe size. Discovers BLOCK_RAW disks. */
void raid_init(enum raid_level level, size_t stripe_size);

/** Read a sector from the RAID array. */
void raid_read(block_sector_t sector, void* buffer);

/** Write a sector to the RAID array. */
void raid_write(block_sector_t sector, const void* buffer);

/** Return total usable sectors in RAID array. */
block_sector_t raid_size(void);

/** Check if RAID is initialized. */
bool raid_is_initialized(void);

/*
 * RAID 0 Helper Functions
 */

/** Calculate which disk holds a given logical sector. */
size_t raid0_disk_for_sector(block_sector_t sector);

/** Calculate the physical sector number on the target disk. */
block_sector_t raid0_physical_sector(block_sector_t sector);

/*
 * RAID 5 Helper Functions
 */

/** Calculate which disk holds parity for a given stripe. */
size_t raid5_parity_disk(block_sector_t stripe);

/** Calculate which disk holds data for a given logical sector. */
size_t raid5_data_disk(block_sector_t sector);

/** Calculate the physical sector number on the target disk. */
block_sector_t raid5_physical_sector(block_sector_t sector);

/** Compute parity by XORing data buffers together. */
void raid5_compute_parity(void* parity, void* data[], size_t count);

#endif /* devices/raid.h */
