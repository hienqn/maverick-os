#include "vm/mmap.h"
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/inode.h"
#include "lib/syscall-nr.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/filedesc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================
 */

/* Get the current process's mmap_list. */
static struct list* get_mmap_list(void) {
  struct thread* t = thread_current();
  ASSERT(t->pcb != NULL);
  return &t->pcb->mmap_list;
}

/* Get the current process's mmap_lock. */
static struct lock* get_mmap_lock(void) {
  struct thread* t = thread_current();
  ASSERT(t->pcb != NULL);
  return &t->pcb->mmap_lock;
}

/* Get the current process's SPT. */
static struct spt* get_spt(void) {
  struct thread* t = thread_current();
  ASSERT(t->pcb != NULL);
  return &t->pcb->spt;
}

/* Get the current process's page directory. */
static uint32_t* get_pagedir(void) {
  struct thread* t = thread_current();
  ASSERT(t->pcb != NULL);
  return t->pcb->pagedir;
}

/* Forward declarations for static functions. */
static struct mmap_region* mmap_find_region_locked(void* addr);
static bool mmap_range_available(void* addr, size_t length);
static void* find_free_address(size_t length);

/* Base address for mmap allocations when addr hint is NULL.
   Start above typical code/data segments but below stack. */
#define MMAP_BASE ((void*)0x40000000)

/* Clean up all pages in a region: writeback dirty pages, free frames, remove SPT entries.
   Does NOT free the region struct or close the file - caller handles that.

   SYNCHRONIZATION:
   ----------------
   This function must synchronize with frame_evict() to prevent concurrent writeback
   of the same mmap page, which would corrupt file data.

   Lock ordering constraint: frame_evict() acquires frame_lock then spt_lock.
   To avoid deadlock, we use a two-phase approach:
   - Phase 1: Under spt_lock, get kpage snapshot
   - Phase 2: Without locks, pin the frame (acquires frame_lock)
   - Phase 3: Under spt_lock, validate and do cleanup with frame pinned
   - Phase 4: Outside locks, free resources

   If pinning fails (frame was evicted), eviction already handled writeback. */
static void mmap_cleanup_pages(struct mmap_region* region) {
  struct spt* spt = get_spt();
  uint32_t* pd = get_pagedir();

  for (size_t i = 0; i < region->page_count; i++) {
    void* upage = (uint8_t*)region->start_addr + i * PGSIZE;

    bool done = false;
    while (!done) {
      void* kpage_snapshot = NULL;
      bool frame_pinned = false;

      /* Phase 1: Get kpage snapshot under spt_lock. */
      lock_acquire(&spt->spt_lock);
      struct spt_entry* entry = spt_find(spt, upage);
      if (entry == NULL) {
        lock_release(&spt->spt_lock);
        done = true;
        continue;
      }
      if (entry->status == PAGE_FRAME && entry->kpage != NULL) {
        kpage_snapshot = entry->kpage;
      }
      lock_release(&spt->spt_lock);

      /* Phase 2: Pin the frame without holding spt_lock (avoids deadlock). */
      if (kpage_snapshot != NULL) {
        frame_pinned = frame_pin_if_present(kpage_snapshot);
      }

      /* Phase 3: Validate and do cleanup under spt_lock. */
      lock_acquire(&spt->spt_lock);
      entry = spt_find(spt, upage);
      if (entry == NULL) {
        /* Entry removed (shouldn't happen, but defensive). */
        lock_release(&spt->spt_lock);
        if (frame_pinned)
          frame_unpin(kpage_snapshot);
        done = true;
        continue;
      }

      void* kpage_to_free = NULL;
      bool do_writeback = false;
      size_t read_bytes = entry->read_bytes;
      size_t swap_slot = entry->swap_slot;
      enum page_status status = entry->status;

      if (status == PAGE_FRAME && entry->kpage != NULL) {
        if (frame_pinned && entry->kpage == kpage_snapshot) {
          /* Successfully pinned the correct frame. Eviction cannot touch it now. */
          kpage_to_free = entry->kpage;
          do_writeback = !region->is_anonymous && pagedir_is_dirty(pd, upage);
          pagedir_clear_page(pd, upage);
          done = true;
        } else {
          /* Either didn't pin (frame evicted) or pinned wrong frame (evicted & reused).
             In the first case, eviction handled writeback. In the second case, the page
             was faulted back in with a new kpage. Retry to handle the new frame. */
          lock_release(&spt->spt_lock);
          if (frame_pinned)
            frame_unpin(kpage_snapshot);
          continue; /* Retry */
        }
      } else {
        /* Page not in frame (PAGE_FILE, PAGE_SWAP, PAGE_ZERO, PAGE_COW). */
        done = true;
      }

      /* Remove entry from SPT. */
      hash_delete(&spt->pages, &entry->hash_elem);
      lock_release(&spt->spt_lock);

      /* Phase 4: Perform I/O and free resources outside of locks. */
      if (do_writeback && kpage_to_free != NULL) {
        off_t page_offset = region->offset + ((uintptr_t)upage - (uintptr_t)region->start_addr);
        file_write_at(region->file, kpage_to_free, read_bytes, page_offset);
      }

      if (frame_pinned)
        frame_unpin(kpage_snapshot);
      if (kpage_to_free != NULL)
        frame_free(kpage_to_free);
      if (status == PAGE_SWAP)
        swap_free(swap_slot);

      free(entry);
    }
  }
}

