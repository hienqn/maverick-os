/*
 * ============================================================================
 *                           SWAP SPACE
 * ============================================================================
 *
 * Swap space provides secondary storage for evicted pages.
 * When a page is evicted from memory, its contents are written to a swap
 * slot on disk. When the page is needed again, it's read back from swap.
 *
 * SWAP SLOT:
 * ----------
 * A swap slot is a contiguous region of disk that holds one page (4KB).
 * Since disk blocks are 512 bytes, each slot uses 8 consecutive sectors.
 *
 * SWAP BITMAP:
 * ------------
 * A bitmap tracks which slots are in use. When a page is swapped out,
 * we find a free slot. When swapped in or freed, we mark the slot free.
 *
 * SWAP DEVICE:
 * ------------
 * Pintos uses a separate block device for swap, accessed via block_get_role().
 * The swap partition is optional - if not present, eviction fails.
 *
 * ============================================================================
 */

#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include <stddef.h>

/* Sectors per page (4096 / 512 = 8). */
#define SECTORS_PER_PAGE 8

/* Invalid swap slot indicator. */
#define SWAP_SLOT_INVALID ((size_t)-1)

/* ============================================================================
 * SWAP TABLE STRUCTURE
 * ============================================================================
 *
 * The swap table uses global variables (defined in swap.c):
 *   - swap_block:  Block device for swap partition
 *   - swap_bitmap: Bitmap tracking which slots are in use
 *   - swap_lock:   Lock protecting all swap operations
 *
 * This is a global (not per-process) resource since swap slots are
 * a system-wide pool of secondary storage.
 */

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/* Initialize the swap subsystem.
   Called once during vm_init().
   Finds the swap block device and initializes the slot bitmap. */
void swap_init(void);

/* ============================================================================
 * SWAP OPERATIONS
 * ============================================================================ */

/* Swap out a page to disk.
   KPAGE: Kernel virtual address of page to swap out.
   Returns the swap slot index, or SWAP_SLOT_INVALID on failure. */
size_t swap_out(void* kpage);

/* Swap in a page from disk.
   SLOT: Swap slot index to read from.
   KPAGE: Kernel virtual address of destination page.
   The slot is automatically freed after reading. */
void swap_in(size_t slot, void* kpage);

/* Free a swap slot without reading its contents.
   Used when a page is freed while swapped out. */
void swap_free(size_t slot);

/* ============================================================================
 * STATISTICS (Optional)
 * ============================================================================ */

/* Get number of swap slots in use. */
size_t swap_used_slots(void);

/* Get total number of swap slots. */
size_t swap_total_slots(void);

#endif /* vm/swap.h */
