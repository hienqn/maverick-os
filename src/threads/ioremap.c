/**
 * @file threads/ioremap.c
 * @brief I/O memory mapping implementation.
 *
 * Maps physical MMIO addresses into kernel virtual address space.
 * Uses a dedicated virtual address region starting at IOREMAP_BASE.
 */

#include "threads/ioremap.h"
#include "threads/pte.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/init.h"
#include <debug.h>
#include <round.h>
#include <stdio.h>

/* MMIO virtual address region.
   We reserve a 16MB region for MMIO mappings.
   This is above the typical kernel heap but below the page tables. */
#define IOREMAP_BASE 0xFD000000
#define IOREMAP_SIZE (16 * 1024 * 1024) /* 16 MB */
#define IOREMAP_END (IOREMAP_BASE + IOREMAP_SIZE)

/* Track the next available virtual address for MMIO */
static uintptr_t ioremap_next = IOREMAP_BASE;
static struct lock ioremap_lock;
static bool ioremap_initialized = false;

void ioremap_init(void) {
  lock_init(&ioremap_lock);
  ioremap_initialized = true;
}

/**
 * Create a PTE for an MMIO physical address.
 * Unlike pte_create_kernel(), this takes a physical address directly
 * instead of a virtual address.
 */
static uint32_t pte_create_mmio(uintptr_t phys_addr) {
  ASSERT((phys_addr & ~PTE_ADDR) == 0); /* Must be page-aligned */
  /* MMIO: present, writable, kernel-only, no caching would be ideal
     but x86 doesn't have a no-cache bit in basic PTEs (needs PAT/MTRR) */
  return phys_addr | PTE_P | PTE_W;
}

void* ioremap(uintptr_t phys_addr, size_t size) {
  uint32_t* pd;
  uintptr_t vaddr_start, vaddr;
  uintptr_t paddr_page;
  size_t offset, num_pages, i;

  if (!ioremap_initialized) {
    /* Early boot - just return NULL; driver should retry after init */
    return NULL;
  }

  if (size == 0)
    return NULL;

  /* Calculate offset within page and number of pages needed */
  offset = phys_addr & PGMASK;
  paddr_page = phys_addr & ~PGMASK;
  num_pages = DIV_ROUND_UP(offset + size, PGSIZE);

  lock_acquire(&ioremap_lock);

  /* Check if we have enough space */
  if (ioremap_next + num_pages * PGSIZE > IOREMAP_END) {
    lock_release(&ioremap_lock);
    printf("ioremap: out of virtual address space\n");
    return NULL;
  }

  /* Allocate virtual address range */
  vaddr_start = ioremap_next;
  ioremap_next += num_pages * PGSIZE;

  /* Get the kernel's page directory */
  pd = init_page_dir;

  /* Map each page */
  for (i = 0; i < num_pages; i++) {
    vaddr = vaddr_start + i * PGSIZE;
    uintptr_t paddr = paddr_page + i * PGSIZE;
    size_t pde_idx = pd_no((void*)vaddr);
    size_t pte_idx = pt_no((void*)vaddr);
    uint32_t* pt;

    /* Get or create page table */
    if (pd[pde_idx] == 0) {
      /* Allocate a new page table */
      pt = palloc_get_page(PAL_ASSERT | PAL_ZERO);
      pd[pde_idx] = pde_create(pt);
    } else {
      /* Page table already exists - get it */
      pt = pde_get_pt(pd[pde_idx]);
    }

    /* Create PTE for MMIO */
    ASSERT(pt[pte_idx] == 0); /* Should not be already mapped */
    pt[pte_idx] = pte_create_mmio(paddr);
  }

  /* Flush TLB to ensure new mappings are visible */
  asm volatile("movl %%cr3, %%eax; movl %%eax, %%cr3" ::: "eax", "memory");

  lock_release(&ioremap_lock);

  /* Return virtual address with original offset */
  return (void*)(vaddr_start + offset);
}

void iounmap(void* virt_addr, size_t size) {
  /* For simplicity, we don't reclaim MMIO virtual address space.
     Device drivers typically stay loaded for the system lifetime.
     A full implementation would:
     1. Clear the PTEs
     2. Track and recycle the virtual address range
     3. Flush TLB */
  (void)virt_addr;
  (void)size;
}
