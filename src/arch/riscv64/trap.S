/* arch/riscv64/trap.S - RISC-V trap entry and exit.
 *
 * This file contains the low-level trap handling code for RISC-V.
 * All traps (exceptions and interrupts) enter through trap_entry,
 * which saves context and calls the C handler.
 *
 * The trap frame layout matches struct intr_frame in intr.h.
 */

#include "arch/riscv64/memlayout.h"

/* Offsets into struct intr_frame (31 GPRs + 4 CSRs = 35 * 8 = 280 bytes) */
#define FRAME_RA     0
#define FRAME_SP     8
#define FRAME_GP     16
#define FRAME_TP     24
#define FRAME_T0     32
#define FRAME_T1     40
#define FRAME_T2     48
#define FRAME_S0     56
#define FRAME_S1     64
#define FRAME_A0     72
#define FRAME_A1     80
#define FRAME_A2     88
#define FRAME_A3     96
#define FRAME_A4     104
#define FRAME_A5     112
#define FRAME_A6     120
#define FRAME_A7     128
#define FRAME_S2     136
#define FRAME_S3     144
#define FRAME_S4     152
#define FRAME_S5     160
#define FRAME_S6     168
#define FRAME_S7     176
#define FRAME_S8     184
#define FRAME_S9     192
#define FRAME_S10    200
#define FRAME_S11    208
#define FRAME_T3     216
#define FRAME_T4     224
#define FRAME_T5     232
#define FRAME_T6     240
#define FRAME_SEPC   248
#define FRAME_SSTATUS 256
#define FRAME_SCAUSE 264
#define FRAME_STVAL  272
#define FRAME_SIZE   288  /* 280 rounded up to 16-byte alignment */

.section .text
.globl trap_entry
.globl trap_return

/*
 * trap_entry - Entry point for all traps (exceptions and interrupts).
 *
 * On entry:
 *   - Running in S-mode
 *   - sscratch contains kernel stack pointer (if from U-mode) or 0 (if from S-mode)
 *   - sepc, scause, stval contain trap information
 *   - sp contains user stack (if from U-mode) or current kernel stack (if from S-mode)
 *
 * Strategy:
 *   - If from U-mode: swap sp with sscratch to get kernel stack
 *   - If from S-mode: sscratch is 0, keep current sp
 */
.align 4
trap_entry:
    /* Swap sp with sscratch.
     * If from U-mode: sscratch had kernel sp, now sp has it
     * If from S-mode: sscratch was 0, we'll detect and fix */
    csrrw sp, sscratch, sp

    /* Check if we came from S-mode (sp would be 0) */
    bnez sp, 1f

    /* From S-mode: restore sp from sscratch (which now has old sp) */
    csrr sp, sscratch

