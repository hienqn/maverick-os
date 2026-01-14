/* arch/riscv64/intr.c - RISC-V interrupt and exception handling.
 *
 * This file implements the C-level trap handling for RISC-V.
 * The assembly code in trap.S saves context and calls trap_handler().
 */

#include "arch/riscv64/intr.h"
#include "arch/riscv64/csr.h"
#include "arch/riscv64/sbi.h"
#include "arch/riscv64/timer.h"
#include "arch/riscv64/plic.h"
#include "userprog/syscall.h"
#include "userprog/exception.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Attribute to mark unused parameters */
#define UNUSED __attribute__((unused))

/* Interrupt handlers registered for each interrupt type */
static intr_handler_func* interrupt_handlers[16];
static intr_handler_func* exception_handlers[16];

/* External functions from init.c for early debugging */
extern void debug_panic(const char* file, int line, const char* function, const char* message, ...);

/* Assembly function to install trap vector */
extern void install_trap_vector(void);

/* Statistics */
static uint64_t interrupt_count;
static uint64_t exception_count;

/* Interrupt context nesting depth (0 = not in interrupt context) */
static int in_intr_context;

/* Forward declarations */
static void handle_interrupt(struct intr_frame* f, uint64_t cause);
static void handle_exception(struct intr_frame* f, uint64_t cause);

/*
 * intr_init - Initialize interrupt handling.
 *
 * Sets up the trap vector and enables interrupts.
 */
void intr_init(void) {
  /* Install trap vector */
  install_trap_vector();

  /* Initialize counters */
  interrupt_count = 0;
  exception_count = 0;

  /* Clear all handlers */
  for (int i = 0; i < 16; i++) {
    interrupt_handlers[i] = NULL;
    exception_handlers[i] = NULL;
  }
}

/*
 * intr_enable - Enable interrupts.
 * Returns the previous interrupt state.
 */
enum intr_level intr_enable(void) {
  enum intr_level old = intr_get_level();
  csr_set(sstatus, SSTATUS_SIE);
  return old;
}

/*
 * intr_disable - Disable interrupts.
 * Returns the previous interrupt state.
 */
enum intr_level intr_disable(void) {
  enum intr_level old = intr_get_level();
  csr_clear(sstatus, SSTATUS_SIE);
  return old;
}

/*
 * intr_get_level - Get current interrupt enable state.
 */
enum intr_level intr_get_level(void) {
  uint64_t sstatus = csr_read(sstatus);
  return (sstatus & SSTATUS_SIE) ? INTR_ON : INTR_OFF;
}

/*
 * intr_set_level - Set interrupt enable state.
 */
enum intr_level intr_set_level(enum intr_level level) {
  enum intr_level old = intr_get_level();
  if (level == INTR_ON)
    intr_enable();
  else
    intr_disable();
  return old;
}

/*
 * intr_register_int - Register an interrupt handler.
 */
void intr_register_int(uint8_t vec, int dpl UNUSED, enum intr_level level UNUSED,
                       intr_handler_func* handler, const char* name UNUSED) {
  if (vec < 16)
    interrupt_handlers[vec] = handler;
}

/*
 * intr_register_ext - Register an external interrupt handler.
 *
 * For RISC-V, external interrupts go through PLIC.
 */
void intr_register_ext(uint8_t vec, intr_handler_func* handler, const char* name UNUSED) {
  /* External interrupts use SCAUSE_SEI (9) */
  if (vec < 16)
    interrupt_handlers[vec] = handler;
}

/*
 * trap_handler - Main C trap handler called from assembly.
 *
 * Dispatches to appropriate handler based on scause.
 */
void trap_handler(struct intr_frame* f) {
  uint64_t cause = f->scause;

  /* Mark that we're in interrupt context */
  in_intr_context++;

  if (SCAUSE_IS_INTERRUPT(cause)) {
    /* Interrupt */
    interrupt_count++;
    handle_interrupt(f, SCAUSE_CODE(cause));
  } else {
    /* Exception */
    exception_count++;
    handle_exception(f, cause);
  }

  /* Leaving interrupt context */
  in_intr_context--;
}

/*
 * handle_interrupt - Handle an interrupt.
 */
static void handle_interrupt(struct intr_frame* f, uint64_t cause) {
  switch (cause) {
    case SCAUSE_SSI:
      /* Supervisor software interrupt */
      /* Clear the software interrupt pending bit */
      csr_clear(sip, SIP_SSIP);
      if (interrupt_handlers[SCAUSE_SSI])
        interrupt_handlers[SCAUSE_SSI](f);
      break;

    case SCAUSE_STI:
      /* Supervisor timer interrupt */
      if (interrupt_handlers[SCAUSE_STI])
        interrupt_handlers[SCAUSE_STI](f);
      else
        timer_interrupt(f);
      break;

    case SCAUSE_SEI:
      /* Supervisor external interrupt (from PLIC) */
      if (interrupt_handlers[SCAUSE_SEI])
        interrupt_handlers[SCAUSE_SEI](f);
      else
        plic_handle_interrupt(f);
      break;

    default:
      debug_panic(__FILE__, __LINE__, __func__, "Unknown interrupt cause");
      break;
  }
}

/*
 * handle_exception - Handle an exception.
 */
