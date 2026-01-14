/* arch/i386/arch.h - i386 architecture master header.
 *
 * This header ties together all i386-specific definitions.
 * Portable code should include arch/common/*.h headers instead.
 *
 * i386 architecture characteristics:
 *   - 32-bit addresses and registers
 *   - 2-level page tables (10-10-12 bit split)
 *   - x87 FPU with optional SSE
 *   - Port-mapped I/O (IN/OUT instructions)
 *   - 8259 PIC for interrupt routing
 *   - 8254 PIT for timer
 *   - EFLAGS for processor state
 *   - GDT/LDT for segmentation
 *   - TSS for privilege level transitions
 */

#ifndef ARCH_I386_ARCH_H
#define ARCH_I386_ARCH_H

#include "arch/common/arch.h"

/* Verify we're building for i386 */
#ifndef ARCH_I386
#error "arch/i386/arch.h included but ARCH_I386 not defined"
#endif

/* i386-specific constants */
#define ARCH_I386_PAGE_SIZE 4096 /* 4KB pages */
#define ARCH_I386_PAGE_SHIFT 12
#define ARCH_I386_PT_LEVELS 2 /* 2-level page tables */
#define ARCH_I386_PDE_BITS 10 /* Page directory index bits */
#define ARCH_I386_PTE_BITS 10 /* Page table index bits */

/* Physical memory mapping location (3GB mark) */
#define ARCH_I386_PHYS_BASE 0xC0000000

/* Kernel loads at 128KB physical */
#define ARCH_I386_KERN_BASE 0x20000

/* GDT selectors - must match loader.S and gdt.c */
#define ARCH_I386_SEL_NULL 0x00
#define ARCH_I386_SEL_KCSEG 0x08 /* Kernel code segment */
#define ARCH_I386_SEL_KDSEG 0x10 /* Kernel data segment */
#define ARCH_I386_SEL_UCSEG 0x1B /* User code segment (RPL=3) */
#define ARCH_I386_SEL_UDSEG 0x23 /* User data segment (RPL=3) */
#define ARCH_I386_SEL_TSS 0x28   /* Task state segment */

/* EFLAGS bits */
#define ARCH_I386_FLAG_CF 0x00000001   /* Carry flag */
#define ARCH_I386_FLAG_PF 0x00000004   /* Parity flag */
#define ARCH_I386_FLAG_AF 0x00000010   /* Auxiliary carry */
#define ARCH_I386_FLAG_ZF 0x00000040   /* Zero flag */
#define ARCH_I386_FLAG_SF 0x00000080   /* Sign flag */
#define ARCH_I386_FLAG_TF 0x00000100   /* Trap flag */
#define ARCH_I386_FLAG_IF 0x00000200   /* Interrupt enable */
#define ARCH_I386_FLAG_DF 0x00000400   /* Direction flag */
#define ARCH_I386_FLAG_OF 0x00000800   /* Overflow flag */
#define ARCH_I386_FLAG_IOPL 0x00003000 /* I/O privilege level */
#define ARCH_I386_FLAG_NT 0x00004000   /* Nested task */
#define ARCH_I386_FLAG_RF 0x00010000   /* Resume flag */
#define ARCH_I386_FLAG_VM 0x00020000   /* Virtual 8086 mode */
#define ARCH_I386_FLAG_AC 0x00040000   /* Alignment check */
#define ARCH_I386_FLAG_VIF 0x00080000  /* Virtual interrupt */
#define ARCH_I386_FLAG_VIP 0x00100000  /* Virtual interrupt pending */
#define ARCH_I386_FLAG_ID 0x00200000   /* CPUID available */

/* Minimum EFLAGS value (bit 1 must always be set) */
#define ARCH_I386_FLAG_MBS 0x00000002

/* CR0 bits */
#define ARCH_I386_CR0_PE 0x00000001 /* Protection enable */
#define ARCH_I386_CR0_MP 0x00000002 /* Monitor coprocessor */
#define ARCH_I386_CR0_EM 0x00000004 /* Emulation */
#define ARCH_I386_CR0_TS 0x00000008 /* Task switched */
#define ARCH_I386_CR0_ET 0x00000010 /* Extension type */
#define ARCH_I386_CR0_NE 0x00000020 /* Numeric error */
#define ARCH_I386_CR0_WP 0x00010000 /* Write protect */
#define ARCH_I386_CR0_AM 0x00040000 /* Alignment mask */
#define ARCH_I386_CR0_NW 0x20000000 /* Not write-through */
#define ARCH_I386_CR0_CD 0x40000000 /* Cache disable */
#define ARCH_I386_CR0_PG 0x80000000 /* Paging enable */

/* CR4 bits */
#define ARCH_I386_CR4_VME 0x00000001 /* Virtual 8086 extensions */
#define ARCH_I386_CR4_PVI 0x00000002 /* Protected virtual interrupts */
#define ARCH_I386_CR4_TSD 0x00000004 /* Time stamp disable */
#define ARCH_I386_CR4_DE 0x00000008  /* Debugging extensions */
#define ARCH_I386_CR4_PSE 0x00000010 /* Page size extensions */
#define ARCH_I386_CR4_PAE 0x00000020 /* Physical address extension */
#define ARCH_I386_CR4_MCE 0x00000040 /* Machine check enable */
#define ARCH_I386_CR4_PGE 0x00000080 /* Page global enable */

/* Exception/interrupt vector numbers */
#define ARCH_I386_VEC_DE 0  /* Divide error */
#define ARCH_I386_VEC_DB 1  /* Debug */
#define ARCH_I386_VEC_NMI 2 /* Non-maskable interrupt */
#define ARCH_I386_VEC_BP 3  /* Breakpoint */
#define ARCH_I386_VEC_OF 4  /* Overflow */
#define ARCH_I386_VEC_BR 5  /* Bound range exceeded */
#define ARCH_I386_VEC_UD 6  /* Invalid opcode */
#define ARCH_I386_VEC_NM 7  /* Device not available */
#define ARCH_I386_VEC_DF 8  /* Double fault */
#define ARCH_I386_VEC_TS 10 /* Invalid TSS */
#define ARCH_I386_VEC_NP 11 /* Segment not present */
#define ARCH_I386_VEC_SS 12 /* Stack segment fault */
#define ARCH_I386_VEC_GP 13 /* General protection fault */
#define ARCH_I386_VEC_PF 14 /* Page fault */
#define ARCH_I386_VEC_MF 16 /* x87 FPU error */
#define ARCH_I386_VEC_AC 17 /* Alignment check */
#define ARCH_I386_VEC_MC 18 /* Machine check */
#define ARCH_I386_VEC_XM 19 /* SIMD floating point */

/* Page fault error code bits (pushed by CPU) */
#define ARCH_I386_PF_P 0x1    /* 1 = protection violation, 0 = not present */
#define ARCH_I386_PF_W 0x2    /* 1 = write fault, 0 = read fault */
#define ARCH_I386_PF_U 0x4    /* 1 = user mode, 0 = kernel mode */
#define ARCH_I386_PF_RSVD 0x8 /* 1 = reserved bit violation */
#define ARCH_I386_PF_ID 0x10  /* 1 = instruction fetch */

#endif /* ARCH_I386_ARCH_H */
