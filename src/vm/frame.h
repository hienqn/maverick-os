/*
 * ============================================================================
 *                           FRAME TABLE
 * ============================================================================
 *
 * The frame table tracks all physical frames allocated to user pages.
 * It enables:
 *   - Frame allocation with automatic eviction when memory is full
 *   - Finding frames by their kernel virtual address
 *   - Eviction using clock (second-chance) algorithm
 *
 * This is a GLOBAL data structure shared by all processes.
 * Synchronization is required for all operations.
 *
 * RELATIONSHIP TO PALLOC:
 * -----------------------
 * Frame table wraps palloc_get_page(PAL_USER) to track allocated frames.
 * Never call palloc directly for user pages - use frame_alloc instead.
 *
 * ============================================================================
 */

#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * FRAME TABLE ENTRY
 * ============================================================================
 *
 * TODO: Define your frame_entry structure here.
 *
 * Suggested fields:
 *   - void *kpage           : Kernel virtual address of frame
 *   - void *upage           : User virtual address mapped to this frame
 *   - struct thread *owner  : Thread/process that owns this frame
 *   - bool pinned           : If true, frame cannot be evicted
 *   - struct list_elem elem : For frame list (if using list)
 *
 * For eviction:
 *   - void *spt_entry       : Pointer to SPT entry for this page
 */

/* TODO: Define struct frame_entry here. */

/* ============================================================================
 * FRAME TABLE STRUCTURE
 * ============================================================================
 *
 * TODO: Define your frame table structure here.
 *
 * Suggested structure:
 *   struct frame_table {
 *       struct list frames;      // List of allocated frames
 *       struct lock lock;        // Protects frame table operations
 *       struct list_elem *clock_hand;  // For clock eviction algorithm
 *   };
 */

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/* Initialize the frame table.
   Called once during vm_init(). */
void frame_init(void);

/* ============================================================================
 * FRAME ALLOCATION
 * ============================================================================ */

/* Allocate a frame for user virtual address UPAGE.
   UPAGE: User virtual address that will map to this frame.
   WRITABLE: Whether the page should be writable.

   If no frames are available, evicts a frame using clock algorithm.
   Returns kernel virtual address of allocated frame, or NULL on failure.

   The frame is automatically tracked in the frame table. */
void* frame_alloc(void* upage, bool writable);

/* Free a frame and remove it from the frame table.
   KPAGE: Kernel virtual address of frame to free. */
void frame_free(void* kpage);

/* ============================================================================
 * FRAME PINNING
 * ============================================================================
 *
 * Pinned frames cannot be evicted. Use this when:
 *   - Kernel is actively accessing user memory (during syscall)
 *   - Frame is being used for I/O
 */

/* Pin a frame to prevent eviction. */
void frame_pin(void* kpage);

/* Unpin a frame to allow eviction. */
void frame_unpin(void* kpage);

/* ============================================================================
 * FRAME LOOKUP
 * ============================================================================ */

/* Find frame table entry for kernel page KPAGE.
   Returns NULL if not found. */
void* frame_lookup(void* kpage);

/* ============================================================================
 * EVICTION
 * ============================================================================
 *
 * Eviction is automatically triggered by frame_alloc when memory is full.
 * The clock (second-chance) algorithm is recommended:
 *
 * 1. Start at clock hand position
 * 2. For each frame:
 *    - Skip if pinned
 *    - If accessed bit set: clear it, move to next
 *    - If accessed bit clear: evict this frame
 * 3. Wrap around if necessary
 *
 * When evicting:
 *   - If dirty, write to swap (or file for mmap pages)
 *   - Update SPT entry status to PAGE_SWAP
 *   - Clear page table entry (pagedir_clear_page)
 *   - Return the freed frame
 */

/* Evict a frame and return its kernel virtual address.
   Called internally by frame_alloc when no free frames.
   Returns NULL if eviction fails (all frames pinned). */
void* frame_evict(void);

#endif /* vm/frame.h */