/* Get file from file descriptor.
   Returns NULL if fd is invalid or not a file. */
static struct file* get_file_from_fd(int fd) {
  struct thread* t = thread_current();
  ASSERT(t->pcb != NULL);

  /* Validate fd range (stdin=0, stdout=1 are not valid for mmap) */
  if (fd < 2 || fd >= MAX_FILE_DESCRIPTOR) {
    return NULL;
  }

  struct fd_entry* entry = &t->pcb->fd_table[fd];
  if (entry->ofd == NULL || entry->ofd->type != FD_FILE || entry->ofd->file == NULL) {
    return NULL;
  }

  return entry->ofd->file;
}

/* ============================================================================
 * MMAP LIFECYCLE
 * ============================================================================
 */

void* mmap_create(void* addr, size_t length, int fd, off_t offset) {
  /* Validate parameters */
  if (addr == NULL || pg_ofs(addr) != 0) {
    return MAP_FAILED;
  }

  if (pg_ofs((void*)(uintptr_t)offset) != 0) {
    return MAP_FAILED;
  }

  if (length == 0) {
    return MAP_FAILED;
  }

  if (fd < 2) {
    return MAP_FAILED;
  }

  /* Get file from fd */
  struct file* file = get_file_from_fd(fd);
  if (file == NULL) {
    return MAP_FAILED;
  }

  /* Verify it's not a directory (get_file_from_fd already filters this, but double-check) */
  struct inode* inode = file_get_inode(file);
  if (inode_is_dir(inode)) {
    return MAP_FAILED;
  }

  /* Check file properties */
  off_t file_size = file_length(file);
  if (file_size <= 0) {
    return MAP_FAILED;
  }

  /* Calculate page count */
  size_t page_count = DIV_ROUND_UP(length, PGSIZE);

  /* Acquire lock to protect mmap_list and prevent race conditions */
  struct lock* mmap_lock = get_mmap_lock();
  lock_acquire(mmap_lock);

  /* Check address range is available (while holding lock) */
  if (!mmap_range_available(addr, page_count * PGSIZE)) {
    lock_release(mmap_lock);
    return MAP_FAILED;
  }

  /* Create private file reference */
  struct file* map_file = file_reopen(file);
  if (map_file == NULL) {
    lock_release(mmap_lock);
    return MAP_FAILED;
  }

  /* Allocate mmap_region */
  struct mmap_region* region = malloc(sizeof(struct mmap_region));
  if (region == NULL) {
    file_close(map_file);
    lock_release(mmap_lock);
    return MAP_FAILED;
  }

  /* Fill in all fields */
  region->start_addr = addr;
  region->length = length;
  region->page_count = page_count;
  region->file = map_file;
  region->offset = offset;
  region->flags = 0;
  region->is_anonymous = false;
  /* TODO: inode_sector stored for potential future use (e.g., sharing detection) */
  region->inode_sector = inode_get_inumber(file_get_inode(map_file));
  region->writable = true;

  /* Create SPT entries for each page.
     IMPORTANT: Hold spt_lock during all SPT operations to prevent race
     condition with frame_evict() which does spt_find() under spt_lock.
     Without this lock, hash_insert and hash_find could run concurrently,
     corrupting the hash table. */
  struct spt* spt = get_spt();
  size_t remaining = length;

  lock_acquire(&spt->spt_lock);
  for (size_t i = 0; i < page_count; i++) {
    void* upage = (uint8_t*)addr + i * PGSIZE;
    off_t file_ofs = offset + i * PGSIZE;
    size_t read_bytes = (remaining > PGSIZE) ? PGSIZE : remaining;
    size_t zero_bytes = PGSIZE - read_bytes;

    if (!spt_create_file_page(spt, upage, map_file, file_ofs, read_bytes, zero_bytes, true)) {
      /* Cleanup on failure: remove already-created entries */
      for (size_t j = 0; j < i; j++) {
        void* cleanup_page = (uint8_t*)addr + j * PGSIZE;
        spt_remove(spt, cleanup_page);
      }
      lock_release(&spt->spt_lock);
      file_close(map_file);
      free(region);
      lock_release(mmap_lock);
      return MAP_FAILED;
    }
    /* Mark this as an mmap page (so eviction writes back to file, not swap) */
    struct spt_entry* entry = spt_find(spt, upage);
    if (entry != NULL) {
      entry->is_mmap = true;
    }
    remaining -= read_bytes;
  }
  lock_release(&spt->spt_lock);

  /* Add region to mmap_list (while holding lock) */
  list_push_back(get_mmap_list(), &region->elem);

  /* Release lock before returning */
  lock_release(mmap_lock);

  /* Return the mapped address */
  return addr;
}

