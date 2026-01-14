# PintOS i386 to RISC-V (RV64) Port Plan

## Executive Summary

This document evaluates the effort required to port PintOS from the i386 (x86-32) architecture to RISC-V RV64GC. The target platform is QEMU's `virt` machine with OpenSBI firmware and Sv39 paging.

| Metric | Value |
|--------|-------|
| **Total New/Modified Code** | ~7,350 lines |
| **Estimated Duration** | 16-17 person-weeks |
| **Complexity** | High |
| **Educational Value** | Very High |
| **Includes HAL** | Yes (~1,010 lines) |

---

## 1. Current Architecture Inventory

### 1.1 Assembly Files (~853 lines)

| File | Lines | Purpose | RISC-V Equivalent |
|------|-------|---------|-------------------|
| `src/threads/loader.S` | 264 | BIOS boot loader, real mode, partition scan | Not needed (OpenSBI handles) |
| `src/threads/start.S` | 204 | Real→protected mode, A20, GDT, enable paging | S-mode entry, SATP setup |
| `src/threads/intr-stubs.S` | 203 | 256 interrupt vector stubs | Single trap vector |
| `src/threads/switch.S` | 65 | Thread context switch (EBX, ESI, EDI, EBP) | Context switch (s0-s11, ra) |
| `src/threads/kernel.lds.S` | 33 | ELF32-i386 linker script | ELF64-riscv linker script |

**Note**: The existing `switch.S` saves only 4 callee-saved registers per x86 cdecl ABI. RISC-V requires saving 12 callee-saved registers (s0-s11) plus ra, resulting in a larger switch frame.

### 1.2 Inline Assembly in C Files

| File | Functions | x86 Instructions | RISC-V Equivalent |
|------|-----------|------------------|-------------------|
| `threads/interrupt.c` | intr_enable/disable, intr_init | CLI, STI, LIDT, PUSHFL/POPL | csrw/csrr sstatus, stvec |
| `userprog/pagedir.c` | pagedir_activate, active_pd | MOV CR3 | csrw/csrr satp |
| `userprog/gdt.c` | gdt_init | LGDT, LTR | Not needed |
| `threads/io.h` | inb/outb (12 functions) | IN, OUT (port I/O) | Memory-mapped I/O |
| `lib/float.h` | fpu_save/restore | FSAVE, FRSTOR, FNINIT | F extension instrs |

### 1.3 Architecture-Specific Headers

| Header | Purpose | Changes Required |
|--------|---------|------------------|
| `threads/pte.h` | x86 page table format (10-10-12) | Rewrite for Sv39 (9-9-9-12) |
| `threads/vaddr.h` | Virtual address layout, PHYS_BASE | Update for 64-bit, new PHYS_BASE |
| `threads/flags.h` | EFLAGS register bits | Replace with sstatus bits |
| `threads/loader.h` | Boot constants (0x7c00, GDT selectors) | Replace with RISC-V constants |
| `userprog/gdt.h` | GDT segment selectors | Remove (no segmentation) |
| `userprog/tss.h` | Task State Segment | Remove (no TSS) |
| `threads/interrupt.h` | struct intr_frame (x86 registers) | New struct with RISC-V registers |

### 1.4 Device Drivers (All PC-Specific)

| Driver | x86 Hardware | RISC-V QEMU virt Equivalent |
|--------|-------------|----------------------------|
| `devices/pit.c` | 8254 PIT (ports 0x40-0x43) | CLINT timer via SBI |
| `devices/ide.c` | ATA controller (ports 0x1F0, 0x170) | VirtIO block device |
| `devices/serial.c` | 16550 UART (port 0x3F8) | NS16550A (memory-mapped 0x10000000) |
| `devices/keyboard.c` | PS/2 keyboard (port 0x60) | UART input |
| `devices/rtc.c` | CMOS RTC (ports 0x70-0x71) | Not available / timer-based |

---

## 2. Architecture Comparison

### 2.1 Paging

```
x86 Two-Level (32-bit virtual address):
┌────────────┬────────────┬──────────────┐
│ PD (10 bit)│ PT (10 bit)│ Offset (12)  │
└────────────┴────────────┴──────────────┘
    1024 PDEs × 1024 PTEs = 4GB address space

RISC-V Sv39 Three-Level (39-bit virtual address):
┌────────────┬────────────┬────────────┬──────────────┐
│ VPN[2] (9) │ VPN[1] (9) │ VPN[0] (9) │ Offset (12)  │
└────────────┴────────────┴────────────┴──────────────┘
    512 × 512 × 512 = 512GB address space
```

### 2.2 Page Table Entry Format

```
x86 PTE (32-bit):
┌────────────────────────────┬─────────────────────────┐
│  Physical Page Number (20) │ AVL D A 0 0 U W P (12)  │
└────────────────────────────┴─────────────────────────┘

RISC-V Sv39 PTE (64-bit):
┌──────────┬──────────────────────────────┬────────────────────┐
│ Reserved │     Physical Page Number      │ RSW D A G U X W R V│
│  (10)    │          (44 bits)            │      (10 bits)     │
└──────────┴──────────────────────────────┴────────────────────┘
```

### 2.3 Interrupt Handling

| Aspect | x86 | RISC-V |
|--------|-----|--------|
| Vector table | IDT with 256 entries | Single stvec register |
| Dispatch | Hardware jumps to vector | Software reads scause |
| Enable/disable | EFLAGS.IF via CLI/STI | sstatus.SIE via CSR |
| Controller | 8259A PIC (cascade) | PLIC + CLINT |
| Timer | PIT channel 0 → IRQ 0 | CLINT mtimecmp via SBI |

### 2.4 Privilege Levels

| x86 Ring | RISC-V Mode | PintOS Usage |
|----------|-------------|--------------|
| Ring 0 | S-mode (Supervisor) | Kernel |
| Ring 3 | U-mode (User) | User programs |
| - | M-mode (Machine) | OpenSBI firmware |

