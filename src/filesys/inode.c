#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Number of direct block pointers. */
#define DIRECT_BLOCK_COUNT 12

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of block pointers that fit in one sector. */
#define PTRS_PER_BLOCK (BLOCK_SECTOR_SIZE / sizeof(block_sector_t))

/* Sentinel value indicating an invalid sector (no data at requested offset).
   Uses maximum unsigned value, which won't be a valid sector on any
   reasonable-sized disk. */
#define INVALID_SECTOR ((block_sector_t)-1)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long.
   Layout: 12*4 + 4 + 4 + 4 + 4 + 112*4 = 48 + 16 + 448 = 512 bytes */
struct inode_disk {
  block_sector_t direct[DIRECT_BLOCK_COUNT]; /* Direct block pointers: 48 bytes */
  block_sector_t indirect;                   /* Indirect block pointer: 4 bytes */
  block_sector_t doubly_indirect;            /* Doubly-indirect pointer: 4 bytes */
  off_t length;                              /* File size in bytes: 4 bytes */
  uint32_t is_dir;                           /* Whether this inode is a directory */
  unsigned magic;                            /* Magic number: 4 bytes */
  uint32_t unused[111];                      /* Padding to 512 bytes: 448 bytes */
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* In-memory inode.
   
   SYNCHRONIZATION: Each inode has its own lock that protects:
   - open_cnt: prevents races between reopen/close
   - deny_write_cnt: prevents races between deny/allow/write operations
   - removed: prevents races when marking for deletion
   - data (inode_disk): prevents races during file extension
   
   The per-inode lock allows concurrent operations on DIFFERENT files,
   while serializing operations on the SAME file. This is more efficient
   than a single global lock. */
struct inode {
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct lock lock;       /* Per-inode lock (see above). */
  struct inode_disk data; /* Inode content (includes is_dir flag). */
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns INVALID_SECTOR if INODE does not contain data for a byte
   at offset POS (i.e., pos >= file length). */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  ASSERT(inode != NULL);
  if (pos >= inode->data.length)
    return INVALID_SECTOR;

  off_t block_idx = pos / BLOCK_SECTOR_SIZE;

  /* Direct blocks: indices 0 to DIRECT_BLOCK_COUNT-1 */
  if (block_idx < DIRECT_BLOCK_COUNT) {
    return inode->data.direct[block_idx];
  }

  block_idx -= DIRECT_BLOCK_COUNT;

  /* Indirect block: next PTRS_PER_BLOCK blocks */
  if (block_idx < (off_t)PTRS_PER_BLOCK) {
    block_sector_t buffer[PTRS_PER_BLOCK];
    cache_read(inode->data.indirect, buffer);
    return buffer[block_idx];
  }

  block_idx -= PTRS_PER_BLOCK;

  /* Doubly indirect block */
  block_sector_t buffer[PTRS_PER_BLOCK];

  /* First, read the doubly indirect block to find which indirect block */
  cache_read(inode->data.doubly_indirect, buffer);
  size_t indirect_idx = block_idx / PTRS_PER_BLOCK;
  block_sector_t indirect_block = buffer[indirect_idx];

  /* Then read that indirect block to find the data sector */
  cache_read(indirect_block, buffer);
  size_t offset_in_indirect = block_idx % PTRS_PER_BLOCK;
  return buffer[offset_in_indirect];
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Lock protecting the open_inodes list.
   
   SYNCHRONIZATION: This lock protects the global list of open inodes.
   It prevents races when:
   - Searching for an already-open inode (inode_open)
   - Adding a newly-opened inode to the list (inode_open)
   - Removing a closed inode from the list (inode_close)
   
   WHY TWO LOCKS? We need both open_inodes_lock AND per-inode locks because:
   
   1. The list lock protects the LIST STRUCTURE itself (traversal, insertion,
      removal). Without it, one thread could be iterating the list while
      another removes an element, causing undefined behavior.
   
   2. The per-inode lock protects INODE CONTENTS (open_cnt, data, etc.).
      Without it, two threads could simultaneously modify the same inode's
      metadata, causing data races.
   
