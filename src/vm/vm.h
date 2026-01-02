/*
 * ============================================================================
 *                        VIRTUAL MEMORY SUBSYSTEM
 * ============================================================================
 *
 * This module provides the virtual memory infrastructure for Pintos,
 * implementing demand paging, stack growth, and memory-mapped files.
 *
 * COMPONENTS:
 * -----------
 *   vm.c     - Initialization and coordination
 *   page.c   - Supplemental page table (per-process)
 *   frame.c  - Frame table (global, tracks physical pages)
 *   swap.c   - Swap space management
 *
 * INTEGRATION POINTS:
 * -------------------
 *   - exception.c: page_fault() calls into VM to handle faults
 *   - process.c:   SPT created/destroyed with process lifecycle
 *   - syscall.c:   mmap/munmap syscalls (if implemented)
 *
 * ============================================================================
 */

#ifndef VM_VM_H
#define VM_VM_H

#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * VM SUBSYSTEM INITIALIZATION
 * ============================================================================ */

/* Initialize the virtual memory subsystem.
   Called once during kernel startup, after threads and memory are ready. */
void vm_init(void);

/* ============================================================================
 * PAGE FAULT HANDLING
 * ============================================================================
 *
 * Called from exception.c when a page fault occurs.
 * Returns true if the fault was handled successfully (page loaded).
 * Returns false if the fault is invalid (process should be killed).
 */

/* Handle a page fault at FAULT_ADDR.
   USER: true if fault was from user mode, false if kernel mode.
   WRITE: true if fault was a write, false if read.
   NOT_PRESENT: true if page not present, false if protection violation.
   ESP: stack pointer at time of fault (for stack growth detection). */
bool vm_handle_fault(void* fault_addr, bool user, bool write, bool not_present, void* esp);

/* ============================================================================
 * STACK GROWTH
 * ============================================================================ */

/* Maximum stack size (8 MB). */
#define VM_STACK_MAX (8 * 1024 * 1024)

/* Check if FAULT_ADDR is a valid stack access given ESP.
   PUSHA can access up to 32 bytes below ESP. */
bool vm_is_stack_access(void* fault_addr, void* esp);

#endif /* vm/vm.h */
