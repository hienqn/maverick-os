/**
 * @file devices/raid.c
 * @brief Software RAID implementation for Pintos.
 *
 * Implements RAID 0 (striping), RAID 1 (mirroring), and RAID 5 (parity).
 * See raid.h for detailed documentation and usage instructions.
 */

#include "devices/raid.h"
#include "threads/malloc.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/** Global RAID configuration (NULL when RAID is not initialized). */
struct raid_config* raid_device = NULL;

/*
 * ============================================================================
 * INITIALIZATION
 * ============================================================================
 */

/**
 * Initialize the RAID subsystem.
 *
 * Discovers available BLOCK_RAW disks (hdb, hdc, hdd, hde) and configures
 * the RAID array. Panics if insufficient disks are available.
 *
 * @param level RAID level (RAID_LEVEL_0, RAID_LEVEL_1, or RAID_LEVEL_5)
 * @param stripe_size Sectors per stripe unit (used by RAID 0 and 5)
 */
void raid_init(enum raid_level level, size_t stripe_size) {
  /* Allocate RAID configuration */
  raid_device = malloc(sizeof(struct raid_config));
  if (raid_device == NULL)
    PANIC("Failed to allocate RAID config");

  raid_device->level = level;
  raid_device->stripe_size = (stripe_size > 0) ? stripe_size : 1;
  raid_device->num_disks = 0;

  /* Discover available BLOCK_RAW disks.
   * Only use disks that have no assigned role (FILESYS, SWAP, etc.) */
  const char* disk_names[] = {"hdb", "hdc", "hdd", "hde"};
  for (size_t i = 0; i < 4 && raid_device->num_disks < RAID_MAX_DISKS; i++) {
    struct block* disk = block_get_by_name(disk_names[i]);
    if (disk != NULL && block_type(disk) == BLOCK_RAW) {
      raid_device->disks[raid_device->num_disks] = disk;
      raid_device->num_disks++;
    }
  }

  /* Validate disk count */
  if (raid_device->num_disks < 2)
    PANIC("RAID requires at least 2 disks, found %zu", raid_device->num_disks);
  if (level == RAID_LEVEL_5 && raid_device->num_disks < 3)
    PANIC("RAID 5 requires at least 3 disks, found %zu", raid_device->num_disks);

  /* Use smallest disk capacity (RAID arrays use smallest common size) */
  block_sector_t min_disk_sectors = block_size(raid_device->disks[0]);
  for (size_t i = 1; i < raid_device->num_disks; i++) {
    block_sector_t sectors = block_size(raid_device->disks[i]);
    if (sectors < min_disk_sectors)
      min_disk_sectors = sectors;
  }

  /* Calculate total usable capacity based on RAID level */
  switch (level) {
    case RAID_LEVEL_0:
      /* All capacity used for data */
      raid_device->total_sectors = raid_device->num_disks * min_disk_sectors;
      break;
    case RAID_LEVEL_1:
      /* All disks are mirrors */
      raid_device->total_sectors = min_disk_sectors;
      break;
    case RAID_LEVEL_5:
      /* One disk worth of capacity used for parity */
      raid_device->total_sectors = (raid_device->num_disks - 1) * min_disk_sectors;
      break;
  }

  const char* level_names[] = {"0", "1", "5"};
  printf("RAID %s initialized: %zu disks, %u total sectors\n", level_names[level],
         raid_device->num_disks, raid_device->total_sectors);
}

/*
 * ============================================================================
 * CORE I/O OPERATIONS
 * ============================================================================
 */

/**
 * Read a sector from the RAID array.
 *
 * Routes the read to the appropriate disk(s) based on RAID level:
 * - RAID 0: Read from calculated stripe disk
 * - RAID 1: Read from first disk (all mirrors are identical)
 * - RAID 5: Read from calculated data disk
 */
void raid_read(block_sector_t sector, void* buffer) {
  ASSERT(raid_device != NULL);
  ASSERT(sector < raid_device->total_sectors);

  switch (raid_device->level) {
    case RAID_LEVEL_0: {
      size_t disk = raid0_disk_for_sector(sector);
      block_sector_t phys = raid0_physical_sector(sector);
      block_read(raid_device->disks[disk], phys, buffer);
      break;
    }

    case RAID_LEVEL_1:
      /* All mirrors have same data, read from first */
      block_read(raid_device->disks[0], sector, buffer);
      break;

    case RAID_LEVEL_5: {
      size_t disk = raid5_data_disk(sector);
      block_sector_t phys = raid5_physical_sector(sector);
      block_read(raid_device->disks[disk], phys, buffer);
      break;
    }
  }
}

/**
 * Write a sector to the RAID array.
 *
 * Routes the write to the appropriate disk(s) based on RAID level:
 * - RAID 0: Write to calculated stripe disk
 * - RAID 1: Write to ALL disks (mirroring)
 * - RAID 5: Read-modify-write to update data and parity
 */