### 2.5 System Calls

```
x86:
    mov eax, SYSCALL_NUMBER
    mov ecx, arg1
    mov edx, arg2
    int 0x30

RISC-V:
    li a7, SYSCALL_NUMBER
    mv a0, arg1
    mv a1, arg2
    ecall
```

### 2.6 Memory Ordering Models

| Aspect | x86 (TSO) | RISC-V (RVWMO) |
|--------|-----------|----------------|
| **Model** | Total Store Order | Weak Memory Ordering |
| **Store-Store** | Ordered | Requires `fence w,w` |
| **Load-Load** | Ordered | Requires `fence r,r` |
| **Load-Store** | Ordered | Requires `fence r,w` |
| **Store-Load** | May reorder | Requires `fence w,r` |
| **Acquire/Release** | Implicit | Explicit `.aq`/`.rl` suffixes |

**Impact on PintOS**: Code that relies on x86's strong ordering may have subtle bugs on RISC-V. Key areas to audit:
- Spinlock implementations in `synch.c`
- Interrupt queue operations in `intq.c`
- Page table updates in `pagedir.c`

**Mitigation**: Use `fence` instructions in the HAL's `cpu_memory_barrier()` and review all lock-free code paths.

### 2.7 Calling Convention Comparison

| Aspect | x86 (cdecl) | RISC-V (LP64D) |
|--------|-------------|----------------|
| **Arguments** | Stack (right-to-left push) | a0-a7, then stack |
| **Return value** | eax (32-bit) | a0-a1 (128-bit max) |
| **Callee-saved** | ebx, esi, edi, ebp | s0-s11, ra |
| **Caller-saved** | eax, ecx, edx | t0-t6, a0-a7 |
| **Frame pointer** | ebp (optional) | s0/fp (optional) |
| **Stack alignment** | 4 bytes (16 at call) | 16 bytes always |
| **Red zone** | None | None (Linux ABI has 128B) |

### 2.8 Floating Point Context

| Aspect | x86 (x87/SSE) | RISC-V (F/D extensions) |
|--------|---------------|-------------------------|
| **Registers** | 8 × 80-bit x87 stack + 8 × 128-bit XMM | 32 × 64-bit f0-f31 |
| **Save size** | 108 bytes (FSAVE) or 512 bytes (FXSAVE) | 256 bytes (f0-f31) + fcsr |
| **Status** | x87 FPU status word | fcsr (8 bytes) |
| **Save instruction** | FSAVE/FXSAVE | sd f0-f31 (manual) |
| **Lazy save** | Via CR0.TS bit | Via sstatus.FS field |

**PintOS FP Strategy**: Currently uses lazy FP context switching via `lib/float.h`. For RISC-V:
- Check `sstatus.FS` field (0=Off, 1=Initial, 2=Clean, 3=Dirty)
- Save f0-f31 + fcsr only when FS=Dirty
- Restore and set FS=Clean on context switch

---

## 3. Portable Code (Reusable)

The following components require minimal or no changes:

| Component | Files | Changes |
|-----------|-------|---------|
| Thread scheduling | `threads/thread.c` | Stack setup, minor 64-bit |
| Synchronization | `threads/synch.c` | None |
| Memory allocator | `threads/malloc.c` | None |
| Data structures | `lib/kernel/list.c`, `hash.c`, `bitmap.c` | None |
| File system | `filesys/*` | None |
| VM abstractions | `vm/page.c`, `frame.c` | 64-bit pointer updates |
| Process logic | `userprog/process.c` | Arch-specific calls |

---

## 4. Proposed Directory Structure

### 4.1 File Relocation Table

The following table shows exact source→destination mappings for existing i386 code:

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `threads/loader.S` | `arch/i386/loader.S` | x86-only, no RISC-V equivalent |
| `threads/start.S` | `arch/i386/start.S` | Real→protected mode transition |
| `threads/switch.S` | `arch/i386/switch.S` | x86 context switch |
| `threads/intr-stubs.S` | `arch/i386/intr-stubs.S` | 256 IDT stubs |
| `threads/kernel.lds.S` | `arch/i386/kernel.lds.S` | ELF32-i386 linker script |
| `threads/interrupt.c` | `arch/i386/intr.c` | PIC/IDT initialization |
| `threads/pte.h` | `arch/i386/pte.h` | x86 2-level PTE format |
| `threads/vaddr.h` | Keep + abstract | Add arch-specific PHYS_BASE |
| `threads/flags.h` | `arch/i386/flags.h` | EFLAGS bits |
| `threads/io.h` | `arch/i386/io.h` | Port I/O (inb/outb) |
| `threads/loader.h` | `arch/i386/loader.h` | Boot constants |
| `threads/switch.h` | `arch/i386/switch.h` | x86 switch frame |
| `userprog/pagedir.c` | `arch/i386/pagedir.c` | x86 2-level page tables |
| `userprog/gdt.c` | `arch/i386/gdt.c` | x86 segmentation |
| `userprog/gdt.h` | `arch/i386/gdt.h` | GDT selectors |
| `userprog/tss.c` | `arch/i386/tss.c` | Task State Segment |
| `userprog/tss.h` | `arch/i386/tss.h` | TSS structure |
| `devices/pit.c` | `arch/i386/pit.c` | 8254 timer (x86-only) |
| `devices/ide.c` | `arch/i386/ide.c` | ATA via port I/O (x86-only) |

### 4.2 Directory Layout

