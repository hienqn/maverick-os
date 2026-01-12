/* arch/riscv64/intr.h - RISC-V interrupt handling definitions.
 *
 * This header defines the RISC-V interrupt frame and related functions.
 * The interrupt frame saves all 31 general-purpose registers plus
 * relevant supervisor CSRs during a trap.
 */

#ifndef ARCH_RISCV64_INTR_H
#define ARCH_RISCV64_INTR_H

#include <stdbool.h>
#include <stdint.h>

/* Interrupts on or off? */
enum intr_level {
  INTR_OFF, /* Interrupts disabled. */
  INTR_ON   /* Interrupts enabled. */
};

enum intr_level intr_get_level(void);
enum intr_level intr_set_level(enum intr_level);
enum intr_level intr_enable(void);
enum intr_level intr_disable(void);

/*
 * RISC-V interrupt/trap frame.
 *
 * This structure holds all 31 general-purpose registers (x0 is hardwired
 * to zero) plus relevant supervisor CSRs. The layout must match what
 * trap.S saves and restores.
 *
 * Size: 35 * 8 = 280 bytes, padded to 288 for 16-byte alignment.
 */
struct intr_frame {
  /* General purpose registers (31 total, x0 is hardwired to 0) */
  uint64_t ra;  /* x1:  Return address */
  uint64_t sp;  /* x2:  Stack pointer (user's, if from U-mode) */
  uint64_t gp;  /* x3:  Global pointer */
  uint64_t tp;  /* x4:  Thread pointer */
  uint64_t t0;  /* x5:  Temporary */
  uint64_t t1;  /* x6:  Temporary */
  uint64_t t2;  /* x7:  Temporary */
  uint64_t s0;  /* x8:  Saved register / frame pointer */
  uint64_t s1;  /* x9:  Saved register */
  uint64_t a0;  /* x10: Argument / return value */
  uint64_t a1;  /* x11: Argument / return value */
  uint64_t a2;  /* x12: Argument */
  uint64_t a3;  /* x13: Argument */
  uint64_t a4;  /* x14: Argument */
  uint64_t a5;  /* x15: Argument */
  uint64_t a6;  /* x16: Argument */
  uint64_t a7;  /* x17: Argument / syscall number */
  uint64_t s2;  /* x18: Saved register */
  uint64_t s3;  /* x19: Saved register */
  uint64_t s4;  /* x20: Saved register */
  uint64_t s5;  /* x21: Saved register */
  uint64_t s6;  /* x22: Saved register */
  uint64_t s7;  /* x23: Saved register */
  uint64_t s8;  /* x24: Saved register */
  uint64_t s9;  /* x25: Saved register */
  uint64_t s10; /* x26: Saved register */
  uint64_t s11; /* x27: Saved register */
  uint64_t t3;  /* x28: Temporary */
  uint64_t t4;  /* x29: Temporary */
  uint64_t t5;  /* x30: Temporary */
  uint64_t t6;  /* x31: Temporary */

  /* Supervisor CSRs saved on trap */
  uint64_t sepc;    /* Exception program counter */
  uint64_t sstatus; /* Status register */
  uint64_t scause;  /* Trap cause */
  uint64_t stval;   /* Trap value (faulting address or instruction) */
};

/* Frame size must be 16-byte aligned for RISC-V ABI */
#define INTR_FRAME_SIZE 288

/* scause exception codes (bit 63 = 0) */
#define CAUSE_MISALIGNED_FETCH 0
#define CAUSE_FETCH_ACCESS 1
#define CAUSE_ILLEGAL_INSTRUCTION 2
#define CAUSE_BREAKPOINT 3
#define CAUSE_MISALIGNED_LOAD 4
#define CAUSE_LOAD_ACCESS 5
#define CAUSE_MISALIGNED_STORE 6
#define CAUSE_STORE_ACCESS 7
#define CAUSE_USER_ECALL 8
#define CAUSE_SUPERVISOR_ECALL 9
#define CAUSE_INSTRUCTION_PAGE_FAULT 12
#define CAUSE_LOAD_PAGE_FAULT 13
#define CAUSE_STORE_PAGE_FAULT 15

/* Interrupt bit (bit 63 of scause) */
#define CAUSE_INTERRUPT_FLAG (1UL << 63)
#define CAUSE_IS_INTERRUPT(c) ((c)&CAUSE_INTERRUPT_FLAG)

/* Interrupt causes (with bit 63 set) */
#define CAUSE_SUPERVISOR_SOFTWARE (CAUSE_INTERRUPT_FLAG | 1)
#define CAUSE_SUPERVISOR_TIMER (CAUSE_INTERRUPT_FLAG | 5)
#define CAUSE_SUPERVISOR_EXTERNAL (CAUSE_INTERRUPT_FLAG | 9)

typedef void intr_handler_func(struct intr_frame*);

/* Called from trap.S */
void trap_handler(struct intr_frame* f);

void intr_init(void);
void intr_register_ext(uint8_t vec, intr_handler_func*, const char* name);
void intr_register_int(uint8_t vec, int dpl, enum intr_level, intr_handler_func*, const char* name);
bool intr_context(void);
void intr_yield_on_return(void);

void intr_dump_frame(const struct intr_frame*);
const char* intr_name(uint8_t vec);
void intr_get_stats(uint64_t* interrupts, uint64_t* exceptions);

#endif /* ARCH_RISCV64_INTR_H */
