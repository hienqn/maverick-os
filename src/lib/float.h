#ifndef __LIB_FLOAT_H
#define __LIB_FLOAT_H

#define E_VAL 2.718281
#define TOL 0.000002

#ifdef ARCH_I386
/* x86 FPU operations using x87 stack */

/* Pushes integer num to the FPU */
static inline void fpu_push(int num) {
  asm volatile("pushl %0; flds (%%esp); addl $4, %%esp" : : "m"(num));
}

/* Pops integer from the FPU */
static inline int fpu_pop(void) {
  int val;
  asm volatile("subl $4, %%esp; fstps (%%esp); mov (%%esp), %0; addl $4, %%esp"
               : "=r"(val)
               :
               : "memory");
  return val;
}

/* Stores a clean copy of the FPU to a 108B memory location DEST.
   Uses a 108B memory location BUF as a temporary storage */
static inline void fpu_save_init(void* dest, void* buf) {
  asm volatile("fsave (%0); fninit; fsave (%1); frstor (%2)" : : "r"(buf), "r"(dest), "r"(buf));
}

#elif defined(ARCH_RISCV64)
/* RISC-V FP operations using F/D extensions.
 *
 * RISC-V has 32 FP registers (f0-f31) rather than a stack.
 * We use a simple temporary register approach for compatibility
 * with the x86 FPU push/pop semantics.
 */

/* Temporary FP value storage for emulating x87 stack behavior */
static float __fpu_temp;

/* Pushes integer num to the "FPU" (stores as float) */
static inline void fpu_push(int num) {
  /* Convert integer to float and store in temp */
  __fpu_temp = (float)num;
}

/* Pops integer from the "FPU" (retrieves stored float as int bits) */
static inline int fpu_pop(void) {
  /* Return the float as raw integer bits */
  union {
    float f;
    int i;
  } u;
  u.f = __fpu_temp;
  return u.i;
}

/* Stores FP state - for RISC-V we just zero the destination.
 * Full FP context (f0-f31 + fcsr = 264 bytes) would need:
 *   fsd f0, 0(dest); fsd f1, 8(dest); ... fsd f31, 248(dest)
 *   csrr t0, fcsr; sd t0, 256(dest)
 */
static inline void fpu_save_init(void* dest, void* buf __attribute__((unused))) {
  /* For now, just zero the destination to indicate clean FP state */
  char* d = (char*)dest;
  for (int i = 0; i < 108; i++) {
    d[i] = 0;
  }
}

#else
#error "No architecture defined for float.h"
#endif

int sys_sum_to_e(int);
double sum_to_e(int);
double abs_val(double);

#endif /* lib/debug.h */
