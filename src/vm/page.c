/*
 * ============================================================================
 *                    SUPPLEMENTAL PAGE TABLE (SPT)
 * ============================================================================
 *
 * IMPLEMENTATION NOTES:
 * ---------------------
 * This file implements the Supplemental Page Table (SPT) for demand paging.
 * The SPT uses a hash table to efficiently track virtual page metadata.
 *
 * HASH TABLE DESIGN:
 * ------------------
 * - Key: User virtual address (page-aligned, void*)
 * - Hash function: hash_bytes() on the address value
 * - Comparison: Direct pointer comparison (addresses are unique)
 * - Average lookup time: O(1)
 *
 * MEMORY MANAGEMENT:
 * ------------------
 * - SPT entries are allocated with malloc() when created
 * - Entries are freed automatically by spt_destroy() or spt_remove()
 * - Resources (frames, swap slots) are freed when entries are destroyed
 * - Files are NOT closed by SPT (managed by process or mmap)
 *
 * ERROR HANDLING:
 * --------------
 * - All functions return false/NULL on failure
 * - Resources are cleaned up on error (no leaks)
 * - Frame allocation failures are propagated to caller
 * - File read failures cause spt_load_page() to fail
 *
 * THREAD SAFETY:
 * --------------
 * - SPT is per-process (no cross-process sharing)
 * - No locking required for single-threaded process operations
 * - Frame allocation may block (eviction can take time)
 * - File operations are synchronized by filesystem layer
 *
 * ============================================================================
 */

#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * HASH TABLE HELPERS
 * ============================================================================
 *
 * These functions are callbacks used by the hash table implementation to:
 * - Compute hash values for entries
 * - Compare entries for ordering
 * - Clean up entries when the table is destroyed
 */

/* Hash function for SPT entries.
   
   Computes a hash value based on the user virtual address. Uses hash_bytes()
   which implements the Fowler-Noll-Vo hash algorithm for good distribution.
   
   @param e Hash element (embedded in spt_entry)
   @param aux Auxiliary data (unused)
   @return Hash value for the entry
   
   The hash is computed over the entire pointer value (typically 4 bytes on
   32-bit systems), ensuring different pages hash to different buckets. */
static unsigned spt_hash_func(const struct hash_elem* e, void* aux UNUSED) {
  struct spt_entry* entry = hash_entry(e, struct spt_entry, hash_elem);
  return hash_bytes(&entry->upage, sizeof(entry->upage));
}

/* Less function for SPT entries.
   
   Compares two entries by their user virtual addresses. Used by the hash
   table to maintain ordering within buckets and detect duplicates.
   
   @param a First hash element
   @param b Second hash element
   @param aux Auxiliary data (unused)
   @return true if a->upage < b->upage, false otherwise
   
   Since virtual addresses are unique per page, this provides a total
   ordering that allows efficient duplicate detection. */
static bool spt_less_func(const struct hash_elem* a, const struct hash_elem* b, void* aux UNUSED) {
  struct spt_entry* entry_a = hash_entry(a, struct spt_entry, hash_elem);
  struct spt_entry* entry_b = hash_entry(b, struct spt_entry, hash_elem);
  return entry_a->upage < entry_b->upage;
}

/* Destroy function for SPT entries.
   
   Called automatically by hash_destroy() for each entry in the table.
   Frees all resources associated with the entry before freeing the entry
   structure itself.
   
   @param e Hash element (embedded in spt_entry)
   @param aux Auxiliary data (unused)
   
   Resource cleanup by status:
   - PAGE_FRAME: Frees the physical frame (returns it to frame table)
   - PAGE_SWAP:  Frees the swap slot (marks it available)
   - PAGE_FILE:  No cleanup (file managed by process/mmap)
   - PAGE_ZERO:  No cleanup (no resources allocated)
   
   After freeing resources, the entry structure itself is freed with free(). */