```
src/
├── arch/
│   ├── common/                  # Shared HAL headers
│   │   ├── cpu.h                # CPU control interface
│   │   ├── intr.h               # Interrupt interface
│   │   ├── mmu.h                # MMU interface
│   │   ├── timer.h              # Timer interface
│   │   └── io.h                 # I/O interface
│   │
│   ├── i386/                    # Existing code relocated
│   │   ├── loader.S             # BIOS bootloader
│   │   ├── start.S              # Real→protected mode
│   │   ├── switch.S             # Context switch
│   │   ├── switch.h             # Switch frame structure
│   │   ├── intr-stubs.S         # 256 IDT stubs
│   │   ├── intr.c               # PIC/IDT setup
│   │   ├── intr.h               # x86 intr_frame
│   │   ├── kernel.lds.S         # ELF32-i386 linker script
│   │   ├── pte.h                # x86 PTE format (10-10-12)
│   │   ├── pagedir.c            # 2-level page tables
│   │   ├── flags.h              # EFLAGS bits
│   │   ├── io.h                 # Port I/O (inb/outb)
│   │   ├── loader.h             # Boot constants
│   │   ├── gdt.c                # GDT setup
│   │   ├── gdt.h                # Segment selectors
│   │   ├── tss.c                # TSS management
│   │   ├── tss.h                # TSS structure
│   │   ├── pit.c                # 8254 PIT timer
│   │   └── ide.c                # ATA disk driver
│   │
│   └── riscv64/                 # New RISC-V implementation
│       ├── start.S              # OpenSBI entry, BSS clear
│       ├── switch.S             # Context switch (s0-s11, ra)
│       ├── switch.h             # RISC-V switch frame
│       ├── trap.S               # Unified trap entry/exit
│       ├── intr.c               # Trap dispatch, CSR ops
│       ├── intr.h               # RISC-V intr_frame
│       ├── kernel.lds.S         # ELF64-riscv linker script
│       ├── pte.h                # Sv39 PTE format (9-9-9-12)
│       ├── pagedir.c            # 3-level page tables, SATP
│       ├── csr.h                # CSR read/write macros
│       ├── sbi.h                # SBI extension IDs
│       ├── sbi.c                # SBI ecall wrappers
│       ├── timer.c              # CLINT timer via SBI
│       ├── plic.c               # PLIC interrupt controller
│       ├── virtio.c             # VirtIO core (virtqueue)
│       ├── virtio.h             # VirtIO structures
│       └── virtio_blk.c         # VirtIO block device
│
├── devices/                     # Platform-agnostic drivers
│   ├── timer.c                  # Calls arch_timer_*()
│   ├── serial.c                 # Abstracted (MMIO or port I/O)
│   ├── block.c                  # Block device abstraction
│   ├── partition.c              # Partition table parsing
│   └── ns16550a.c               # UART (RISC-V, MMIO)
│
├── threads/
│   ├── vaddr.h                  # Kept, includes arch PHYS_BASE
│   └── ...                      # Other portable code
```

---

## 5. Minimal Hardware Abstraction Layer (HAL)

### 5.1 Design Philosophy

The HAL provides thin, inline abstraction headers that:
- Define architecture-neutral interfaces for portable code
- Allow compile-time architecture selection (no runtime overhead)
- Keep implementations simple and auditable
- Enable side-by-side comparison of x86 vs RISC-V

**NOT a goal**: Runtime polymorphism, plugin architectures, or over-engineering.

### 5.2 HAL Directory Structure

```
src/
├── include/
│   └── arch/                    # Architecture-neutral interfaces
│       ├── arch.h               # Master include (selects correct arch)
│       ├── types.h              # Portable type definitions
│       ├── cpu.h                # CPU control interface
│       ├── intr.h               # Interrupt management interface
│       ├── mmu.h                # Memory management interface
│       ├── timer.h              # Timer interface
│       └── io.h                 # I/O abstraction interface
│
├── arch/
│   ├── i386/
│   │   └── include/             # x86 implementations
│   │       ├── arch_types.h
│   │       ├── arch_cpu.h
│   │       ├── arch_intr.h
│   │       ├── arch_mmu.h
│   │       ├── arch_timer.h
│   │       └── arch_io.h
│   │
│   └── riscv64/
│       └── include/             # RISC-V implementations
│           ├── arch_types.h
│           ├── arch_cpu.h
│           ├── arch_intr.h
│           ├── arch_mmu.h
│           ├── arch_timer.h
│           └── arch_io.h
```

### 5.3 HAL Interface Definitions

#### 5.3.1 Master Include (`include/arch/arch.h`)

```c
#ifndef ARCH_ARCH_H
#define ARCH_ARCH_H

/* Select architecture based on build configuration */
#if defined(ARCH_I386)
  #define ARCH_NAME "i386"
  #define ARCH_BITS 32
  #include "arch/i386/include/arch_types.h"
  #include "arch/i386/include/arch_cpu.h"
  #include "arch/i386/include/arch_intr.h"
  #include "arch/i386/include/arch_mmu.h"
#elif defined(ARCH_RISCV64)
  #define ARCH_NAME "riscv64"
  #define ARCH_BITS 64
  #include "arch/riscv64/include/arch_types.h"
  #include "arch/riscv64/include/arch_cpu.h"
  #include "arch/riscv64/include/arch_intr.h"
  #include "arch/riscv64/include/arch_mmu.h"
#else
  #error "No architecture defined. Set ARCH_I386 or ARCH_RISCV64."
#endif

#endif /* ARCH_ARCH_H */
```

#### 5.3.2 Portable Types (`include/arch/types.h`)

```c
#ifndef ARCH_TYPES_H
#define ARCH_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Architecture provides these via arch_types.h: */
/*   typedef uintXX_t  vaddr_t;    -- Virtual address   */
/*   typedef uintXX_t  paddr_t;    -- Physical address  */
/*   typedef uintXX_t  pte_t;      -- Page table entry  */
/*   #define VADDR_FMT "0x%08x"    -- Printf format     */

/* Common definitions */
typedef int64_t  off_t;
typedef uint32_t block_sector_t;

#endif /* ARCH_TYPES_H */
```

#### 5.3.3 CPU Control (`include/arch/cpu.h`)

