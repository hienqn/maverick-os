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
 * This module provides memory-mapped file support, allowing processes to
 * access file contents directly through virtual memory addresses.
 *
 * FEATURES:
 *   - Lazy loading: pages are loaded from disk on first access (page fault)
 *   - Automatic writeback: dirty pages are written back on unmap or exit
 *   - Per-process isolation: each process has its own mapping list
 *
 * USAGE:
 *
 *   // In syscall handler for SYS_MMAP:
 *   void* result = mmap_create(addr, length, fd, 0);
 *   if (result == MAP_FAILED) { ... handle error ... }
 *
 *   // In syscall handler for SYS_MUNMAP:
 *   struct mmap_region* region = mmap_find_region(addr);
 *   if (region != NULL) {
 *     mmap_destroy(region->start_addr, region->length);
 *   }
 *
 *   // In process_exit() - clean up all mappings:
 *   mmap_destroy_all();  // Call BEFORE spt_destroy()
 *
 * INITIALIZATION:
 *   Before using mmap, the process must have initialized:
 *   - pcb->mmap_list (via list_init)
 *   - pcb->mmap_lock (via lock_init)
 *   - pcb->spt (supplemental page table)
 *
 * THREAD SAFETY:
 *   All public functions are thread-safe. Internal locking via pcb->mmap_lock.
 *
 * ============================================================================
 */

/* Returned by mmap_create on failure. */
#define MAP_FAILED ((void*)-1)

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================
 */

/* Represents a memory-mapped region in a process's address space.
 *
 * Each region maps a contiguous range of virtual addresses to a file.
 * The region tracks all information needed for lazy loading and writeback.
 */
struct mmap_region {
  /* Address range */
  void* start_addr;  /* Page-aligned starting virtual address. */
  size_t length;     /* Requested length in bytes (may not be page-aligned). */
  size_t page_count; /* Number of pages: DIV_ROUND_UP(length, PGSIZE). */

  /* File backing (NULL for anonymous mappings) */
  struct file* file; /* Private file handle (from file_reopen). */
  off_t offset;      /* Starting offset within the file. */

  /* Flags */
  int flags;         /* MAP_ANONYMOUS, MAP_PRIVATE, etc. */
  bool is_anonymous; /* True if no file backing (MAP_ANONYMOUS). */

  /* Metadata */
  block_sector_t inode_sector; /* Inode sector (for future page sharing). */
  bool writable;               /* Write permission for this mapping. */

  struct list_elem elem; /* Element in process's mmap_list. */
};

/* ============================================================================
 * PUBLIC API
 * ============================================================================
 */

/* Create a new memory mapping.
 *
 * Maps a file into the process's virtual address space. The mapping uses
 * lazy loading - pages are not read from disk until first accessed.
 *
 * @param addr    Virtual address for mapping (must be page-aligned, non-NULL).
 * @param length  Bytes to map (rounded up to page boundary internally).
 * @param fd      Open file descriptor (must be a regular file, not stdin/stdout).
 * @param offset  Starting offset in file (must be page-aligned).
 *
 * @return The mapped address on success, MAP_FAILED on error.
 *
 * Fails if:
 *   - addr is NULL or not page-aligned
 *   - offset is not page-aligned
 *   - length is 0
 *   - fd is invalid, stdin/stdout, or a directory
 *   - file is empty
 *   - address range overlaps existing mappings or reserved regions (stack)
 */
void* mmap_create(void* addr, size_t length, int fd, off_t offset);

/* Create an anonymous (non-file-backed) memory mapping.
 *
 * Maps zero-filled pages into the process's virtual address space.
 * Pages are allocated lazily on first access (page fault).
 *
 * @param addr    Hint address (NULL to let kernel choose, or page-aligned).
 * @param length  Bytes to map (rounded up to page boundary internally).
 * @param flags   Mapping flags (MAP_PRIVATE | MAP_ANONYMOUS).
 *
 * @return The mapped address on success, MAP_FAILED on error.
 */
void* mmap_create_anon(void* addr, size_t length, int flags);

/* Remove a memory mapping.
 *
 * Unmaps the region and writes back any dirty pages to the file.
 * The addr and length must exactly match the original mmap_create call.
 *
 * @param addr    Starting address of the mapping.
 * @param length  Length of the mapping.
 *
 * @return 0 on success, -1 if no matching mapping exists.
 */
int mmap_destroy(void* addr, size_t length);

/* Remove all memory mappings for the current process.
 *
 * Writes back all dirty pages and frees all mapping resources.
 * Must be called during process exit, BEFORE destroying the SPT.
 */
void mmap_destroy_all(void);

/* Find the mmap_region containing a given address.
 *
 * @param addr  Any virtual address (need not be page-aligned).
 *
 * @return Pointer to the mmap_region, or NULL if addr is not mapped.
 *
 * NOTE: The returned pointer is only valid while the mapping exists.
 *       Do not cache the pointer across operations that might unmap it.
 */
struct mmap_region* mmap_find_region(void* addr);

#endif /* vm/mmap.h */