static void spt_destroy_func(struct hash_elem* e, void* aux UNUSED) {
  struct spt_entry* entry = hash_entry(e, struct spt_entry, hash_elem);

  /* Free resources based on page status. */
  if (entry->status == PAGE_FRAME) {
    /* Free the frame. */
    frame_free(entry->kpage);
  } else if (entry->status == PAGE_SWAP) {
    /* Free the swap slot. */
    swap_free(entry->swap_slot);
  }
  /* For PAGE_FILE, the file is managed elsewhere (process or mmap). */
  /* For PAGE_ZERO, no resources to free. */

  /* Free the entry itself. */
  free(entry);
}

/* ============================================================================
 * SPT LIFECYCLE
 * ============================================================================ */

/* Initialize a supplemental page table.
   
   Sets up the hash table with the appropriate hash and comparison functions.
   The hash table starts with a small number of buckets and grows automatically
   as entries are added.
   
   Implementation: Simply calls hash_init() with our helper functions.
   The hash table will be empty after initialization. */
void spt_init(void* spt) {
  struct spt* s = (struct spt*)spt;
  hash_init(&s->pages, spt_hash_func, spt_less_func, NULL);
  lock_init(&s->spt_lock);
}

/* Destroy a supplemental page table and free all entries.

   Iterates through all entries in the hash table and calls spt_destroy_func()
   for each one, which frees associated resources (frames, swap slots) and
   the entry structure itself. Then destroys the hash table structure.

   Implementation: Calls hash_destroy() which automatically calls our
   destroy function for each entry, ensuring proper cleanup of all resources.

   SYNCHRONIZATION: Must hold spt_lock during destruction to prevent
   frame_evict() from accessing the hash table while we're freeing entries.

   After this call, the SPT should not be used again unless re-initialized. */
void spt_destroy(void* spt) {
  struct spt* s = (struct spt*)spt;
  lock_acquire(&s->spt_lock);
  hash_destroy(&s->pages, spt_destroy_func);
  lock_release(&s->spt_lock);
}

/* ============================================================================
 * SPT OPERATIONS
 * ============================================================================ */

/* Find the SPT entry for UPAGE.
   
   Implementation:
   1. Round down upage to page boundary (ensures we find the right entry)
   2. Create a temporary key entry with the rounded address
   3. Use hash_find() to locate the entry in O(1) average time
   4. Convert hash_elem back to spt_entry using hash_entry() macro
   
   The rounding is important because addresses within a page should all
   map to the same entry (one entry per page, not per byte). */
void* spt_find(void* spt, void* upage) {
  struct spt* s = (struct spt*)spt;
  struct spt_entry key;
  struct hash_elem* e;

  key.upage = pg_round_down(upage);
  e = hash_find(&s->pages, &key.hash_elem);
  return e ? hash_entry(e, struct spt_entry, hash_elem) : NULL;
}

/* Insert a new SPT entry.
   
   Implementation:
   1. Call hash_insert() which checks for duplicates
   2. If duplicate exists, hash_insert() returns the old element
   3. Return true only if no duplicate (old == NULL)
   
   The entry must have entry->upage set and page-aligned. The entry
   structure must be allocated with malloc() as it will be freed by
   the SPT when removed or destroyed. */
bool spt_insert(void* spt, void* entry) {
  struct spt* s = (struct spt*)spt;
  struct spt_entry* e = (struct spt_entry*)entry;
  struct hash_elem* old;

  old = hash_insert(&s->pages, &e->hash_elem);
  return old == NULL; /* Success if no duplicate */
}

/* Remove the SPT entry for UPAGE.
   
   Implementation:
   1. Find the entry (using same key-based lookup as spt_find)
   2. If not found, return false
   3. Delete from hash table
   4. Free resources based on status (frame or swap slot)
   5. Free the entry structure
   
   This function performs the same resource cleanup as spt_destroy_func(),
   but is called explicitly rather than during table destruction. */
