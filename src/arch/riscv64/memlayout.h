/* arch/riscv64/memlayout.h - RISC-V memory layout definitions.
 *
 * Memory map for QEMU virt machine (128MB RAM default):
 *
 * Physical Memory:
 *   0x00000000 - 0x00010000  Reserved
 *   0x00100000 - 0x00101000  CLINT (Core Local Interruptor)
 *   0x0C000000 - 0x10000000  PLIC (Platform-Level Interrupt Controller)
 *   0x10000000 - 0x10000100  UART0 (NS16550A compatible)
 *   0x10001000 - 0x10002000  VirtIO Block Device
 *   0x80000000 - 0x80200000  OpenSBI firmware
 *   0x80200000 - ...         Kernel
 *
 * Virtual Memory (Sv39, after paging enabled):
 *   User space:    0x0000000000000000 - 0x0000003FFFFFFFFF (256GB)
 *   Kernel space:  0xFFFFFFC000000000 - 0xFFFFFFFFFFFFFFFF
 *   PHYS_BASE:     0xFFFFFFFF80000000 (physical memory mapped here)
 */

#ifndef ARCH_RISCV64_MEMLAYOUT_H
#define ARCH_RISCV64_MEMLAYOUT_H

/* ==========================================================================
 * Physical Memory Layout (QEMU virt machine)
 * ========================================================================== */

/* RAM starts at 0x80000000 on QEMU virt */
#define PHYS_RAM_BASE 0x80000000UL

/* OpenSBI firmware occupies first 2MB */
#define OPENSBI_SIZE 0x200000UL

/* Kernel physical load address (after OpenSBI) */
#define KERN_BASE (PHYS_RAM_BASE + OPENSBI_SIZE) /* 0x80200000 */

/* Default RAM size (128MB) */
#define DEFAULT_RAM_SIZE (128 * 1024 * 1024)

/* ==========================================================================
 * Device MMIO Addresses (QEMU virt machine)
 * ========================================================================== */

/* CLINT: Core Local Interruptor (timer and software interrupts) */
#define CLINT_BASE 0x02000000UL
#define CLINT_SIZE 0x10000UL
#define CLINT_MTIMECMP(hartid) (CLINT_BASE + 0x4000 + 8 * (hartid))
#define CLINT_MTIME (CLINT_BASE + 0xBFF8)

/* PLIC: Platform-Level Interrupt Controller */
#define PLIC_BASE 0x0C000000UL
#define PLIC_SIZE 0x4000000UL
#define PLIC_PRIORITY(irq) (PLIC_BASE + 4 * (irq))
#define PLIC_PENDING(irq) (PLIC_BASE + 0x1000 + 4 * ((irq) / 32))
#define PLIC_SENABLE(hart) (PLIC_BASE + 0x2080 + 0x100 * (hart))
#define PLIC_SPRIORITY(hart) (PLIC_BASE + 0x201000 + 0x2000 * (hart))
#define PLIC_SCLAIM(hart) (PLIC_BASE + 0x201004 + 0x2000 * (hart))

/* UART: NS16550A compatible serial port */
#define UART0_BASE 0x10000000UL
#define UART0_IRQ 10

/* VirtIO devices */
#define VIRTIO0_BASE 0x10001000UL
#define VIRTIO0_IRQ 1

/* ==========================================================================
 * Virtual Memory Layout (Sv39)
 *
 * Sv39 provides 39-bit virtual addresses with 3-level page tables:
 *   - 9 bits: VPN[2] (level 2 page table index)
 *   - 9 bits: VPN[1] (level 1 page table index)
 *   - 9 bits: VPN[0] (level 0 page table index)
 *   - 12 bits: Page offset
 *
 * Virtual address space: 512GB (2^39 bytes)
 * Canonical addresses: sign-extended from bit 38
 *   - Lower half: 0x0000000000000000 - 0x0000003FFFFFFFFF (user)
 *   - Upper half: 0xFFFFFFC000000000 - 0xFFFFFFFFFFFFFFFF (kernel)
 * ========================================================================== */

/* Kernel virtual base (high half, sign-extended) */
#define KERN_VIRT_BASE 0xFFFFFFC000000000UL

/* Physical memory is direct-mapped at this virtual address */
#define PHYS_BASE 0xFFFFFFFF80000000UL

/* Maximum user virtual address */
#define USER_VIRT_TOP 0x0000004000000000UL

/* User stack grows down from near the top of user space */
#define USER_STACK_TOP 0x0000003FFFFFF000UL

/* ==========================================================================
 * Page Size Constants
 * ========================================================================== */

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT) /* 4KB */
#define PAGE_MASK (PAGE_SIZE - 1)

/* Megapage (2MB) and Gigapage (1GB) for huge pages */
#define MEGAPAGE_SHIFT 21
#define MEGAPAGE_SIZE (1UL << MEGAPAGE_SHIFT)
#define GIGAPAGE_SHIFT 30
#define GIGAPAGE_SIZE (1UL << GIGAPAGE_SHIFT)

/*
 * Note: Address conversion functions (ptov, vtop, pg_round_up, etc.)
 * are defined in arch/riscv64/vaddr.h to avoid duplication.
 */

#endif /* ARCH_RISCV64_MEMLAYOUT_H */
