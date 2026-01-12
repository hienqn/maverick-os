/* arch/riscv64/pagedir.c - RISC-V page directory implementation.
 *
 * Provides pagedir_* API compatible with i386 for portable code.
 * Uses Sv39 3-level page tables internally.
 */

#include "arch/riscv64/pagedir.h"
#include "arch/riscv64/userprog.h"
#include "arch/riscv64/pte.h"
#include "arch/riscv64/csr.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/vaddr.h"
#include "arch/riscv64/memlayout.h"
#include "threads/palloc.h"
#include <string.h>
#include <debug.h>

/* Cast helpers between pagedir_t and user_page_table* */
static inline struct user_page_table* pd_to_upt(pagedir_t pd) {
  return (struct user_page_table*)pd;
}

static inline pagedir_t upt_to_pd(struct user_page_table* upt) { return (pagedir_t)upt; }

/* Currently active page directory */
static pagedir_t current_pd;

/* ==========================================================================
 * Internal Helper: Walk page table to find PTE
 * ========================================================================== */

/*
 * pagedir_lookup_pte - Find the PTE for a virtual address.
 *
 * @pd: Page directory
 * @vaddr: Virtual address to look up
 * @create: If true, create missing page table levels
 *
 * Returns pointer to PTE, or NULL if not found (and create is false).
 */
static uint64_t* pagedir_lookup_pte(pagedir_t pd, const void* vaddr, bool create) {
  struct user_page_table* upt = pd_to_upt(pd);
  if (!upt || !upt->root)
    return NULL;

  uint64_t va = (uint64_t)vaddr;
  uint64_t* l2 = upt->root;
  unsigned idx2 = vpn2((void*)va);
  unsigned idx1 = vpn1((void*)va);
  unsigned idx0 = vpn0((void*)va);

  /* Level 2 -> Level 1 */
  uint64_t* l1;
  if (!pte_is_valid(l2[idx2])) {
    if (!create)
      return NULL;
    l1 = palloc_get_page(PAL_ZERO);
    if (!l1)
      return NULL;
    l2[idx2] = pte_create_pointer(l1);
  } else if (pte_is_leaf(l2[idx2])) {
    return NULL; /* Gigapage - can't descend */
  } else {
    l1 = pte_get_page(l2[idx2]);
  }

  /* Level 1 -> Level 0 */
  uint64_t* l0;
  if (!pte_is_valid(l1[idx1])) {
    if (!create)
      return NULL;
    l0 = palloc_get_page(PAL_ZERO);
    if (!l0)
      return NULL;
    l1[idx1] = pte_create_pointer(l0);
  } else if (pte_is_leaf(l1[idx1])) {
    return NULL; /* Megapage - can't descend */
  } else {
    l0 = pte_get_page(l1[idx1]);
  }

  return &l0[idx0];
}

/* ==========================================================================
 * Page Directory Lifecycle
 * ========================================================================== */

/*
 * pagedir_create - Create a new page directory.
 *
 * Allocates a 3-level Sv39 page table with kernel mappings.
 */
pagedir_t pagedir_create(void) {
  /* Allocate user_page_table structure */
  struct user_page_table* upt = palloc_get_page(PAL_ZERO);
  if (!upt)
    return NULL;

  /* Allocate root L2 page table */
  uint64_t* root = palloc_get_page(PAL_ZERO);
  if (!root) {
    palloc_free_page(upt);
    return NULL;
  }

  upt->root = root;

  /* Assign ASID */
  static uint64_t next_asid = 1;
  upt->asid = next_asid++;
  if (next_asid > 0xFFFF)
    next_asid = 1;

  /* Copy kernel mappings (upper half of address space) */
  uint64_t* kernel_pt = mmu_get_kernel_pt();
  for (int i = 256; i < PT_ENTRY_CNT; i++) {
    root[i] = kernel_pt[i];
  }

  return upt_to_pd(upt);
}

