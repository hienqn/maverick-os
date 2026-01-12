/* threads/loader.h - Compatibility shim for arch-specific loader constants.
 *
 * This header exists for backward compatibility.
 * New code should include architecture-specific headers directly.
 *
 * Note: RISC-V doesn't use a BIOS loader; OpenSBI handles boot.
 */

#ifndef THREADS_LOADER_H
#define THREADS_LOADER_H

#ifdef ARCH_I386
#include "arch/i386/loader.h"
#elif defined(ARCH_RISCV64)
/* RISC-V boot is handled by OpenSBI, different constants apply */
#include "arch/riscv64/boot.h"
#else
#error "No architecture defined"
#endif

#endif /* THREADS_LOADER_H */