static void handle_exception(struct intr_frame* f, uint64_t cause) {
  switch (cause) {
    case SCAUSE_ECALL_U:
      /* System call from user mode */
      /* Advance sepc past the ecall instruction */
      f->sepc += 4;
      /* Call the shared syscall handler from userprog/syscall.c */
      syscall_handler(f);
      break;

    case SCAUSE_INST_PAGE_FAULT:
    case SCAUSE_LOAD_PAGE_FAULT:
    case SCAUSE_STORE_PAGE_FAULT:
      /* Page fault - handled by VM system via riscv_page_fault */
      riscv_page_fault(f);
      break;

    case SCAUSE_ILLEGAL_INST:
      debug_panic(__FILE__, __LINE__, __func__, "Illegal instruction at sepc=0x%lx, inst=0x%lx",
                  f->sepc, f->stval);
      break;

    case SCAUSE_BREAKPOINT:
      /* Breakpoint - for debugging */
      f->sepc += 2; /* ebreak is 2 bytes (compressed) */
      break;

    case SCAUSE_INST_MISALIGNED:
    case SCAUSE_LOAD_MISALIGNED:
    case SCAUSE_STORE_MISALIGNED:
      debug_panic(__FILE__, __LINE__, __func__, "Misaligned access at sepc=0x%lx, addr=0x%lx",
                  f->sepc, f->stval);
      break;

    case SCAUSE_INST_ACCESS:
    case SCAUSE_LOAD_ACCESS:
    case SCAUSE_STORE_ACCESS:
      debug_panic(__FILE__, __LINE__, __func__, "Access fault at sepc=0x%lx, addr=0x%lx", f->sepc,
                  f->stval);
      break;

    default:
      debug_panic(__FILE__, __LINE__, __func__, "Unknown exception cause=%lu at sepc=0x%lx", cause,
                  f->sepc);
      break;
  }
}

/*
 * intr_yield_on_return - Request a yield when returning from interrupt.
 *
 * This is a hint to the scheduler that should be checked in trap_return.
 * For now, we'll handle this in the timer interrupt directly.
 */
void intr_yield_on_return(void) { /* Will be implemented with thread scheduling */
}

/*
 * intr_context - Returns true if we're in an interrupt context.
 *
 * This returns true when executing inside a trap handler (interrupt or
 * exception), not just when interrupts are disabled. Critical sections
 * disable interrupts but are not "interrupt context".
 */
bool intr_context(void) { return in_intr_context > 0; }

/*
 * intr_get_stats - Get interrupt statistics.
 */
void intr_get_stats(uint64_t* interrupts, uint64_t* exceptions) {
  if (interrupts)
    *interrupts = interrupt_count;
  if (exceptions)
    *exceptions = exception_count;
}

/*
 * Exception cause names for debugging.
 */
static const char* exception_names[] = {
    "Instruction address misaligned", /* 0 */
    "Instruction access fault",       /* 1 */
    "Illegal instruction",            /* 2 */
    "Breakpoint",                     /* 3 */
    "Load address misaligned",        /* 4 */
    "Load access fault",              /* 5 */
    "Store/AMO address misaligned",   /* 6 */
    "Store/AMO access fault",         /* 7 */
    "Environment call from U-mode",   /* 8 */
    "Environment call from S-mode",   /* 9 */
    "Reserved",                       /* 10 */
    "Reserved",                       /* 11 */
    "Instruction page fault",         /* 12 */
    "Load page fault",                /* 13 */
    "Reserved",                       /* 14 */
    "Store/AMO page fault",           /* 15 */
};

/*
 * intr_name - Return the name of an exception/interrupt.
 */
const char* intr_name(uint8_t vec) {
  if (vec < 16)
    return exception_names[vec];
  return "Unknown";
}

/*
 * intr_dump_frame - Dump interrupt frame for debugging.
 */
void intr_dump_frame(const struct intr_frame* f) {
  uint64_t cause = f->scause;
  bool is_interrupt = SCAUSE_IS_INTERRUPT(cause);
  uint64_t code = SCAUSE_CODE(cause);

  printf("Interrupt frame at %p:\n", f);
  printf(" sepc=%016lx  sstatus=%016lx\n", f->sepc, f->sstatus);
  printf(" scause=%016lx (%s: %s)\n", f->scause, is_interrupt ? "interrupt" : "exception",
         is_interrupt ? "timer/external" : intr_name(code));
  printf(" stval=%016lx\n", f->stval);
  printf(" ra=%016lx  sp=%016lx  gp=%016lx  tp=%016lx\n", f->ra, f->sp, f->gp, f->tp);
  printf(" t0=%016lx  t1=%016lx  t2=%016lx\n", f->t0, f->t1, f->t2);
  printf(" s0=%016lx  s1=%016lx\n", f->s0, f->s1);
  printf(" a0=%016lx  a1=%016lx  a2=%016lx  a3=%016lx\n", f->a0, f->a1, f->a2, f->a3);
  printf(" a4=%016lx  a5=%016lx  a6=%016lx  a7=%016lx\n", f->a4, f->a5, f->a6, f->a7);
  printf(" s2=%016lx  s3=%016lx  s4=%016lx  s5=%016lx\n", f->s2, f->s3, f->s4, f->s5);
  printf(" s6=%016lx  s7=%016lx  s8=%016lx  s9=%016lx\n", f->s6, f->s7, f->s8, f->s9);
  printf(" s10=%016lx s11=%016lx\n", f->s10, f->s11);
  printf(" t3=%016lx  t4=%016lx  t5=%016lx  t6=%016lx\n", f->t3, f->t4, f->t5, f->t6);
}