/*
 * pagedir_destroy - Destroy a page directory.
 *
 * Frees all page table levels. Does NOT free mapped user pages
 * (those should be freed by spt_destroy via frame_free).
 */
void pagedir_destroy(pagedir_t pd) {
  struct user_page_table* upt = pd_to_upt(pd);
  if (!upt || !upt->root)
    return;

  uint64_t* l2 = upt->root;

  /* Free user space page tables (indices 0-255) */
  for (int i2 = 0; i2 < 256; i2++) {
    if (!pte_is_valid(l2[i2]) || pte_is_leaf(l2[i2]))
      continue;

    uint64_t* l1 = pte_get_page(l2[i2]);
    for (int i1 = 0; i1 < PT_ENTRY_CNT; i1++) {
      if (!pte_is_valid(l1[i1]) || pte_is_leaf(l1[i1]))
        continue;

      uint64_t* l0 = pte_get_page(l1[i1]);
      palloc_free_page(l0);
    }
    palloc_free_page(l1);
  }

  palloc_free_page(l2);
  palloc_free_page(upt);
}

/*
 * pagedir_dup - Duplicate page directory from parent to child.
 *
 * Copies all present user page mappings. Used by fork().
 */
bool pagedir_dup(pagedir_t child_pd, pagedir_t parent_pd) {
  struct user_page_table* child = pd_to_upt(child_pd);
  struct user_page_table* parent = pd_to_upt(parent_pd);

  if (!child || !parent || !child->root || !parent->root)
    return false;

  uint64_t* pl2 = parent->root;

  /* Iterate through user space (indices 0-255) */
  for (int i2 = 0; i2 < 256; i2++) {
    if (!pte_is_valid(pl2[i2]))
      continue;
    if (pte_is_leaf(pl2[i2]))
      continue; /* Skip gigapages for now */

    uint64_t* pl1 = pte_get_page(pl2[i2]);
    for (int i1 = 0; i1 < PT_ENTRY_CNT; i1++) {
      if (!pte_is_valid(pl1[i1]))
        continue;
      if (pte_is_leaf(pl1[i1]))
        continue; /* Skip megapages */

      uint64_t* pl0 = pte_get_page(pl1[i1]);
      for (int i0 = 0; i0 < PT_ENTRY_CNT; i0++) {
        if (!pte_is_valid(pl0[i0]) || !pte_is_leaf(pl0[i0]))
          continue;

        /* Calculate virtual address */
        uint64_t va = ((uint64_t)i2 << VPN2_SHIFT) | ((uint64_t)i1 << VPN1_SHIFT) |
                      ((uint64_t)i0 << VPN0_SHIFT);

        /* Get parent's physical page and flags */
        uint64_t pa = pte_get_pa(pl0[i0]);
        bool writable = (pl0[i0] & PTE_W) != 0;

        /* Allocate new page for child and copy contents */
        void* new_page = palloc_get_page(PAL_USER);
        if (!new_page)
          return false;

        void* parent_page = ptov(pa);
        memcpy(new_page, parent_page, PGSIZE);

        /* Map in child's address space */
        if (!pagedir_set_page(child_pd, (void*)va, new_page, writable)) {
          palloc_free_page(new_page);
          return false;
        }
      }
    }
  }

  return true;
}

/* ==========================================================================
 * Page Mapping Operations
 * ========================================================================== */

/*
 * pagedir_set_page - Map a user page.
 *
 * @pd: Page directory
 * @upage: User virtual address (must be page-aligned, < PHYS_BASE)
 * @kpage: Kernel virtual address of physical frame
 * @writable: Whether page is writable
 */
bool pagedir_set_page(pagedir_t pd, void* upage, void* kpage, bool writable) {
  ASSERT(pg_ofs(upage) == 0);
  ASSERT(pg_ofs(kpage) == 0);
  ASSERT(is_user_vaddr(upage));

  uint64_t* pte = pagedir_lookup_pte(pd, upage, true);
  if (!pte)
    return false;

  uint64_t pa = vtop(kpage);
  uint64_t flags = PTE_V | PTE_R | PTE_U | PTE_A;
  if (writable)
    flags |= PTE_W | PTE_D;

  *pte = pte_create(pa, flags);
  return true;
}