bool spt_remove(void* spt, void* upage) {
  struct spt* s = (struct spt*)spt;
  struct spt_entry key;
  struct hash_elem* e;
  struct spt_entry* entry;

  key.upage = pg_round_down(upage);
  e = hash_find(&s->pages, &key.hash_elem);
  if (e == NULL)
    return false;

  entry = hash_entry(e, struct spt_entry, hash_elem);
  hash_delete(&s->pages, e);

  /* Free resources based on page status. */
  if (entry->status == PAGE_FRAME) {
    frame_free(entry->kpage);
  } else if (entry->status == PAGE_SWAP) {
    swap_free(entry->swap_slot);
  }

  /* Free the entry itself. */
  free(entry);
  return true;
}

/* ============================================================================
 * PAGE LOADING
 * ============================================================================ */

/* Load a page into memory based on its SPT entry.
   
   This is the core demand paging function. It performs the following steps:
   
   1. FRAME ALLOCATION:
      - Calls frame_alloc() which may trigger eviction if memory is full
      - Frame allocation can block if eviction is needed
      - On failure, returns false immediately
   
   2. DATA LOADING (based on entry->status):
      - PAGE_ZERO:  Zero-fills entire frame (memset)
      - PAGE_FILE:  Seeks to file offset, reads read_bytes, then zero-fills
                    remaining zero_bytes. File must be open and readable.
      - PAGE_SWAP:  Reads entire page from swap slot (swap_in handles this)
      - PAGE_FRAME: Error condition (page already loaded)
   
   3. PAGE TABLE INSTALLATION:
      - Maps user virtual address to kernel virtual address
      - Sets writable permission based on entry->writable
      - On failure, frees frame and returns false
   
   4. ENTRY UPDATE:
      - Updates status to PAGE_FRAME
      - Stores kernel page address for future reference
   
   Error Handling:
   - All error paths free the allocated frame to prevent leaks
   - File read failures are detected by comparing bytes read
   - Page table installation failures are caught and handled
   
   Thread Safety:
   - Must be called from a context with an active page directory
   - Frame allocation may block (eviction can take time)
   - File operations are synchronized by filesystem layer */
bool spt_load_page(void* entry) {
  struct spt_entry* e = (struct spt_entry*)entry;
  void* kpage;
  uint32_t* pd;
  bool success = false;

  /* Get current thread's page directory. */
  struct thread* t = thread_current();
  if (t->pcb == NULL || t->pcb->pagedir == NULL)
    return false;
  pd = t->pcb->pagedir;

  /* Allocate a frame. */
  kpage = frame_alloc(e->upage);
  if (kpage == NULL)
    return false;

  /* Load data based on page status. */
  switch (e->status) {
    case PAGE_ZERO:
      /* Zero the frame. */
      memset(kpage, 0, PGSIZE);
      success = true;
      break;

    case PAGE_FILE:
      /* Read from file into frame. */
      if (e->file != NULL) {
        file_seek(e->file, e->file_offset);
        if (file_read(e->file, kpage, e->read_bytes) == (off_t)e->read_bytes) {
          /* Zero the remaining bytes. */
          memset((uint8_t*)kpage + e->read_bytes, 0, e->zero_bytes);
          success = true;
        }
      }
      break;

    case PAGE_SWAP: {
      /* Read from swap into frame.
         We must mark the page dirty after loading because:
         1. The swap slot is freed after swap_in
         2. If this page is evicted again (even if "clean"), we must
            write it back to swap, not reload from the original source. */
      swap_in(e->swap_slot, kpage);
      success = true;
      break;
    }

    case PAGE_FRAME:
    case PAGE_COW:
      /* Page already loaded - should not happen. */
      frame_free(kpage);
      return false;
  }

  if (!success) {
    frame_free(kpage);
    return false;
  }

  /* Install page in page table. */
  if (!pagedir_set_page(pd, e->upage, kpage, e->writable)) {
    frame_free(kpage);
    return false;
  }

  /* If page came from swap, mark it dirty so it will be written back
     to swap if evicted (we freed the old swap slot, can't reload from there).
     We set both the hardware dirty bit AND our software pinned_dirty flag.
     The hardware bit can be unreliable due to TLB caching, so pinned_dirty
     provides a guaranteed fallback. */
  if (e->status == PAGE_SWAP) {
    pagedir_set_dirty(pd, e->upage, true);
    e->pinned_dirty = true;
  }

  /* Update entry status and kernel page address. */
  e->status = PAGE_FRAME;
  e->kpage = kpage;

  return true;
}