   LOCK ORDERING: Always acquire open_inodes_lock BEFORE any inode->lock.
   This prevents deadlock when multiple threads operate on inodes. */
static struct lock open_inodes_lock;

/* Initializes the inode module. */
void inode_init(void) {
  list_init(&open_inodes);
  lock_init(&open_inodes_lock);
}

/* Frees all data blocks referenced by DISK_INODE.
   Does not free the inode sector itself. */
static void inode_deallocate(struct inode_disk* disk_inode) {
  /* Free direct blocks. */
  for (size_t i = 0; i < DIRECT_BLOCK_COUNT; i++) {
    if (disk_inode->direct[i] != 0)
      free_map_release(disk_inode->direct[i], 1);
  }

  /* Free indirect block and its data blocks. */
  if (disk_inode->indirect != 0) {
    block_sector_t buffer[PTRS_PER_BLOCK];
    cache_read(disk_inode->indirect, buffer);
    for (size_t i = 0; i < PTRS_PER_BLOCK; i++) {
      if (buffer[i] != 0)
        free_map_release(buffer[i], 1);
    }
    free_map_release(disk_inode->indirect, 1);
  }

  /* Free doubly indirect block and all its indirect blocks. */
  if (disk_inode->doubly_indirect != 0) {
    block_sector_t dbl_buffer[PTRS_PER_BLOCK];
    cache_read(disk_inode->doubly_indirect, dbl_buffer);
    for (size_t i = 0; i < PTRS_PER_BLOCK; i++) {
      if (dbl_buffer[i] != 0) {
        block_sector_t ind_buffer[PTRS_PER_BLOCK];
        cache_read(dbl_buffer[i], ind_buffer);
        for (size_t j = 0; j < PTRS_PER_BLOCK; j++) {
          if (ind_buffer[j] != 0)
            free_map_release(ind_buffer[j], 1);
        }
        free_map_release(dbl_buffer[i], 1);
      }
    }
    free_map_release(disk_inode->doubly_indirect, 1);
  }
}

/* Zero-filled block for initializing new data blocks.
   Declared const to indicate it's never modified. */
static const char zeros[BLOCK_SECTOR_SIZE];

/* Internal helper: Creates an inode with LENGTH bytes of data at SECTOR.
   If IS_DIR is true, marks the inode as a directory.
   Returns true if successful, false if memory or disk allocation fails. */
static bool inode_create_internal(block_sector_t sector, off_t length, bool is_dir) {
  struct inode_disk* disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);
  ASSERT(sizeof(struct inode_disk) == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode == NULL)
    return false;

  disk_inode->length = length;
  disk_inode->magic = INODE_MAGIC;
  disk_inode->is_dir = is_dir ? 1 : 0;

  size_t sectors = bytes_to_sectors(length);
  size_t allocated = 0;

  /* Allocate direct blocks. */
  for (size_t i = 0; i < DIRECT_BLOCK_COUNT && allocated < sectors; i++) {
    if (!free_map_allocate_one(&disk_inode->direct[i]))
      goto done;
    cache_write(disk_inode->direct[i], zeros, 0, BLOCK_SECTOR_SIZE);
    allocated++;
  }

  /* Allocate indirect block if needed. */
  if (allocated < sectors) {
    if (!free_map_allocate_one(&disk_inode->indirect))
      goto done;

    block_sector_t indirect_block[PTRS_PER_BLOCK];
    memset(indirect_block, 0, sizeof indirect_block);

    for (size_t i = 0; i < PTRS_PER_BLOCK && allocated < sectors; i++) {
      if (!free_map_allocate_one(&indirect_block[i]))
        goto done;
      cache_write(indirect_block[i], zeros, 0, BLOCK_SECTOR_SIZE);
      allocated++;
    }
    cache_write(disk_inode->indirect, indirect_block, 0, BLOCK_SECTOR_SIZE);
  }

