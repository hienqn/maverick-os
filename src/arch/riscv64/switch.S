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
 * The thread structure has the stack pointer at offset 0.
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

    /* Save current stack pointer to current thread's stack field.
     * struct thread has stack at offset 0. */
    sd sp, 0(a0)

    /* Load new stack pointer from next thread's stack field */
    ld sp, 0(a1)

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
 * This function calls switch_thunk() which pops the thread arguments
 * and calls kernel_thread().
 */
.globl switch_entry
.align 4
switch_entry:
    /* Save ra before calling C code */
    addi sp, sp, -16
    sd ra, 0(sp)

    /* Call switch_thunk to set up arguments and jump to thread function */
    call switch_thunk

    /* Restore ra and return */
    ld ra, 0(sp)
    addi sp, sp, 16
    ret

/*
 * switch_thunk - Prepares arguments for a new thread.
 *
 * When a new thread starts, the switch frame contains cur and next pointers.
 * This function extracts them and sets up for the thread's main function.
 *
 * The actual implementation is in thread.c:schedule_tail().
 */
.globl switch_thunk
.align 4
switch_thunk:
    /* The switch frame is still on the stack from switch_threads.
     * We need to extract cur and next, then call schedule_tail(cur).
     *
     * At this point:
     *   sp points to the switch frame (after switch_entry's adjustment)
     *   We need to get back to where cur/next are stored.
     */

    /* Load cur from the switch frame (it's above switch_entry's frame) */
    ld a0, (16 + SWITCH_CUR)(sp)

    /* Call schedule_tail(cur) to complete the context switch */
    call schedule_tail

    /* schedule_tail returns. Now we should return to kernel_thread
     * or wherever the new thread should start.
     *
     * For a newly created thread, the ra in the switch frame points
     * to kernel_thread_entry, which will call the thread's function. */
    ret

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
