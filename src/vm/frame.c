/*
 * ============================================================================
 *                           FRAME TABLE
 * ============================================================================
 *
 * IMPLEMENTATION NOTES:
 * ---------------------
 * This file implements the frame table for tracking physical page allocations.
 * The frame table wraps palloc to add ownership tracking and enable eviction.
 *
 * DATA STRUCTURE:
 * ---------------
 * - Global list of frame_entry structs (one per allocated frame)
 * - Global lock for thread-safe access
 * - Clock hand pointer for eviction algorithm
 *
 * EVICTION ALGORITHM (Clock / Second-Chance):
 * -------------------------------------------
 * When memory is full and a new frame is needed:
 * 1. Start at clock_hand position
 * 2. For each frame:
 *    - Skip if pinned
 *    - If accessed bit set: clear it, give second chance
 *    - If accessed bit clear: evict this frame
 * 3. Handle dirty pages:
 *    - Mmap pages (writable file-backed): write back to file
 *    - Other dirty pages: write to swap
 * 4. Update owner's SPT to reflect new page location
 *
 * SYNCHRONIZATION:
 * ----------------
 * All operations acquire frame_lock to ensure thread safety.
 * Lock ordering: frame_lock is acquired before any page directory operations.
 *
 * ============================================================================
 */

#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * GLOBAL FRAME TABLE STATE
 * ============================================================================
 *
 * These static variables form the global frame table, shared by all processes.
 */

/* List of all allocated frames (frame_entry structs). */
static struct list frame_list;

/* Lock protecting all frame table operations. */
static struct lock frame_lock;

/* Clock hand for eviction algorithm.
   Points to the next frame to consider for eviction.
   NULL means start from beginning of list. */
static struct list_elem* clock_hand;

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/* Advance clock hand to next element, wrapping around if needed.
   Must be called with frame_lock held.
   Returns the new clock_hand position. */
static struct list_elem* clock_advance(struct list_elem* current) {
  if (list_empty(&frame_list))
    return NULL;

  struct list_elem* next = list_next(current);
  if (next == list_end(&frame_list))
    next = list_begin(&frame_list);
  return next;
}

/* Find frame entry by kernel page address.
   Must be called with frame_lock held.
   Returns NULL if not found. */
static struct frame_entry* frame_find_entry(void* kpage) {
  struct list_elem* e;
  for (e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)) {
    struct frame_entry* fe = list_entry(e, struct frame_entry, elem);
    if (fe->kpage == kpage)
      return fe;
  }
  return NULL;
}

/* Check if a list element is still in frame_list.
   Must be called with frame_lock held.
   Used to validate iterators after releasing and reacquiring the lock. */
static bool frame_elem_valid(struct list_elem* elem) {
  struct list_elem* e;
  for (e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)) {
    if (e == elem)
      return true;
  }
  return false;
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/* Initialize the frame table.
   Called once during vm_init(). */
void frame_init(void) {
  list_init(&frame_list);
  lock_init(&frame_lock);
  clock_hand = NULL;
}

/* ============================================================================
 * FRAME ALLOCATION
 * ============================================================================ */

/* Allocate a frame for user virtual address UPAGE.

   This function:
   1. Tries to get a free page from the user pool
   2. If no free pages, triggers eviction to reclaim one
   3. Creates a frame_entry to track the allocation
   4. Returns the kernel virtual address of the frame

   The frame starts PINNED to prevent eviction during page fault handling.
   Caller must call frame_unpin() when done setting up the page.

   Returns NULL if allocation fails (no memory and all frames pinned). */
void* frame_alloc(void* upage, bool writable UNUSED) {
  void* kpage;
  struct frame_entry* fe;

  /* Try to allocate from user pool. */
  kpage = palloc_get_page(PAL_USER | PAL_ZERO);

  /* If no free pages, try to evict one. */
  if (kpage == NULL) {
    kpage = frame_evict();
    if (kpage == NULL)
      return NULL; /* All frames pinned, cannot evict. */
    /* Zero the reclaimed frame. */
    memset(kpage, 0, PGSIZE);
  }

  /* Create frame table entry. */
  fe = (struct frame_entry*)malloc(sizeof(struct frame_entry));
  if (fe == NULL) {
    palloc_free_page(kpage);
    return NULL;
  }

  /* Initialize entry fields. */
  fe->kpage = kpage;
  fe->upage = upage;
  fe->owner = thread_current();
  fe->ref_count = 1;
  fe->pinned = true;         /* Pin until caller is done setting up. */
  fe->evict_callback = NULL; /* Use default eviction logic. */

  /* Add to frame list. */
  lock_acquire(&frame_lock);
  list_push_back(&frame_list, &fe->elem);
  lock_release(&frame_lock);

  return kpage;
}

