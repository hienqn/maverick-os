/* arch/common/arch.h - Architecture abstraction master header.
 *
 * This header selects the correct architecture-specific headers based on
 * the ARCH_* define set by the build system.
 *
 * Usage: #include "arch/common/arch.h"
 * This will include the correct architecture-specific implementations.
 */

#ifndef ARCH_COMMON_ARCH_H
#define ARCH_COMMON_ARCH_H

#if defined(ARCH_I386)
#define ARCH_NAME "i386"
#define ARCH_BITS 32
/* i386 uses headers from their original locations for now */

#elif defined(ARCH_RISCV64)
#define ARCH_NAME "riscv64"
#define ARCH_BITS 64
/* RISC-V headers will be in arch/riscv64/ */

#else
#error "No architecture defined. Build system should define ARCH_I386 or ARCH_RISCV64."
#endif

#endif /* ARCH_COMMON_ARCH_H */