  /* Allocate doubly indirect block if needed. */
  if (allocated < sectors) {
    if (!free_map_allocate_one(&disk_inode->doubly_indirect))
      goto done;

    block_sector_t dbl_block[PTRS_PER_BLOCK];
    memset(dbl_block, 0, sizeof dbl_block);

    for (size_t i = 0; i < PTRS_PER_BLOCK && allocated < sectors; i++) {
      block_sector_t ind_sector;
      if (!free_map_allocate_one(&ind_sector))
        goto done;
      dbl_block[i] = ind_sector;

      block_sector_t ind_block[PTRS_PER_BLOCK];
      memset(ind_block, 0, sizeof ind_block);

      for (size_t j = 0; j < PTRS_PER_BLOCK && allocated < sectors; j++) {
        if (!free_map_allocate_one(&ind_block[j]))
          goto done;
        cache_write(ind_block[j], zeros, 0, BLOCK_SECTOR_SIZE);
        allocated++;
      }
      cache_write(ind_sector, ind_block, 0, BLOCK_SECTOR_SIZE);
    }
    cache_write(disk_inode->doubly_indirect, dbl_block, 0, BLOCK_SECTOR_SIZE);
  }

  /* Write the inode metadata. */
  cache_write(sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  success = true;

done:
  if (!success)
    inode_deallocate(disk_inode);
  free(disk_inode);
  return success;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system device.
   Returns true if successful, false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length) {
  return inode_create_internal(sector, length, false);
}

/* Returns true if INODE represents a directory. */
bool inode_is_dir(struct inode* inode) {
  ASSERT(inode != NULL);
  return inode->data.is_dir != 0;
}

/* Creates a directory inode at sector SECTOR with initial length LENGTH.
   Returns true if successful, false if memory or disk allocation fails. */
bool inode_create_dir(block_sector_t sector, off_t length) {
  return inode_create_internal(sector, length, true);
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  lock_acquire(&open_inodes_lock);

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      lock_release(&open_inodes_lock);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL) {
    lock_release(&open_inodes_lock);
    return NULL;
  }

  /* Initialize. */
  list_push_front(&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->lock);
  /* Read inode metadata from cache. */
  cache_read(inode->sector, &inode->data);

