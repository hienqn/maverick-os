#ifndef DEVICES_RAID_H
#define DEVICES_RAID_H

#include "devices/block.h"
#include <stdbool.h>

#define RAID_MAX_DISKS 4

enum raid_level {
  RAID_LEVEL_0, /* Striping */
  RAID_LEVEL_1, /* Mirroring */
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

/* TODO: Implement these functions in raid.c */
void raid_init(enum raid_level level, size_t stripe_size);
void raid_read(block_sector_t sector, void* buffer);
void raid_write(block_sector_t sector, const void* buffer);
block_sector_t raid_size(void);
bool raid_is_initialized(void);

#endif /* devices/raid.h */
