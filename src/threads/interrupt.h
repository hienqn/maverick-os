/* threads/interrupt.h - Compatibility shim for arch-specific interrupts.
 *
 * This header exists for backward compatibility.
 * New code should:
 *   - Use "arch/common/intr.h" for portable interrupt control
 *   - Use arch-specific headers for intr_frame structure
 *
 * Key differences:
 *   i386:    intr_frame has 76 bytes (8 GPRs + segments + error code)
 *   RISC-V:  intr_frame has 280 bytes (31 GPRs + 4 CSRs)
 */

#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#ifdef ARCH_I386
#include "arch/i386/intr.h"
#elif defined(ARCH_RISCV64)
#include "arch/riscv64/intr.h"
#else
#error "No architecture defined"
#endif

#endif /* THREADS_INTERRUPT_H */
