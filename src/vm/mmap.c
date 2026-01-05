#include "vm/mmap.h"
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/page.h"

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

/* Get file from file descriptor.
   Returns NULL if fd is invalid. */
static struct file* get_file_from_fd(int fd) {
  struct thread* t = thread_current();
  ASSERT(t->pcb != NULL);

  /* TODO: Implement - look up fd in process's fd_table.
     Return NULL for invalid fd (stdin=0, stdout=1, out of range, not a file). */

  (void)fd; /* Suppress unused warning. */
  return NULL;
}

/* ============================================================================
 * MMAP LIFECYCLE
 * ============================================================================
 */

void* mmap_create(void* addr, size_t length, int fd, off_t offset) {
  /* TODO: Implement mmap_create.

     Steps:
     1. Validate parameters:
        - addr != NULL
        - addr is page-aligned: pg_ofs(addr) == 0
        - offset is page-aligned: pg_ofs(offset) == 0
        - length > 0
        - fd >= 2 (not stdin/stdout)

     2. Get file from fd:
        - Use get_file_from_fd(fd)
        - Return MAP_FAILED if NULL or is a directory

     3. Check file properties:
        - file_length(file) > 0
        - Return MAP_FAILED if empty file

     4. Calculate page count:
        - size_t page_count = DIV_ROUND_UP(length, PGSIZE);

     5. Check address range is available:
        - Use mmap_range_available(addr, page_count * PGSIZE)
        - Check it doesn't overlap with stack (below PHYS_BASE)

     6. Create private file reference:
        - struct file* map_file = file_reopen(file);
        - Return MAP_FAILED if NULL

     7. Allocate mmap_region:
        - malloc(sizeof(struct mmap_region))
        - Fill in all fields including inode_sector for future use:
          region->inode_sector = inode_get_inumber(file_get_inode(map_file));

     8. Create SPT entries for each page:
        for (size_t i = 0; i < page_count; i++) {
          void* upage = addr + i * PGSIZE;
          off_t file_ofs = offset + i * PGSIZE;
          size_t read_bytes = (remaining > PGSIZE) ? PGSIZE : remaining;
          size_t zero_bytes = PGSIZE - read_bytes;

          if (!spt_create_file_page(spt, upage, map_file, file_ofs,
                                     read_bytes, zero_bytes, true)) {
            // Cleanup on failure: remove already-created entries, close file
            ...
            return MAP_FAILED;
          }
          remaining -= read_bytes;
        }

     9. Add region to mmap_list:
        - list_push_back(get_mmap_list(), &region->elem);

     10. Return the mapped address.
  */

  (void)addr;
  (void)length;
  (void)fd;
  (void)offset;

  return MAP_FAILED; /* TODO: Replace with actual implementation. */
}

int mmap_destroy(void* addr, size_t length) {
  /* TODO: Implement mmap_destroy.

     Steps:
     1. Find the region:
        - Use mmap_find_region(addr)
        - Verify region->start_addr == addr and region->length == length
        - Return -1 if not found or mismatch

     2. For each page in the region:
        for (size_t i = 0; i < region->page_count; i++) {
          void* upage = region->start_addr + i * PGSIZE;

          a. Look up SPT entry:
             struct spt_entry* entry = spt_find(spt, upage);

          b. If entry->status == PAGE_FRAME (page is loaded):
             - Check if dirty: pagedir_is_dirty(pd, upage)
             - If dirty: mmap_writeback_page(region, upage)
             - Free the frame: frame_free(entry->kpage)
             - Clear page table entry: pagedir_clear_page(pd, upage)

          c. Remove SPT entry:
             - spt_remove(spt, upage) or just free the entry
        }

     3. Close file reference:
        - file_close(region->file);

     4. Remove from list and free:
        - list_remove(&region->elem);
        - free(region);

     5. Return 0.
  */

  (void)addr;
  (void)length;

  return -1; /* TODO: Replace with actual implementation. */
}

void mmap_destroy_all(void) {
  /* TODO: Implement mmap_destroy_all.

     Called from process_exit() BEFORE spt_destroy().

     Steps:
     1. Get the mmap_list.

     2. While list is not empty:
        - Get first element
        - Call mmap_destroy(region->start_addr, region->length)
        OR iterate and destroy inline (more efficient, avoid repeated lookups)

     Alternative implementation (inline destruction):
     while (!list_empty(mmap_list)) {
       struct list_elem* e = list_pop_front(mmap_list);
       struct mmap_region* region = list_entry(e, struct mmap_region, elem);

       // Writeback dirty pages
       for (size_t i = 0; i < region->page_count; i++) {
         void* upage = region->start_addr + i * PGSIZE;
         mmap_writeback_page(region, upage);
         // Note: SPT entries will be cleaned up by spt_destroy()
       }

       file_close(region->file);
       free(region);
     }
  */
}

/* ============================================================================
 * MMAP UTILITIES
 * ============================================================================
 */

struct mmap_region* mmap_find_region(void* addr) {
  /* TODO: Implement mmap_find_region.

     Steps:
     1. Get the mmap_list.

     2. Iterate through all regions:
        for each region in mmap_list:
          void* start = region->start_addr;
          void* end = start + region->page_count * PGSIZE;
          if (addr >= start && addr < end)
            return region;

     3. Return NULL if not found.
  */

  (void)addr;

  return NULL; /* TODO: Replace with actual implementation. */
}

bool mmap_range_available(void* addr, size_t length) {
  /* TODO: Implement mmap_range_available.

     Steps:
     1. Check basic validity:
        - addr must be in user space: is_user_vaddr(addr)
        - addr + length must not overflow and must be in user space

     2. Check for SPT conflicts:
        - For each page in the range, check if spt_find() returns non-NULL
        - If any page already has an SPT entry, return false

     3. Check stack region:
        - Ensure range doesn't overlap with potential stack area
        - Stack grows down from PHYS_BASE
        - A common approach: reject if range overlaps [PHYS_BASE - MAX_STACK_SIZE, PHYS_BASE)

     4. Return true if all checks pass.
  */

  (void)addr;
  (void)length;

  return false; /* TODO: Replace with actual implementation. */
}

bool mmap_writeback_page(struct mmap_region* region, void* upage) {
  /* TODO: Implement mmap_writeback_page.

     Steps:
     1. Get page directory and SPT.

     2. Look up the SPT entry:
        struct spt_entry* entry = spt_find(spt, upage);
        if (entry == NULL || entry->status != PAGE_FRAME)
          return true;  // Nothing to write back

     3. Check if page is dirty:
        if (!pagedir_is_dirty(pd, upage))
          return true;  // Not modified, nothing to do

     4. Calculate file offset for this page:
        off_t page_offset = region->offset +
                            ((upage - region->start_addr) / PGSIZE) * PGSIZE;

     5. Calculate bytes to write:
        - Usually PGSIZE, but last page may be partial
        size_t write_bytes = ...;

     6. Write to file:
        file_write_at(region->file, entry->kpage, write_bytes, page_offset);

     7. Clear dirty bit (optional, but good practice):
        pagedir_set_dirty(pd, upage, false);

     8. Return true.
  */

  (void)region;
  (void)upage;

  return true; /* TODO: Replace with actual implementation. */
}
