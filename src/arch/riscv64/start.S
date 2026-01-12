/* arch/riscv64/start.S - RISC-V kernel entry point.
 *
 * This is the first code executed when OpenSBI jumps to the kernel.
 * OpenSBI passes:
 *   a0 = hartid (hardware thread ID)
 *   a1 = pointer to device tree blob (DTB)
 *
 * We need to:
 *   1. Set up a temporary stack
 *   2. Clear BSS
 *   3. Call the C initialization function
 */

#include "arch/riscv64/memlayout.h"

.section .text.entry
.globl _start
.type _start, @function

_start:
    /* Disable interrupts */
    csrw    sie, zero
    csrw    sip, zero

    /* Save hartid and DTB pointer for later */
    mv      s0, a0          /* s0 = hartid */
    mv      s1, a1          /* s1 = DTB pointer */

    /*
     * Set up the global pointer (gp).
     * The linker places __global_pointer$ in .sdata for efficient access
     * to small global variables within +/- 2KB of gp.
     */
.option push
.option norelax
    la      gp, __global_pointer$
.option pop

    /*
     * Set up a temporary boot stack.
     * Each hart gets its own 4KB stack, indexed by hartid.
     * For now, only hart 0 continues; others spin.
     */
    bnez    s0, park_hart   /* Only hart 0 boots, others park */

    la      sp, _boot_stack_top

    /*
     * Clear BSS section.
     * BSS contains uninitialized global variables that must be zeroed.
     */
    la      t0, _start_bss
    la      t1, _end_bss
    beq     t0, t1, bss_done
bss_clear:
    sd      zero, 0(t0)
    addi    t0, t0, 8
    blt     t0, t1, bss_clear
bss_done:

    /*
     * Set up trap vector for early exceptions.
     * Point stvec to our early trap handler.
     */
    la      t0, early_trap_handler
    csrw    stvec, t0

    /*
     * Call C initialization code.
     * riscv_init(hartid, dtb_ptr)
     */
    mv      a0, s0          /* hartid */
    mv      a1, s1          /* DTB pointer */
    call    riscv_init

    /* riscv_init should not return, but if it does, park this hart */
    j       park_hart

/*
 * Park a hart (put it to sleep waiting for interrupts).
 * Secondary harts wait here until SMP support is added.
 */
.globl park_hart
park_hart:
    wfi                     /* Wait for interrupt */
    j       park_hart       /* Loop if spurious wakeup */

/*
 * Early trap handler for debugging.
 * Just prints the trap cause and hangs.
 */
.align 4
early_trap_handler:
    /* Save a few registers for debugging */
    csrr    t0, scause
    csrr    t1, sepc
    csrr    t2, stval

    /* For now, just park the hart */
    j       park_hart

/* Mark end of _start code section */
_start_end:
.size _start, _start_end - _start

/*
 * Boot stack.
 * 16KB stack for the boot hart (hart 0).
 */
.section .bss
.align 16
_boot_stack:
    .space  16384           /* 16KB boot stack */
_boot_stack_top:
