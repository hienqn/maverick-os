/* arch/common/intr.h - Interrupt management interface.
 *
 * Architecture-neutral interface for interrupt enable/disable.
 * Each architecture implements these in arch/<arch>/intr.h or intr.c.
 */

#ifndef ARCH_COMMON_INTR_H
#define ARCH_COMMON_INTR_H

#include <stdbool.h>
#include <stdint.h>

/* Interrupt enable/disable state. */
enum intr_level {
  INTR_OFF, /* Interrupts disabled */
  INTR_ON   /* Interrupts enabled */
};

/* Get current interrupt state. */
enum intr_level intr_get_level(void);

/* Set interrupt state, return previous state. */
enum intr_level intr_set_level(enum intr_level level);

/* Disable interrupts, return previous state. */
enum intr_level intr_disable(void);

/* Enable interrupts, return previous state. */
enum intr_level intr_enable(void);

/* Check if currently in interrupt context. */
bool intr_context(void);

/* Initialize interrupt subsystem.
 * Architecture-specific: sets up IDT (x86) or stvec (RISC-V). */
void intr_init(void);

/* Interrupt handler function type.
 * The intr_frame structure is architecture-specific. */
struct intr_frame;
typedef void intr_handler_func(struct intr_frame*);

/* Register handler for external interrupt (device IRQ). */
void intr_register_ext(uint8_t vec_no, intr_handler_func* handler, const char* name);

/* Register handler for internal interrupt (exception/trap). */
void intr_register_int(uint8_t vec_no, int dpl, enum intr_level level, intr_handler_func* handler,
                       const char* name);

#endif /* ARCH_COMMON_INTR_H */
