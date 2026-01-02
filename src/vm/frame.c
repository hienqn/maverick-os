/*
 * ============================================================================
 *                           FRAME TABLE
 * ============================================================================
 */

#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include <string.h>

/* ============================================================================
 * GLOBAL FRAME TABLE
 * ============================================================================
 *
 * TODO: Define your global frame table here.
 *
 * static struct frame_table ft;
 *
 * Or if using simpler approach:
 * static struct list frame_list;
 * static struct lock frame_lock;
 * static struct list_elem *clock_hand;
 */

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/* Initialize the frame table. */
void frame_init(void) {
  /* TODO: Initialize frame table data structures.
   *
   * list_init(&frame_list);
   * lock_init(&frame_lock);
   * clock_hand = NULL;
   */
}

/* ============================================================================
 * FRAME ALLOCATION
 * ============================================================================ */

/* Allocate a frame for UPAGE. */
void* frame_alloc(void* upage UNUSED, bool writable UNUSED) {
  /* TODO: Allocate a frame.
   *
   * 1. Try to get a page from user pool:
   *    void *kpage = palloc_get_page(PAL_USER | PAL_ZERO);
   *
   * 2. If no page available, evict:
   *    if (kpage == NULL) {
   *        kpage = frame_evict();
   *        if (kpage == NULL)
   *            return NULL;  // All frames pinned
   *        memset(kpage, 0, PGSIZE);
   *    }
   *
   * 3. Create frame table entry:
   *    struct frame_entry *fe = malloc(sizeof(struct frame_entry));
   *    fe->kpage = kpage;
   *    fe->upage = upage;
   *    fe->owner = thread_current();
   *    fe->pinned = true;  // Pin until page fault handler is done
   *
   * 4. Add to frame table (with lock):
   *    lock_acquire(&frame_lock);
   *    list_push_back(&frame_list, &fe->elem);
   *    lock_release(&frame_lock);
   *
   * 5. Return kernel virtual address
   */
  return NULL;
}

/* Free a frame. */
void frame_free(void* kpage UNUSED) {
  /* TODO: Free a frame.
   *
   * 1. Acquire lock
   * 2. Find frame entry for kpage
   * 3. Remove from frame list
   * 4. Update clock_hand if it points to this entry
   * 5. Release lock
   * 6. Free the frame entry struct
   * 7. Return page to palloc: palloc_free_page(kpage)
   */
}

/* ============================================================================
 * FRAME PINNING
 * ============================================================================ */

/* Pin a frame to prevent eviction. */
void frame_pin(void* kpage UNUSED) {
  /* TODO: Find frame entry and set pinned = true.
   * Remember to use locking.
   */
}

/* Unpin a frame to allow eviction. */
void frame_unpin(void* kpage UNUSED) {
  /* TODO: Find frame entry and set pinned = false.
   * Remember to use locking.
   */
}

/* ============================================================================
 * FRAME LOOKUP
 * ============================================================================ */

/* Find frame table entry for KPAGE. */
void* frame_lookup(void* kpage UNUSED) {
  /* TODO: Search frame table for entry with matching kpage.
   *
   * lock_acquire(&frame_lock);
   * for each entry in frame_list:
   *     if entry->kpage == kpage:
   *         lock_release(&frame_lock);
   *         return entry;
   * lock_release(&frame_lock);
   * return NULL;
   */
  return NULL;
}

/* ============================================================================
 * EVICTION (CLOCK ALGORITHM)
 * ============================================================================ */

/* Evict a frame and return its kernel virtual address. */
void* frame_evict(void) {
  /* TODO: Implement clock (second-chance) algorithm.
   *
   * lock_acquire(&frame_lock);
   *
   * // Find a victim frame
   * struct list_elem *start = clock_hand ? clock_hand : list_begin(&frame_list);
   * struct list_elem *e = start;
   *
   * do {
   *     struct frame_entry *fe = list_entry(e, struct frame_entry, elem);
   *
   *     // Skip pinned frames
   *     if (fe->pinned) {
   *         e = list_next(e);
   *         if (e == list_end(&frame_list))
   *             e = list_begin(&frame_list);
   *         continue;
   *     }
   *
   *     // Check accessed bit
   *     bool accessed = pagedir_is_accessed(fe->owner->pagedir, fe->upage);
   *
   *     if (accessed) {
   *         // Give second chance
   *         pagedir_set_accessed(fe->owner->pagedir, fe->upage, false);
   *     } else {
   *         // Evict this frame
   *         clock_hand = list_next(e);
   *         if (clock_hand == list_end(&frame_list))
   *             clock_hand = list_begin(&frame_list);
   *
   *         void *kpage = fe->kpage;
   *
   *         // Write to swap if dirty
   *         bool dirty = pagedir_is_dirty(fe->owner->pagedir, fe->upage);
   *         if (dirty) {
   *             // TODO: Write to swap or file
   *             // size_t slot = swap_out(kpage);
   *             // Update SPT entry with swap slot
   *         }
   *
   *         // Clear page table entry
   *         pagedir_clear_page(fe->owner->pagedir, fe->upage);
   *
   *         // Remove from frame table
   *         list_remove(e);
   *         free(fe);
   *
   *         lock_release(&frame_lock);
   *         return kpage;
   *     }
   *
   *     e = list_next(e);
   *     if (e == list_end(&frame_list))
   *         e = list_begin(&frame_list);
   *
   * } while (e != start);
   *
   * lock_release(&frame_lock);
   * return NULL;  // All frames are pinned
   */
  return NULL;
}
