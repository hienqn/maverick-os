/* arch/riscv64/switch.h - RISC-V context switch frame structure.
 *
 * The RISC-V LP64D ABI requires saving 12 callee-saved registers (s0-s11)
 * plus the return address (ra). This is significantly larger than x86's
 * 4 callee-saved registers.
 *
 * Switch frame layout (16 64-bit words = 128 bytes):
 *   Offset  Register  Purpose
 *   0       s0        Callee-saved / frame pointer
 *   8       s1        Callee-saved
 *   16      s2        Callee-saved
 *   24      s3        Callee-saved
 *   32      s4        Callee-saved
 *   40      s5        Callee-saved
 *   48      s6        Callee-saved
 *   56      s7        Callee-saved
 *   64      s8        Callee-saved
 *   72      s9        Callee-saved
 *   80      s10       Callee-saved
 *   88      s11       Callee-saved
 *   96      ra        Return address
 *   104     cur       Argument: current thread
 *   112     next      Argument: next thread
 *   120     (pad)     Padding for 16-byte alignment
 */

#ifndef ARCH_RISCV64_SWITCH_H
#define ARCH_RISCV64_SWITCH_H

/* Frame offsets for assembly code */
#define SWITCH_S0 0
#define SWITCH_S1 8
#define SWITCH_S2 16
#define SWITCH_S3 24
#define SWITCH_S4 32
#define SWITCH_S5 40
#define SWITCH_S6 48
#define SWITCH_S7 56
#define SWITCH_S8 64
#define SWITCH_S9 72
#define SWITCH_S10 80
#define SWITCH_S11 88
#define SWITCH_RA 96
#define SWITCH_CUR 104
#define SWITCH_NEXT 112

/* Total frame size (must be 16-byte aligned) */
#define SWITCH_FRAME_SIZE 128

/* Offset of 'stack' field in struct thread.
 * Layout: tid_t(4) + status(4) + name[16] = 24 bytes before stack.
 * This MUST match offsetof(struct thread, stack). */
#define THREAD_STACK_OFS 24

#ifndef __ASSEMBLER__
#include <stdint.h>

/* Forward declaration */
struct thread;

/* switch_threads()'s stack frame.
 * Saves all callee-saved registers per RISC-V LP64D ABI. */
struct switch_threads_frame {
  uint64_t s0;         /*   0: Saved s0 (frame pointer). */
  uint64_t s1;         /*   8: Saved s1. */
  uint64_t s2;         /*  16: Saved s2. */
  uint64_t s3;         /*  24: Saved s3. */
  uint64_t s4;         /*  32: Saved s4. */
  uint64_t s5;         /*  40: Saved s5. */
  uint64_t s6;         /*  48: Saved s6. */
  uint64_t s7;         /*  56: Saved s7. */
  uint64_t s8;         /*  64: Saved s8. */
  uint64_t s9;         /*  72: Saved s9. */
  uint64_t s10;        /*  80: Saved s10. */
  uint64_t s11;        /*  88: Saved s11. */
  uint64_t ra;         /*  96: Return address. */
  struct thread* cur;  /* 104: switch_threads()'s CUR argument. */
  struct thread* next; /* 112: switch_threads()'s NEXT argument. */
  uint64_t _pad;       /* 120: Padding for 16-byte alignment. */
};

/* Switches from CUR, which must be the running thread, to NEXT,
   which must also be running switch_threads(), returning CUR in
   NEXT's context. */
struct thread* switch_threads(struct thread* cur, struct thread* next);

/* Stack frame for switch_entry(). */
struct switch_entry_frame {
  uint64_t ra; /* Return address (points to kernel_thread). */
};

/* Entry point for newly created threads. */
void switch_entry(void);

/* Pops the CUR and NEXT arguments off the stack, for use in
   initializing threads. */
void switch_thunk(void);

/* Start a new thread - jumps to function with argument. */
void thread_launch(void (*function)(void*), void* aux);

#endif /* !__ASSEMBLER__ */

#endif /* ARCH_RISCV64_SWITCH_H */