/* Register an already-allocated page with the frame table.

   Used when a page was allocated via palloc_get_page directly (e.g., by
   pagedir_dup during fork) and needs to be tracked by the frame table.

   Returns true if registration succeeded, false on failure. */
bool frame_register(void* kpage, void* upage, struct thread* owner) {
  if (kpage == NULL)
    return false;

  /* Check if already registered. */
  lock_acquire(&frame_lock);
  struct frame_entry* existing = frame_find_entry(kpage);
  if (existing != NULL) {
    lock_release(&frame_lock);
    return false; /* Already registered. */
  }
  lock_release(&frame_lock);

  /* Create frame table entry. */
  struct frame_entry* fe = (struct frame_entry*)malloc(sizeof(struct frame_entry));
  if (fe == NULL)
    return false;

  /* Initialize entry fields. */
  fe->kpage = kpage;
  fe->upage = upage;
  fe->owner = owner;
  fe->ref_count = 1;
  fe->pinned = true;         /* Start pinned like frame_alloc. */
  fe->evict_callback = NULL; /* Use default eviction logic. */

  /* Add to frame list. */
  lock_acquire(&frame_lock);
  list_push_back(&frame_list, &fe->elem);
  lock_release(&frame_lock);

  return true;
}

/* =========================================================================
 * frame.c - Frame Table
 *
 * The frame table keeps track of all physical frames allocated to
 * user processes. Each frame is represented by a frame_entry struct,
 * which records information such as the kernel virtual address (kpage),
 * the associated user page (upage), the owning thread, the reference
 * count (for COW/shared frames), and whether the frame is pinned.
 *
 * Main functions:
 * - frame_alloc(): Allocates a physical frame, inserts it into the table.
 * - frame_register(): Registers a frame allocated outside of frame_alloc.
 * - frame_share(): Increases the reference count of a frame (for COW).
 * - frame_free(): Decrements reference count, frees the frame if no longer used.
 *
 * Synchronization:
 *   The frame table is protected by the frame_lock, which must be
 *   acquired before modifying the table.
 * ========================================================================= */

void frame_share(void* kpage) {
  if (kpage == NULL)
    return;

  lock_acquire(&frame_lock);
  struct frame_entry* fe = frame_find_entry(kpage);
  if (fe != NULL)
    fe->ref_count++;
  lock_release(&frame_lock);
}

/* Free a frame and return it to the pool.

   This function:
   1. Finds and removes the frame_entry from the table
   2. Updates clock_hand if it pointed to this entry
   3. Frees the frame_entry struct
   4. Returns the physical page to palloc

   Does nothing if kpage is not in the frame table. */
void frame_free(void* kpage) {
  if (kpage == NULL)
    return;

  lock_acquire(&frame_lock);

  struct frame_entry* fe = frame_find_entry(kpage);
  if (fe == NULL) {
    lock_release(&frame_lock);
    return;
  }

  /* Update ref_count. */
  fe->ref_count--;
  if (fe->ref_count > 0) {
    /* Frame still has other references (future: shared pages). */
    lock_release(&frame_lock);
    return;
  }

  /* Update clock hand if it points to this entry. */
  if (clock_hand == &fe->elem)
    clock_hand = clock_advance(&fe->elem);

  /* Handle edge case: if clock_hand now points back to the entry we're
     removing (list had only one element), set it to NULL. */
  if (clock_hand == &fe->elem)
    clock_hand = NULL;

  /* Remove from list. */
  list_remove(&fe->elem);

  lock_release(&frame_lock);

  /* Free entry struct. */
  free(fe);

  /* Return page to palloc. */
  palloc_free_page(kpage);
}

/* ============================================================================
 * FRAME PINNING
 * ============================================================================ */

/* Pin a frame to prevent eviction.
   Used when kernel is actively accessing user memory. */
void frame_pin(void* kpage) {
  lock_acquire(&frame_lock);
  struct frame_entry* fe = frame_find_entry(kpage);
  if (fe != NULL)
    fe->pinned = true;
  lock_release(&frame_lock);
}

/* Unpin a frame to allow eviction.
   Called when kernel is done accessing user memory. */
void frame_unpin(void* kpage) {
  lock_acquire(&frame_lock);
  struct frame_entry* fe = frame_find_entry(kpage);
  if (fe != NULL)
    fe->pinned = false;
  lock_release(&frame_lock);
}

/* Set eviction callback for a frame.
   Allows custom eviction handling instead of default logic. */
