/* arch/common/mmu.h - Memory management unit interface.
 *
 * Architecture-neutral interface for page table operations.
 * Each architecture implements these in arch/<arch>/pagedir.c.
 *
 * Note: Page size is 4KB (4096 bytes) on both i386 and RISC-V.
 */

#ifndef ARCH_COMMON_MMU_H
#define ARCH_COMMON_MMU_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Page size constants (same for i386 and RISC-V). */
#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT) /* 4096 bytes */
#define PAGE_MASK (PAGE_SIZE - 1)

/* Round address down to page boundary. */
static inline void* pg_round_down(const void* va) { return (void*)((uintptr_t)va & ~PAGE_MASK); }

/* Round address up to page boundary. */
static inline void* pg_round_up(const void* va) {
  return (void*)(((uintptr_t)va + PAGE_MASK) & ~PAGE_MASK);
}

/* Get offset within page. */
static inline unsigned pg_ofs(const void* va) { return (uintptr_t)va & PAGE_MASK; }

/* Get page number. */
static inline uintptr_t pg_no(const void* va) { return (uintptr_t)va >> PAGE_SHIFT; }

/*
 * The following are architecture-specific and defined in each arch's headers:
 *
 * PHYS_BASE - Virtual address where physical memory is mapped
 * ptov(paddr) - Convert physical to kernel virtual address
 * vtop(vaddr) - Convert kernel virtual to physical address
 * is_user_vaddr(vaddr) - Check if address is in user space
 * is_kernel_vaddr(vaddr) - Check if address is in kernel space
 *
 * Page directory operations (in arch/<arch>/pagedir.c):
 * - pagedir_create() - Create new page directory
 * - pagedir_destroy() - Destroy page directory
 * - pagedir_activate() - Make page directory active
 * - pagedir_set_page() - Map virtual to physical
 * - pagedir_get_page() - Get physical page for virtual address
 * - pagedir_clear_page() - Unmap a page
 * - pagedir_is_dirty() - Check dirty bit
 * - pagedir_is_accessed() - Check accessed bit
 */

#endif /* ARCH_COMMON_MMU_H */
