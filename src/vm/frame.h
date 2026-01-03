/*
 * ============================================================================
 *                           FRAME TABLE
 * ============================================================================
 *
 * OVERVIEW:
 * ---------
 * The frame table tracks all physical frames allocated to user pages.
 * It is a GLOBAL data structure shared by all processes, enabling:
 *   - Frame allocation with automatic eviction when memory is full
 *   - Finding frames by their kernel virtual address
 *   - Eviction using clock (second-chance) algorithm
 *
 * PURPOSE:
 * --------
 * While palloc.c manages raw physical memory (free vs. used), the frame
 * table adds ownership tracking needed for virtual memory:
 *   - Which process owns each frame?
 *   - What virtual address maps to it?
 *   - Can it be evicted (or is it pinned)?
 *
 * RELATIONSHIP TO PALLOC:
 * -----------------------
 * Frame table WRAPS palloc_get_page(PAL_USER) - it doesn't replace it.
 *   - frame_alloc() calls palloc_get_page() internally
 *   - frame_free() calls palloc_free_page() internally
 *   - Never call palloc directly for user pages - use frame_alloc instead
 *
 * EVICTION:
 * ---------
 * When palloc_get_page() returns NULL (no free memory), the frame table
 * uses the clock algorithm to select a victim frame:
 *   1. Skip pinned frames (in use by kernel)
 *   2. Check accessed bit - if set, clear it and give second chance
 *   3. If not accessed, evict: write to swap if dirty, update SPT
 *
 * SYNCHRONIZATION:
 * ----------------
 * The frame table uses a global lock to protect all operations.
 * This is necessary because:
 *   - Multiple processes may allocate/free frames concurrently
 *   - Eviction must atomically select and remove a victim
 *
 * ============================================================================
 */

#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <list.h>

/* Forward declarations. */
struct thread;

/* ============================================================================
 * FRAME TABLE ENTRY
 * ============================================================================
 *
 * Each entry represents one physical frame (4KB) allocated to a user process.
 * The entry tracks ownership and eviction-related metadata.
 *
 * CURRENT DESIGN (Simple - No Sharing):
 * -------------------------------------
 * Each frame has exactly ONE owner and ONE mapped virtual address.
 * This is sufficient for basic Pintos because:
 *   - fork() does eager copying (no copy-on-write)
 *   - No shared libraries
 *   - No MAP_SHARED mmap support
 *
 * FUTURE EXTENSION (Shared Pages / COW):
 * --------------------------------------
 * To support shared pages (COW, shared libraries, MAP_SHARED), replace
 * the single owner/upage with a list of mappings:
 *
 *   // Replace owner/upage with:
 *   struct list mappings;  // List of frame_mapping structs
 *
 *   struct frame_mapping {
 *       struct thread *owner;
 *       void *upage;
 *       struct list_elem elem;
 *   };
 *
 * Then update:
 *   - frame_alloc(): Create initial mapping, set ref_count = 1
 *   - frame_share(): Add new mapping, increment ref_count
 *   - frame_unmap(): Remove mapping, decrement ref_count, free if 0
 *   - frame_evict(): Update ALL page tables in mappings list
 *
 * LIFECYCLE:
 * ----------
 *   1. Created by frame_alloc() when a frame is allocated
 *   2. May be pinned/unpinned during its lifetime
 *   3. May be evicted (if not pinned) when memory is needed
 *   4. Destroyed by frame_free() or during eviction
 */

struct frame_entry {
  /* ===== Core Identification ===== */

  /* Kernel virtual address of this frame.
     This is the address returned by palloc_get_page().
     Used as the key for frame_lookup(). */
  void* kpage;

  /* ===== Ownership (Single-Owner Model) ===== */
  /* NOTE: For shared pages, replace these two fields with a mappings list. */

  /* User virtual address that maps to this frame.
     Each frame is mapped to exactly one user virtual address.
     Used during eviction to update the process's page table. */
  void* upage;

  /* Process/thread that owns this frame.
     Used during eviction to:
       - Access the owner's page directory (to check/clear accessed bit)
       - Access the owner's SPT (to update page status) */
  struct thread* owner;

  /* ===== Eviction Control ===== */

  /* Reference count for this frame.
     Currently always 1 (single owner). For shared pages, this would
     track the number of mappings. Frame can only be freed when 0. */
  int ref_count;

  /* If true, this frame cannot be evicted.
     Set when:
       - Kernel is actively accessing user memory (during syscall)
       - Frame is being used for I/O operations
     Must be cleared when operation completes. */
  bool pinned;

  /* ===== List Management ===== */

  /* List element for the global frame list.
     Used by the clock algorithm to iterate through frames. */
  struct list_elem elem;
};

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

/* Register an already-allocated page with the frame table.
   Used when a page was allocated via palloc_get_page directly (e.g., by
   pagedir_dup during fork) and needs to be tracked by the frame table.

   KPAGE: Kernel virtual address of the already-allocated page.
   UPAGE: User virtual address that maps to this page.
   OWNER: Thread that owns this frame.

   Returns true if registration succeeded, false if already registered or
   on failure. The frame starts pinned. */
bool frame_register(void* kpage, void* upage, struct thread* owner);

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