void frame_set_evict_callback(void* kpage, frame_evict_callback_fn callback) {
  lock_acquire(&frame_lock);
  struct frame_entry* fe = frame_find_entry(kpage);
  if (fe != NULL)
    fe->evict_callback = callback;
  lock_release(&frame_lock);
}

/* ============================================================================
 * FRAME LOOKUP
 * ============================================================================ */

/* Find frame table entry for kernel page KPAGE.
   Returns pointer to frame_entry, or NULL if not found.

   Note: The returned pointer is valid only while frame_lock is not
   held by another operation that might free the entry. */
void* frame_lookup(void* kpage) {
  lock_acquire(&frame_lock);
  struct frame_entry* fe = frame_find_entry(kpage);
  lock_release(&frame_lock);
  return fe;
}

/* ============================================================================
 * EVICTION (CLOCK ALGORITHM)
 * ============================================================================ */

/* Evict a frame and return its kernel virtual address.

   Uses the clock (second-chance) algorithm:
   1. Start at clock_hand position
   2. Skip pinned frames
   3. If accessed bit set: clear it, move on (second chance)
   4. If accessed bit clear: evict this frame

   When evicting:
   - Write dirty pages to swap
   - Update owner's SPT entry to reflect PAGE_SWAP status
   - Clear the page table entry
   - Remove from frame table

   Returns NULL if all frames are pinned (cannot evict). */
