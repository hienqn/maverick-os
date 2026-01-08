/**
 * @file threads/ioremap.h
 * @brief I/O memory mapping for MMIO regions.
 *
 * Provides functions to map physical MMIO addresses (like those from
 * PCI BARs) into kernel virtual address space. This is necessary because
 * MMIO regions are typically at physical addresses above PHYS_BASE and
 * cannot be accessed through the normal ptov() mechanism.
 *
 * Usage:
 *   // Get MMIO base from PCI BAR
 *   uint32_t mmio_paddr = pci_read_config(...);
 *
 *   // Map into kernel virtual address space
 *   void *mmio = ioremap(mmio_paddr, 128 * 1024);  // 128KB region
 *
 *   // Access MMIO registers
 *   volatile uint32_t *regs = mmio;
 *   regs[0] = 0x12345678;
 *
 *   // When done (rarely needed for device drivers)
 *   iounmap(mmio, 128 * 1024);
 */

#ifndef THREADS_IOREMAP_H
#define THREADS_IOREMAP_H

#include <stdint.h>
#include <stddef.h>

/**
 * Initialize the MMIO mapper.
 * Must be called after paging_init() and before any ioremap() calls.
 */
void ioremap_init(void);

/**
 * Map a physical MMIO region into kernel virtual address space.
 *
 * @param phys_addr  Physical address of MMIO region (from PCI BAR)
 * @param size       Size of region in bytes (will be rounded up to page size)
 * @return Virtual address for accessing the MMIO region, or NULL on failure
 *
 * @note The returned pointer can be used to access device registers.
 * @note MMIO accesses are uncached and not reordered by the CPU.
 */
void* ioremap(uintptr_t phys_addr, size_t size);

/**
 * Unmap a previously mapped MMIO region.
 *
 * @param virt_addr  Virtual address returned by ioremap()
 * @param size       Size of region (must match the ioremap() call)
 *
 * @note This is rarely needed for device drivers that stay loaded.
 */
void iounmap(void* virt_addr, size_t size);

#endif /* threads/ioremap.h */
