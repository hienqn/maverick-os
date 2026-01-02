/*
 * ============================================================================
 *                           SWAP SPACE
 * ============================================================================
 */

#include "vm/swap.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <bitmap.h>
#include <stdio.h>

/* ============================================================================
 * GLOBAL SWAP STATE
 * ============================================================================
 *
 * TODO: Define your global swap state here.
 */

/* Swap block device. NULL if no swap partition. */
static struct block* swap_block;

/* Bitmap tracking which slots are in use. */
static struct bitmap* swap_bitmap;

/* Lock protecting swap operations. */
static struct lock swap_lock;

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/* Initialize the swap subsystem. */
void swap_init(void) {
  /* Get the swap block device. */
  swap_block = block_get_role(BLOCK_SWAP);

  if (swap_block == NULL) {
    /* No swap partition - eviction will fail. */
    printf("Warning: No swap partition detected.\n");
    swap_bitmap = NULL;
    return;
  }

  /* Calculate number of swap slots.
     Each slot holds one page (SECTORS_PER_PAGE sectors). */
  size_t num_slots = block_size(swap_block) / SECTORS_PER_PAGE;

  /* Create bitmap to track slot usage. */
  swap_bitmap = bitmap_create(num_slots);
  if (swap_bitmap == NULL) {
    PANIC("Failed to create swap bitmap");
  }

  /* Initialize lock. */
  lock_init(&swap_lock);

  printf("Swap: %zu slots available (%zu MB)\n", num_slots, num_slots * PGSIZE / (1024 * 1024));
}

/* ============================================================================
 * SWAP OPERATIONS
 * ============================================================================ */

/* Swap out a page to disk. */
size_t swap_out(void* kpage UNUSED) {
  /* Check if swap is available. */
  if (swap_block == NULL || swap_bitmap == NULL) {
    return SWAP_SLOT_INVALID;
  }

  /* TODO: Implement swap out.
   *
   * 1. Acquire lock
   *
   * 2. Find a free slot:
   *    size_t slot = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
   *    if (slot == BITMAP_ERROR) {
   *        lock_release(&swap_lock);
   *        return SWAP_SLOT_INVALID;  // Swap full
   *    }
   *
   * 3. Write page to swap (8 sectors):
   *    block_sector_t sector = slot * SECTORS_PER_PAGE;
   *    for (int i = 0; i < SECTORS_PER_PAGE; i++) {
   *        block_write(swap_block, sector + i,
   *                    kpage + i * BLOCK_SECTOR_SIZE);
   *    }
   *
   * 4. Release lock
   *
   * 5. Return slot index
   */

  return SWAP_SLOT_INVALID;
}

/* Swap in a page from disk. */
void swap_in(size_t slot UNUSED, void* kpage UNUSED) {
  /* Check if swap is available. */
  if (swap_block == NULL || swap_bitmap == NULL) {
    PANIC("swap_in: no swap partition");
  }

  /* TODO: Implement swap in.
   *
   * 1. Acquire lock
   *
   * 2. Verify slot is in use:
   *    ASSERT(bitmap_test(swap_bitmap, slot));
   *
   * 3. Read page from swap (8 sectors):
   *    block_sector_t sector = slot * SECTORS_PER_PAGE;
   *    for (int i = 0; i < SECTORS_PER_PAGE; i++) {
   *        block_read(swap_block, sector + i,
   *                   kpage + i * BLOCK_SECTOR_SIZE);
   *    }
   *
   * 4. Mark slot as free:
   *    bitmap_reset(swap_bitmap, slot);
   *
   * 5. Release lock
   */
}

/* Free a swap slot. */
void swap_free(size_t slot UNUSED) {
  if (swap_block == NULL || swap_bitmap == NULL) {
    return;
  }

  /* TODO: Free the slot.
   *
   * lock_acquire(&swap_lock);
   * ASSERT(bitmap_test(swap_bitmap, slot));
   * bitmap_reset(swap_bitmap, slot);
   * lock_release(&swap_lock);
   */
}

/* ============================================================================
 * STATISTICS
 * ============================================================================ */

/* Get number of swap slots in use. */
size_t swap_used_slots(void) {
  if (swap_bitmap == NULL) {
    return 0;
  }
  return bitmap_count(swap_bitmap, 0, bitmap_size(swap_bitmap), true);
}

/* Get total number of swap slots. */
size_t swap_total_slots(void) {
  if (swap_bitmap == NULL) {
    return 0;
  }
  return bitmap_size(swap_bitmap);
}
