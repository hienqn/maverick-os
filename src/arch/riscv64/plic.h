/* arch/riscv64/plic.h - RISC-V PLIC (Platform-Level Interrupt Controller).
 *
 * The PLIC handles external interrupts from devices like UART and VirtIO.
 * Each interrupt source has a priority and can be enabled/disabled per hart.
 */

#ifndef ARCH_RISCV64_PLIC_H
#define ARCH_RISCV64_PLIC_H

#include <stdint.h>
#include <stdbool.h>

struct intr_frame;

/* Maximum number of interrupt sources */
#define PLIC_NUM_SOURCES 128

/* IRQ numbers for QEMU virt machine */
#define PLIC_IRQ_VIRTIO0 1
#define PLIC_IRQ_UART0 10

/* Initialize the PLIC */
void plic_init(void);

/* Enable an interrupt source */
void plic_enable(uint32_t irq);

/* Disable an interrupt source */
void plic_disable(uint32_t irq);

/* Set priority for an interrupt source (1-7, 0 disables) */
void plic_set_priority(uint32_t irq, uint32_t priority);

/* Set the priority threshold (only interrupts > threshold are delivered) */
void plic_set_threshold(uint32_t threshold);

/* Handle external interrupt (called from intr.c) */
void plic_handle_interrupt(struct intr_frame* f);

/* Claim an interrupt (returns IRQ number, or 0 if none pending) */
uint32_t plic_claim(void);

/* Complete an interrupt */
void plic_complete(uint32_t irq);

/* Register a handler for an external interrupt */
typedef void (*plic_handler_func)(void);
void plic_register_handler(uint32_t irq, plic_handler_func handler);

#endif /* ARCH_RISCV64_PLIC_H */
