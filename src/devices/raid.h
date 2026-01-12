#ifndef DEVICES_RAID_H
#define DEVICES_RAID_H

#include "devices/block.h"
#include <stdbool.h>

#define RAID_MAX_DISKS 4

enum raid_level {
  RAID_LEVEL_0, /* Striping - no redundancy */
  RAID_LEVEL_1, /* Mirroring - full redundancy */
  RAID_LEVEL_5, /* Striping with distributed parity */
};

struct raid_config {
  enum raid_level level;
  size_t num_disks;
  struct block* disks[RAID_MAX_DISKS];
  size_t stripe_size;
  block_sector_t total_sectors;
};

/* Global RAID device - initialize this in raid_init() */
extern struct raid_config* raid_device;

/* Core RAID functions - implement in raid.c */
void raid_init(enum raid_level level, size_t stripe_size);
void raid_read(block_sector_t sector, void* buffer);
void raid_write(block_sector_t sector, const void* buffer);
block_sector_t raid_size(void);
bool raid_is_initialized(void);

/* RAID 0 helper functions */
size_t raid0_disk_for_sector(block_sector_t sector);
block_sector_t raid0_physical_sector(block_sector_t sector);

/* RAID 5 helper functions */
size_t raid5_parity_disk(block_sector_t stripe);
size_t raid5_data_disk(block_sector_t sector);
block_sector_t raid5_physical_sector(block_sector_t sector);
void raid5_compute_parity(void* parity, void* data[], size_t count);

#endif /* devices/raid.h */
