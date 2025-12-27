/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                        EXCEPTION MODULE                                   ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  Handles CPU exceptions caused by user programs. Most exceptions         ║
 * ║  result in terminating the offending process with exit code -1.          ║
 * ║                                                                          ║
 * ║  Page faults are currently handled as errors, but for virtual memory     ║
 * ║  they would trigger demand paging, stack growth, or copy-on-write.       ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
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
