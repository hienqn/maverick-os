/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                         SYSCALL MODULE                                    ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  System call interface for user programs. System calls are invoked via   ║
 * ║  INT 0x30 with the syscall number and arguments on the user stack.       ║
 * ║                                                                          ║
 * ║  See lib/syscall-nr.h for the list of syscall numbers.                   ║
 * ║  See lib/user/syscall.h for the user-space syscall wrappers.             ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* Forward declaration of interrupt frame */
struct intr_frame;

/* Initializes the system call handler.
   Called once during kernel startup. */
void syscall_init(void);

/* System call handler - dispatches syscalls to their implementations.
   x86: Called from INT 0x30 handler.
   RISC-V: Called from trap handler on ECALL. */
void syscall_handler(struct intr_frame* f);

#endif /* userprog/syscall.h */
