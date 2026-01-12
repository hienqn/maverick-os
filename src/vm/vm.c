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
#include <string.h>

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

/* Handle a page fault. Returns true if handled, false if invalid.

   SYNCHRONIZATION:
   ----------------
   This function must handle races with frame eviction. Key considerations:
   1. SPT entries can be modified by eviction at any time
   2. COW pages can be evicted between when we read kpage and try to pin it
   3. We must not hold spt_lock when calling frame_alloc (could deadlock)

   Lock ordering: frame_lock -> spt_lock (eviction holds this order)
   We cannot hold spt_lock when calling frame_alloc because:
   - frame_alloc may call frame_evict
   - frame_evict acquires frame_lock, then spt_lock
   - If we held spt_lock, we'd deadlock

   Solution for COW: Use frame_pin_if_present which atomically checks and pins.
   If it fails, the frame was evicted and we treat it as a not-present fault. */
bool vm_handle_fault(void* fault_addr, bool user UNUSED, bool write, bool not_present, void* esp) {
  struct thread* t = thread_current();

  /* Must have a valid PCB. */
  if (t->pcb == NULL)
    return false;

  /* Round down to page boundary. */
  void* fault_page = pg_round_down(fault_addr);

  /* Must be a user address. */
  if (!is_user_vaddr(fault_addr))
    return false;

  struct spt* spt = &t->pcb->spt;

  /* Look up in supplemental page table while holding spt_lock.
     This prevents eviction from modifying the entry while we read it. */
  lock_acquire(&spt->spt_lock);
  struct spt_entry* spte = spt_find(spt, fault_page);

  /* Handle COW fault: page is present but write-protected for COW.
     Detection: not_present==false, write==true, spte->status==PAGE_COW */
  if (!not_present && write && spte != NULL && spte->status == PAGE_COW) {
    /* Must be originally writable to allow COW copy. */
    if (!spte->writable) {
      lock_release(&spt->spt_lock);
      return false;
    }

    /* Read kpage while holding lock, then try to pin it atomically. */
    void* old_kpage = spte->kpage;
    lock_release(&spt->spt_lock);

    /* Try to pin the old frame. If this fails, the frame was evicted
       between our SPT lookup and now. In that case, treat this as a
       not-present fault (the page needs to be loaded from swap). */
    if (!frame_pin_if_present(old_kpage)) {
      /* Frame was evicted. The SPT entry should now be PAGE_SWAP.
         Retry as a not-present fault to load from swap. */
      return vm_handle_fault(fault_addr, user, write, true, esp);
    }

    /* Allocate a new frame for the private copy.
       Note: We don't hold spt_lock here to avoid deadlock with eviction. */
    void* new_kpage = frame_alloc(fault_page, true);
    if (new_kpage == NULL) {
      frame_unpin(old_kpage);
      return false;
    }

    /* Copy contents from shared frame to new frame. */
    memcpy(new_kpage, old_kpage, PGSIZE);

    /* Unpin old frame now that copy is complete. */
    frame_unpin(old_kpage);

    /* Update page table to point to new frame with write permission. */
    pagedir_clear_page(t->pcb->pagedir, fault_page);
    if (!pagedir_set_page(t->pcb->pagedir, fault_page, new_kpage, true)) {
      frame_free(new_kpage);
      return false;
    }

    /* Update SPT entry while holding lock. */
    lock_acquire(&spt->spt_lock);
    spte->status = PAGE_FRAME;
    spte->kpage = new_kpage;
    lock_release(&spt->spt_lock);

    /* Release reference to old shared frame. */
    frame_free(old_kpage);

    /* Unpin the new frame. */
    frame_unpin(new_kpage);

    return true;
  }

  /* For non-COW faults, only handle not-present faults.
     Other protection violations are invalid. */
  if (!not_present) {
    lock_release(&spt->spt_lock);
    return false;
  }

  if (spte == NULL) {
    lock_release(&spt->spt_lock);

    /* Page not in SPT. Check if this is valid stack growth. */
    if (vm_is_stack_access(fault_addr, esp)) {
      /* Create a zero page for stack growth. */
      if (!spt_create_zero_page(spt, fault_page, true))
        return false;

      /* Look it up again now that we created it. */
      lock_acquire(&spt->spt_lock);
      spte = spt_find(spt, fault_page);
      if (spte == NULL) {
        lock_release(&spt->spt_lock);
        return false;
      }
    } else {
      /* Not a valid access. */
      return false;
    }
  }

  /* Check write permission. */
  if (write && !spte->writable) {
    lock_release(&spt->spt_lock);
    return false;
  }

  /* Page is already loaded? Shouldn't happen for not_present fault. */
  if (spte->status == PAGE_FRAME) {
    lock_release(&spt->spt_lock);
    return false;
  }

  /* Release lock before loading - spt_load_page calls frame_alloc which
     could trigger eviction, and eviction needs to acquire spt_lock.
     The entry's status is not PAGE_FRAME, so eviction won't touch it. */
  lock_release(&spt->spt_lock);

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
