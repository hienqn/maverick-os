/*
 * ============================================================================
 *                    SUPPLEMENTAL PAGE TABLE (SPT)
 * ============================================================================
 *
 * OVERVIEW:
 * ---------
 * The Supplemental Page Table (SPT) tracks metadata about each virtual page
 * that the hardware page table cannot store. While the hardware page table
 * only stores physical frame mappings and access permissions, the SPT tracks:
 *
 *   - Where is the page's data? (frame, swap, file, or zeros)
 *   - Is the page writable?
 *   - For file-backed pages: which file, offset, and size?
 *   - For swapped pages: which swap slot?
 *
 * Each process has its own SPT instance. The SPT is created when the process
 * starts and destroyed when it exits.
 *
 * PURPOSE:
 * --------
 * The SPT enables demand paging (lazy loading) by allowing the kernel to:
 *   1. Defer page allocation until first access (page fault)
 *   2. Track pages that are swapped out to disk
 *   3. Support memory-mapped files
 *   4. Handle stack growth dynamically
 *
 * DATA STRUCTURE:
 * ---------------
 * This implementation uses a hash table (lib/kernel/hash.h) for O(1) average
 * lookup time. The hash table is keyed by user virtual address (page-aligned).
 * This is optimal for sparse address spaces where most virtual pages are
 * unused.
 *
 * PAGE LIFECYCLE:
 * --------------
 *   1. PAGE_ZERO:  Page created but not yet loaded (lazy allocation)
 *   2. PAGE_FILE:   Page backed by file (executable or mmap)
 *   3. PAGE_SWAP:   Page swapped out to disk
 *   4. PAGE_FRAME:  Page loaded in physical memory
 *
 * INTEGRATION:
 * ------------
 * The SPT works with:
 *   - Frame table (vm/frame.h): Physical frame allocation
 *   - Swap space (vm/swap.h):   Disk storage for evicted pages
 *   - Page directory (userprog/pagedir.h): Hardware page table
 *
 * ============================================================================
 */

#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stddef.h>
#include "filesys/off_t.h"
#include "lib/kernel/hash.h"

/* Forward declarations. */
struct file;

/* ============================================================================
 * PAGE STATUS
 * ============================================================================
 *
 * Each page can be in one of these states, indicating where its data resides:
 */

enum page_status {
  PAGE_ZERO,  /* Page contains all zeros, not yet allocated to a frame.
                 Used for BSS segments and stack growth. */
  PAGE_FRAME, /* Page is currently loaded in a physical frame.
                 The kpage field points to the kernel virtual address. */
  PAGE_SWAP,  /* Page has been swapped out to disk.
                 The swap_slot field contains the swap slot index. */
  PAGE_FILE   /* Page is backed by a file (executable segment or mmap).
                 The file, file_offset, read_bytes, and zero_bytes fields
                 specify how to load the page. */
  ,
  PAGE_COW /* Page is marked for copy-on-write. Shared until a write occurs,
                at which point a private copy is made for the process. */
};

/* ============================================================================
 * SPT ENTRY STRUCTURE
 * ============================================================================
 *
 * Each entry in the SPT represents one page (4KB) of the process's virtual
 * address space. The entry tracks where the page's data is stored and how
 * to load it when needed.
 *
 * FIELD USAGE BY STATUS:
 * ----------------------
 * All entries use: upage, status, writable, hash_elem
 *
 * PAGE_ZERO:  Only core fields used (no additional data needed)
 * PAGE_FRAME: kpage points to the physical frame
 * PAGE_SWAP:  swap_slot contains the swap slot index
 * PAGE_FILE:  file, file_offset, read_bytes, zero_bytes specify file data
 */

struct spt_entry {
  /* User virtual address (page-aligned).
     This is the key used for hash table lookups. */
  void* upage;

  /* Page status - indicates where the page's data is stored.
     Determines which other fields are valid. */
  enum page_status status;

  /* Whether the page is writable.
     Used when installing the page in the hardware page table. */
  bool writable;

  /* For PAGE_FRAME: kernel virtual address of the physical frame.
     NULL for other statuses. */
  void* kpage;

  /* For PAGE_SWAP: swap slot index where the page is stored.
     Unused for other statuses. */
  size_t swap_slot;

