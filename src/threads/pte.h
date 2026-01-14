/* threads/pte.h - Compatibility shim for arch-specific page table entries.
 *
 * This header exists for backward compatibility.
 * New code should:
 *   - Use "arch/common/mmu.h" for portable page operations
 *   - Use "arch/i386/pte.h" or "arch/riscv64/pte.h" for arch-specific PTE formats
 *
 * Page table differences:
 *   i386:    2-level (10-10-12), 32-bit PTEs
 *   RISC-V:  3-level Sv39 (9-9-9-12), 64-bit PTEs
 */

#ifndef THREADS_PTE_H
#define THREADS_PTE_H

#ifdef ARCH_I386
#include "arch/i386/pte.h"
#elif defined(ARCH_RISCV64)
#include "arch/riscv64/pte.h"
#else
#error "No architecture defined"
#endif

#endif /* THREADS_PTE_H */
