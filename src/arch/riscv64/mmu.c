/* arch/riscv64/mmu.c - RISC-V Sv39 memory management.
 *
 * This file implements the 3-level Sv39 page table management for RISC-V.
 * The kernel uses a direct mapping where physical addresses starting at
 * PHYS_RAM_BASE (0x80000000) are mapped to virtual addresses starting at
 * PHYS_BASE (0xFFFFFFFF80000000).
 */

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/pte.h"
#include "arch/riscv64/csr.h"
#include "arch/riscv64/memlayout.h"
#include "arch/riscv64/vaddr.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Kernel's root page table (level 2).
 * Statically allocated to avoid chicken-and-egg with palloc. */
uint64_t kernel_pt_l2[PT_ENTRY_CNT] __attribute__((aligned(PGSIZE)));

/* Level 1 page tables for kernel space.
 * We need enough to cover physical memory. For 128MB, one L1 table covers 1GB,
 * so we need just one. For larger memory, add more. */
uint64_t kernel_pt_l1[PT_ENTRY_CNT] __attribute__((aligned(PGSIZE)));

/* Level 0 page tables for kernel space (for fine-grained mapping).
 * We'll use 2MB megapages where possible, but need L0 for MMIO regions. */
uint64_t kernel_pt_l0_mmio[PT_ENTRY_CNT] __attribute__((aligned(PGSIZE)));

/* Forward declarations */
static void map_kernel_memory(void);
static void map_mmio_regions(void);

/* External symbols from linker script */
extern char _start[];
extern char _end[];
extern char _end_kernel_text[];

/*
 * mmu_init - Initialize the MMU and enable Sv39 paging.
 *
 * Sets up the kernel's page tables with:
 *   1. Identity mapping for early boot (removed after jump to high addresses)
 *   2. High-half kernel mapping at PHYS_BASE
 *   3. MMIO device mappings
 */
void mmu_init(void) {
  /* Clear page tables */
  memset(kernel_pt_l2, 0, sizeof(kernel_pt_l2));
  memset(kernel_pt_l1, 0, sizeof(kernel_pt_l1));
  memset(kernel_pt_l0_mmio, 0, sizeof(kernel_pt_l0_mmio));

  /* Set up kernel mappings */
  map_kernel_memory();
  map_mmio_regions();
}

/*
 * mmu_enable - Enable Sv39 paging by writing to SATP CSR.
 *
 * After this call, all memory accesses go through the page tables.
 */
void mmu_enable(void) {
  /* Get physical address of root page table */
  uintptr_t root_pa = (uintptr_t)kernel_pt_l2;

  /* Build SATP value: Sv39 mode, ASID 0, root page table PPN */
  uint64_t satp = SATP_MODE_SV39 | (root_pa >> PGBITS);

  /* Ensure all page table writes are visible */
  fence();

  /* Write SATP and flush TLB */
  csr_write(satp, satp);
  sfence_vma_all();
}

/*
 * mmu_disable - Disable paging (switch to bare mode).
 */
void mmu_disable(void) {
  csr_write(satp, SATP_MODE_BARE);
  sfence_vma_all();
}

/*
 * map_kernel_memory - Set up kernel virtual memory mappings.
 *
 * Maps physical RAM to the kernel's virtual address space at PHYS_BASE.
 * Uses 1GB gigapages for efficiency where possible.
 */
static void map_kernel_memory(void) {
  /*
   * Sv39 virtual address space layout:
   *   Lower half (user):  0x0000_0000_0000_0000 - 0x0000_003F_FFFF_FFFF
   *   Upper half (kernel): 0xFFFF_FFC0_0000_0000 - 0xFFFF_FFFF_FFFF_FFFF
   *
   * We want to map:
   *   PA 0x8000_0000 -> VA 0xFFFF_FFFF_8000_0000 (PHYS_BASE)
   *
   * PHYS_BASE = 0xFFFFFFFF80000000
   *   VPN[2] = (0xFFFFFFFF80000000 >> 30) & 0x1FF = 0x1FE (510)
   *   VPN[1] = (0xFFFFFFFF80000000 >> 21) & 0x1FF = 0x1FC (508)
   *   VPN[0] = (0xFFFFFFFF80000000 >> 12) & 0x1FF = 0x000
   */

  uintptr_t phys_base_va = (uintptr_t)PHYS_BASE;
  unsigned l2_idx = vpn2((void*)phys_base_va);

  /*
   * Option 1: Use 1GB gigapage (simplest)
   * Map entire 1GB starting at 0x80000000 to PHYS_BASE
   * This is efficient and covers all of physical RAM for typical configs.
   */
  uint64_t gigapage_pte =
      pte_create(PHYS_RAM_BASE, PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D | PTE_G);
  kernel_pt_l2[l2_idx] = gigapage_pte;

  /*
   * Also create an identity mapping for early boot.
   * PA 0x80000000 -> VA 0x80000000
   * VPN[2] for 0x80000000 = 2
   */
  unsigned identity_l2_idx = vpn2((void*)PHYS_RAM_BASE);
  kernel_pt_l2[identity_l2_idx] = gigapage_pte;
}

