/* arch/riscv64/csr.h - RISC-V Control and Status Register definitions.
 *
 * CSRs are special registers accessed via CSR instructions (csrr, csrw, etc.).
 * This header provides macros for CSR access and defines important CSR values.
 *
 * Reference: RISC-V Privileged Specification v1.12
 */

#ifndef ARCH_RISCV64_CSR_H
#define ARCH_RISCV64_CSR_H

#include <stdint.h>

/* ==========================================================================
 * CSR Access Macros
 * ========================================================================== */

/* Read a CSR */
#define csr_read(csr)                                                                              \
  ({                                                                                               \
    uint64_t __val;                                                                                \
    asm volatile("csrr %0, " #csr : "=r"(__val));                                                  \
    __val;                                                                                         \
  })

/* Write a CSR */
#define csr_write(csr, val)                                                                        \
  ({                                                                                               \
    uint64_t __val = (uint64_t)(val);                                                              \
    asm volatile("csrw " #csr ", %0" : : "r"(__val));                                              \
  })

/* Read and then write a CSR (returns old value) */
#define csr_swap(csr, val)                                                                         \
  ({                                                                                               \
    uint64_t __val = (uint64_t)(val);                                                              \
    uint64_t __old;                                                                                \
    asm volatile("csrrw %0, " #csr ", %1" : "=r"(__old) : "r"(__val));                             \
    __old;                                                                                         \
  })

/* Set bits in a CSR (returns old value) */
#define csr_set(csr, val)                                                                          \
  ({                                                                                               \
    uint64_t __val = (uint64_t)(val);                                                              \
    uint64_t __old;                                                                                \
    asm volatile("csrrs %0, " #csr ", %1" : "=r"(__old) : "r"(__val));                             \
    __old;                                                                                         \
  })

/* Clear bits in a CSR (returns old value) */
#define csr_clear(csr, val)                                                                        \
  ({                                                                                               \
    uint64_t __val = (uint64_t)(val);                                                              \
    uint64_t __old;                                                                                \
    asm volatile("csrrc %0, " #csr ", %1" : "=r"(__old) : "r"(__val));                             \
    __old;                                                                                         \
  })

/* ==========================================================================
 * Supervisor-mode CSR Addresses
 * ========================================================================== */

/* Supervisor Trap Setup */
#define CSR_SSTATUS 0x100 /* Supervisor status register */
#define CSR_SIE 0x104     /* Supervisor interrupt-enable register */
#define CSR_STVEC 0x105   /* Supervisor trap handler base address */
#define CSR_SCOUNTEREN 0x106

/* Supervisor Configuration */
#define CSR_SENVCFG 0x10A

/* Supervisor Trap Handling */
#define CSR_SSCRATCH 0x140 /* Scratch register for supervisor trap handlers */
#define CSR_SEPC 0x141     /* Supervisor exception program counter */
#define CSR_SCAUSE 0x142   /* Supervisor trap cause */
#define CSR_STVAL 0x143    /* Supervisor bad address or instruction */
#define CSR_SIP 0x144      /* Supervisor interrupt pending */

/* Supervisor Protection and Translation */
#define CSR_SATP 0x180 /* Supervisor address translation and protection */

/* ==========================================================================
 * sstatus Register Bits
 * ========================================================================== */

#define SSTATUS_SIE (1UL << 1)  /* Supervisor Interrupt Enable */
#define SSTATUS_SPIE (1UL << 5) /* Prior Supervisor Interrupt Enable */
#define SSTATUS_UBE (1UL << 6)  /* User-mode Big Endian */
#define SSTATUS_SPP (1UL << 8)  /* Supervisor Previous Privilege (0=U, 1=S) */
#define SSTATUS_VS (3UL << 9)   /* Vector extension state */
#define SSTATUS_FS (3UL << 13)  /* Floating-point state */
#define SSTATUS_XS (3UL << 15)  /* User extension state */
#define SSTATUS_SUM (1UL << 18) /* Supervisor User Memory access */
#define SSTATUS_MXR (1UL << 19) /* Make eXecutable Readable */
#define SSTATUS_UXL (3UL << 32) /* User XLEN */
#define SSTATUS_SD (1UL << 63)  /* State Dirty summary */

/* FS field values */
#define SSTATUS_FS_OFF (0UL << 13)
#define SSTATUS_FS_INITIAL (1UL << 13)
#define SSTATUS_FS_CLEAN (2UL << 13)
#define SSTATUS_FS_DIRTY (3UL << 13)

/* ==========================================================================
 * sie/sip Register Bits (Supervisor Interrupt Enable/Pending)
 * ========================================================================== */

#define SIE_SSIE (1UL << 1) /* Supervisor Software Interrupt Enable */
#define SIE_STIE (1UL << 5) /* Supervisor Timer Interrupt Enable */
#define SIE_SEIE (1UL << 9) /* Supervisor External Interrupt Enable */

#define SIP_SSIP (1UL << 1) /* Supervisor Software Interrupt Pending */
#define SIP_STIP (1UL << 5) /* Supervisor Timer Interrupt Pending */
#define SIP_SEIP (1UL << 9) /* Supervisor External Interrupt Pending */

/* ==========================================================================
 * scause Register Values
 * ========================================================================== */

/* Interrupt bit (set if scause is an interrupt, clear if exception) */
#define SCAUSE_INTERRUPT (1UL << 63)

/* Exception codes (bit 63 = 0) */
#define SCAUSE_INST_MISALIGNED 0
#define SCAUSE_INST_ACCESS 1
#define SCAUSE_ILLEGAL_INST 2
#define SCAUSE_BREAKPOINT 3
#define SCAUSE_LOAD_MISALIGNED 4
#define SCAUSE_LOAD_ACCESS 5
#define SCAUSE_STORE_MISALIGNED 6
#define SCAUSE_STORE_ACCESS 7
#define SCAUSE_ECALL_U 8 /* Environment call from U-mode */
#define SCAUSE_ECALL_S 9 /* Environment call from S-mode */
#define SCAUSE_INST_PAGE_FAULT 12
#define SCAUSE_LOAD_PAGE_FAULT 13
#define SCAUSE_STORE_PAGE_FAULT 15

/* Interrupt codes (bit 63 = 1, codes are in lower bits) */
#define SCAUSE_SSI 1 /* Supervisor Software Interrupt */
#define SCAUSE_STI 5 /* Supervisor Timer Interrupt */
#define SCAUSE_SEI 9 /* Supervisor External Interrupt */

/* Helper macros */
#define SCAUSE_IS_INTERRUPT(cause) ((cause)&SCAUSE_INTERRUPT)
#define SCAUSE_CODE(cause) ((cause) & ~SCAUSE_INTERRUPT)

/* ==========================================================================
 * satp Register (Supervisor Address Translation and Protection)
 * ========================================================================== */

/* SATP modes for RV64 */
#define SATP_MODE_BARE (0UL << 60)  /* No translation */
#define SATP_MODE_SV39 (8UL << 60)  /* 39-bit virtual addressing */
#define SATP_MODE_SV48 (9UL << 60)  /* 48-bit virtual addressing */
#define SATP_MODE_SV57 (10UL << 60) /* 57-bit virtual addressing */

/* SATP field masks */
#define SATP_MODE_MASK (0xFUL << 60)
#define SATP_ASID_MASK (0xFFFFUL << 44)
#define SATP_PPN_MASK ((1UL << 44) - 1)

/* Build SATP value */
#define SATP_VALUE(mode, asid, ppn) ((mode) | ((uint64_t)(asid) << 44) | ((ppn)&SATP_PPN_MASK))

/* ==========================================================================
 * Memory Barrier Instructions
 * ========================================================================== */

/* Full memory fence */
#define fence() asm volatile("fence" ::: "memory")

/* Instruction fence */
#define fence_i() asm volatile("fence.i" ::: "memory")

/* TLB flush for all entries */
#define sfence_vma_all() asm volatile("sfence.vma" ::: "memory")

/* TLB flush for specific virtual address */
#define sfence_vma_va(va) asm volatile("sfence.vma %0, zero" : : "r"(va) : "memory")

/* TLB flush for specific ASID */
#define sfence_vma_asid(asid) asm volatile("sfence.vma zero, %0" : : "r"(asid) : "memory")

/* TLB flush for specific VA and ASID */
#define sfence_vma(va, asid) asm volatile("sfence.vma %0, %1" : : "r"(va), "r"(asid) : "memory")

/* ==========================================================================
 * Trap Vector Modes
 * ========================================================================== */

#define STVEC_MODE_DIRECT 0   /* All traps go to BASE */
#define STVEC_MODE_VECTORED 1 /* Interrupts go to BASE + 4*cause */

#endif /* ARCH_RISCV64_CSR_H */
