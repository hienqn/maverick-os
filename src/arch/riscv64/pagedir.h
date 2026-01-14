/* arch/riscv64/pagedir.h - RISC-V page directory interface.
 *
 * Provides pagedir_* API compatible with i386 for portable code.
 * Internally uses Sv39 3-level page tables via upt_* functions.
 *
 * On RISC-V, the pagedir_t (uint64_t*) actually points to a
 * struct user_page_table, which contains both the root L2 table
 * and the ASID for SATP.
 */

#ifndef ARCH_RISCV64_PAGEDIR_H
#define ARCH_RISCV64_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>

/* Page directory type - pointer to user_page_table struct */
typedef uint64_t* pagedir_t;

/* Page Directory Lifecycle */
pagedir_t pagedir_create(void);
void pagedir_destroy(pagedir_t pd);
bool pagedir_dup(pagedir_t child_pd, pagedir_t parent_pd);

/* Page Mapping Operations */
bool pagedir_set_page(pagedir_t pd, void* upage, void* kpage, bool writable);
void* pagedir_get_page(pagedir_t pd, const void* uaddr);
void pagedir_clear_page(pagedir_t pd, void* upage);

/* PTE Status Bits */
bool pagedir_is_dirty(pagedir_t pd, const void* vpage);
void pagedir_set_dirty(pagedir_t pd, const void* vpage, bool dirty);
bool pagedir_is_accessed(pagedir_t pd, const void* vpage);
void pagedir_set_accessed(pagedir_t pd, const void* vpage, bool accessed);
bool pagedir_is_writable(pagedir_t pd, const void* vpage);
void pagedir_set_writable(pagedir_t pd, void* vpage, bool writable);

/* Page Directory Activation */
void pagedir_activate(pagedir_t pd);
pagedir_t active_pd(void);

#endif /* ARCH_RISCV64_PAGEDIR_H */