  lock_release(&open_inodes_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL) {
    lock_acquire(&inode->lock);
    inode->open_cnt++;
    lock_release(&inode->lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Acquire locks in correct order: list lock first, then inode lock. */
  lock_acquire(&open_inodes_lock);
  lock_acquire(&inode->lock);

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    /* Remove from inode list while holding both locks. */
    list_remove(&inode->elem);
    lock_release(&inode->lock);
    lock_release(&open_inodes_lock);

    /* Deallocate blocks if removed (no locks needed, inode is now private). */
    if (inode->removed) {
      free_map_release(inode->sector, 1);
      inode_deallocate(&inode->data);
    }

    free(inode);
  } else {
    lock_release(&inode->lock);
    lock_release(&open_inodes_lock);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  lock_acquire(&inode->lock);
  inode->removed = true;
  lock_release(&inode->lock);
}

/* Returns true if INODE has been marked for removal. */
bool inode_is_removed(const struct inode* inode) {
  ASSERT(inode != NULL);
  return inode->removed;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached.
   Uses the buffer cache for all disk reads - no bounce buffer needed. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    /* Read directly from cache into caller's buffer.
       cache_read_at handles both full and partial sector reads. */
    cache_read_at(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }

  return bytes_read;
}

/* Extends INODE to be at least NEW_LENGTH bytes long.
   Allocates new blocks as needed and zero-fills them.
   Returns true on success, false if allocation fails.
   
   NOTE: On failure, some blocks may have been allocated but the inode's
   length is NOT updated, so those blocks become "leaked" until the inode
   is deleted. This is a known limitation that trades simplicity for 
   perfect space recovery. A production system might track allocations
   and roll them back on failure. */
static bool inode_extend(struct inode* inode, off_t new_length) {
  ASSERT(new_length > inode->data.length);

  size_t old_sectors = bytes_to_sectors(inode->data.length);
  size_t new_sectors = bytes_to_sectors(new_length);

  if (new_sectors == old_sectors) {
    /* No new blocks needed, just update length. */
    inode->data.length = new_length;
    cache_write(inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
    return true;
  }

  size_t allocated = old_sectors;

  /* Allocate direct blocks. Uses global const zeros for zero-filling. */
  for (size_t i = allocated; i < DIRECT_BLOCK_COUNT && allocated < new_sectors; i++) {
    if (!free_map_allocate_one(&inode->data.direct[i]))
      return false;
    cache_write(inode->data.direct[i], zeros, 0, BLOCK_SECTOR_SIZE);
    allocated++;
  }

  if (allocated >= new_sectors)
    goto success;

  /* Allocate indirect block if needed. */
  block_sector_t indirect_block[PTRS_PER_BLOCK];
  if (inode->data.indirect == 0) {
    /* Need to create the indirect block. */
    if (!free_map_allocate_one(&inode->data.indirect))
      return false;
    memset(indirect_block, 0, sizeof indirect_block);
  } else {
    /* Read existing indirect block. */
    cache_read(inode->data.indirect, indirect_block);
  }

  /* Allocate data blocks in indirect block. */
  size_t indirect_start = (old_sectors > DIRECT_BLOCK_COUNT) ? old_sectors - DIRECT_BLOCK_COUNT : 0;
  for (size_t i = indirect_start; i < PTRS_PER_BLOCK && allocated < new_sectors; i++) {
    if (indirect_block[i] == 0) {
      if (!free_map_allocate_one(&indirect_block[i]))
        return false;
      cache_write(indirect_block[i], zeros, 0, BLOCK_SECTOR_SIZE);
    }
    allocated++;
  }
  cache_write(inode->data.indirect, indirect_block, 0, BLOCK_SECTOR_SIZE);

  if (allocated >= new_sectors)
    goto success;

  /* Allocate doubly indirect block if needed. */
  block_sector_t dbl_block[PTRS_PER_BLOCK];
  if (inode->data.doubly_indirect == 0) {
    if (!free_map_allocate_one(&inode->data.doubly_indirect))
      return false;
    memset(dbl_block, 0, sizeof dbl_block);
  } else {
    cache_read(inode->data.doubly_indirect, dbl_block);
  }

  /* Calculate starting position within doubly indirect. */
  size_t dbl_base = DIRECT_BLOCK_COUNT + PTRS_PER_BLOCK;
  size_t dbl_start = (old_sectors > dbl_base) ? old_sectors - dbl_base : 0;
  size_t dbl_idx = dbl_start / PTRS_PER_BLOCK;
  size_t ind_idx = dbl_start % PTRS_PER_BLOCK;

  while (allocated < new_sectors && dbl_idx < PTRS_PER_BLOCK) {
    block_sector_t ind_block[PTRS_PER_BLOCK];

    if (dbl_block[dbl_idx] == 0) {
      /* Need to create this indirect block. */
      if (!free_map_allocate_one(&dbl_block[dbl_idx]))
        return false;
      memset(ind_block, 0, sizeof ind_block);
    } else {
      cache_read(dbl_block[dbl_idx], ind_block);
    }

    /* Allocate data blocks in this indirect block. */
    while (ind_idx < PTRS_PER_BLOCK && allocated < new_sectors) {
      if (ind_block[ind_idx] == 0) {
        if (!free_map_allocate_one(&ind_block[ind_idx]))
          return false;
        cache_write(ind_block[ind_idx], zeros, 0, BLOCK_SECTOR_SIZE);
      }
      ind_idx++;
      allocated++;
    }
    cache_write(dbl_block[dbl_idx], ind_block, 0, BLOCK_SECTOR_SIZE);

    dbl_idx++;
    ind_idx = 0;
  }
  cache_write(inode->data.doubly_indirect, dbl_block, 0, BLOCK_SECTOR_SIZE);

success:
  inode->data.length = new_length;
  cache_write(inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
  return true;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if disk space runs out or an error occurs.
   Extends the file if writing past EOF.
   Uses the buffer cache for all disk writes. */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;

  lock_acquire(&inode->lock);

  if (inode->deny_write_cnt) {
    lock_release(&inode->lock);
    return 0;
  }

  /* Extend file if writing past EOF. */
  off_t end_pos = offset + size;
  if (end_pos > inode->data.length) {
    if (!inode_extend(inode, end_pos)) {
      lock_release(&inode->lock);
      return 0; /* Extension failed, can't write anything. */
    }
  }

  lock_release(&inode->lock);

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    /* Write to cache. cache_write handles both full and partial sector
       writes, including read-modify-write for partial sectors. */
    cache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  lock_acquire(&inode->lock);
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  lock_acquire(&inode->lock);
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) { return inode->data.length; }
