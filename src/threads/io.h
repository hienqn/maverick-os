/* threads/io.h - Compatibility shim for arch-specific I/O.
 *
 * This header exists for backward compatibility.
 * New code should:
 *   - Use "arch/common/io.h" for portable MMIO operations
 *   - Use "arch/i386/io.h" directly for x86 port I/O
 */

#ifndef THREADS_IO_H
#define THREADS_IO_H

#ifdef ARCH_I386
#include "arch/i386/io.h"
#elif defined(ARCH_RISCV64)
/* RISC-V uses MMIO, not port I/O. Include arch/common/io.h for MMIO. */
#error "threads/io.h provides x86 port I/O. Use arch/common/io.h for MMIO."
#else
#error "No architecture defined"
#endif

#endif /* THREADS_IO_H */