```c
#ifndef ARCH_CPU_H
#define ARCH_CPU_H

/*
 * CPU Control Interface
 *
 * Each architecture must implement these as static inline functions
 * in their arch_cpu.h header.
 */

/* Halt the CPU until next interrupt */
static inline void cpu_halt(void);

/* Memory barrier - ensure all memory operations complete */
static inline void cpu_memory_barrier(void);

/* Read the current stack pointer */
static inline void *cpu_get_sp(void);

/* Read the current frame pointer */
static inline void *cpu_get_fp(void);

/* Architecture-specific CPU identification (for SMP) */
static inline unsigned cpu_id(void);

#endif /* ARCH_CPU_H */
```

#### 5.3.4 Interrupt Management (`include/arch/intr.h`)

```c
#ifndef ARCH_INTR_H
#define ARCH_INTR_H

#include <stdbool.h>

/*
 * Interrupt Management Interface
 *
 * Provides architecture-neutral interrupt enable/disable.
 * The interrupt frame structure is architecture-specific
 * and defined in each arch's interrupt.h.
 */

/* Interrupt levels */
enum intr_level {
    INTR_OFF,   /* Interrupts disabled */
    INTR_ON     /* Interrupts enabled */
};

/* Get current interrupt state */
static inline enum intr_level intr_get_level(void);

/* Set interrupt state, return previous state */
static inline enum intr_level intr_set_level(enum intr_level level);

/* Disable interrupts, return previous state */
static inline enum intr_level intr_disable(void);

/* Enable interrupts, return previous state */
static inline enum intr_level intr_enable(void);

/* Check if interrupts are enabled */
static inline bool intr_enabled(void);

/*
 * Architecture must also provide:
 *   struct intr_frame    -- Saved register state on interrupt
 *   void intr_init(void) -- Initialize interrupt subsystem
 *   void intr_register_ext(uint8_t vec, handler, name) -- External IRQ
 *   void intr_register_int(uint8_t vec, dpl, level, handler, name) -- Internal
 */

#endif /* ARCH_INTR_H */
```

#### 5.3.5 Memory Management (`include/arch/mmu.h`)

```c
#ifndef ARCH_MMU_H
#define ARCH_MMU_H

#include <stdbool.h>
#include "arch/types.h"

/*
 * Memory Management Interface
 *
 * Abstracts page table operations across architectures.
 */

/* Page size (4KB on both x86 and RISC-V) */
#define PAGE_SIZE  4096
#define PAGE_SHIFT 12

/* Round address down/up to page boundary */
static inline vaddr_t pg_round_down(vaddr_t addr);
static inline vaddr_t pg_round_up(vaddr_t addr);

/* Extract page number from address */
static inline unsigned long pg_no(vaddr_t addr);

/* Extract offset within page */
static inline unsigned pg_ofs(vaddr_t addr);

/* Physical/virtual address conversion (kernel addresses only) */
static inline vaddr_t ptov(paddr_t paddr);
static inline paddr_t vtop(vaddr_t vaddr);

/* Check address ranges */
static inline bool is_user_vaddr(vaddr_t addr);
static inline bool is_kernel_vaddr(vaddr_t addr);

/*
 * Page directory operations (implemented in arch pagedir.c):
 *   pagedir_t *pagedir_create(void);
 *   void pagedir_destroy(pagedir_t *pd);
 *   void pagedir_activate(pagedir_t *pd);
 *   pagedir_t *pagedir_active(void);
 *   bool pagedir_set_page(pagedir_t *pd, vaddr_t upage,
 *                         paddr_t kpage, bool writable);
 *   paddr_t pagedir_get_page(pagedir_t *pd, vaddr_t upage);
 *   void pagedir_clear_page(pagedir_t *pd, vaddr_t upage);
 *   bool pagedir_is_dirty(pagedir_t *pd, vaddr_t upage);
 *   bool pagedir_is_accessed(pagedir_t *pd, vaddr_t upage);
 */

/* TLB management */
static inline void mmu_invalidate_page(vaddr_t vaddr);
static inline void mmu_invalidate_all(void);

#endif /* ARCH_MMU_H */
```

#### 5.3.6 Timer Interface (`include/arch/timer.h`)

```c
#ifndef ARCH_TIMER_H
#define ARCH_TIMER_H

#include <stdint.h>

/*
 * Timer Interface
 *
 * Abstracts hardware timer across architectures.
 */

/* Initialize timer to fire at given frequency (Hz) */
void arch_timer_init(unsigned frequency);

/* Read current tick count (monotonic) */
uint64_t arch_timer_ticks(void);

/* Calibrate timer (called during boot) */
void arch_timer_calibrate(void);

/* Acknowledge timer interrupt (if needed) */
void arch_timer_ack(void);

#endif /* ARCH_TIMER_H */
```

#### 5.3.7 I/O Interface (`include/arch/io.h`)

```c
#ifndef ARCH_IO_H
#define ARCH_IO_H

#include <stdint.h>

/*
 * I/O Interface
 *
 * x86: Port-mapped I/O (IN/OUT instructions)
 * RISC-V: Memory-mapped I/O (load/store to device addresses)
 *
 * For new code, prefer mmio_* functions which work on both.
 * The pio_* functions are x86-only for legacy device compatibility.
 */

/* Memory-mapped I/O (works on all architectures) */
static inline uint8_t  mmio_read8(volatile void *addr);
static inline uint16_t mmio_read16(volatile void *addr);
static inline uint32_t mmio_read32(volatile void *addr);
static inline void mmio_write8(volatile void *addr, uint8_t val);
static inline void mmio_write16(volatile void *addr, uint16_t val);
static inline void mmio_write32(volatile void *addr, uint32_t val);

#ifdef ARCH_I386
/* Port I/O (x86 only) */
static inline uint8_t  pio_inb(uint16_t port);
static inline uint16_t pio_inw(uint16_t port);
static inline uint32_t pio_inl(uint16_t port);
static inline void pio_outb(uint16_t port, uint8_t val);
static inline void pio_outw(uint16_t port, uint16_t val);
static inline void pio_outl(uint16_t port, uint32_t val);
#endif

#endif /* ARCH_IO_H */
```

