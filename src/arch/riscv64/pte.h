/* arch/riscv64/pte.h - RISC-V Sv39 page table entry definitions.
 *
 * Sv39 uses a 3-level page table with 9-9-9-12 bit split:
 *   - 9 bits: VPN[2] (level 2 page table index)
 *   - 9 bits: VPN[1] (level 1 page table index)
 *   - 9 bits: VPN[0] (level 0 page table index)
 *   - 12 bits: Page Offset
 *
 * Virtual address format (39 bits, sign-extended to 64):
 *   63    39 38      30 29      21 20      12 11       0
 *   +-------+----------+----------+----------+----------+
 *   | sign  |  VPN[2]  |  VPN[1]  |  VPN[0]  |  offset  |
 *   +-------+----------+----------+----------+----------+
 *
 * PTE format (64 bits):
 *   63    54 53    28 27    19 18    10 9  8 7 6 5 4 3 2 1 0
 *   +-------+--------+--------+--------+----+-+-+-+-+-+-+-+-+
 *   |  Res  | PPN[2] | PPN[1] | PPN[0] |RSW |D|A|G|U|X|W|R|V|
 *   +-------+--------+--------+--------+----+-+-+-+-+-+-+-+-+
 */

#ifndef ARCH_RISCV64_PTE_H
#define ARCH_RISCV64_PTE_H

#include <stdint.h>
#include <stdbool.h>
#include <debug.h>
#include "arch/riscv64/vaddr.h"
#include "arch/riscv64/memlayout.h"

/* ==========================================================================
 * Page Table Structure Constants
 * ========================================================================== */

/* Number of entries per page table (512 = 2^9) */
#define PT_ENTRY_CNT 512

/* Page table index bits */
#define PT_BITS 9
#define PT_MASK ((1UL << PT_BITS) - 1)

/* VPN[0]: Level 0 page table index (bits 12:20) */
#define VPN0_SHIFT 12
#define VPN0_MASK (PT_MASK << VPN0_SHIFT)

/* VPN[1]: Level 1 page table index (bits 21:29) */
#define VPN1_SHIFT 21
#define VPN1_MASK (PT_MASK << VPN1_SHIFT)

/* VPN[2]: Level 2 page table index (bits 30:38) */
#define VPN2_SHIFT 30
#define VPN2_MASK (PT_MASK << VPN2_SHIFT)

/* For compatibility with x86 PintOS code */
#define PTSHIFT VPN0_SHIFT
#define PTBITS PT_BITS
#define PTSPAN (1UL << (PT_BITS + PGBITS)) /* 2MB - bytes covered by one L1 entry */
#define PTMASK VPN0_MASK

#define PDSHIFT VPN1_SHIFT
#define PDBITS PT_BITS
#define PDMASK VPN1_MASK

/* Extract VPN indices from virtual address */
static inline unsigned vpn0(const void* va) { return ((uintptr_t)va >> VPN0_SHIFT) & PT_MASK; }

static inline unsigned vpn1(const void* va) { return ((uintptr_t)va >> VPN1_SHIFT) & PT_MASK; }

static inline unsigned vpn2(const void* va) { return ((uintptr_t)va >> VPN2_SHIFT) & PT_MASK; }

/* Compatibility with x86 naming */
static inline unsigned pt_no(const void* va) { return vpn0(va); }
static inline uintptr_t pd_no(const void* va) { return vpn1(va); }

/* ==========================================================================
 * PTE Bit Definitions (Sv39)
 * ========================================================================== */

#define PTE_V (1UL << 0) /* Valid */
#define PTE_R (1UL << 1) /* Readable */
#define PTE_W (1UL << 2) /* Writable */
#define PTE_X (1UL << 3) /* Executable */
#define PTE_U (1UL << 4) /* User-accessible */
#define PTE_G (1UL << 5) /* Global mapping */
#define PTE_A (1UL << 6) /* Accessed */
#define PTE_D (1UL << 7) /* Dirty */

/* RSW (Reserved for Software) bits 8-9 */
#define PTE_RSW_SHIFT 8
#define PTE_RSW_MASK (3UL << PTE_RSW_SHIFT)