/* ============================================================================
 * LAZY LOADING SUPPORT
 * ============================================================================ */

/* Create an SPT entry for a file-backed page.
   
   Implementation:
   1. Round upage to page boundary (ensures proper alignment)
   2. Allocate new spt_entry structure
   3. Initialize all fields:
      - Core: upage, status=PAGE_FILE, writable
      - File: file pointer, offset, read_bytes, zero_bytes
      - Unused: kpage=NULL, swap_slot=0
   4. Insert into SPT (which takes ownership)
   5. On failure (duplicate or malloc failure), free entry and return false
   
   The file must remain open until the page is loaded. Typically, the file
   is kept open by the process for executable segments, or by mmap for
   memory-mapped files.
   
   Note: read_bytes + zero_bytes should equal PGSIZE, but we don't validate
   this here (caller's responsibility). */
bool spt_create_file_page(void* spt, void* upage, struct file* file, off_t offset,
                          size_t read_bytes, size_t zero_bytes, bool writable) {
  struct spt_entry* entry;

  /* Round down to page boundary. */
  upage = pg_round_down(upage);

  /* Allocate a new spt_entry. */
  entry = (struct spt_entry*)malloc(sizeof(struct spt_entry));
  if (entry == NULL)
    return false;

  /* Initialize entry fields. */
  entry->upage = upage;
  entry->status = PAGE_FILE;
  entry->writable = writable;
  entry->file = file;
  entry->file_offset = offset;
  entry->read_bytes = read_bytes;
  entry->zero_bytes = zero_bytes;

  /* Initialize unused fields. */
  entry->kpage = NULL;
  entry->swap_slot = 0;
  entry->is_mmap = false;      /* Executable pages, not mmap */
  entry->pinned_dirty = false; /* Not loaded from swap */

  /* Insert into SPT. */
  if (!spt_insert(spt, entry)) {
    free(entry);
    return false;
  }

  return true;
}

/* Create an SPT entry for a zero-filled page.
   
   Implementation:
   1. Round upage to page boundary
   2. Allocate new spt_entry structure
   3. Initialize fields:
      - Core: upage, status=PAGE_ZERO, writable
      - All other fields set to NULL/0 (unused for zero pages)
   4. Insert into SPT
   5. On failure, free entry and return false
   
   This is simpler than file pages because no file information is needed.
   The page will be zero-filled when first loaded via spt_load_page(). */
bool spt_create_zero_page(void* spt, void* upage, bool writable) {
  struct spt_entry* entry;

  /* Round down to page boundary. */
  upage = pg_round_down(upage);

  /* Allocate a new spt_entry. */
  entry = (struct spt_entry*)malloc(sizeof(struct spt_entry));
  if (entry == NULL)
    return false;

  /* Initialize entry fields. */
  entry->upage = upage;
  entry->status = PAGE_ZERO;
  entry->writable = writable;

  /* Initialize unused fields. */
  entry->kpage = NULL;
  entry->swap_slot = 0;
  entry->file = NULL;
  entry->file_offset = 0;
  entry->read_bytes = 0;
  entry->zero_bytes = 0;
  entry->is_mmap = false;      /* Not an mmap page */
  entry->pinned_dirty = false; /* Not loaded from swap */

  /* Insert into SPT. */
  if (!spt_insert(spt, entry)) {
    free(entry);
    return false;
  }

  return true;
}

/* ============================================================================
 * FORK SUPPORT
 * ============================================================================ */

/* Helper function to clone a single SPT entry.
   Returns the cloned entry on success, NULL on failure. */
