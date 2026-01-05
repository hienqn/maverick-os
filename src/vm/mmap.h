#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>
#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/off_t.h"

/* Forward declarations. */
struct file;

/* ============================================================================
 * MEMORY-MAPPED FILES (mmap)
 * ============================================================================
 *
 * Memory-mapped files allow processes to access file contents as if they
 * were in memory. This implementation uses lazy loading via the SPT.
 *
 * Key features:
 *   - Lazy loading: pages loaded on first access (page fault)
 *   - Dirty writeback: modified pages written back on munmap/exit
 *   - Per-process: each process has its own mmap_list
 *
 * Future: Will integrate with address_space for shared file pages.
 * See docs/ADDRESS_SPACE_DESIGN.md for the evolution plan.
 *
 * ============================================================================
 */

/* Return value for failed mmap. */
#define MAP_FAILED ((void*)-1)

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================
 */

/* A memory-mapped region in a process's address space. */
struct mmap_region {
  void* start_addr;  /* Page-aligned starting virtual address. */
  size_t length;     /* Requested length in bytes. */
  size_t page_count; /* Number of pages: DIV_ROUND_UP(length, PGSIZE). */

  struct file* file; /* Private file reference (via file_reopen). */
  off_t offset;      /* Starting offset in file. */

  /* Future: for address_space integration. */
  block_sector_t inode_sector; /* Inode sector for future page cache lookup. */
  bool writable;               /* Mapping permissions (for future COW). */

  struct list_elem elem; /* Element in process's mmap_list. */
};

/* ============================================================================
 * MMAP LIFECYCLE
 * ============================================================================
 */

/* Create a new memory mapping.

   Maps LENGTH bytes from file FD starting at file offset OFFSET
   into the process's virtual address space at ADDR.

   @param addr    Page-aligned virtual address for the mapping.
   @param length  Number of bytes to map (will be rounded up to page boundary).
   @param fd      File descriptor of an open file.
   @param offset  Page-aligned offset within the file.

   @return The mapped address on success, MAP_FAILED on error.

   Validation (return MAP_FAILED if any fail):
   - addr != NULL and page-aligned
   - offset is page-aligned
   - length > 0
   - fd is a valid file (not stdin/stdout, not a directory)
   - file length > 0
   - address range doesn't overlap existing mappings or stack

   Implementation steps:
   1. Validate all parameters
   2. Get file from fd, call file_reopen() for independent reference
   3. Allocate and initialize mmap_region
   4. Create SPT entries for each page (PAGE_FILE status, lazy loading)
   5. Add region to process's mmap_list
   6. Return the mapped address */
void* mmap_create(void* addr, size_t length, int fd, off_t offset);

/* Remove a memory mapping.

   Unmaps the region starting at ADDR with LENGTH bytes.

   @param addr    Starting address of the mapping (must match mmap_create).
   @param length  Length of the mapping.

   @return 0 on success, -1 if no such mapping exists.

   Implementation steps:
   1. Find the mmap_region by address
   2. For each page in the region:
      a. Look up SPT entry
      b. If PAGE_FRAME and dirty: write back to file
      c. Free the frame (if loaded)
      d. Remove SPT entry
   3. Close the file reference
   4. Remove region from list and free it */
int mmap_destroy(void* addr, size_t length);

/* Remove all memory mappings for the current process.

   Called during process_exit(), BEFORE spt_destroy().
   Writes back all dirty pages and frees all resources. */
void mmap_destroy_all(void);

/* ============================================================================
 * MMAP UTILITIES
 * ============================================================================
 */

/* Find the mmap_region containing the given address.

   @param addr  A virtual address (need not be page-aligned).

   @return Pointer to the mmap_region if found, NULL otherwise. */
struct mmap_region* mmap_find_region(void* addr);

/* Check if an address range is available for mapping.

   @param addr    Starting address (must be page-aligned).
   @param length  Length in bytes.

   @return true if the range is free (no SPT entries, not in stack region). */
bool mmap_range_available(void* addr, size_t length);

/* Write back a single page to the file if dirty.

   @param region  The mmap_region containing the page.
   @param upage   The user virtual address of the page.

   @return true if writeback succeeded (or page wasn't dirty). */
bool mmap_writeback_page(struct mmap_region* region, void* upage);

#endif /* vm/mmap.h */