/* Physical page number starts at bit 10 */
#define PTE_PPN_SHIFT 10
#define PTE_PPN_MASK (0xFFFFFFFFFFFUL << PTE_PPN_SHIFT) /* 44 bits of PPN */

/* Compatibility aliases for x86 PintOS code */
#define PTE_P PTE_V /* Present = Valid */
/* Note: PTE_W, PTE_U, PTE_A, PTE_D have same meaning */

/* Flag bits mask (bits 0-9) */
#define PTE_FLAGS 0x3FFUL

/* ==========================================================================
 * PTE Helper Functions
 * ========================================================================== */

/* Check if PTE is valid */
static inline bool pte_is_valid(uint64_t pte) { return (pte & PTE_V) != 0; }

/* Check if PTE is a leaf (has R, W, or X set) */
static inline bool pte_is_leaf(uint64_t pte) { return (pte & (PTE_R | PTE_W | PTE_X)) != 0; }

/* Check if PTE points to next level page table (non-leaf) */
static inline bool pte_is_pointer(uint64_t pte) { return pte_is_valid(pte) && !pte_is_leaf(pte); }

/* Extract physical address from PTE */
static inline uintptr_t pte_get_pa(uint64_t pte) { return ((pte >> PTE_PPN_SHIFT) << PGBITS); }

/* Get kernel virtual address of page that PTE points to */
static inline void* pte_get_page(uint64_t pte) { return ptov(pte_get_pa(pte)); }

/* Create PTE from physical address and flags */
static inline uint64_t pte_create(uintptr_t pa, uint64_t flags) {
  ASSERT((pa & PGMASK) == 0); /* PA must be page-aligned */
  return ((pa >> PGBITS) << PTE_PPN_SHIFT) | flags;
}

/* Create a non-leaf PTE pointing to next level page table */
static inline uint64_t pte_create_pointer(void* pt) {
  ASSERT(pg_ofs(pt) == 0);
  return pte_create(vtop(pt), PTE_V);
}

/* Create a kernel page PTE (supervisor only, not user accessible) */
static inline uint64_t pte_create_kernel(void* page, bool writable) {
  ASSERT(pg_ofs(page) == 0);
  uint64_t flags = PTE_V | PTE_R | PTE_A;
  if (writable)
    flags |= PTE_W | PTE_D;
  return pte_create(vtop(page), flags);
}

/* Create a kernel code PTE (executable) */
static inline uint64_t pte_create_kernel_exec(void* page) {
  ASSERT(pg_ofs(page) == 0);
  return pte_create(vtop(page), PTE_V | PTE_R | PTE_X | PTE_A);
}

/* Create a user page PTE */
static inline uint64_t pte_create_user(void* page, bool writable) {
  ASSERT(pg_ofs(page) == 0);
  uint64_t flags = PTE_V | PTE_R | PTE_U | PTE_A;
  if (writable)
    flags |= PTE_W | PTE_D;
  return pte_create(vtop(page), flags);
}

/* ==========================================================================
 * Page Directory Entry (PDE) compatibility functions
 *
 * In Sv39, there's no distinction between PDE and PTE formats.
 * These functions provide compatibility with x86 PintOS code.
 * ========================================================================== */

/* Create a PDE pointing to a page table */
static inline uint64_t pde_create(uint64_t* pt) { return pte_create_pointer(pt); }

/* Get page table pointer from PDE */
static inline uint64_t* pde_get_pt(uint64_t pde) {
  ASSERT(pte_is_valid(pde));
  return (uint64_t*)pte_get_page(pde);
}

/* ==========================================================================
 * Gigapage and Megapage support (huge pages)
 * ========================================================================== */

/* Create a 1GB gigapage PTE (at level 2) */
static inline uint64_t pte_create_gigapage(uintptr_t pa, uint64_t flags) {
  ASSERT((pa & (GIGAPAGE_SIZE - 1)) == 0); /* Must be 1GB aligned */
  return pte_create(pa, flags);
}

/* Create a 2MB megapage PTE (at level 1) */
static inline uint64_t pte_create_megapage(uintptr_t pa, uint64_t flags) {
  ASSERT((pa & (MEGAPAGE_SIZE - 1)) == 0); /* Must be 2MB aligned */
  return pte_create(pa, flags);
}

#endif /* ARCH_RISCV64_PTE_H */
