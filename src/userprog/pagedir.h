#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * PAGE DIRECTORY MANAGEMENT
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * This module manages x86 two-level page tables for user processes.
 *
 * Page Table Structure (x86 32-bit):
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Virtual Address (32 bits)                                              │
 * │  ┌──────────────┬──────────────┬──────────────────────┐                │
 * │  │ PD Index(10) │ PT Index(10) │ Page Offset (12)     │                │
 * │  └──────┬───────┴──────┬───────┴──────────────────────┘                │
 * │         │              │                                                │
 * │         ▼              ▼                                                │
 * │  Page Directory ──► Page Table ──► Physical Page                       │
 * │  (1024 entries)    (1024 entries)   (4KB each)                         │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * Key PTE Bits:
 *   PTE_P (Present)   - Page is in physical memory
 *   PTE_W (Writable)  - Page can be written
 *   PTE_U (User)      - Page accessible from user mode
 *   PTE_A (Accessed)  - Page has been read (set by CPU)
 *   PTE_D (Dirty)     - Page has been written (set by CPU)
 *
 * Thread Safety: These functions are NOT thread-safe. The caller must ensure
 * exclusive access to the page directory being modified.
 * ═══════════════════════════════════════════════════════════════════════════
 */

/* ─────────────────────────────────────────────────────────────────────────
 * Page Directory Lifecycle
 * ───────────────────────────────────────────────────────────────────────── */

/* Creates a new page directory with kernel mappings only.
   The kernel portion (PHYS_BASE and above) is copied from init_page_dir.
   User portion (below PHYS_BASE) is initially empty.
   @return New page directory, or NULL if allocation fails. */
uint32_t* pagedir_create(void);

/* Destroys page directory PD, freeing all page tables.
   Note: User pages themselves are freed by spt_destroy() via frame_free().
   This function only frees the page table structures.
   @param pd  Page directory to destroy. May be NULL (no-op). */
void pagedir_destroy(uint32_t* pd);

/* Duplicates parent's address space into child's page directory.
   Copies all present user pages (content and mappings) from parent to child.
   Used by fork() to create child process address space.
   @param child_pagedir   Child's page directory (must be created first)
   @param parent_pagedir  Parent's page directory to copy from
   @return true on success, false if memory allocation fails (partial copy). */
bool pagedir_dup(uint32_t* child_pagedir, uint32_t* parent_pagedir);

/* ─────────────────────────────────────────────────────────────────────────
 * Page Mapping Operations
 * ───────────────────────────────────────────────────────────────────────── */

/* Maps user virtual page UPAGE to kernel virtual address KPAGE.
   Creates page table if needed. UPAGE must not already be mapped.
   @param pd        Page directory to modify
   @param upage     User virtual address (must be page-aligned, < PHYS_BASE)
   @param kpage     Kernel virtual address of physical frame
   @param writable  true = read/write, false = read-only
   @return true on success, false if page table allocation fails. */
bool pagedir_set_page(uint32_t* pd, void* upage, void* kpage, bool writable);

/* Looks up the physical frame mapped to user virtual address UADDR.
   @param pd     Page directory to search
   @param uaddr  User virtual address (need not be page-aligned)
   @return Kernel virtual address of the frame + page offset, or NULL if unmapped. */
void* pagedir_get_page(uint32_t* pd, const void* uaddr);

/* Marks user virtual page UPAGE as "not present" (unmaps it).
   Later accesses will cause a page fault. Does NOT free the physical frame.
   @param pd     Page directory to modify
   @param upage  User virtual address (must be page-aligned). */
void pagedir_clear_page(uint32_t* pd, void* upage);

/* ─────────────────────────────────────────────────────────────────────────
 * Page Table Entry (PTE) Status Bits
 * ───────────────────────────────────────────────────────────────────────── */

/* Returns true if page VPAGE has been written since mapping (dirty bit set).
   @param pd     Page directory to check
   @param vpage  User virtual address (page-aligned)
   @return true if dirty, false if clean or unmapped. */
bool pagedir_is_dirty(uint32_t* pd, const void* vpage);

/* Sets or clears the dirty bit for page VPAGE.
   @param pd     Page directory to modify
   @param vpage  User virtual address (page-aligned)
   @param dirty  true to set dirty, false to clear. */
void pagedir_set_dirty(uint32_t* pd, const void* vpage, bool dirty);

/* Returns true if page VPAGE has been accessed (read or written) recently.
   Used by page replacement algorithms (e.g., clock algorithm).
   @param pd     Page directory to check
   @param vpage  User virtual address (page-aligned)
   @return true if accessed, false if not accessed or unmapped. */
bool pagedir_is_accessed(uint32_t* pd, const void* vpage);

/* Sets or clears the accessed bit for page VPAGE.
   Typically cleared periodically by page replacement algorithm.
   @param pd       Page directory to modify
   @param vpage    User virtual address (page-aligned)
   @param accessed true to set accessed, false to clear. */
void pagedir_set_accessed(uint32_t* pd, const void* vpage, bool accessed);

/* Returns true if page VPAGE is mapped with write permission.
   @param pd     Page directory to check
   @param vpage  User virtual address (page-aligned)
   @return true if writable, false if read-only or unmapped. */
bool pagedir_is_writable(uint32_t* pd, const void* vpage);

/* Sets the writable bit for user virtual page VPAGE in PD.
   Used for copy-on-write: mark shared pages read-only, then restore
   write permission after copying.
   @param pd       Page directory
   @param vpage    User virtual address (page-aligned)
   @param writable true for read/write, false for read-only */
void pagedir_set_writable(uint32_t* pd, void* vpage, bool writable);

/* ─────────────────────────────────────────────────────────────────────────
 * Page Directory Activation (CR3 Register)
 * ───────────────────────────────────────────────────────────────────────── */

/* Loads page directory PD into CPU's CR3 register, activating it.
   This switches the CPU to use PD for all address translations.
   Also flushes the TLB (Translation Lookaside Buffer).
   @param pd  Page directory to activate. NULL uses init_page_dir. */
void pagedir_activate(uint32_t* pd);

/* Returns the currently active page directory (from CR3 register).
   @return Kernel virtual address of the active page directory. */
uint32_t* active_pd(void);

#endif /* userprog/pagedir.h */
