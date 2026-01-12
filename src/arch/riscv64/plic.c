/* arch/riscv64/plic.c - RISC-V PLIC implementation.
 *
 * PLIC memory map for QEMU virt (from memlayout.h):
 *   Base:          0x0C000000
 *   Priority:      base + irq * 4
 *   Pending:       base + 0x1000
 *   S-mode enable: base + 0x2080 + hart * 0x100
 *   S-mode threshold: base + 0x201000 + hart * 0x2000
 *   S-mode claim:  base + 0x201004 + hart * 0x2000
 */

#include "arch/riscv64/plic.h"
#include "arch/riscv64/memlayout.h"
#include "arch/riscv64/csr.h"
#include <stdint.h>
#include <stddef.h>

/* Boot hart ID from init.c */
extern uint64_t boot_hartid;

/* PLIC handler table */
static plic_handler_func plic_handlers[PLIC_NUM_SOURCES];

/* Current hart ID (set during init) */
static uint32_t plic_hart;

/*
 * MMIO read/write helpers
 */
static inline uint32_t plic_read(uintptr_t addr) { return *(volatile uint32_t*)addr; }

static inline void plic_write(uintptr_t addr, uint32_t val) { *(volatile uint32_t*)addr = val; }

/*
 * plic_init - Initialize the PLIC for the current hart.
 */
void plic_init(void) {
  /* Get hart ID from boot parameters */
  plic_hart = (uint32_t)boot_hartid;

  /* Set all priorities to 0 (disabled) initially */
  for (int i = 1; i < PLIC_NUM_SOURCES; i++) {
    plic_write(PLIC_PRIORITY(i), 0);
  }

  /* Disable all interrupts for this hart */
  plic_write(PLIC_SENABLE(plic_hart), 0);
  plic_write(PLIC_SENABLE(plic_hart) + 4, 0);
  plic_write(PLIC_SENABLE(plic_hart) + 8, 0);
  plic_write(PLIC_SENABLE(plic_hart) + 12, 0);

  /* Set priority threshold to 0 (accept all priorities > 0) */
  plic_write(PLIC_SPRIORITY(plic_hart), 0);

  /* Clear handler table */
  for (int i = 0; i < PLIC_NUM_SOURCES; i++) {
    plic_handlers[i] = NULL;
  }

  /* Enable supervisor external interrupts */
  csr_set(sie, SIE_SEIE);
}

/*
 * plic_enable - Enable an interrupt source.
 */
void plic_enable(uint32_t irq) {
  if (irq == 0 || irq >= PLIC_NUM_SOURCES)
    return;

  /* Calculate which enable register and bit */
  uint32_t word = irq / 32;
  uint32_t bit = irq % 32;

  uintptr_t enable_addr = PLIC_SENABLE(plic_hart) + word * 4;
  uint32_t val = plic_read(enable_addr);
  val |= (1U << bit);
  plic_write(enable_addr, val);
}

/*
 * plic_disable - Disable an interrupt source.
 */
void plic_disable(uint32_t irq) {
  if (irq == 0 || irq >= PLIC_NUM_SOURCES)
    return;

  uint32_t word = irq / 32;
  uint32_t bit = irq % 32;

  uintptr_t enable_addr = PLIC_SENABLE(plic_hart) + word * 4;
  uint32_t val = plic_read(enable_addr);
  val &= ~(1U << bit);
  plic_write(enable_addr, val);
}

/*
 * plic_set_priority - Set priority for an interrupt source.
 */
void plic_set_priority(uint32_t irq, uint32_t priority) {
  if (irq == 0 || irq >= PLIC_NUM_SOURCES)
    return;
  if (priority > 7)
    priority = 7;

  plic_write(PLIC_PRIORITY(irq), priority);
}

/*
 * plic_set_threshold - Set priority threshold.
 */
void plic_set_threshold(uint32_t threshold) { plic_write(PLIC_SPRIORITY(plic_hart), threshold); }

/*
 * plic_claim - Claim a pending interrupt.
 *
 * Returns the IRQ number or 0 if no interrupt is pending.
 */
uint32_t plic_claim(void) { return plic_read(PLIC_SCLAIM(plic_hart)); }

/*
 * plic_complete - Signal completion of interrupt handling.
 */
void plic_complete(uint32_t irq) { plic_write(PLIC_SCLAIM(plic_hart), irq); }

/*
 * plic_register_handler - Register a handler for an IRQ.
 */
void plic_register_handler(uint32_t irq, plic_handler_func handler) {
  if (irq == 0 || irq >= PLIC_NUM_SOURCES)
    return;

  plic_handlers[irq] = handler;
}

/*
 * plic_handle_interrupt - Handle an external interrupt.
 *
 * Called from intr.c when a supervisor external interrupt occurs.
 */
void plic_handle_interrupt(struct intr_frame* f) {
  (void)f; /* Unused for now */

  /* Claim the interrupt */
  uint32_t irq = plic_claim();

  if (irq == 0) {
    /* Spurious interrupt */
    return;
  }

  /* Call handler if registered */
  if (plic_handlers[irq] != NULL) {
    plic_handlers[irq]();
  }

  /* Complete the interrupt */
  plic_complete(irq);
}
