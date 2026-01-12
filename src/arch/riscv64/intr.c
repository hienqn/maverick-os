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
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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
      if (exception_handlers[SCAUSE_ECALL_U])
        exception_handlers[SCAUSE_ECALL_U](f);
      break;

    case SCAUSE_INST_PAGE_FAULT:
    case SCAUSE_LOAD_PAGE_FAULT:
    case SCAUSE_STORE_PAGE_FAULT:
      /* Page fault - to be handled by VM system */
      if (exception_handlers[cause])
        exception_handlers[cause](f);
      else {
        debug_panic(__FILE__, __LINE__, __func__,
                    "Page fault at sepc=0x%lx, stval=0x%lx, cause=%lu", f->sepc, f->stval, cause);
      }
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
