/* arch/common/arch.h - Architecture abstraction master header.
 *
 * This header provides architecture detection and common definitions.
 * It does NOT automatically include all arch-specific headers to avoid
 * circular dependencies. Include specific HAL headers as needed:
 *
 *   #include "arch/common/cpu.h"   - CPU control (barriers, halt)
 *   #include "arch/common/intr.h"  - Interrupt enable/disable
 *   #include "arch/common/mmu.h"   - Page table operations
 *   #include "arch/common/timer.h" - Timer interface
 *   #include "arch/common/io.h"    - I/O operations
 */

#ifndef ARCH_COMMON_ARCH_H
#define ARCH_COMMON_ARCH_H

/* Architecture detection - set by build system via -DARCH_I386 or -DARCH_RISCV64 */
#if defined(ARCH_I386)

#define ARCH_NAME "i386"
#define ARCH_BITS 32
#define ARCH_IS_32BIT 1
#define ARCH_IS_64BIT 0

#elif defined(ARCH_RISCV64)
#define ARCH_NAME "riscv64"
#define ARCH_BITS 64
#define ARCH_IS_32BIT 0
#define ARCH_IS_64BIT 1

#else
#error "No architecture defined. Build system should define ARCH_I386 or ARCH_RISCV64."
#endif

/* Common type definitions */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Architecture-specific pointer-sized types */
#if ARCH_IS_32BIT
typedef uint32_t uword_t; /* Unsigned word (register size) */
typedef int32_t sword_t;  /* Signed word */
#define WORD_FMT "0x%08x"
#else
typedef uint64_t uword_t;
typedef int64_t sword_t;
#define WORD_FMT "0x%016lx"
#endif

#endif /* ARCH_COMMON_ARCH_H */
