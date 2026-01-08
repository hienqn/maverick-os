#include "devices/raid.h"
#include "threads/malloc.h"
#include <stdio.h>
#include <string.h>

/* Global RAID configuration */
struct raid_config* raid_device = NULL;

void raid_init(enum raid_level level, size_t stripe_size) {
  /* TODO: Implement initialization
   *
   * 1. Allocate raid_device using malloc()
   * 2. Set level and stripe_size
   * 3. Discover disks using block_get_by_name("hdb"), block_get_by_name("hdc"), etc.
   * 4. Store found disks in raid_device->disks[] and count in num_disks
   * 5. Calculate total_sectors based on RAID level:
   *    - RAID 0: sum of all disk capacities
   *    - RAID 1: capacity of single disk (all mirrors)
   *    Use block_size(disk) to get a disk's sector count
   */
  PANIC("raid_init not implemented!");
}

void raid_read(block_sector_t sector, void* buffer) {
  /* TODO: Implement RAID read
   *
   * RAID 0 (striping):
   *   - Calculate which disk: (sector / stripe_size) % num_disks
   *   - Calculate physical sector on that disk
   *   - Call block_read(disk, physical_sector, buffer)
   *
   * RAID 1 (mirroring):
   *   - All disks have same data, read from any one
   *   - Call block_read(disks[0], sector, buffer)
   */
  PANIC("raid_read not implemented!");
}

void raid_write(block_sector_t sector, const void* buffer) {
  /* TODO: Implement RAID write
   *
   * RAID 0 (striping):
   *   - Same calculation as read
   *   - Write to ONE disk
   *
   * RAID 1 (mirroring):
   *   - Write to ALL disks (loop through all)
   */
  PANIC("raid_write not implemented!");
}

block_sector_t raid_size(void) {
  /* TODO: Return raid_device->total_sectors */
  PANIC("raid_size not implemented!");
  return 0;
}

bool raid_is_initialized(void) { return raid_device != NULL; }
