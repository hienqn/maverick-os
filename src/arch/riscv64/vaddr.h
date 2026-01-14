/* arch/riscv64/vaddr.h - RISC-V virtual address helpers.
 *
 * Sv39 virtual memory layout:
 *   User space:   0x0000000000000000 - 0x0000003FFFFFFFFF (256GB)
 *   Kernel space: 0xFFFFFFC000000000 - 0xFFFFFFFFFFFFFFFF
 *   PHYS_BASE:    0xFFFFFFFF80000000 (physical memory mapped here)
 */

#ifndef ARCH_RISCV64_VADDR_H
#define ARCH_RISCV64_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

/* Physical RAM starts at 0x80000000 on QEMU virt machine */
#define PHYS_RAM_BASE 0x80000000UL

/* Functions and macros for working with virtual addresses. */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* Page offset (bits 0:12). */
#define PGSHIFT 0                       /* Index of first offset bit. */
#define PGBITS 12                       /* Number of offset bits. */
#define PGSIZE (1 << PGBITS)            /* Bytes in a page. */
#define PGMASK BITMASK(PGSHIFT, PGBITS) /* Page offset bits (0:12). */

/* Offset within a page. */
static inline unsigned pg_ofs(const void* va) { return (uintptr_t)va & PGMASK; }

/* Virtual page number. */
static inline uintptr_t pg_no(const void* va) { return (uintptr_t)va >> PGBITS; }

/* Round up to nearest page boundary. */
static inline void* pg_round_up(const void* va) {
  return (void*)(((uintptr_t)va + PGSIZE - 1) & ~PGMASK);
}

/* Round down to nearest page boundary. */
static inline void* pg_round_down(const void* va) { return (void*)((uintptr_t)va & ~PGMASK); }

/* Base address of the 1:1 physical-to-virtual mapping.  Physical
   memory is mapped starting at this virtual address.  Thus,
   physical address 0x80000000 is accessible at PHYS_BASE, physical
   address 0x80001234 at (uint8_t *) PHYS_BASE + 0x1234, and so on.

   This address also marks the end of user programs' address space.
   Up to this point in memory, user programs are allowed to map
   whatever they like.  At this point and above, the virtual address
   space belongs to the kernel.

   Note: PHYS_BASE is defined in memlayout.h as an integer constant.
   We use that definition and cast to void* when needed. */
#include "arch/riscv64/memlayout.h"
/* PHYS_BASE is defined in memlayout.h */

/* Maximum user virtual address (256GB in Sv39 lower half) */
#define USER_VIRT_MAX 0x0000004000000000UL

/* Returns true if VADDR is a user virtual address. */
static inline bool is_user_vaddr(const void* vaddr) { return (uintptr_t)vaddr < USER_VIRT_MAX; }

/* Returns true if VADDR is a kernel virtual address. */
static inline bool is_kernel_vaddr(const void* vaddr) {
  return (uintptr_t)vaddr >= (uintptr_t)PHYS_BASE;
}

/* Returns kernel virtual address at which physical address PADDR
   is mapped. PADDR should be relative to PHYS_RAM_BASE. */
static inline void* ptov(uintptr_t paddr) {
  /* Physical memory starts at 0x80000000 on QEMU virt */
  ASSERT(paddr >= PHYS_RAM_BASE);
  return (void*)(paddr - PHYS_RAM_BASE + (uintptr_t)PHYS_BASE);
}

/* Returns physical address at which kernel virtual address VADDR
   is mapped. */
static inline uintptr_t vtop(const void* vaddr) {
  ASSERT(is_kernel_vaddr(vaddr));
  return (uintptr_t)vaddr - (uintptr_t)PHYS_BASE + PHYS_RAM_BASE;
}

#endif /* ARCH_RISCV64_VADDR_H */