/*
 * pagedir_get_page - Look up physical frame for user address.
 *
 * Returns kernel virtual address of frame, or NULL if unmapped.
 */
void* pagedir_get_page(pagedir_t pd, const void* uaddr) {
  uint64_t* pte = pagedir_lookup_pte(pd, uaddr, false);
  if (!pte || !pte_is_valid(*pte))
    return NULL;

  uint64_t pa = pte_get_pa(*pte);
  return (void*)(ptov(pa) + pg_ofs(uaddr));
}

/*
 * pagedir_clear_page - Unmap a user page.
 */
void pagedir_clear_page(pagedir_t pd, void* upage) {
  uint64_t* pte = pagedir_lookup_pte(pd, upage, false);
  if (pte && pte_is_valid(*pte)) {
    *pte = 0;
    sfence_vma_va((uintptr_t)upage);
  }
}

/* ==========================================================================
 * PTE Status Bit Operations
 * ========================================================================== */

bool pagedir_is_dirty(pagedir_t pd, const void* vpage) {
  uint64_t* pte = pagedir_lookup_pte(pd, vpage, false);
  return pte && (*pte & PTE_D);
}

void pagedir_set_dirty(pagedir_t pd, const void* vpage, bool dirty) {
  uint64_t* pte = pagedir_lookup_pte(pd, vpage, false);
  if (pte) {
    if (dirty)
      *pte |= PTE_D;
    else
      *pte &= ~PTE_D;
    sfence_vma_va((uintptr_t)vpage);
  }
}

bool pagedir_is_accessed(pagedir_t pd, const void* vpage) {
  uint64_t* pte = pagedir_lookup_pte(pd, vpage, false);
  return pte && (*pte & PTE_A);
}

void pagedir_set_accessed(pagedir_t pd, const void* vpage, bool accessed) {
  uint64_t* pte = pagedir_lookup_pte(pd, vpage, false);
  if (pte) {
    if (accessed)
      *pte |= PTE_A;
    else
      *pte &= ~PTE_A;
    sfence_vma_va((uintptr_t)vpage);
  }
}

bool pagedir_is_writable(pagedir_t pd, const void* vpage) {
  uint64_t* pte = pagedir_lookup_pte(pd, vpage, false);
  return pte && (*pte & PTE_W);
}

void pagedir_set_writable(pagedir_t pd, void* vpage, bool writable) {
  uint64_t* pte = pagedir_lookup_pte(pd, vpage, false);
  if (pte) {
    if (writable)
      *pte |= PTE_W;
    else
      *pte &= ~PTE_W;
    sfence_vma_va((uintptr_t)vpage);
  }
}

/* ==========================================================================
 * Page Directory Activation
 * ========================================================================== */

/*
 * pagedir_activate - Switch to a page directory.
 *
 * If pd is NULL, switches to kernel page table.
 */
void pagedir_activate(pagedir_t pd) {
  if (pd == NULL) {
    /* Use kernel page table */
    uint64_t* kernel_pt = mmu_get_kernel_pt();
    uint64_t satp = SATP_VALUE(SATP_MODE_SV39, 0, vtop(kernel_pt) >> PGBITS);
    csr_write(satp, satp);
    sfence_vma_all();
    current_pd = NULL;
  } else {
    struct user_page_table* upt = pd_to_upt(pd);
    uint64_t satp = SATP_VALUE(SATP_MODE_SV39, upt->asid, vtop(upt->root) >> PGBITS);
    csr_write(satp, satp);
    sfence_vma_all();
    current_pd = pd;
  }
}

/*
 * active_pd - Return currently active page directory.
 */
pagedir_t active_pd(void) { return current_pd; }
