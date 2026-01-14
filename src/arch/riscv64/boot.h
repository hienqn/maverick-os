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

/* ==========================================================================
 * Command Line Argument Support
 * ==========================================================================
 * These provide compatibility with the x86 loader interface.
 * On RISC-V, arguments come from QEMU's -append flag via device tree,
 * or from default test configuration.
 */

/* Maximum length of kernel command line (matches x86 loader) */
#define LOADER_ARGS_LEN 128

/* Command line storage - use kernel BSS instead of fixed addresses
 * (OpenSBI protects 0x80000000-0x8001ffff) */
extern uint32_t riscv_arg_cnt;
extern char riscv_args_buffer[LOADER_ARGS_LEN];

/* Compatibility macros pointing to kernel variables */
#define LOADER_ARG_CNT ((uint64_t)&riscv_arg_cnt)
#define LOADER_ARGS ((uint64_t)riscv_args_buffer)

/* Initialize command line from device tree or defaults.
 * Called early in boot before paging is fully enabled.
 */
void riscv_init_cmdline(const char* cmdline);

#endif /* ARCH_RISCV64_BOOT_H */
