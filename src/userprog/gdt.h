/* userprog/gdt.h - Compatibility shim for arch-specific GDT.
 *
 * This header exists for backward compatibility.
 * GDT (Global Descriptor Table) is x86-specific for segmentation.
 * RISC-V doesn't have segmentation; uses page table U bit instead.
 */

#ifndef USERPROG_GDT_H
#define USERPROG_GDT_H

#ifdef ARCH_I386
#include "arch/i386/gdt.h"
#elif defined(ARCH_RISCV64)
/* RISC-V has no segmentation. Privilege is controlled via PTE.U bit
   and sstatus.SPP. Provide stub declarations for code that conditionally
   compiles. */
static inline void gdt_init(void) { /* No-op on RISC-V */
}
#else
#error "No architecture defined"
#endif

#endif /* USERPROG_GDT_H */
