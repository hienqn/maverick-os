/* arch/riscv64/mmu.h - RISC-V Sv39 MMU interface.
 *
 * This header provides the interface for RISC-V memory management.
 */

#ifndef ARCH_RISCV64_MMU_H
#define ARCH_RISCV64_MMU_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize MMU and set up kernel page tables */
void mmu_init(void);

/* Enable Sv39 paging */
void mmu_enable(void);

/* Disable paging (switch to bare mode) */
void mmu_disable(void);

/* Map a single 4KB page */
bool mmu_map_page(uint64_t* root, uintptr_t va, uintptr_t pa, uint64_t flags);

/* Unmap a single 4KB page */
void mmu_unmap_page(uint64_t* root, uintptr_t va);

/* Get SATP value for a page table */
uint64_t mmu_get_satp(uintptr_t root_pa, uint16_t asid);

/* Switch to a different address space */
void mmu_switch(uint64_t satp);

/* Flush entire TLB */
void mmu_flush_tlb(void);

/* Flush TLB entry for a specific page */
void mmu_flush_tlb_page(uintptr_t va);

/* Get the kernel's root page table */
uint64_t* mmu_get_kernel_pt(void);

/* Kernel page table (defined in mmu.c) */
extern uint64_t kernel_pt_l2[];

#endif /* ARCH_RISCV64_MMU_H */
