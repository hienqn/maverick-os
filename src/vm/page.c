/*
 * ============================================================================
 *                    SUPPLEMENTAL PAGE TABLE (SPT)
 * ============================================================================
 */

#include "vm/page.h"
#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <string.h>

/* ============================================================================
 * HASH TABLE HELPERS (if using hash table)
 * ============================================================================
 *
 * TODO: If using a hash table, implement these functions:
 *
 * static unsigned spt_hash_func(const struct hash_elem *e, void *aux);
 * static bool spt_less_func(const struct hash_elem *a,
 *                           const struct hash_elem *b, void *aux);
 * static void spt_destroy_func(struct hash_elem *e, void *aux);
 */

/* ============================================================================
 * SPT LIFECYCLE
 * ============================================================================ */

/* Initialize a supplemental page table. */
void spt_init(void* spt UNUSED) {
  /* TODO: Initialize your SPT data structure.
   *
   * If using hash table:
   *   hash_init(&spt->pages, spt_hash_func, spt_less_func, NULL);
   */
}

/* Destroy a supplemental page table and free all entries. */
void spt_destroy(void* spt UNUSED) {
  /* TODO: Free all SPT entries and their resources.
   *
   * For each entry:
   *   - If PAGE_FRAME: free the frame
   *   - If PAGE_SWAP: free the swap slot
   *   - Free the entry struct itself
   *
   * If using hash table:
   *   hash_destroy(&spt->pages, spt_destroy_func);
   */
}

/* ============================================================================
 * SPT OPERATIONS
 * ============================================================================ */

/* Find the SPT entry for UPAGE. */
void* spt_find(void* spt UNUSED, void* upage UNUSED) {
  /* TODO: Look up UPAGE in the SPT.
   *
   * If using hash table:
   *   struct spt_entry key;
   *   key.upage = upage;
   *   struct hash_elem *e = hash_find(&spt->pages, &key.hash_elem);
   *   return e ? hash_entry(e, struct spt_entry, hash_elem) : NULL;
   */
  return NULL;
}

/* Insert a new SPT entry. */
bool spt_insert(void* spt UNUSED, void* entry UNUSED) {
  /* TODO: Add ENTRY to the SPT.
   *
   * If using hash table:
   *   struct hash_elem *old = hash_insert(&spt->pages, &entry->hash_elem);
   *   return old == NULL;  // Success if no duplicate
   */
  return false;
}

/* Remove the SPT entry for UPAGE. */
bool spt_remove(void* spt UNUSED, void* upage UNUSED) {
  /* TODO: Remove and free the entry for UPAGE.
   *
   * 1. Find the entry
   * 2. Remove from hash table
   * 3. Free any associated resources (frame, swap slot)
   * 4. Free the entry struct
   */
  return false;
}

/* ============================================================================
 * PAGE LOADING
 * ============================================================================ */

/* Load a page into memory based on its SPT entry. */
bool spt_load_page(void* entry UNUSED) {
  /* TODO: Load the page described by ENTRY into a frame.
   *
   * 1. Allocate a frame (frame_alloc)
   *
   * 2. Based on entry->status:
   *    PAGE_ZERO: memset frame to 0
   *    PAGE_FILE: read from file into frame
   *    PAGE_SWAP: read from swap into frame
   *
   * 3. Install in page table (pagedir_set_page)
   *
   * 4. Update entry->status to PAGE_FRAME
   *
   * 5. Return true on success
   */
  return false;
}

/* ============================================================================
 * LAZY LOADING SUPPORT
 * ============================================================================ */

/* Create an SPT entry for a file-backed page. */
bool spt_create_file_page(void* spt UNUSED, void* upage UNUSED, struct file* file UNUSED,
                          off_t offset UNUSED, size_t read_bytes UNUSED, size_t zero_bytes UNUSED,
                          bool writable UNUSED) {
  /* TODO: Create and insert an SPT entry for a file-backed page.
   *
   * 1. Allocate a new spt_entry
   * 2. Set upage, status = PAGE_FILE, writable
   * 3. Set file, file_offset, read_bytes, zero_bytes
   * 4. Insert into SPT
   */
  return false;
}

/* Create an SPT entry for a zero-filled page. */
bool spt_create_zero_page(void* spt UNUSED, void* upage UNUSED, bool writable UNUSED) {
  /* TODO: Create and insert an SPT entry for a zero page.
   *
   * 1. Allocate a new spt_entry
   * 2. Set upage, status = PAGE_ZERO, writable
   * 3. Insert into SPT
   */
  return false;
}