static struct spt_entry* spt_clone_entry(struct spt_entry* parent_entry, uint32_t* child_pagedir,
                                         uint32_t* parent_pagedir) {
  struct spt_entry* child_entry = malloc(sizeof(struct spt_entry));
  if (child_entry == NULL)
    return NULL;

  /* Copy basic fields. */
  child_entry->upage = parent_entry->upage;
  child_entry->writable = parent_entry->writable;
  child_entry->file = parent_entry->file;
  child_entry->file_offset = parent_entry->file_offset;
  child_entry->read_bytes = parent_entry->read_bytes;
  child_entry->zero_bytes = parent_entry->zero_bytes;
  child_entry->is_mmap = parent_entry->is_mmap;
  child_entry->pinned_dirty = parent_entry->pinned_dirty;
  child_entry->kpage = NULL;
  child_entry->swap_slot = 0;

  switch (parent_entry->status) {
    case PAGE_ZERO:
    case PAGE_FILE:
      /* Lazy-loaded pages: just copy the metadata. */
      child_entry->status = parent_entry->status;
      break;

    case PAGE_COW:
    case PAGE_FRAME: {
      /* Page is in memory: share the parent's frame for copy-on-write.
         For PAGE_FRAME: convert to COW sharing.
         For PAGE_COW: add another sharer to existing COW page.

         Steps:
         1. Map parent's kpage into child's page directory (read-only)
         2. Mark parent's page read-only (if not already)
         3. Increment ref_count via frame_share()
         4. Set child entry to PAGE_COW, pointing to same kpage
         Note: Parent's status is updated in spt_clone() after all entries succeed. */

      /* 1. Map parent's frame into child's page directory (read-only). */
      if (!pagedir_set_page(child_pagedir, child_entry->upage, parent_entry->kpage, false)) {
        free(child_entry);
        return NULL;
      }

      /* 2. Mark parent's page read-only for COW. */
      pagedir_set_writable(parent_pagedir, parent_entry->upage, false);

      /* 3. Increment ref_count. */
      frame_share(parent_entry->kpage);

      /* 4. Child points to same kpage with COW status. */
      child_entry->status = PAGE_COW;
      child_entry->kpage = parent_entry->kpage;
      /* Note: child_entry->writable preserves original writability for later COW copy. */
      break;
    }

    case PAGE_SWAP: {
      /* Page is in swap: read into new frame, then swap out to new slot.
         This avoids sharing swap slots between processes. */
      void* temp_kpage = frame_alloc(child_entry->upage);
      if (temp_kpage == NULL) {
        free(child_entry);
        return NULL;
      }

      /* Read parent's swap data into temporary frame.
         NOTE: swap_in frees the parent's slot, so we must handle failures. */
      swap_in(parent_entry->swap_slot, temp_kpage);

      /* The parent's swap slot is now free, so we need to swap parent back out. */
      size_t parent_new_slot = swap_out(temp_kpage);
      if (parent_new_slot == SWAP_SLOT_INVALID) {
        /* Failed to get new slot for parent. Try to restore parent's data. */
        size_t restore_slot = swap_out(temp_kpage);
        if (restore_slot != SWAP_SLOT_INVALID) {
          ((struct spt_entry*)parent_entry)->swap_slot = restore_slot;
        }
        /* If restore also fails, parent data is lost - nothing we can do. */
        frame_free(temp_kpage);
        free(child_entry);
        return NULL;
      }
      /* Update parent's swap slot (cast away const for this special case). */
      ((struct spt_entry*)parent_entry)->swap_slot = parent_new_slot;

      /* Now swap out the child's copy. */
      size_t child_slot = swap_out(temp_kpage);
      frame_free(temp_kpage);

      if (child_slot == SWAP_SLOT_INVALID) {
        /* Child swap failed. Parent is OK (has parent_new_slot). */
        free(child_entry);
        return NULL;
      }

      child_entry->status = PAGE_SWAP;
      child_entry->swap_slot = child_slot;
      break;
    }

    default:
      free(child_entry);
      return NULL;
  }

  return child_entry;
}