void* frame_evict(void) {
  lock_acquire(&frame_lock);

  /* Handle empty list. */
  if (list_empty(&frame_list)) {
    lock_release(&frame_lock);
    return NULL;
  }

  /* Initialize clock hand if needed. */
  if (clock_hand == NULL)
    clock_hand = list_begin(&frame_list);

  struct list_elem* start = clock_hand;
  struct list_elem* e = start;

  /* We may need to go around twice: first to give second chances, second to evict. */
  int max_iterations = 2 * list_size(&frame_list);
  int iterations = 0;

  /* Clock algorithm: scan frames looking for eviction candidate. */
  while (iterations < max_iterations) {
    iterations++;
    struct frame_entry* fe = list_entry(e, struct frame_entry, elem);

    /* Skip pinned frames. */
    if (fe->pinned) {
      e = clock_advance(e);
      continue;
    }

    /* Skip shared frames (COW pages with multiple references).
       We can't safely evict these because we only track one owner,
       but multiple processes have SPT entries pointing to this frame.
       Evicting would leave other sharers with dangling kpage pointers. */
    if (fe->ref_count > 1) {
      e = clock_advance(e);
      continue;
    }

    /* Check if owner's PCB is still valid. */
    if (fe->owner == NULL || fe->owner->pcb == NULL || fe->owner->pcb->pagedir == NULL) {
      /* Owner is gone, reclaim this frame directly. */
      void* kpage = fe->kpage;
      clock_hand = clock_advance(e);
      if (clock_hand == e)
        clock_hand = NULL;
      list_remove(e);
      lock_release(&frame_lock);
      free(fe);
      return kpage;
    }

    uint32_t* pd = fe->owner->pcb->pagedir;

    /* Check accessed bit. */
    bool accessed = pagedir_is_accessed(pd, fe->upage);

    if (accessed) {
      /* Give second chance: clear accessed bit. */
      pagedir_set_accessed(pd, fe->upage, false);
      e = clock_advance(e);
      continue;
    }

    /* Found victim: not pinned and not recently accessed. */

    /* Save frame info before modifying. */
    void* kpage = fe->kpage;
    void* upage = fe->upage;
    struct thread* owner = fe->owner;
    frame_evict_callback_fn callback = fe->evict_callback;

    /* If a custom eviction callback is registered, use it instead of
       the default logic. This allows for dependency inversion - the
       owner can handle their own eviction without frame.c needing to
       know about SPT, pagedir, etc. */
    if (callback != NULL) {
      /* Release lock before callback to avoid deadlock. */
      lock_release(&frame_lock);

      bool success = callback(owner, upage, kpage);
      lock_acquire(&frame_lock);

      /* Validate iterator is still valid after lock was released.
         Another thread may have modified the frame list. */
      if (!frame_elem_valid(e)) {
        /* Iterator invalidated, restart from clock_hand. */
        if (clock_hand == NULL || list_empty(&frame_list)) {
          lock_release(&frame_lock);
          return NULL;
        }
        e = clock_hand;
        start = e;
        continue;
      }

      if (!success) {
        /* Callback failed (e.g., swap full). Try next frame. */
        e = clock_advance(e);
        if (e == start) {
          lock_release(&frame_lock);
          return NULL;
        }
        continue;
      }

      /* Callback succeeded - remove entry and return frame. */
      clock_hand = clock_advance(e);
      if (clock_hand == e)
        clock_hand = NULL;
      list_remove(e);
      lock_release(&frame_lock);
      free(fe);
      return kpage;
    }

    /* Default eviction logic (no callback registered). */

    /* Flush TLB before checking dirty bit.
       The CPU may cache the dirty bit in the TLB and not write it to
       the page table in memory immediately. Reloading CR3 flushes the TLB,
       forcing any cached dirty bits to be written back to memory.
       This ensures pagedir_is_dirty() returns the correct value.

       We need to temporarily switch to the owner's page directory to flush
       the right TLB entries, then restore the original page directory. */
    uint32_t* orig_pd = active_pd();
    pagedir_activate(pd);
    pagedir_activate(orig_pd);

    /* Get SPT entry to determine how to handle eviction.
       Acquire SPT lock to prevent concurrent modifications. */
    struct spt_entry* spte = NULL;
    struct spt* spt = NULL;
    if (owner->pcb != NULL) {
      spt = &owner->pcb->spt;
      lock_acquire(&spt->spt_lock);
      spte = spt_find(spt, upage);
    }

    /* Check if dirty - need to write back data.
       We check both the hardware dirty bit AND our software pinned_dirty flag.
       The hardware bit can be unreliable due to TLB caching, but pinned_dirty
       is set when loading from swap and guarantees we preserve the data. */
    bool dirty = pagedir_is_dirty(pd, upage);
    if (spte != NULL && spte->pinned_dirty) {
      dirty = true;
    }

    if (dirty) {
      /* Check if this is an mmap page (writable file-backed).
         Only mmap pages should be written back to file.
         Executable segment pages (is_mmap=false) go to swap. */
      if (spte != NULL && spte->file != NULL && spte->writable && spte->is_mmap) {
        /* Mmap page: try to write back to file. */
        off_t written = file_write_at(spte->file, kpage, spte->read_bytes, spte->file_offset);
        if (written == (off_t)spte->read_bytes) {
          /* Write succeeded, can reload from file later. */
          spte->status = PAGE_FILE;
          spte->kpage = NULL;
        } else {
          /* Write failed, fall back to swap. */
          size_t swap_slot = swap_out(kpage);
          if (swap_slot == SWAP_SLOT_INVALID) {
            /* Both file write and swap failed. Skip this frame. */
            if (spt != NULL)
              lock_release(&spt->spt_lock);
            e = clock_advance(e);
            if (e == start) {
              lock_release(&frame_lock);
              return NULL;
            }
            continue;
          }
          spte->status = PAGE_SWAP;
          spte->swap_slot = swap_slot;
          spte->kpage = NULL;
          spte->pinned_dirty = true;
        }
      } else {
        /* Other dirty pages: write to swap. */
        size_t swap_slot = swap_out(kpage);
        if (swap_slot == SWAP_SLOT_INVALID) {
          /* Swap is full, try next frame. */
          if (spt != NULL)
            lock_release(&spt->spt_lock);
          e = clock_advance(e);
          if (e == start) {
            lock_release(&frame_lock);
            return NULL;
          }
          continue;
        }

        /* Update SPT entry to reflect page is now in swap. */
        if (spte != NULL) {
          spte->status = PAGE_SWAP;
          spte->swap_slot = swap_slot;
          spte->kpage = NULL;
          /* Set pinned_dirty so that when loaded from swap and evicted again,
             we always write back to swap even if HW dirty bit is wrong. */
          spte->pinned_dirty = true;
        }
      }
    } else {
      /* Not dirty - just update SPT. */
      if (spte != NULL) {
        if (spte->file != NULL) {
          /* File-backed: can reload from file. */
          spte->status = PAGE_FILE;
        } else {
          /* Zero page: can just re-zero. */
          spte->status = PAGE_ZERO;
        }
        spte->kpage = NULL;
      }
    }

    /* Release SPT lock after modifications. */
    if (spt != NULL)
      lock_release(&spt->spt_lock);

    /* Clear the page table entry. */
    pagedir_clear_page(pd, upage);

    /* Update clock hand. */
    clock_hand = clock_advance(e);
    if (clock_hand == e)
      clock_hand = NULL;

    /* Remove from frame table. */
    list_remove(e);

    lock_release(&frame_lock);

    /* Free the entry struct. */
    free(fe);

    /* Return the reclaimed frame. */
    return kpage;
  }

  /* Exhausted max iterations - all frames pinned or all accessed even after second chance. */
  lock_release(&frame_lock);
  return NULL;
}
