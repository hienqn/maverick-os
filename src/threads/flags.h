/* threads/flags.h - Compatibility shim for arch-specific flags.
 *
 * This header exists for backward compatibility.
 * New code should include "arch/i386/flags.h" directly for i386
 * or use architecture-neutral patterns.
 */

#ifndef THREADS_FLAGS_H
#define THREADS_FLAGS_H

#ifdef ARCH_I386
#include "arch/i386/flags.h"
#elif defined(ARCH_RISCV64)
/* RISC-V doesn't have EFLAGS; uses sstatus CSR instead */
#error "threads/flags.h is x86-specific. Use arch/riscv64/csr.h for RISC-V."
#else
#error "No architecture defined"
#endif

#endif /* THREADS_FLAGS_H */