  /* For PAGE_FILE: file-backed page information.
     Unused for other statuses. */
  struct file* file; /* File to read from (must remain open) */
  off_t file_offset; /* Byte offset in file where page data starts */
  size_t read_bytes; /* Number of bytes to read from file */
  size_t zero_bytes; /* Number of bytes to zero-fill after read_bytes */

  /* Hash table element for storing in the hash table.
     Used internally by the hash table implementation. */
  struct hash_elem hash_elem;
};

/* ============================================================================
 * SPT STRUCTURE
 * ============================================================================
 *
 * The supplemental page table is a per-process data structure that stores
 * all SPT entries for that process. It is typically embedded in the process
 * control block (PCB).
 *
 * USAGE:
 * ------
 *   struct spt spt;
 *   spt_init(&spt);
 *   // ... use SPT ...
 *   spt_destroy(&spt);
 */

struct spt {
  /* Hash table of spt_entry structures, keyed by user virtual address.
     Provides O(1) average-case lookup time. */
  struct hash pages;
};

/* ============================================================================
 * SPT LIFECYCLE
 * ============================================================================ */

/* Initialize a supplemental page table for a new process.
   
   This function initializes the hash table used to store SPT entries.
   Must be called before any other SPT operations.
   
   @param spt Pointer to an uninitialized struct spt.
   
   @pre spt != NULL
   @post The SPT is ready to accept entries via spt_insert().
   
   Called from process creation (process_execute or similar). */
void spt_init(void* spt);

/* Destroy a supplemental page table and free all entries.
   
   This function:
   - Frees all SPT entries in the table
   - For each entry, frees associated resources (frames, swap slots)
   - Destroys the hash table structure
   
   @param spt Pointer to an initialized struct spt.
   
   @pre spt was initialized with spt_init()
   @post All entries are freed, spt should not be used again.
   
   Called when process exits to clean up all virtual memory resources. */
void spt_destroy(void* spt);

/* ============================================================================
 * SPT OPERATIONS
 * ============================================================================ */

/* Find the SPT entry for user virtual address UPAGE.
   
   Looks up the SPT entry for the page containing the given virtual address.
   The address is automatically rounded down to the page boundary.
   
   @param spt Pointer to an initialized struct spt.
   @param upage User virtual address (need not be page-aligned).
   
   @return Pointer to the spt_entry if found, NULL otherwise.
   
   @pre spt was initialized with spt_init()
   @post Returned pointer is valid until entry is removed or SPT is destroyed.
   
   Used by page fault handler to find page metadata. */
void* spt_find(void* spt, void* upage);

/* Insert a new SPT entry.
   
   Adds an entry to the SPT. The entry must have a unique upage value.
   The entry's upage field must be page-aligned.
   
   @param spt Pointer to an initialized struct spt.
   @param entry Pointer to an allocated spt_entry with upage set.
   
   @return true if insertion succeeded, false if duplicate upage exists.
   
   @pre spt was initialized with spt_init()
   @pre entry->upage is page-aligned
   @pre entry->upage does not already exist in the table
   @post Entry is owned by the SPT and will be freed by spt_destroy()
   
   The entry must be allocated with malloc() and will be freed automatically
   when removed or when the SPT is destroyed. */
bool spt_insert(void* spt, void* entry);

/* Remove the SPT entry for UPAGE.
   
   Removes the entry from the SPT and frees associated resources:
   - If PAGE_FRAME: frees the physical frame
   - If PAGE_SWAP: frees the swap slot
   - Always: frees the entry structure itself
   
   @param spt Pointer to an initialized struct spt.
   @param upage User virtual address (need not be page-aligned).
   
   @return true if entry was found and removed, false if not found.
   
   @pre spt was initialized with spt_init()
   @post Entry and its resources are freed, entry pointer is invalid
   
   Used when unmapping pages (e.g., munmap) or during process cleanup. */
bool spt_remove(void* spt, void* upage);

/* ============================================================================
 * PAGE LOADING
 * ============================================================================ */

