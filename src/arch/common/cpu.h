/* arch/common/cpu.h - CPU control interface.
 *
 * Architecture-neutral interface for CPU operations.
 * Each architecture implements these in arch/<arch>/cpu.h or cpu.c.
 */

#ifndef ARCH_COMMON_CPU_H
#define ARCH_COMMON_CPU_H

#include <stdint.h>
#include <stdbool.h>

/* Halt the CPU until the next interrupt.
 * Used for idle loop to save power. */
void cpu_idle(void);

/* Full memory barrier - ensures all memory operations complete
 * before any subsequent operations begin. */
void cpu_memory_barrier(void);

/* Read memory barrier - ensures all prior reads complete. */
void cpu_read_barrier(void);

/* Write memory barrier - ensures all prior writes complete. */
void cpu_write_barrier(void);

/* Flush TLB entry for a specific virtual address. */
void cpu_flush_tlb_page(void* va);

/* Flush entire TLB. */
void cpu_flush_tlb_all(void);

/* Get current CPU ID (for SMP, returns 0 for uniprocessor). */
unsigned cpu_id(void);

#endif /* ARCH_COMMON_CPU_H */
