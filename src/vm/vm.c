/*
 * ============================================================================
 *                    VIRTUAL MEMORY - INITIALIZATION
 * ============================================================================
 */

#include "vm/vm.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/vaddr.h"
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
bool vm_handle_fault(void* fault_addr, bool user UNUSED, bool write UNUSED, bool not_present UNUSED,
                     void* esp UNUSED) {
  /* Round down to page boundary. */
  void* fault_page = pg_round_down(fault_addr);

  (void)fault_page;

  /* TODO: Implement page fault handling:
   *
   * 1. Look up fault_page in current process's supplemental page table
   *
   * 2. If not found:
   *    a. Check if this is valid stack growth (vm_is_stack_access)
   *    b. If valid stack growth, create new SPT entry and allocate frame
   *    c. Otherwise, return false (invalid access)
   *
   * 3. If found, load page based on its status:
   *    a. PAGE_ZERO:  Allocate zeroed frame
   *    b. PAGE_FILE:  Read from file into frame
   *    c. PAGE_SWAP:  Read from swap into frame
   *
   * 4. Install page in hardware page table (pagedir_set_page)
   *
   * 5. Update SPT entry status
   *
   * 6. Return true (fault handled)
   */

  return false;
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

  (void)fault_addr;
  (void)esp;

  /* TODO: Implement stack growth detection. */
  return false;
}