1:
    /* Allocate trap frame on kernel stack */
    addi sp, sp, -FRAME_SIZE

    /* Save general-purpose registers */
    sd ra,  FRAME_RA(sp)
    /* sp will be saved later after we recover the original */
    sd gp,  FRAME_GP(sp)
    sd tp,  FRAME_TP(sp)
    sd t0,  FRAME_T0(sp)
    sd t1,  FRAME_T1(sp)
    sd t2,  FRAME_T2(sp)
    sd s0,  FRAME_S0(sp)
    sd s1,  FRAME_S1(sp)
    sd a0,  FRAME_A0(sp)
    sd a1,  FRAME_A1(sp)
    sd a2,  FRAME_A2(sp)
    sd a3,  FRAME_A3(sp)
    sd a4,  FRAME_A4(sp)
    sd a5,  FRAME_A5(sp)
    sd a6,  FRAME_A6(sp)
    sd a7,  FRAME_A7(sp)
    sd s2,  FRAME_S2(sp)
    sd s3,  FRAME_S3(sp)
    sd s4,  FRAME_S4(sp)
    sd s5,  FRAME_S5(sp)
    sd s6,  FRAME_S6(sp)
    sd s7,  FRAME_S7(sp)
    sd s8,  FRAME_S8(sp)
    sd s9,  FRAME_S9(sp)
    sd s10, FRAME_S10(sp)
    sd s11, FRAME_S11(sp)
    sd t3,  FRAME_T3(sp)
    sd t4,  FRAME_T4(sp)
    sd t5,  FRAME_T5(sp)
    sd t6,  FRAME_T6(sp)

    /* Save original sp (user sp or previous kernel sp) */
    csrr t0, sscratch
    sd t0, FRAME_SP(sp)

    /* If from U-mode, clear sscratch; if from S-mode, it should already be 0 */
    csrw sscratch, zero

    /* Save CSRs */
    csrr t0, sepc
    sd t0, FRAME_SEPC(sp)

    csrr t0, sstatus
    sd t0, FRAME_SSTATUS(sp)

    csrr t0, scause
    sd t0, FRAME_SCAUSE(sp)

    csrr t0, stval
    sd t0, FRAME_STVAL(sp)

    /* Call C trap handler with pointer to trap frame */
    mv a0, sp
    call trap_handler

    /* Fall through to trap_return */

/*
 * trap_return - Return from trap to interrupted context.
 *
 * On entry:
 *   - sp points to trap frame
 *   - The trap frame may have been modified by the C handler
 */
trap_return:
    /* Restore CSRs first */
    ld t0, FRAME_SEPC(sp)
    csrw sepc, t0

    ld t0, FRAME_SSTATUS(sp)
    csrw sstatus, t0

    /* Check if returning to U-mode (SPP bit = 0 means U-mode) */
    andi t1, t0, 0x100  /* SSTATUS_SPP */
    beqz t1, return_to_user

return_to_kernel:
    /* Returning to S-mode: sscratch should remain 0 */
    j restore_regs

return_to_user:
    /* Returning to U-mode: set sscratch to kernel sp for next trap */
    addi t0, sp, FRAME_SIZE
    csrw sscratch, t0

restore_regs:
    /* Restore general-purpose registers */
    ld ra,  FRAME_RA(sp)
    /* Skip sp for now, restore at the end */
    ld gp,  FRAME_GP(sp)
    /* Skip tp - it's thread-local and shouldn't change */
    ld t0,  FRAME_T0(sp)
    ld t1,  FRAME_T1(sp)
    ld t2,  FRAME_T2(sp)
    ld s0,  FRAME_S0(sp)
    ld s1,  FRAME_S1(sp)
    ld a0,  FRAME_A0(sp)
    ld a1,  FRAME_A1(sp)
    ld a2,  FRAME_A2(sp)
    ld a3,  FRAME_A3(sp)
    ld a4,  FRAME_A4(sp)
    ld a5,  FRAME_A5(sp)
    ld a6,  FRAME_A6(sp)
    ld a7,  FRAME_A7(sp)
    ld s2,  FRAME_S2(sp)
    ld s3,  FRAME_S3(sp)
    ld s4,  FRAME_S4(sp)
    ld s5,  FRAME_S5(sp)
    ld s6,  FRAME_S6(sp)
    ld s7,  FRAME_S7(sp)
    ld s8,  FRAME_S8(sp)
    ld s9,  FRAME_S9(sp)
    ld s10, FRAME_S10(sp)
    ld s11, FRAME_S11(sp)
    ld t3,  FRAME_T3(sp)
    ld t4,  FRAME_T4(sp)
    ld t5,  FRAME_T5(sp)
    ld t6,  FRAME_T6(sp)

    /* Restore sp last */
    ld sp, FRAME_SP(sp)

    /* Return from trap */
    sret

/*
 * install_trap_vector - Set stvec to point to trap_entry.
 *
 * Called from C code during initialization.
 */
.globl install_trap_vector
install_trap_vector:
    la t0, trap_entry
    csrw stvec, t0
    ret
