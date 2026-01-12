/* arch/riscv64/boot.h - Boot-time constants and variables for RISC-V.
 *
 * This header provides boot-time information that other kernel
 * subsystems need (like the page allocator).
 *
 * On RISC-V, boot is handled by OpenSBI rather than a BIOS loader.
 * Memory detection happens in init.c via device tree or defaults.
 */

#ifndef ARCH_RISCV64_BOOT_H
#define ARCH_RISCV64_BOOT_H

#include <stdint.h>

/* Number of pages of physical RAM available */
extern uint64_t init_ram_pages;

/* End of physical RAM (physical address) */
extern uint64_t ram_end;

/* Boot hart ID (the hart that booted the kernel) */
extern uint64_t boot_hartid;

/* Device tree blob pointer (from OpenSBI) */
extern void* dtb_ptr;

/* Kernel page directory (root page table) */
extern uint64_t* init_page_dir;

#endif /* ARCH_RISCV64_BOOT_H */
