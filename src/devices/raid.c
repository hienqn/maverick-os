#include "devices/raid.h"
#include "threads/malloc.h"
#include <stdio.h>
#include <string.h>

/* Global RAID configuration */
struct raid_config* raid_device = NULL;

void raid_init(enum raid_level level, size_t stripe_size) {
  /* Allocate and initialize the RAID configuration */
  raid_device = malloc(sizeof(struct raid_config));
  if (raid_device == NULL)
    PANIC("Failed to allocate RAID config");

  raid_device->level = level;
  raid_device->stripe_size = (stripe_size > 0) ? stripe_size : 1;
  raid_device->num_disks = 0;

  /* Discover available disks: hdb, hdc, hdd (hda is typically the boot disk) */
  const char* disk_names[] = {"hdb", "hdc", "hdd", "hde"};
  for (size_t i = 0; i < 4 && raid_device->num_disks < RAID_MAX_DISKS; i++) {
    struct block* disk = block_get_by_name(disk_names[i]);
    if (disk != NULL) {
      raid_device->disks[raid_device->num_disks] = disk;
      raid_device->num_disks++;
    }
  }

  if (raid_device->num_disks < 2)
    PANIC("RAID requires at least 2 disks, found %zu", raid_device->num_disks);

  if (level == RAID_LEVEL_5 && raid_device->num_disks < 3)
    PANIC("RAID 5 requires at least 3 disks, found %zu", raid_device->num_disks);

  /* Get capacity of smallest disk (RAID uses smallest common size) */
  block_sector_t min_disk_size = block_size(raid_device->disks[0]);
  for (size_t i = 1; i < raid_device->num_disks; i++) {
    block_sector_t size = block_size(raid_device->disks[i]);
    if (size < min_disk_size)
      min_disk_size = size;
  }

  /* TODO: Calculate total_sectors based on RAID level
   *
   * YOUR CODE HERE (3-5 lines):
   * - RAID 0: total = num_disks * min_disk_size (all capacity used for data)
   * - RAID 1: total = min_disk_size (same data on all disks)
   * - RAID 5: total = (num_disks - 1) * min_disk_size (one disk worth for parity)
   *
   * Set raid_device->total_sectors based on raid_device->level
   */
  (void)min_disk_size; /* Remove this line when you implement */
  PANIC("TODO: Calculate total_sectors based on RAID level");

  printf("RAID %d initialized: %zu disks, %u total sectors\n", level, raid_device->num_disks,
         raid_device->total_sectors);
}

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
       * All disks have same data, read from any one (e.g., disks[0])
       */
      PANIC("RAID 1 read not implemented!");
      break;

    case RAID_LEVEL_5:
      /* TODO: Implement RAID 5 read
       * 1. Use raid5_data_disk() to find which disk has the data
       * 2. Use raid5_physical_sector() to get physical sector
       * 3. Call block_read(disks[disk_index], physical_sector, buffer)
       */
      PANIC("RAID 5 read not implemented!");
      break;
  }
}

void raid_write(block_sector_t sector, const void* buffer) {
  ASSERT(raid_device != NULL);
  ASSERT(sector < raid_device->total_sectors);

  switch (raid_device->level) {
    case RAID_LEVEL_0:
      /* TODO: Implement RAID 0 write
       * 1. Use raid0_disk_for_sector() to find target disk
       * 2. Use raid0_physical_sector() to get physical sector
       * 3. Call block_write(disks[disk_index], physical_sector, buffer)
       */
      PANIC("RAID 0 write not implemented!");
      break;

    case RAID_LEVEL_1:
      /* TODO: Implement RAID 1 write
       * Write to ALL disks (loop through num_disks)
       */
      PANIC("RAID 1 write not implemented!");
      break;

    case RAID_LEVEL_5:
      /* TODO: Implement RAID 5 write (read-modify-write)
       * 1. Find data disk using raid5_data_disk()
       * 2. Find parity disk using raid5_parity_disk()
       * 3. Read OLD data from data disk
       * 4. Read OLD parity from parity disk
       * 5. Compute NEW parity: new_parity = old_parity ^ old_data ^ new_data
       * 6. Write new data to data disk
       * 7. Write new parity to parity disk
       */
      PANIC("RAID 5 write not implemented!");
      break;
  }
}

block_sector_t raid_size(void) {
  /* TODO: Return raid_device->total_sectors */
  PANIC("raid_size not implemented!");
  return 0;
}

bool raid_is_initialized(void) { return raid_device != NULL; }

/* ========== RAID 0 HELPER FUNCTIONS ========== */

size_t raid0_disk_for_sector(block_sector_t sector) {
  /* TODO: Calculate which disk holds this sector
   * Formula: (sector / stripe_size) % num_disks
   */
  (void)sector;
  PANIC("raid0_disk_for_sector not implemented!");
  return 0;
}

block_sector_t raid0_physical_sector(block_sector_t sector) {
  /* TODO: Calculate the physical sector number on the target disk
   * Formula: (sector / stripe_size / num_disks) * stripe_size + (sector % stripe_size)
   */
  (void)sector;
  PANIC("raid0_physical_sector not implemented!");
  return 0;
}

/* ========== RAID 5 HELPER FUNCTIONS ========== */

size_t raid5_parity_disk(block_sector_t stripe) {
  /* TODO: Calculate which disk holds parity for this stripe
   * Parity rotates: stripe 0 -> last disk, stripe 1 -> second-to-last, etc.
   * Formula: (num_disks - 1 - (stripe % num_disks))
   */
  (void)stripe;
  PANIC("raid5_parity_disk not implemented!");
  return 0;
}

size_t raid5_data_disk(block_sector_t sector) {
  /* TODO: Calculate which disk holds this logical sector's data
   * Must account for parity disk rotation - skip the parity disk position
   *
   * Steps:
   * 1. Calculate stripe = sector / (num_disks - 1)
   * 2. Calculate position within stripe = sector % (num_disks - 1)
   * 3. Find parity disk for this stripe
   * 4. Map position to actual disk, skipping parity disk
   */
  (void)sector;
  PANIC("raid5_data_disk not implemented!");
  return 0;
}

block_sector_t raid5_physical_sector(block_sector_t sector) {
  /* TODO: Calculate physical sector number on the target disk
   * Each stripe uses one row across all disks
   * Physical sector = stripe number (since each disk holds one sector per stripe)
   * Formula: sector / (num_disks - 1)
   */
  (void)sector;
  PANIC("raid5_physical_sector not implemented!");
  return 0;
}

void raid5_compute_parity(void* parity, void* data[], size_t count) {
  /* TODO: XOR all data buffers together to compute parity
   * parity[i] = data[0][i] ^ data[1][i] ^ ... ^ data[count-1][i]
   *
   * 1. memset(parity, 0, BLOCK_SECTOR_SIZE)
   * 2. Loop through each data buffer
   * 3. XOR each byte into parity
   */
  (void)parity;
  (void)data;
  (void)count;
  PANIC("raid5_compute_parity not implemented!");
}