/* Clone an SPT from parent to child during fork.

   Two-pass approach for COW safety:
   Pass 1: Create all child entries (parent entries unchanged except page table permissions)
   Pass 2: Mark parent PAGE_FRAME entries as PAGE_COW (only after Pass 1 succeeds)

   This ensures parent state is not corrupted if fork fails partway through.

   SYNCHRONIZATION:
   ----------------
   We must hold parent->spt_lock to prevent eviction from modifying parent entries
   while we iterate. However, we cannot hold the lock when calling frame_alloc
   (PAGE_SWAP case) because that could deadlock with eviction.

   Lock ordering: frame_lock -> spt_lock (eviction uses this order)
   If we held spt_lock and called frame_alloc -> frame_evict -> spt_lock, deadlock!

   Solution:
   - For PAGE_ZERO/PAGE_FILE: No frame_alloc, safe under lock
   - For PAGE_FRAME/PAGE_COW: Pin frame before releasing lock, preventing eviction
   - For PAGE_SWAP: Release lock, do swap operations, re-acquire lock */
bool spt_clone(void* child_spt, void* parent_spt, uint32_t* child_pagedir,
               uint32_t* parent_pagedir) {
  struct spt* parent = (struct spt*)parent_spt;
  struct spt* child = (struct spt*)child_spt;
  struct hash_iterator i;

  /* Pass 1: Create all child entries.
     Hold parent's spt_lock to prevent concurrent eviction from modifying entries. */
  lock_acquire(&parent->spt_lock);

  hash_first(&i, &parent->pages);
  while (hash_next(&i)) {
    struct spt_entry* parent_entry = hash_entry(hash_cur(&i), struct spt_entry, hash_elem);

    /* For PAGE_FRAME/PAGE_COW entries, pin the frame first to prevent eviction.
       This must be done while holding spt_lock so the status doesn't change. */
    void* pinned_kpage = NULL;
    if (parent_entry->status == PAGE_FRAME || parent_entry->status == PAGE_COW) {
      pinned_kpage = parent_entry->kpage;
      frame_pin(pinned_kpage);
    }

    /* For PAGE_SWAP, we need to release lock before calling frame_alloc.
       Save the necessary info first. */
    bool is_swap = (parent_entry->status == PAGE_SWAP);

    if (is_swap) {
      /* Release lock for swap operations that may trigger eviction. */
      lock_release(&parent->spt_lock);
    }

    /* Clone this entry. */
    struct spt_entry* child_entry = spt_clone_entry(parent_entry, child_pagedir, parent_pagedir);

    if (is_swap) {
      /* Re-acquire lock after swap operations. */
      lock_acquire(&parent->spt_lock);
    }

    /* Unpin the frame if we pinned it. */
    if (pinned_kpage != NULL) {
      frame_unpin(pinned_kpage);
    }

    if (child_entry == NULL) {
      lock_release(&parent->spt_lock);
      return false;
    }

    /* Insert into child's SPT. */
    if (!spt_insert(child, child_entry)) {
      /* Failed to insert - free the cloned entry and its resources. */
      if (child_entry->status == PAGE_COW || child_entry->status == PAGE_FRAME)
        frame_free(child_entry->kpage); /* Decrements ref_count */
      else if (child_entry->status == PAGE_SWAP)
        swap_free(child_entry->swap_slot);
      free(child_entry);
      lock_release(&parent->spt_lock);
      return false;
    }
  }

  /* Pass 2: Mark parent PAGE_FRAME entries as PAGE_COW.
     Only runs after all child entries are successfully created.
     Still holding parent->spt_lock from Pass 1. */
  hash_first(&i, &parent->pages);
  while (hash_next(&i)) {
    struct spt_entry* parent_entry = hash_entry(hash_cur(&i), struct spt_entry, hash_elem);
    if (parent_entry->status == PAGE_FRAME) {
      parent_entry->status = PAGE_COW;
    }
    /* PAGE_COW entries stay as PAGE_COW (already shared). */
  }

  lock_release(&parent->spt_lock);
  return true;
}
