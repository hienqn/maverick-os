/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                        EXCEPTION MODULE                                   ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  Handles CPU exceptions caused by user programs. Most exceptions         ║
 * ║  result in terminating the offending process with exit code -1.          ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 *
 * PAGE FAULT HANDLING ALGORITHM
 * ─────────────────────────────
 *
 * When a page fault occurs, the handler follows this decision tree:
 *
 *   Page Fault at address A
 *         │
 *         ▼
 *   ┌─────────────────────────────┐
 *   │ Call vm_handle_fault(A)     │
 *   └─────────────┬───────────────┘
 *                 │
 *         ┌───────┴───────┐
 *         │ Handled?      │
 *         └───────┬───────┘
 *            yes/ \no
 *              /   \
 *             ▼     ▼
 *     ┌─────────┐  ┌─────────────────────────────┐
 *     │ Return  │  │ Is kernel accessing user    │
 *     │ to user │  │ address? (syscall context)  │
 *     └─────────┘  └─────────────┬───────────────┘
 *                           yes/ \no
 *                             /   \
 *                            ▼     ▼
 *                    ┌───────────┐ ┌───────────┐
 *                    │ Kill user │ │ Kill/Panic│
 *                    │ process   │ │           │
 *                    └───────────┘ └───────────┘
 *
 * vm_handle_fault() checks (in order):
 *   1. Stack growth: Is A below esp but within allowed growth range?
 *   2. Demand paging: Is there an SPT entry for this page?
 *   3. If SPT entry exists, load page (from file, swap, or zero-fill)
 *
 * Stack Growth Detection:
 *   - Valid if fault_addr >= esp - 32 (PUSHA instruction pushes 32 bytes)
 *   - Valid if fault_addr >= STACK_BOTTOM (stack cannot grow indefinitely)
 *   - Must be a write access to a not-present page
 *
 * Termination Conditions:
 *   - Access to unmapped memory (no SPT entry, not valid stack growth)
 *   - Write to read-only page (protection violation)
 *   - Access to kernel address from user mode
 */

#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H

/* ═══════════════════════════════════════════════════════════════════════════
 * PAGE FAULT ERROR CODE BITS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * When a page fault occurs, the CPU pushes an error code with these bits:
 *
 *   Bit 0 (PF_P): Present
 *     0 = Fault caused by non-present page
 *     1 = Fault caused by protection violation (page present but access denied)
 *
 *   Bit 1 (PF_W): Write
 *     0 = Fault caused by a read access
 *     1 = Fault caused by a write access
 *
 *   Bit 2 (PF_U): User
 *     0 = Fault occurred in kernel mode
 *     1 = Fault occurred in user mode
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

#define PF_P 0x1 /* 0: not-present page. 1: access rights violation. */
#define PF_W 0x2 /* 0: read, 1: write. */
#define PF_U 0x4 /* 0: kernel, 1: user process. */

/* ═══════════════════════════════════════════════════════════════════════════
 * PUBLIC INTERFACE
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Initializes exception handlers. Called once during kernel startup. */
void exception_init(void);

/* Prints exception statistics (number of page faults). */
void exception_print_stats(void);

#endif /* userprog/exception.h */
