/* arch/common/io.h - I/O interface.
 *
 * Architecture-neutral interface for device I/O.
 *
 * i386: Uses port-mapped I/O (IN/OUT instructions)
 * RISC-V: Uses memory-mapped I/O (load/store to device addresses)
 *
 * For portable code, use the mmio_* functions which work on both
 * architectures. The pio_* (port I/O) functions are x86-only.
 */

#ifndef ARCH_COMMON_IO_H
#define ARCH_COMMON_IO_H

#include <stdint.h>

/* ==========================================================================
 * Memory-Mapped I/O (works on all architectures)
 * ========================================================================== */

/* Read from memory-mapped device register. */
static inline uint8_t mmio_read8(volatile void* addr) { return *(volatile uint8_t*)addr; }

static inline uint16_t mmio_read16(volatile void* addr) { return *(volatile uint16_t*)addr; }

static inline uint32_t mmio_read32(volatile void* addr) { return *(volatile uint32_t*)addr; }

static inline uint64_t mmio_read64(volatile void* addr) { return *(volatile uint64_t*)addr; }

/* Write to memory-mapped device register. */
static inline void mmio_write8(volatile void* addr, uint8_t val) { *(volatile uint8_t*)addr = val; }

static inline void mmio_write16(volatile void* addr, uint16_t val) {
  *(volatile uint16_t*)addr = val;
}

static inline void mmio_write32(volatile void* addr, uint32_t val) {
  *(volatile uint32_t*)addr = val;
}

static inline void mmio_write64(volatile void* addr, uint64_t val) {
  *(volatile uint64_t*)addr = val;
}

/* ==========================================================================
 * Port-Mapped I/O (i386 only)
 *
 * On RISC-V, these are stub functions that should not be called.
 * Legacy device drivers (IDE, PIT, 8259 PIC) use port I/O and are
 * only compiled for i386.
 * ========================================================================== */

#ifdef ARCH_I386
/* These are defined in arch/i386/io.h or threads/io.h */
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
uint32_t inl(uint16_t port);
void outb(uint16_t port, uint8_t data);
void outw(uint16_t port, uint16_t data);
void outl(uint16_t port, uint32_t data);

/* Block I/O */
void insb(uint16_t port, void* addr, size_t cnt);
void insw(uint16_t port, void* addr, size_t cnt);
void insl(uint16_t port, void* addr, size_t cnt);
void outsb(uint16_t port, const void* addr, size_t cnt);
void outsw(uint16_t port, const void* addr, size_t cnt);
void outsl(uint16_t port, const void* addr, size_t cnt);
#endif

#endif /* ARCH_COMMON_IO_H */