### 5.4 Example Implementations

#### 5.4.1 x86 Interrupt Control (`arch/i386/include/arch_intr.h`)

```c
#ifndef ARCH_I386_INTR_H
#define ARCH_I386_INTR_H

#include "arch/intr.h"

#define FLAG_IF 0x00000200  /* EFLAGS Interrupt Flag */

static inline enum intr_level intr_get_level(void) {
    uint32_t flags;
    asm volatile("pushfl; popl %0" : "=g"(flags));
    return (flags & FLAG_IF) ? INTR_ON : INTR_OFF;
}

static inline enum intr_level intr_set_level(enum intr_level level) {
    return level == INTR_ON ? intr_enable() : intr_disable();
}

static inline enum intr_level intr_disable(void) {
    enum intr_level old = intr_get_level();
    asm volatile("cli" : : : "memory");
    return old;
}

static inline enum intr_level intr_enable(void) {
    enum intr_level old = intr_get_level();
    asm volatile("sti");
    return old;
}

static inline bool intr_enabled(void) {
    return intr_get_level() == INTR_ON;
}

#endif /* ARCH_I386_INTR_H */
```

#### 5.4.2 RISC-V Interrupt Control (`arch/riscv64/include/arch_intr.h`)

```c
#ifndef ARCH_RISCV64_INTR_H
#define ARCH_RISCV64_INTR_H

#include "arch/intr.h"
#include "arch/riscv64/include/csr.h"

#define SSTATUS_SIE  (1UL << 1)  /* Supervisor Interrupt Enable */

static inline enum intr_level intr_get_level(void) {
    uint64_t sstatus = csr_read(sstatus);
    return (sstatus & SSTATUS_SIE) ? INTR_ON : INTR_OFF;
}

static inline enum intr_level intr_set_level(enum intr_level level) {
    return level == INTR_ON ? intr_enable() : intr_disable();
}

static inline enum intr_level intr_disable(void) {
    enum intr_level old = intr_get_level();
    csr_clear(sstatus, SSTATUS_SIE);
    return old;
}

static inline enum intr_level intr_enable(void) {
    enum intr_level old = intr_get_level();
    csr_set(sstatus, SSTATUS_SIE);
    return old;
}

static inline bool intr_enabled(void) {
    return intr_get_level() == INTR_ON;
}

#endif /* ARCH_RISCV64_INTR_H */
```

#### 5.4.3 x86 MMU Operations (`arch/i386/include/arch_mmu.h`)

```c
#ifndef ARCH_I386_MMU_H
#define ARCH_I386_MMU_H

#include "arch/mmu.h"

#define PHYS_BASE 0xC0000000  /* 3GB kernel boundary */

static inline vaddr_t ptov(paddr_t paddr) {
    return (vaddr_t)(paddr + PHYS_BASE);
}

static inline paddr_t vtop(vaddr_t vaddr) {
    return (paddr_t)(vaddr - PHYS_BASE);
}

static inline bool is_user_vaddr(vaddr_t addr) {
    return addr < PHYS_BASE;
}

static inline bool is_kernel_vaddr(vaddr_t addr) {
    return addr >= PHYS_BASE;
}

static inline void mmu_invalidate_page(vaddr_t vaddr) {
    asm volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

static inline void mmu_invalidate_all(void) {
    uint32_t cr3;
    asm volatile("movl %%cr3, %0" : "=r"(cr3));
    asm volatile("movl %0, %%cr3" : : "r"(cr3) : "memory");
}

#endif /* ARCH_I386_MMU_H */
```

#### 5.4.4 RISC-V MMU Operations (`arch/riscv64/include/arch_mmu.h`)

```c
#ifndef ARCH_RISCV64_MMU_H
#define ARCH_RISCV64_MMU_H

#include "arch/mmu.h"
#include "arch/riscv64/include/csr.h"

/* Sv39: Kernel at top of 39-bit address space */
#define PHYS_BASE 0xFFFFFFC000000000ULL

static inline vaddr_t ptov(paddr_t paddr) {
    return (vaddr_t)(paddr + PHYS_BASE);
}

static inline paddr_t vtop(vaddr_t vaddr) {
    return (paddr_t)(vaddr - PHYS_BASE);
}

static inline bool is_user_vaddr(vaddr_t addr) {
    return addr < PHYS_BASE;
}

static inline bool is_kernel_vaddr(vaddr_t addr) {
    return addr >= PHYS_BASE;
}

static inline void mmu_invalidate_page(vaddr_t vaddr) {
    asm volatile("sfence.vma %0, zero" : : "r"(vaddr) : "memory");
}

static inline void mmu_invalidate_all(void) {
    asm volatile("sfence.vma zero, zero" : : : "memory");
}

#endif /* ARCH_RISCV64_MMU_H */
```

### 5.5 RISC-V Interrupt Frame Structure

The RISC-V `intr_frame` must save all 31 general-purpose registers plus relevant CSRs:

```c
/* arch/riscv64/intr.h - RISC-V interrupt/trap frame */
struct intr_frame {
    /* General purpose registers (31 total, x0 is hardwired to 0) */
    uint64_t ra;       /* x1:  Return address */
    uint64_t sp;       /* x2:  Stack pointer (user's, if from U-mode) */
    uint64_t gp;       /* x3:  Global pointer */
    uint64_t tp;       /* x4:  Thread pointer */
    uint64_t t0;       /* x5:  Temporary */
    uint64_t t1;       /* x6:  Temporary */
    uint64_t t2;       /* x7:  Temporary */
    uint64_t s0;       /* x8:  Saved register / frame pointer */
    uint64_t s1;       /* x9:  Saved register */
    uint64_t a0;       /* x10: Argument / return value */
    uint64_t a1;       /* x11: Argument / return value */
    uint64_t a2;       /* x12: Argument */
    uint64_t a3;       /* x13: Argument */
    uint64_t a4;       /* x14: Argument */
    uint64_t a5;       /* x15: Argument */
    uint64_t a6;       /* x16: Argument */
    uint64_t a7;       /* x17: Argument / syscall number */
    uint64_t s2;       /* x18: Saved register */
    uint64_t s3;       /* x19: Saved register */
    uint64_t s4;       /* x20: Saved register */
    uint64_t s5;       /* x21: Saved register */
    uint64_t s6;       /* x22: Saved register */
    uint64_t s7;       /* x23: Saved register */
    uint64_t s8;       /* x24: Saved register */
    uint64_t s9;       /* x25: Saved register */
    uint64_t s10;      /* x26: Saved register */
    uint64_t s11;      /* x27: Saved register */
    uint64_t t3;       /* x28: Temporary */
    uint64_t t4;       /* x29: Temporary */
    uint64_t t5;       /* x30: Temporary */
    uint64_t t6;       /* x31: Temporary */

    /* Supervisor CSRs saved on trap */
    uint64_t sepc;     /* Exception program counter */
    uint64_t sstatus;  /* Status register */
    uint64_t scause;   /* Trap cause */
    uint64_t stval;    /* Trap value (faulting address or instruction) */
};

/* Size: 35 × 8 = 280 bytes (must be 16-byte aligned) */
#define INTR_FRAME_SIZE  288  /* Rounded up for alignment */

/* scause values */
#define CAUSE_MISALIGNED_FETCH      0
#define CAUSE_FETCH_ACCESS          1
#define CAUSE_ILLEGAL_INSTRUCTION   2
#define CAUSE_BREAKPOINT            3
#define CAUSE_MISALIGNED_LOAD       4
#define CAUSE_LOAD_ACCESS           5
#define CAUSE_MISALIGNED_STORE      6
#define CAUSE_STORE_ACCESS          7
#define CAUSE_USER_ECALL            8
#define CAUSE_SUPERVISOR_ECALL      9
#define CAUSE_INSTRUCTION_PAGE_FAULT 12
#define CAUSE_LOAD_PAGE_FAULT       13
#define CAUSE_STORE_PAGE_FAULT      15

/* Interrupt bit (bit 63 of scause) */
#define CAUSE_INTERRUPT_FLAG        (1UL << 63)
#define CAUSE_IS_INTERRUPT(c)       ((c) & CAUSE_INTERRUPT_FLAG)

/* Interrupt causes (with bit 63 set) */
#define CAUSE_SUPERVISOR_SOFTWARE   (CAUSE_INTERRUPT_FLAG | 1)
#define CAUSE_SUPERVISOR_TIMER      (CAUSE_INTERRUPT_FLAG | 5)
#define CAUSE_SUPERVISOR_EXTERNAL   (CAUSE_INTERRUPT_FLAG | 9)
```

**Comparison with x86 intr_frame**:

| Aspect | x86 (PintOS current) | RISC-V |
|--------|---------------------|--------|
| Size | 76 bytes | 280 bytes |
| GP registers saved | 8 (edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax) | 31 (all except x0) |
| Error code | Hardware-provided for some exceptions | None (use stval) |
| Return address | cs:eip from stack | sepc CSR |
| Privilege info | cs selector bits | sstatus.SPP |

### 5.6 Using the HAL in Portable Code

#### Before (x86-specific):
```c
/* threads/thread.c - old version */
#include "threads/interrupt.h"
#include "threads/vaddr.h"

void thread_block(void) {
    ASSERT(!intr_context());
    ASSERT(intr_get_level() == INTR_OFF);  /* x86-specific check */
    // ...
}
```

#### After (HAL-based):
```c
/* threads/thread.c - portable version */
#include "arch/arch.h"  /* Includes correct arch headers */

void thread_block(void) {
    ASSERT(!intr_context());
    ASSERT(intr_get_level() == INTR_OFF);  /* Now portable! */
    // ...
}
```

### 5.6 HAL Effort Estimate

| Component | Header LOC | i386 Impl | RISC-V Impl | Total |
|-----------|------------|-----------|-------------|-------|
| arch.h + types.h | 60 | 30 | 40 | 130 |
| cpu.h | 30 | 40 | 40 | 110 |
| intr.h | 50 | 60 | 60 | 170 |
| mmu.h | 80 | 100 | 120 | 300 |
| timer.h | 30 | 50 | 60 | 140 |
| io.h | 40 | 80 | 40 | 160 |
| **Total** | **290** | **360** | **360** | **~1,010** |

### 5.7 HAL Benefits

1. **Single source of truth**: Portable code uses one interface
2. **Compile-time selection**: No runtime overhead
3. **Easy comparison**: Students can diff arch implementations
4. **Future-proof**: Adding ARM64 means implementing ~360 LOC
5. **Testable**: Can mock HAL for unit testing portable code

---

## 6. Implementation Phases

### Phase 0: Build System Infrastructure
**Duration**: 1 week | **LOC**: ~800

- Add `ARCH=riscv64` Makefile variable
- RISC-V toolchain detection (`riscv64-unknown-elf-gcc`)
- Conditional source file inclusion
- QEMU launch script for RISC-V