/*
 * map_mmio_regions - Map memory-mapped I/O device regions.
 *
 * MMIO regions need to be mapped uncached. On RISC-V, this is typically
 * handled by the Svpbmt extension or PMA (Physical Memory Attributes).
 * For QEMU virt machine, normal mappings work fine.
 */
static void map_mmio_regions(void) {
  /*
   * QEMU virt MMIO regions are in low physical memory:
   *   UART:   0x10000000
   *   VirtIO: 0x10001000
   *   PLIC:   0x0C000000
   *   CLINT:  0x02000000
   *
   * Create identity mapping for low MMIO region (0x00000000 - 0x3FFFFFFF).
   * L2 index 0 covers this range as a 1GB gigapage.
   */
  uint64_t mmio_pte = pte_create(0, PTE_V | PTE_R | PTE_W | PTE_A | PTE_D | PTE_G);
  kernel_pt_l2[0] = mmio_pte;
}

/*
 * mmu_map_page - Map a single 4KB page.
 *
 * @root: Root page table (L2)
 * @va:   Virtual address (must be page-aligned)
 * @pa:   Physical address (must be page-aligned)
 * @flags: PTE flags (PTE_V, PTE_R, PTE_W, etc.)
 *
 * Returns true on success, false on failure (e.g., out of memory).
 *
 * Note: This function requires palloc to be initialized.
 * For early boot, use the static page tables instead.
 */
bool mmu_map_page(uint64_t* root, uintptr_t va, uintptr_t pa, uint64_t flags) {
  ASSERT((va & PGMASK) == 0);
  ASSERT((pa & PGMASK) == 0);
  ASSERT(flags & PTE_V);

  /* Get L2 entry */
  unsigned l2_idx = vpn2((void*)va);
  uint64_t* l2_entry = &root[l2_idx];

  /* Get or create L1 table */
  uint64_t* l1_table;
  if (!pte_is_valid(*l2_entry)) {
    /* Need to allocate L1 table - requires palloc */
    /* For now, this is not implemented; use static tables */
    return false;
  }
  l1_table = pde_get_pt(*l2_entry);

  /* Get L1 entry */
  unsigned l1_idx = vpn1((void*)va);
  uint64_t* l1_entry = &l1_table[l1_idx];

  /* Get or create L0 table */
  uint64_t* l0_table;
  if (!pte_is_valid(*l1_entry)) {
    /* Need to allocate L0 table */
    return false;
  }
  l0_table = pde_get_pt(*l1_entry);

  /* Set L0 entry */
  unsigned l0_idx = vpn0((void*)va);
  l0_table[l0_idx] = pte_create(pa, flags);

  /* Flush TLB for this VA */
  sfence_vma_va(va);

  return true;
}

/*
 * mmu_unmap_page - Unmap a single 4KB page.
 *
 * @root: Root page table (L2)
 * @va:   Virtual address to unmap
 */
void mmu_unmap_page(uint64_t* root, uintptr_t va) {
  unsigned l2_idx = vpn2((void*)va);
  uint64_t* l2_entry = &root[l2_idx];

  if (!pte_is_valid(*l2_entry))
    return;

  uint64_t* l1_table = pde_get_pt(*l2_entry);
  unsigned l1_idx = vpn1((void*)va);
  uint64_t* l1_entry = &l1_table[l1_idx];

  if (!pte_is_valid(*l1_entry))
    return;

  uint64_t* l0_table = pde_get_pt(*l1_entry);
  unsigned l0_idx = vpn0((void*)va);

  /* Clear the PTE */
  l0_table[l0_idx] = 0;

  /* Flush TLB */
  sfence_vma_va(va);
}

/*
 * mmu_get_satp - Get SATP value for a page table.
 *
 * @root: Root page table physical address
 * @asid: Address Space ID (0 for kernel)
 */
uint64_t mmu_get_satp(uintptr_t root_pa, uint16_t asid) {
  return SATP_VALUE(SATP_MODE_SV39, asid, root_pa >> PGBITS);
}

/*
 * mmu_switch - Switch to a different address space.
 *
 * @satp: SATP value for the new address space
 */
void mmu_switch(uint64_t satp) {
  csr_write(satp, satp);
  sfence_vma_all();
}

/*
 * mmu_flush_tlb - Flush entire TLB.
 */
void mmu_flush_tlb(void) { sfence_vma_all(); }

/*
 * mmu_flush_tlb_page - Flush TLB entry for a specific page.
 *
 * @va: Virtual address of the page
 */
void mmu_flush_tlb_page(uintptr_t va) { sfence_vma_va(va); }

/*
 * Get the kernel's root page table.
 */
uint64_t* mmu_get_kernel_pt(void) { return kernel_pt_l2; }
