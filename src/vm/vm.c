/*
 * ============================================================================
 *                    VIRTUAL MEMORY - INITIALIZATION
 * ============================================================================
 */

#include "vm/vm.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include <stdio.h>

/* ============================================================================
 * VM INITIALIZATION
 * ============================================================================ */

/* Initialize the virtual memory subsystem. */
void vm_init(void) {
  /* Initialize frame table. */
  frame_init();

  /* Initialize swap space. */
  swap_init();

  /* TODO: Any other global VM initialization. */
}

/* ============================================================================
 * PAGE FAULT HANDLING
 * ============================================================================ */

/* Handle a page fault. Returns true if handled, false if invalid. */
bool vm_handle_fault(void* fault_addr, bool user UNUSED, bool write, bool not_present, void* esp) {
  struct thread* t = thread_current();

  /* Must have a valid PCB. */
  if (t->pcb == NULL)
    return false;

  /* Only handle not-present faults for now.
     Protection violations (write to read-only) are handled separately. */
  if (!not_present)
    return false;

  /* Round down to page boundary. */
  void* fault_page = pg_round_down(fault_addr);

  /* Must be a user address. */
  if (!is_user_vaddr(fault_addr))
    return false;

  /* Look up in supplemental page table. */
  struct spt_entry* spte = spt_find(&t->pcb->spt, fault_page);

  if (spte == NULL) {
    /* Page not in SPT. Check if this is valid stack growth. */
    if (vm_is_stack_access(fault_addr, esp)) {
      /* Create a zero page for stack growth. */
      if (!spt_create_zero_page(&t->pcb->spt, fault_page, true))
        return false;

      /* Look it up again now that we created it. */
      spte = spt_find(&t->pcb->spt, fault_page);
      if (spte == NULL)
        return false;
    } else {
      /* Not a valid access. */
      return false;
    }
  }

  /* Check write permission. */
  if (write && !spte->writable)
    return false;

  /* Page is already loaded? Shouldn't happen for not_present fault. */
  if (spte->status == PAGE_FRAME)
    return false;

  /* Load the page into memory. */
  if (!spt_load_page(spte))
    return false;

  /* Unpin the frame so it can be evicted later. */
  frame_unpin(spte->kpage);

  return true;
}

/* ============================================================================
 * STACK GROWTH
 * ============================================================================ */

/* Check if FAULT_ADDR is a valid stack access. */
bool vm_is_stack_access(void* fault_addr, void* esp) {
  /* Stack grows downward from PHYS_BASE.
     PUSHA instruction can touch up to 32 bytes below ESP.

     Valid stack access if:
     - fault_addr is below PHYS_BASE (user space)
     - fault_addr >= esp - 32 (within PUSHA range)
     - fault_addr >= PHYS_BASE - VM_STACK_MAX (within max stack size)
  */

  /* Must be in user space. */
  if (!is_user_vaddr(fault_addr))
    return false;

  /* Must be within 32 bytes of ESP (PUSHA can push 32 bytes). */
  if ((uint8_t*)fault_addr < (uint8_t*)esp - 32)
    return false;

  /* Must be within max stack size (8MB from PHYS_BASE). */
  if ((uint8_t*)fault_addr < (uint8_t*)PHYS_BASE - VM_STACK_MAX)
    return false;

  return true;
}
