/* userprog/tss.h - Compatibility shim for arch-specific TSS.
 *
 * This header exists for backward compatibility.
 * TSS (Task State Segment) is x86-specific for ring transitions.
 * RISC-V handles kernel stack via sscratch CSR instead.
 */

#ifndef USERPROG_TSS_H
#define USERPROG_TSS_H

#ifdef ARCH_I386
#include "arch/i386/tss.h"
#elif defined(ARCH_RISCV64)
/* RISC-V has no TSS. Kernel stack pointer is saved in sscratch CSR
   during trap entry from user mode. Provide stubs for compatibility. */
struct tss;                         /* Opaque on RISC-V */
static inline void tss_init(void) { /* No-op on RISC-V */
}
static inline struct tss* tss_get(void) { return (void*)0; }
static inline void tss_update(void) { /* No-op on RISC-V */
}
#else
#error "No architecture defined"
#endif

#endif /* USERPROG_TSS_H */