/* Load a page into memory based on its SPT entry.
   
   This is the core function for demand paging. It:
   1. Allocates a physical frame (may trigger eviction)
   2. Loads data into the frame based on entry->status:
      - PAGE_ZERO:  Zero-fills the frame
      - PAGE_FILE:  Reads from file into frame, then zero-fills remainder
      - PAGE_SWAP:   Reads from swap slot into frame
      - PAGE_FRAME:  Error (page already loaded)
   3. Installs the page in the hardware page table
   4. Updates entry status to PAGE_FRAME
   
   @param entry Pointer to an spt_entry (typically obtained via spt_find()).
   
   @return true if page was successfully loaded, false on failure.
   
   @pre entry != NULL
   @pre entry->status != PAGE_FRAME (page not already loaded)
   @pre Current thread has an active page directory
   @post If successful: entry->status == PAGE_FRAME, page is accessible
   @post If failed: no resources leaked (frame freed if allocated)
   
   Called by the page fault handler when a page needs to be loaded.
   May block if frame allocation requires eviction. */
bool spt_load_page(void* entry);

/* ============================================================================
 * LAZY LOADING SUPPORT
 * ============================================================================
 *
 * These functions create SPT entries for lazy-loaded pages. Instead of
 * immediately allocating frames and loading data, they create entries that
 * will be loaded on-demand when the page is first accessed (page fault).
 *
 * This enables:
 * - Faster process startup (defer loading until needed)
 * - Memory savings (unused pages never loaded)
 * - Efficient stack growth (allocate pages as stack grows)
 */

/* Create an SPT entry for a file-backed page.
   
   Creates an entry for a page that will be loaded from a file. The page
   is not immediately loaded; it will be loaded on first access via
   spt_load_page().
   
   @param spt Pointer to an initialized struct spt.
   @param upage User virtual address (automatically rounded to page boundary).
   @param file File to read from (must remain open until page is loaded).
   @param offset Byte offset in file where page data starts.
   @param read_bytes Number of bytes to read from file (must be <= PGSIZE).
   @param zero_bytes Number of bytes to zero-fill after read_bytes.
                     Must satisfy: read_bytes + zero_bytes == PGSIZE
   @param writable Whether the page should be writable.
   
   @return true if entry was created and inserted, false on failure.
   
   @pre spt was initialized with spt_init()
   @pre file != NULL
   @pre read_bytes + zero_bytes == PGSIZE
   @pre upage does not already exist in the table
   @post Entry is in SPT with status PAGE_FILE
   
   Called from load_segment() during process loading to set up executable
   segments for lazy loading. The file must remain open until all pages
   are loaded (typically until process exits). */
bool spt_create_file_page(void* spt, void* upage, struct file* file, off_t offset,
                          size_t read_bytes, size_t zero_bytes, bool writable);

/* Create an SPT entry for a zero-filled page.
   
   Creates an entry for a page that will be filled with zeros when loaded.
   Used for:
   - BSS segments (uninitialized data)
   - Stack growth (new stack pages)
   - Anonymous memory allocations
   
   @param spt Pointer to an initialized struct spt.
   @param upage User virtual address (automatically rounded to page boundary).
   @param writable Whether the page should be writable.
   
   @return true if entry was created and inserted, false on failure.
   
   @pre spt was initialized with spt_init()
   @pre upage does not already exist in the table
   @post Entry is in SPT with status PAGE_ZERO
   
   Used for BSS segments during process loading and for stack growth
   when handling page faults below the stack pointer. */
bool spt_create_zero_page(void* spt, void* upage, bool writable);

/* ============================================================================
 * FORK SUPPORT
 * ============================================================================ */

/* Clone an SPT from parent to child during fork.

   Iterates through all entries in the parent SPT and creates corresponding
   entries in the child SPT. For each entry:
   - PAGE_FRAME: Copy the frame contents to a new frame
   - PAGE_FILE:  Copy the file metadata (lazy loading preserved)
   - PAGE_SWAP:  Copy swap data to a new swap slot
   - PAGE_ZERO:  Copy the entry (lazy loading preserved)

   @param child_spt Pointer to child's initialized (empty) SPT.
   @param parent_spt Pointer to parent's SPT to clone.
   @param child_pagedir Child's page directory to install copied pages into.

   @return true if all entries were cloned successfully, false on failure.

   @pre child_spt is initialized and empty
   @pre parent_spt is a valid SPT
   @post On success: child_spt contains clones of all parent entries
   @post On failure: partial state may remain, caller should destroy child_spt

   Called from fork_process() to duplicate parent's virtual address space. */
bool spt_clone(void* child_spt, void* parent_spt, uint32_t* child_pagedir,
               uint32_t* parent_pagedir);

#endif /* vm/page.h */
