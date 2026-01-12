/* threads/switch.h - Compatibility shim for arch-specific context switch.
 *
 * This header exists for backward compatibility.
 * New code should include architecture-specific headers directly.
 *
 * Context switch differences:
 *   i386:    Saves 4 callee-saved regs (ebx, esi, edi, ebp)
 *   RISC-V:  Saves 12 callee-saved regs (s0-s11) + ra
 */

#ifndef THREADS_SWITCH_H
#define THREADS_SWITCH_H

#ifdef ARCH_I386
#include "arch/i386/switch.h"
#elif defined(ARCH_RISCV64)
#include "arch/riscv64/switch.h"
#else
#error "No architecture defined"
#endif

#endif /* THREADS_SWITCH_H */
