/*
 * ============================================================================
 *                    SUPPLEMENTAL PAGE TABLE (SPT)
 * ============================================================================
 *
 * The SPT tracks metadata about each virtual page that the hardware page
 * table cannot store:
 *   - Where is the page's data? (frame, swap, file, or zeros)
 *   - Is the page writable?
 *   - For file-backed pages: which file, offset, and size?
 *   - For swapped pages: which swap slot?
 *
 * Each process has its own SPT. The SPT is created when the process starts
 * and destroyed when it exits.
 *
 * DATA STRUCTURE CHOICE:
 * ----------------------
 * You need to choose a data structure for the SPT. Options:
 *   - Hash table (lib/kernel/hash.h) - O(1) lookup, recommended
 *   - List - Simple but O(n) lookup
 *   - Array - Fast but wastes memory for sparse address spaces
 *
 * ============================================================================
 */

#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stddef.h>
#include "filesys/off_t.h"

/* Forward declarations. */
struct file;

/* ============================================================================
 * PAGE STATUS
 * ============================================================================
 *
 * Each page can be in one of these states:
 */

enum page_status {
  PAGE_ZERO,  /* All zeros, not yet allocated. */
  PAGE_FRAME, /* Currently in a physical frame. */
  PAGE_SWAP,  /* Swapped out to disk. */
  PAGE_FILE   /* Backed by a file (executable or mmap). */
};

/* ============================================================================
 * SPT ENTRY STRUCTURE
 * ============================================================================
 *
 * TODO: Define your spt_entry structure here.
 *
 * Suggested fields:
 *   - void *upage           : User virtual address (page-aligned)
 *   - enum page_status      : Where is the data?
 *   - bool writable         : Is page writable?
 *
 *   For PAGE_FRAME:
 *   - void *kpage           : Kernel virtual address of frame
 *
 *   For PAGE_SWAP:
 *   - size_t swap_slot      : Swap slot index
 *
 *   For PAGE_FILE:
 *   - struct file *file     : File to read from
 *   - off_t file_offset     : Offset in file
 *   - size_t read_bytes     : Bytes to read from file
 *   - size_t zero_bytes     : Bytes to zero after read
 *
 *   For hash table:
 *   - struct hash_elem      : Hash table element
 */

/* TODO: Define struct spt_entry here. */

/* ============================================================================
 * SPT STRUCTURE
 * ============================================================================
 *
 * TODO: Define your supplemental page table structure here.
 *
 * If using hash table:
 *   struct spt {
 *       struct hash pages;
 *   };
 */

/* TODO: Define struct spt here. */

/* ============================================================================
 * SPT LIFECYCLE
 * ============================================================================ */

/* Initialize a supplemental page table for a new process.
   Called from process creation (start_process or similar). */
void spt_init(void* spt);

/* Destroy a supplemental page table and free all entries.
   Called when process exits. */
void spt_destroy(void* spt);

/* ============================================================================
 * SPT OPERATIONS
 * ============================================================================ */

/* Find the SPT entry for user virtual address UPAGE.
   Returns NULL if no entry exists. */
void* spt_find(void* spt, void* upage);

/* Insert a new SPT entry. Returns true on success.
   UPAGE must not already exist in the table. */
bool spt_insert(void* spt, void* entry);

/* Remove the SPT entry for UPAGE. Returns true if found and removed. */
bool spt_remove(void* spt, void* upage);

/* ============================================================================
 * PAGE LOADING
 * ============================================================================ */

/* Load a page into memory based on its SPT entry.
   Allocates a frame and populates it with data.
   Returns true on success, false on failure. */
bool spt_load_page(void* entry);

/* ============================================================================
 * LAZY LOADING SUPPORT
 * ============================================================================
 *
 * These functions create SPT entries for lazy-loaded pages.
 * Called during load_segment() instead of immediately loading pages.
 */

/* Create an SPT entry for a file-backed page.
   Called from load_segment() for lazy loading. */
bool spt_create_file_page(void* spt, void* upage, struct file* file, off_t offset,
                          size_t read_bytes, size_t zero_bytes, bool writable);

/* Create an SPT entry for a zero-filled page.
   Used for BSS segment and stack growth. */
bool spt_create_zero_page(void* spt, void* upage, bool writable);

#endif /* vm/page.h */
