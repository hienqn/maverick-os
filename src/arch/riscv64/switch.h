/* arch/riscv64/switch.h - RISC-V context switch frame structure.
 *
 * The RISC-V switch frame saves 12 callee-saved registers (s0-s11)
 * plus the return address (ra) per the LP64D calling convention.
 */

#ifndef ARCH_RISCV64_SWITCH_H
#define ARCH_RISCV64_SWITCH_H

#ifndef __ASSEMBLER__
#include <stdint.h>

/* Forward declaration */
struct thread;

/*
 * switch_threads()'s stack frame.
 *
 * RISC-V LP64D ABI callee-saved registers: s0-s11, ra
 * Total: 13 registers * 8 bytes = 104 bytes, plus arguments
 */
struct switch_threads_frame {
  uint64_t s0;         /*   0: Saved s0 (frame pointer) */
  uint64_t s1;         /*   8: Saved s1 */
  uint64_t s2;         /*  16: Saved s2 */
  uint64_t s3;         /*  24: Saved s3 */
  uint64_t s4;         /*  32: Saved s4 */
  uint64_t s5;         /*  40: Saved s5 */
  uint64_t s6;         /*  48: Saved s6 */
  uint64_t s7;         /*  56: Saved s7 */
  uint64_t s8;         /*  64: Saved s8 */
  uint64_t s9;         /*  72: Saved s9 */
  uint64_t s10;        /*  80: Saved s10 */
  uint64_t s11;        /*  88: Saved s11 */
  void (*ra)(void);    /*  96: Return address */
  struct thread* cur;  /* 104: switch_threads()'s CUR argument (a0) */
  struct thread* next; /* 112: switch_threads()'s NEXT argument (a1) */
};

/* Switches from CUR, which must be the running thread, to NEXT,
   which must also be running switch_threads(), returning CUR in
   NEXT's context. */
struct thread* switch_threads(struct thread* cur, struct thread* next);

/* Stack frame for switch_entry(). */
struct switch_entry_frame {
  void (*ra)(void);
};

void switch_entry(void);

/* Pops the CUR and NEXT arguments off the stack, for use in
   initializing threads. */
void switch_thunk(void);
#endif

/* Offsets used by switch.S (in bytes) */
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

#endif /* ARCH_RISCV64_SWITCH_H */