void raid_write(block_sector_t sector, const void* buffer) {
  ASSERT(raid_device != NULL);
  ASSERT(sector < raid_device->total_sectors);

  switch (raid_device->level) {
    case RAID_LEVEL_0: {
      size_t disk = raid0_disk_for_sector(sector);
      block_sector_t phys = raid0_physical_sector(sector);
      block_write(raid_device->disks[disk], phys, buffer);
      break;
    }

    case RAID_LEVEL_1: {
      /* Write to all mirrors */
      for (size_t i = 0; i < raid_device->num_disks; i++) {
        block_write(raid_device->disks[i], sector, buffer);
      }
      break;
    }

    case RAID_LEVEL_5: {
      /* Read-modify-write algorithm:
       * 1. Read old data and old parity
       * 2. Compute new parity: P_new = P_old XOR D_old XOR D_new
       * 3. Write new data and new parity */
      size_t data_disk = raid5_data_disk(sector);
      size_t stripe = sector / (raid_device->num_disks - 1);
      size_t parity_disk = raid5_parity_disk(stripe);
      block_sector_t phys = raid5_physical_sector(sector);

      uint8_t old_data[BLOCK_SECTOR_SIZE];
      uint8_t old_parity[BLOCK_SECTOR_SIZE];
      uint8_t new_parity[BLOCK_SECTOR_SIZE];

      block_read(raid_device->disks[data_disk], phys, old_data);
      block_read(raid_device->disks[parity_disk], phys, old_parity);

      const uint8_t* new_data = (const uint8_t*)buffer;
      for (size_t i = 0; i < BLOCK_SECTOR_SIZE; i++) {
        new_parity[i] = old_parity[i] ^ old_data[i] ^ new_data[i];
      }

      block_write(raid_device->disks[data_disk], phys, buffer);
      block_write(raid_device->disks[parity_disk], phys, new_parity);
      break;
    }
  }
}

/** Return total usable sectors, or 0 if RAID not initialized. */
block_sector_t raid_size(void) { return raid_device != NULL ? raid_device->total_sectors : 0; }

/** Return true if RAID has been initialized. */
bool raid_is_initialized(void) { return raid_device != NULL; }

/*
 * ============================================================================
 * RAID 0 HELPERS
 * ============================================================================
 *
 * RAID 0 distributes data across disks in "stripes". With stripe_size=2
 * and 3 disks:
 *
 *   Logical:  0  1 | 2  3 | 4  5 | 6  7 | ...
 *   Disk 0:   0  1         6  7
 *   Disk 1:         2  3         8  9
 *   Disk 2:               4  5
 */

/**
 * Calculate which disk holds a given logical sector.
 * Formula: (sector / stripe_size) % num_disks
 */
size_t raid0_disk_for_sector(block_sector_t sector) {
  return (sector / raid_device->stripe_size) % raid_device->num_disks;
}

/**
 * Calculate the physical sector number on the target disk.
 * Formula: (stripe_round * stripe_size) + offset_in_stripe
 */
block_sector_t raid0_physical_sector(block_sector_t sector) {
  size_t stripe_size = raid_device->stripe_size;
  size_t num_disks = raid_device->num_disks;
  size_t stripe_round = sector / stripe_size / num_disks;
  size_t offset = sector % stripe_size;
  return stripe_round * stripe_size + offset;
}

/*
 * ============================================================================
 * RAID 5 HELPERS
 * ============================================================================
 *
 * RAID 5 stripes data with rotating parity. With 4 disks:
 *
 *   Stripe 0:  D0   D1   D2   P0   <- parity on disk 3
 *   Stripe 1:  D3   D4   P1   D5   <- parity on disk 2
 *   Stripe 2:  D6   P2   D7   D8   <- parity on disk 1
 *   Stripe 3:  P3   D9   D10  D11  <- parity on disk 0
 *
 * Parity rotation distributes write load evenly across disks.
 */

/**
 * Calculate which disk holds parity for a given stripe.
 * Parity rotates backwards: stripe 0 -> last disk, stripe 1 -> second-to-last.
 */
size_t raid5_parity_disk(block_sector_t stripe) {
  size_t num_disks = raid_device->num_disks;
  return (num_disks - 1 - (stripe % num_disks));
}

/**
 * Calculate which disk holds data for a given logical sector.
 * Must skip the parity disk position for each stripe.
 */
size_t raid5_data_disk(block_sector_t sector) {
  size_t num_disks = raid_device->num_disks;
  size_t stripe = sector / (num_disks - 1);
  size_t pos_in_stripe = sector % (num_disks - 1);
  size_t parity_disk = raid5_parity_disk(stripe);

  /* Map position to actual disk, skipping parity disk */
  size_t disk = 0;
  for (size_t d = 0, cnt = 0; d < num_disks; d++) {
    if (d == parity_disk)
      continue;
    if (cnt == pos_in_stripe) {
      disk = d;
      break;
    }
    cnt++;
  }
  return disk;
}

/**
 * Calculate physical sector number on the target disk.
 * Each stripe occupies one row, so physical sector = stripe number.
 */
block_sector_t raid5_physical_sector(block_sector_t sector) {
  size_t num_disks = raid_device->num_disks;
  return sector / (num_disks - 1);
}

/**
 * Compute parity by XORing all data buffers together.
 * Used for full-stripe writes or parity reconstruction.
 */
void raid5_compute_parity(void* parity, void* data[], size_t count) {
  memset(parity, 0, BLOCK_SECTOR_SIZE);
  for (size_t i = 0; i < count; i++) {
    const uint8_t* buf = (const uint8_t*)data[i];
    for (size_t j = 0; j < BLOCK_SECTOR_SIZE; j++) {
      ((uint8_t*)parity)[j] ^= buf[j];
    }
  }
}