**Key Make.config Changes**:
```makefile
# Architecture selection (default to i386 for backward compatibility)
ARCH ?= i386

ifeq ($(ARCH),i386)
    # Existing i386 configuration
    CC = gcc -m32
    LD = ld -melf_i386
    OBJCOPY = objcopy
    ARCH_CFLAGS = -march=i686 -m32
    ARCH_LDFLAGS = -melf_i386
    KERNEL_FORMAT = elf32-i386

else ifeq ($(ARCH),riscv64)
    # RISC-V 64-bit configuration
    CROSS_COMPILE ?= riscv64-unknown-elf-
    CC = $(CROSS_COMPILE)gcc
    LD = $(CROSS_COMPILE)ld
    OBJCOPY = $(CROSS_COMPILE)objcopy

    # RV64GC: General + Compressed + FP
    ARCH_CFLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany
    ARCH_ASFLAGS = -march=rv64gc -mabi=lp64d
    ARCH_LDFLAGS = -melf64lriscv
    KERNEL_FORMAT = elf64-littleriscv

    # No loader.bin for RISC-V (OpenSBI handles boot)
    NO_LOADER = 1

else
    $(error Unknown ARCH=$(ARCH). Use i386 or riscv64)
endif

# Architecture define for conditional compilation
DEFINES += -DARCH_$(shell echo $(ARCH) | tr a-z A-Z)
```

**Makefile.build Conditional Sources**:
```makefile
# Architecture-specific sources
ARCH_DIR = arch/$(ARCH)

ifeq ($(ARCH),i386)
    arch_SRC = $(ARCH_DIR)/loader.S $(ARCH_DIR)/start.S
    arch_SRC += $(ARCH_DIR)/switch.S $(ARCH_DIR)/intr-stubs.S
    arch_SRC += $(ARCH_DIR)/intr.c $(ARCH_DIR)/pagedir.c
    arch_SRC += $(ARCH_DIR)/gdt.c $(ARCH_DIR)/tss.c
    devices_ARCH_SRC = $(ARCH_DIR)/pit.c $(ARCH_DIR)/ide.c
else ifeq ($(ARCH),riscv64)
    arch_SRC = $(ARCH_DIR)/start.S $(ARCH_DIR)/switch.S
    arch_SRC += $(ARCH_DIR)/trap.S $(ARCH_DIR)/intr.c
    arch_SRC += $(ARCH_DIR)/pagedir.c $(ARCH_DIR)/sbi.c
    arch_SRC += $(ARCH_DIR)/timer.c $(ARCH_DIR)/plic.c
    devices_ARCH_SRC = $(ARCH_DIR)/virtio.c $(ARCH_DIR)/virtio_blk.c
    devices_ARCH_SRC += devices/ns16550a.c
endif
```

### Phase 0.5: HAL Infrastructure
**Duration**: 1 week | **LOC**: ~1,010

- Create `include/arch/` abstraction headers (see Section 5)
- Implement i386 HAL wrappers (wrap existing inline asm)
- Create RISC-V HAL stubs (to be filled in subsequent phases)
- Update portable code to use HAL interfaces
- Key files: `arch.h`, `cpu.h`, `intr.h`, `mmu.h`, `timer.h`, `io.h`

**Milestone**: `make ARCH=i386` still works with HAL layer

### Phase 1: Bare Metal Boot
**Duration**: 2 weeks | **LOC**: ~700

- `start.S`: Entry from OpenSBI, stack setup, BSS zeroing
- `kernel.lds`: ELF64-riscv linker script
- `csr.h`: Control and Status Register definitions
- `sbi.h` + `sbi_console.c`: SBI calls for early printf

**Milestone**: "Pintos booting..." appears on console

### Phase 2: Memory Management
**Duration**: 2 weeks | **LOC**: ~850

- `pte.h`: Sv39 PTE/PDE format and macros
- `vaddr.h`: 64-bit virtual addresses, new PHYS_BASE
- `pagedir.c`: Three-level page table operations, SATP management
- Enable Sv39 paging

**Milestone**: Kernel runs with virtual memory enabled

### Phase 3: Interrupts and Exceptions
**Duration**: 2 weeks | **LOC**: ~1,080

- `trap.S`: Unified trap entry, save/restore all 32 registers
- `interrupt.c`: CSR-based enable/disable, trap dispatch
- `plic.c`: Platform-Level Interrupt Controller driver
- `clint.c`: Timer interrupts via SBI set_timer

**Milestone**: Timer interrupts fire, thread_tick() executes

### Phase 4: Thread Context Switching
**Duration**: 1 week | **LOC**: ~210

- `switch.S`: Save/restore s0-s11, ra (callee-saved)
- `switch.h`: Stack frame structure
- Thread stack initialization for RISC-V

**Milestone**: Kernel threads schedule correctly

### Phase 5: Device Drivers
**Duration**: 2 weeks | **LOC**: ~1,300

- `virtio.c`: VirtQueue management, device discovery
- `virtio_blk.c`: Block device (replaces IDE)
- `ns16550a.c`: Memory-mapped UART
- Basic device tree parsing for RAM size

**Milestone**: File system can read/write, console works

### Phase 6: User Mode Support
**Duration**: 3 weeks | **LOC**: ~950

- User/kernel separation via PTE_U bit
- `trap.S`: Handle ecall from U-mode
- `syscall.c`: Syscall dispatch using a0-a7
- `lib/user/syscall.c`: User-space ecall stubs
- Process loading and entry

**Milestone**: "echo hello" runs successfully

### Phase 7: Virtual Memory
**Duration**: 2 weeks | **LOC**: ~450

- Update SPT and frame table for 64-bit
- Page fault handler using scause/stval
- Stack growth, mmap support

**Milestone**: All VM tests pass

---

## 7. Effort Summary

| Phase | New LOC | Modified LOC | Total |
|-------|---------|--------------|-------|
| Build System | 600 | 200 | 800 |
| **HAL Layer** | **650** | **360** | **1,010** |
| Boot | 600 | 100 | 700 |
| Memory | 700 | 150 | 850 |
| Interrupts | 930 | 150 | 1,080 |
| Context Switch | 110 | 100 | 210 |
| Devices | 1,200 | 100 | 1,300 |
| User Mode | 700 | 250 | 950 |
| VM | 250 | 200 | 450 |
| **Total** | **5,740** | **1,610** | **7,350** |

