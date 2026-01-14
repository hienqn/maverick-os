/* arch/riscv64/switch.S - RISC-V thread context switching.
 *
 * This file implements context switching for RISC-V threads.
 * The RISC-V LP64D ABI defines s0-s11 as callee-saved registers.
 */

#include "arch/riscv64/switch.h"

.section .text

/*
 * switch_threads - Switch from one thread to another.
 *
 * Saves callee-saved registers (s0-s11, ra) on the current thread's stack,
 * switches stacks, and restores the new thread's registers.
 *
 * C prototype:
 *   struct thread *switch_threads(struct thread *cur, struct thread *next);
 *
 * Arguments:
 *   a0 - Current thread (cur)
 *   a1 - Next thread (next)
 *
 * Returns:
 *   a0 - Previous thread (the thread that was running before 'next')
 *
 * The thread's stack pointer is at offset THREAD_STACK_OFS in struct thread.
 */
.globl switch_threads
.align 4
switch_threads:
    /* Allocate switch frame on current thread's stack */
    addi sp, sp, -SWITCH_FRAME_SIZE

    /* Save callee-saved registers */
    sd s0,  SWITCH_S0(sp)
    sd s1,  SWITCH_S1(sp)
    sd s2,  SWITCH_S2(sp)
    sd s3,  SWITCH_S3(sp)
    sd s4,  SWITCH_S4(sp)
    sd s5,  SWITCH_S5(sp)
    sd s6,  SWITCH_S6(sp)
    sd s7,  SWITCH_S7(sp)
    sd s8,  SWITCH_S8(sp)
    sd s9,  SWITCH_S9(sp)
    sd s10, SWITCH_S10(sp)
    sd s11, SWITCH_S11(sp)
    sd ra,  SWITCH_RA(sp)

    /* Save arguments for switch_thunk */
    sd a0,  SWITCH_CUR(sp)
    sd a1,  SWITCH_NEXT(sp)

    /* Save current stack pointer to current thread's stack field. */
    sd sp, THREAD_STACK_OFS(a0)

    /* Load new stack pointer from next thread's stack field */
    ld sp, THREAD_STACK_OFS(a1)

    /* Restore callee-saved registers from new thread's stack */
    ld s0,  SWITCH_S0(sp)
    ld s1,  SWITCH_S1(sp)
    ld s2,  SWITCH_S2(sp)
    ld s3,  SWITCH_S3(sp)
    ld s4,  SWITCH_S4(sp)
    ld s5,  SWITCH_S5(sp)
    ld s6,  SWITCH_S6(sp)
    ld s7,  SWITCH_S7(sp)
    ld s8,  SWITCH_S8(sp)
    ld s9,  SWITCH_S9(sp)
    ld s10, SWITCH_S10(sp)
    ld s11, SWITCH_S11(sp)
    ld ra,  SWITCH_RA(sp)

    /* Load cur and next arguments for the returning thread */
    ld a0,  SWITCH_CUR(sp)
    ld a1,  SWITCH_NEXT(sp)

    /* Deallocate switch frame */
    addi sp, sp, SWITCH_FRAME_SIZE

    /* Return to the new thread's ra (which may be switch_entry) */
    ret

/*
 * switch_entry - Entry point for newly created threads.
 *
 * When a new thread is first scheduled, switch_threads() returns here.
 * At this point:
 *   - a0 contains 'cur' (previous thread) from switch_threads
 *   - sp points to switch_entry_frame (containing ra = kernel_thread)
 *   - Above that is kernel_thread_frame (containing function, aux)
 *
 * This function:
 *   1. Calls schedule_tail(cur) to complete the context switch
 *   2. Pops switch_entry_frame to get kernel_thread address
 *   3. Pops kernel_thread_frame to get function and aux into a0, a1
 *   4. Jumps to kernel_thread(function, aux)
 */
.globl switch_entry
.align 4
switch_entry:
    /* a0 already has 'cur' from switch_threads */
    /* sp points to switch_entry_frame */

    /* Call thread_switch_tail(cur) to complete context switch */
    call thread_switch_tail

    /* Pop switch_entry_frame: get kernel_thread address */
    ld t0, 0(sp)            /* t0 = address of kernel_thread */
    addi sp, sp, 8          /* pop switch_entry_frame (8 bytes) */

    /* Pop kernel_thread_frame: get function and aux */
    ld a0, 0(sp)            /* a0 = function */
    ld a1, 8(sp)            /* a1 = aux */
    addi sp, sp, 16         /* pop kernel_thread_frame (16 bytes) */

    /* Jump to kernel_thread(function, aux) */
    jr t0

/*
 * switch_thunk - Legacy entry point, now unused.
 *
 * Previously used to call thread_switch_tail, but switch_entry now handles
 * everything directly. Kept for compatibility.
 */
.globl switch_thunk
.align 4
switch_thunk:
    /* This is now called directly by switch_entry, just call thread_switch_tail */
    tail thread_switch_tail

/*
 * thread_launch - Start executing a new thread.
 *
 * This is called to begin execution of a newly created thread.
 * Arguments are set up in the thread's stack by thread_create().
 *
 * Not currently used - threads start via switch_entry/switch_thunk.
 */
.globl thread_launch
.align 4
thread_launch:
    /* a0 contains the function to call
     * a1 contains the argument to pass */
    mv t0, a0
    mv a0, a1
    jalr t0

    /* Thread function returned - this shouldn't happen for kernel threads */
    /* Call thread_exit() */
    call thread_exit

    /* Should never reach here */
1:  wfi
    j 1b
