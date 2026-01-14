/* threads/vaddr.h - Compatibility shim for arch-specific virtual addresses.
 *
 * This header exists for backward compatibility.
 * New code should:
 *   - Use "arch/common/mmu.h" for portable page functions (pg_round_up, etc.)
 *   - Use arch-specific headers for PHYS_BASE, ptov(), vtop()
 *
 * Memory layout differences:
 *   i386:    PHYS_BASE = 0xC0000000 (3GB), 32-bit addresses
 *   RISC-V:  PHYS_BASE = 0xFFFFFFFF80000000, 64-bit Sv39 addresses
 */

#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#ifdef ARCH_I386
#include "arch/i386/vaddr.h"
#elif defined(ARCH_RISCV64)
#include "arch/riscv64/vaddr.h"
#else
#error "No architecture defined"
#endif

#endif /* THREADS_VADDR_H */