/* Find a free address range of the given length.
   NOTE: Caller must hold mmap_lock. */
static void* find_free_address(size_t length) {
  /* Search from MMAP_BASE upward, stopping before the stack region */
  size_t max_stack_size = MAX_STACK_PAGES * PGSIZE;
  uint8_t* stack_start = (uint8_t*)PHYS_BASE - max_stack_size;
  uint8_t* addr = (uint8_t*)MMAP_BASE;

  /* Round up length to page boundary */
  length = ROUND_UP(length, PGSIZE);

  while (addr + length <= stack_start) {
    if (mmap_range_available(addr, length)) {
      return addr;
    }
    /* Move to next page */
    addr += PGSIZE;
  }

  return NULL; /* No free range found */
}

void* mmap_create_anon(void* addr, size_t length, int flags) {
  /* Validate length */
  if (length == 0) {
    return MAP_FAILED;
  }

  /* If addr is provided, it must be page-aligned */
  if (addr != NULL && pg_ofs(addr) != 0) {
    return MAP_FAILED;
  }

  /* Calculate page count */
  size_t page_count = DIV_ROUND_UP(length, PGSIZE);

  /* Acquire lock to protect mmap_list and address space */
  struct lock* mmap_lock = get_mmap_lock();
  lock_acquire(mmap_lock);

  /* Find address if not specified */
  if (addr == NULL) {
    addr = find_free_address(page_count * PGSIZE);
    if (addr == NULL) {
      lock_release(mmap_lock);
      return MAP_FAILED;
    }
  } else {
    /* Check that provided address range is available */
    if (!mmap_range_available(addr, page_count * PGSIZE)) {
      lock_release(mmap_lock);
      return MAP_FAILED;
    }
  }

  /* Allocate mmap_region */
  struct mmap_region* region = malloc(sizeof(struct mmap_region));
  if (region == NULL) {
    lock_release(mmap_lock);
    return MAP_FAILED;
  }

  /* Fill in all fields */
  region->start_addr = addr;
  region->length = length;
  region->page_count = page_count;
  region->file = NULL;
  region->offset = 0;
  region->flags = flags;
  region->is_anonymous = true;
  region->inode_sector = 0;
  region->writable = true;

  /* Create SPT entries for each page (zero-filled on demand).
     IMPORTANT: Hold spt_lock during all SPT operations to prevent race
     condition with frame_evict() which does spt_find() under spt_lock. */
  struct spt* spt = get_spt();

  lock_acquire(&spt->spt_lock);
  for (size_t i = 0; i < page_count; i++) {
    void* upage = (uint8_t*)addr + i * PGSIZE;

    if (!spt_create_zero_page(spt, upage, true)) {
      /* Cleanup on failure: remove already-created entries */
      for (size_t j = 0; j < i; j++) {
        void* cleanup_page = (uint8_t*)addr + j * PGSIZE;
        spt_remove(spt, cleanup_page);
      }
      lock_release(&spt->spt_lock);
      free(region);
      lock_release(mmap_lock);
      return MAP_FAILED;
    }
    /* Mark this as an mmap page for consistent handling */
    struct spt_entry* entry = spt_find(spt, upage);
    if (entry != NULL) {
      entry->is_mmap = true;
    }
  }
  lock_release(&spt->spt_lock);

  /* Add region to mmap_list */
  list_push_back(get_mmap_list(), &region->elem);

  lock_release(mmap_lock);

  return addr;
}

int mmap_destroy(void* addr, size_t length) {
  /* Acquire lock to protect mmap_list */
  struct lock* mmap_lock = get_mmap_lock();
  lock_acquire(mmap_lock);

  /* Find the region (using internal version since we already hold the lock) */
  struct mmap_region* region = mmap_find_region_locked(addr);
  if (region == NULL) {
    lock_release(mmap_lock);
    return -1;
  }

  /* Verify region matches exactly */
  if (region->start_addr != addr || region->length != length) {
    lock_release(mmap_lock);
    return -1;
  }

  /* Clean up pages, close file (if any), remove from list, free region */
  mmap_cleanup_pages(region);
  if (!region->is_anonymous && region->file != NULL) {
    file_close(region->file);
  }
  list_remove(&region->elem);
  lock_release(mmap_lock);
  free(region);

  return 0;
}