---

## 8. Key Design Decisions

### 8.1 Virtual Address Layout

Use high-half kernel to preserve PHYS_BASE semantics:

```c
/* Sv39: Kernel mapped at top of 39-bit space */
#define PHYS_BASE  0xFFFFFFC000000000ULL  /* -256 GB */
#define KERN_BASE  0xFFFFFFFF80000000ULL  /* -2 GB */
```

### 8.2 Kernel Stack on Trap Entry

Use sscratch CSR to hold kernel stack pointer:

```asm
trap_entry:
    csrrw sp, sscratch, sp   # Swap user sp with kernel sp
    # If was already in S-mode, adjust accordingly
```

### 8.3 System Call Convention

Follow Linux RISC-V convention for familiarity:
- Syscall number: a7
- Arguments: a0-a5
- Return value: a0

### 8.4 Boot Method

OpenSBI as M-mode firmware, kernel as S-mode payload:

```bash
qemu-system-riscv64 -machine virt -bios default -kernel kernel.bin
```

### 8.5 Device Discovery

Start with hardcoded QEMU virt addresses:

```c
#define UART_BASE   0x10000000
#define PLIC_BASE   0x0C000000
#define CLINT_BASE  0x02000000
#define VIRTIO_BASE 0x10001000
```

---

## 9. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Sv39 3-level paging bugs | High | High | Extensive printf debugging in page table setup; verify each level separately |
| VirtIO driver complexity | Medium | High | Start with polling mode; use ramdisk as fallback; reference Linux driver |
| 64-bit pointer issues | Medium | Medium | Use `uintptr_t` consistently; audit all casts; enable `-Wpointer-arith` |
| RISC-V toolchain issues | Low | High | Use standard riscv-gnu-toolchain; document exact version requirements |
| SBI compatibility | Low | Medium | Target OpenSBI 1.0+; use only base extension initially |
| **Memory ordering bugs** | **Medium** | **High** | **Audit spinlocks and intq.c; add fence instructions in HAL barriers** |
| **FP context not saved** | **Low** | **Medium** | **Start with FP disabled (sstatus.FS=0); add lazy save later** |
| **sscratch handling errors** | **Medium** | **High** | **Carefully distinguish S-mode re-entry from U-mode entry in trap.S** |
| **PLIC priority/threshold** | **Low** | **Medium** | **Start with all priorities=1, threshold=0; tune if needed** |

### 9.1 Detailed Risk Mitigations

**Sv39 Paging Debugging Strategy**:
1. Implement identity mapping first (VA=PA for kernel)
2. Add extensive `printf` at each page table level creation
3. Dump full page table hierarchy before enabling SATP
4. Use QEMU `-d mmu` flag to trace translations
5. Test with single page mapping before full memory map

**Memory Ordering Audit Checklist**:
- [ ] `threads/synch.c`: lock_acquire/release need acquire/release semantics
- [ ] `devices/intq.c`: ring buffer head/tail updates need proper ordering
- [ ] `userprog/pagedir.c`: PTE writes before SFENCE.VMA
- [ ] `threads/thread.c`: thread_current() after any stack switch

**sscratch Trap Entry Logic**:
```asm
trap_entry:
    csrrw sp, sscratch, sp    # Swap sp with sscratch
    beqz sp, trap_from_kernel # If sp==0, was already in S-mode
    # ... save user context using kernel sp ...
    j trap_common
trap_from_kernel:
    csrrw sp, sscratch, sp    # Restore original sp
    addi sp, sp, -FRAME_SIZE  # Allocate frame on kernel stack
    # ... save kernel context ...
trap_common:
    # ... call C handler ...
```

---

## 10. Testing Strategy

### Per-Phase Verification

| Phase | Test | Expected Output |
|-------|------|-----------------|
| Boot | Console output | "Pintos booting..." |
| Memory | palloc_get_page | Successful allocations |
| Interrupts | Timer | "Timer: N ticks" messages |
| Context Switch | alarm-single | Test passes |
| Devices | File read | Contents match |
| User Mode | do-nothing | Clean exit |
| VM | page-parallel | All pages load correctly |

### QEMU Command

```bash
qemu-system-riscv64 \
    -machine virt \
    -nographic \
    -bios default \
    -kernel build/kernel.bin \
    -drive file=build/os.dsk,format=raw,if=none,id=hd0 \
    -device virtio-blk-device,drive=hd0 \
    -m 128M \
    -s -S  # Optional: for GDB debugging
```

---

## 11. Educational Value

This port provides significant learning opportunities:

1. **Architecture Comparison**: Understanding how two different ISAs solve the same OS problems
2. **Cleaner Design**: RISC-V's lack of segmentation and simpler trap model makes concepts clearer
3. **Modern Hardware**: RISC-V is increasingly used in education and industry
4. **Deeper VM Understanding**: Three-level Sv39 paging teaches more general concepts than x86's two-level

---

## 12. Conclusion

Porting PintOS to RISC-V is a **substantial but well-structured project**. The main challenges are:

1. **Creating a HAL** - The minimal HAL (~1,010 lines) separates arch-specific code cleanly
2. **New paging model** - Sv39 three-level tables differ significantly from x86
3. **Device driver rewrites** - All I/O must change from port-based to memory-mapped

However, the benefits for educational purposes are significant:
- RISC-V's cleaner design aids understanding
- Comparing architectures deepens comprehension of OS abstractions
- Modern, industry-relevant architecture exposure
- HAL enables easy addition of future architectures (ARM64, etc.)

**Recommended approach**:
1. Start with build system and HAL infrastructure (phases 0 + HAL)
2. Complete boot through context switch (phases 1-4) for a running kernel
3. Add user mode and VM features incrementally (phases 5-7)