int mmap_find_and_destroy(void* addr) {
  /* Atomically find and destroy a mapping containing addr.
     This avoids the TOCTOU race in find-then-destroy patterns. */
  struct lock* mmap_lock = get_mmap_lock();
  lock_acquire(mmap_lock);

  struct mmap_region* region = mmap_find_region_locked(addr);
  if (region == NULL) {
    lock_release(mmap_lock);
    return -1;
  }

  /* Clean up pages, close file (if any), remove from list, free region */
  mmap_cleanup_pages(region);
  if (!region->is_anonymous && region->file != NULL) {
    file_close(region->file);
  }
  list_remove(&region->elem);
  lock_release(mmap_lock);
  free(region);

  return 0;
}

void mmap_destroy_all(void) {
  struct lock* mmap_lock = get_mmap_lock();
  lock_acquire(mmap_lock);

  struct list* mmap_list = get_mmap_list();

  while (!list_empty(mmap_list)) {
    struct list_elem* e = list_pop_front(mmap_list);
    struct mmap_region* region = list_entry(e, struct mmap_region, elem);

    mmap_cleanup_pages(region);
    /* Only close file for file-backed mappings */
    if (!region->is_anonymous && region->file != NULL) {
      file_close(region->file);
    }
    free(region);
  }

  lock_release(mmap_lock);
}

/* ============================================================================
 * MMAP UTILITIES
 * ============================================================================
 */

/* Internal version that assumes lock is already held. */
static struct mmap_region* mmap_find_region_locked(void* addr) {
  struct list* mmap_list = get_mmap_list();
  struct list_elem* e;

  for (e = list_begin(mmap_list); e != list_end(mmap_list); e = list_next(e)) {
    struct mmap_region* region = list_entry(e, struct mmap_region, elem);
    void* start = region->start_addr;
    void* end = (uint8_t*)start + region->page_count * PGSIZE;

    if (addr >= start && addr < end) {
      return region;
    }
  }

  return NULL;
}

/* Find region and hold lock. Caller must release *lock_out when done. */
static struct mmap_region* mmap_find_region_hold_lock(void* addr, struct lock** lock_out) {
  struct lock* mmap_lock = get_mmap_lock();
  lock_acquire(mmap_lock);

  struct mmap_region* region = mmap_find_region_locked(addr);

  if (region != NULL) {
    *lock_out = mmap_lock;
    return region;
  }

  /* No region found, release lock */
  lock_release(mmap_lock);
  *lock_out = NULL;
  return NULL;
}

struct mmap_region* mmap_find_region(void* addr) {
  struct lock* held_lock;
  struct mmap_region* region = mmap_find_region_hold_lock(addr, &held_lock);
  if (held_lock != NULL) {
    lock_release(held_lock);
  }
  return region;
}

/* Check if address range is available for mmap.
   NOTE: Caller must hold mmap_lock to prevent TOCTOU races. */
static bool mmap_range_available(void* addr, size_t length) {
  /* Check basic validity */
  if (!is_user_vaddr(addr)) {
    return false;
  }

  /* Check that addr + length doesn't overflow and stays in user space */
  uintptr_t addr_val = (uintptr_t)addr;
  uintptr_t end_val = addr_val + length;

  /* Check for overflow: if addition wrapped around, end_val < addr_val */
  if (end_val < addr_val) {
    return false;
  }

  /* Check that end address is still in user space */
  if ((void*)end_val >= PHYS_BASE) {
    return false;
  }

  /* Check for SPT conflicts.
     NOTE: We don't hold spt_lock here because:
     1. mmap_lock prevents concurrent mmaps to the same address range
     2. Stack growth only happens for the current thread
     3. The subsequent page creation loop holds spt_lock for the insert */
  struct spt* spt = get_spt();
  size_t page_count = DIV_ROUND_UP(length, PGSIZE);

  for (size_t i = 0; i < page_count; i++) {
    void* upage = (uint8_t*)addr + i * PGSIZE;
    if (spt_find(spt, upage) != NULL) {
      return false; /* Page already has an SPT entry */
    }
  }

  /* Check stack region */
  /* Stack region: [PHYS_BASE - MAX_STACK_SIZE, PHYS_BASE) */
  size_t max_stack_size = MAX_STACK_PAGES * PGSIZE;
  uint8_t* stack_start = (uint8_t*)PHYS_BASE - max_stack_size;
  void* stack_end = PHYS_BASE;

  /* Mmap range: [addr, addr + length) */
  /* Check if ranges overlap: mmap_start < stack_end AND mmap_end > stack_start */
  if (addr < stack_end && (uint8_t*)addr + length > stack_start) {
    return false; /* Range overlaps with stack region */
  }

  /* All checks passed */
  return true;
}
