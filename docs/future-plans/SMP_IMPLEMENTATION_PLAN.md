# PintOS SMP (Symmetric Multi-Processing) Implementation Plan

## Overview

This document outlines a production-quality SMP implementation for PintOS on 32-bit x86 (i386). The implementation uses legacy xAPIC for interrupt handling, starts with a global run queue protected by spinlocks, and includes comprehensive testing.

**Target**: 2-8 CPU support with QEMU `-smp N` flag

---

## Architecture Summary

### Current State (Uniprocessor)
- Synchronization via `intr_disable()` - **breaks on SMP**
- Single global TSS, GDT, idle thread
- 8259 PIC for interrupts (no per-CPU timer)
- Global ready queues without protection

### Target State (SMP)
- Spinlocks with atomic instructions for synchronization
- Per-CPU: TSS, GDT, idle thread, kernel stack, LAPIC timer
- Local APIC + I/O APIC for interrupt routing
- Global ready queue with spinlock (per-CPU queues in future phase)

---

## Phase 1: Foundation - Spinlocks and Per-CPU Data

### New Files

| File | Purpose |
|------|---------|
| `src/threads/atomic.h` | Atomic operations (`xchg`, `cmpxchg`, `xadd`) and memory barriers |
| `src/threads/spinlock.h/c` | Spinlock with test-and-set, PAUSE hint, debug tracking |
| `src/threads/cpu.h/c` | Per-CPU data structure and `cpu_current()` accessor |
| `src/threads/cpuid.h/c` | CPUID wrapper for feature detection |

### Key Structures

```c
struct spinlock {
    volatile uint32_t locked;     /* 0=unlocked, 1=locked */
    struct thread *holder;        /* Debug: who holds it */
    const char *name;
};

struct cpu {
    uint8_t id;                   /* CPU number (0 = BSP) */
    uint8_t apic_id;              /* Local APIC ID */
    bool started;                 /* AP startup complete? */
    struct thread *idle_thread;   /* Per-CPU idle thread */
    struct thread *current;       /* Currently running thread */
    uint32_t thread_ticks;        /* Ticks since context switch */
    uint64_t gdt[SEL_CNT + 1];    /* Per-CPU GDT */
    struct tss tss;               /* Per-CPU TSS */
};
```

### Atomic Operations (x86)
```c
/* Test-and-set using XCHG (implicitly locked) */
static inline uint32_t atomic_xchg(volatile uint32_t *ptr, uint32_t val) {
    asm volatile("xchgl %0, %1" : "+r"(val), "+m"(*ptr) : : "memory");
    return val;
}

/* Compare-and-swap */
static inline bool atomic_cmpxchg(volatile uint32_t *ptr,
                                   uint32_t expected, uint32_t desired) {
    uint32_t old;
    asm volatile("lock cmpxchgl %2, %1"
                 : "=a"(old), "+m"(*ptr)
                 : "r"(desired), "0"(expected)
                 : "memory");
    return old == expected;
}
```

### Testing
- Spinlock contention test (multiple threads incrementing shared counter)
- CPUID feature verification

---

## Phase 2: APIC Support

### New Files

| File | Purpose |
|------|---------|
| `src/threads/msr.h` | MSR read/write (`rdmsr`, `wrmsr`) |
| `src/devices/lapic.h/c` | Local APIC init, timer, IPI, EOI |
| `src/devices/ioapic.h/c` | I/O APIC interrupt routing |
| `src/devices/acpi.h/c` | ACPI MADT parsing for CPU/APIC discovery |

### Local APIC Registers (Memory-mapped at 0xFEE00000)

| Offset | Register | Purpose |
|--------|----------|---------|
| 0x020 | LAPIC_ID | CPU's APIC ID |
| 0x0B0 | LAPIC_EOI | End-of-interrupt signal |
| 0x0F0 | LAPIC_SVR | Spurious vector + enable bit |
| 0x300 | LAPIC_ICRLO | IPI command (low) |
| 0x310 | LAPIC_ICRHI | IPI destination (high) |
| 0x320 | LAPIC_TIMER | Timer LVT entry |
| 0x380 | LAPIC_TICR | Timer initial count |

### IPI Vectors

| Vector | Purpose |
|--------|---------|
| 0xF0 | TLB shootdown |
| 0xF1 | Reschedule request |
| 0xF2 | CPU stop (for shutdown) |

### Key Functions
```c
void lapic_init(void);           /* BSP LAPIC setup */
void lapic_init_ap(void);        /* AP LAPIC setup */
void lapic_eoi(void);            /* Acknowledge interrupt */
void lapic_send_ipi(uint8_t dest, uint8_t vector);
void lapic_send_init_ipi(uint8_t dest);
void lapic_send_startup_ipi(uint8_t dest, uint8_t vector);
```

### Modified Files
- `src/threads/interrupt.c` - Replace `pic_end_of_interrupt()` with `lapic_eoi()`
- `src/devices/timer.c` - Support LAPIC timer alongside PIT

---

## Phase 3: AP Bootstrap

### New Files

| File | Purpose |
|------|---------|
| `src/threads/trampoline.S` | 16-bit real mode AP startup code |

### AP Startup Sequence (INIT-SIPI-SIPI)

```
BSP                                    AP (Application Processor)
 │                                      │
 │── Send INIT IPI ────────────────────>│ (Reset AP)
 │                                      │
 │   [10ms delay]                       │
 │                                      │
 │── Send SIPI (vector=0x07) ──────────>│ Start at 0x7000 (real mode)
 │                                      │
 │   [200us delay]                      ├── Load GDT, enable protected mode
 │                                      ├── Enable paging (use BSP's CR3)
 │── Send SIPI (retry) ────────────────>├── Set up per-CPU stack
 │                                      ├── Call ap_main()
 │                                      ├── Init per-CPU GDT/TSS
 │                                      ├── Init local APIC
 │<── Increment ap_started_count ───────┤
 │                                      │
 │── Set smp_started = true ───────────>│
 │                                      ├── Enable interrupts
 │                                      └── Enter idle loop
```

### Trampoline Code (16-bit entry point at < 1MB)
```asm
.code16
ap_trampoline_start:
    cli
    lgdt ap_gdtdesc           # Load temporary GDT
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0            # Enable protected mode
    ljmp $SEL_KCSEG, $ap_protected_entry

.code32
ap_protected_entry:
    # Set up segments, stack, paging
    # Jump to ap_main() in C
```

### Modified Files
- `src/threads/init.c` - Add `smp_init()`, `ap_main()`
- `src/userprog/gdt.c` - Per-CPU GDT initialization
- `src/userprog/tss.c` - Per-CPU TSS initialization

---

## Phase 4: SMP Scheduler

### Synchronization Conversion

**Before (uniprocessor):**
```c
void sema_down(struct semaphore *sema) {
    enum intr_level old = intr_disable();
    while (sema->value == 0) {
        list_push_back(&sema->waiters, &thread_current()->elem);
        thread_block();
    }
    sema->value--;
    intr_set_level(old);
}
```

**After (SMP):**
```c
void sema_down(struct semaphore *sema) {
    enum intr_level old = spinlock_acquire_irq(&sema->lock);
    while (sema->value == 0) {
        list_push_back(&sema->waiters, &thread_current()->elem);
        thread_block_releasing(&sema->lock, old);  /* Release before sleep */
        old = spinlock_acquire_irq(&sema->lock);   /* Reacquire after wake */
    }
    sema->value--;
    spinlock_release_irq(&sema->lock, old);
}
```

### Global Run Queue Protection

```c
/* In thread.c */
static struct spinlock ready_lock;      /* Protects ready queues */
static struct spinlock all_list_lock;   /* Protects all_list */

void thread_unblock(struct thread *t) {
    enum intr_level old = spinlock_acquire_irq(&ready_lock);
    t->status = THREAD_READY;
    thread_enqueue(t);
    spinlock_release_irq(&ready_lock, old);
}

static struct thread *next_thread_to_run(void) {
    /* Called with ready_lock held */
    struct thread *t = scheduler_dispatch();
    return t ? t : cpu_current()->idle_thread;
}
```

### Per-CPU Timer Handling

```c
static void timer_interrupt(struct intr_frame *f UNUSED) {
    struct cpu *c = cpu_current();

    if (c->id == 0) {
        atomic_add(&ticks, 1);          /* BSP maintains global ticks */
        wake_sleeping_threads();         /* BSP handles sleep queue */
    }

    if (++c->thread_ticks >= TIME_SLICE)
        intr_yield_on_return();

    lapic_eoi();
}
```

### Modified Files
- `src/threads/synch.c` - Convert all primitives to use spinlocks
- `src/threads/synch.h` - Add spinlock field to semaphore
- `src/threads/thread.c` - Add ready_lock, per-CPU idle threads
- `src/devices/timer.c` - Per-CPU timer tick handling

---

## Phase 5: Advanced Features

### TLB Shootdown (Required for VM)

When a page table entry is invalidated, all CPUs must flush their TLBs:

```c
void tlb_shootdown(void *vaddr) {
    /* Invalidate locally */
    asm volatile("invlpg (%0)" : : "r"(vaddr) : "memory");

    /* Send IPI to all other CPUs */
    for (int i = 0; i < cpu_count; i++) {
        if (i != cpu_id())
            lapic_send_ipi(cpus[i].apic_id, IPI_TLB_SHOOTDOWN);
    }

    /* Wait for acknowledgments */
    while (tlb_ack_count < cpu_count - 1)
        asm volatile("pause");
}
```

### Console Lock (Prevent Interleaved Output)

```c
static struct spinlock console_lock;

int vprintf(const char *format, va_list args) {
    enum intr_level old = spinlock_acquire_irq(&console_lock);
    int result = vprintf_internal(format, args);
    spinlock_release_irq(&console_lock, old);
    return result;
}
```

### Future: Per-CPU Run Queues

Design hooks for future migration:
```c
struct cpu {
    /* ... */
    struct list local_ready_list;     /* Per-CPU queue */
    struct spinlock local_ready_lock;
    int queue_length;                  /* For load balancing */
};

void load_balance(void);  /* Periodic migration between CPUs */
```

---

## Phase 6: Testing

### New Test Files

| File | Purpose |
|------|---------|
| `src/tests/threads/smp/smp-spinlock.c` | Spinlock contention under load |
| `src/tests/threads/smp/smp-parallel.c` | Verify concurrent execution |
| `src/tests/threads/smp/smp-priority.c` | Priority scheduling on SMP |
| `src/tests/threads/smp/smp-tlb.c` | TLB shootdown correctness |

### Test: Spinlock Contention
```c
static volatile int counter = 0;
static struct spinlock lock;

void increment_thread(void *aux UNUSED) {
    for (int i = 0; i < 10000; i++) {
        spinlock_acquire(&lock);
        counter++;
        spinlock_release(&lock);
    }
}

void test_spinlock(void) {
    spinlock_init(&lock, "test");
    for (int i = 0; i < cpu_count; i++)
        thread_create("inc", PRI_DEFAULT, increment_thread, NULL);
    /* Wait and verify: counter == cpu_count * 10000 */
}
```

### Test: Parallel Execution
```c
void test_parallel(void) {
    volatile int cpu_ran[MAX_CPUS] = {0};
    barrier_t barrier;

    /* Threads mark their CPU and wait at barrier */
    for (int i = 0; i < cpu_count; i++)
        thread_create("mark", PRI_DEFAULT, mark_and_wait, &barrier);

    /* All CPUs should have marked concurrently */
    for (int i = 0; i < cpu_count; i++)
        ASSERT(cpu_ran[i] == 1);
}
```

### Existing Test Verification
- Run all `tests/threads/` tests with `-smp 4`
- Priority donation tests must pass with spinlock-based locks
- MLFQS tests must handle per-CPU `recent_cpu` tracking

---

## Implementation Order & Dependencies

```
Phase 1: Foundation
   atomic.h ──────────────────────┐
   spinlock.h/c ──────────────────┼──> Phase 4: SMP Scheduler
   cpu.h/c ───────────────────────┤      synch.c modifications
   cpuid.h/c ─────────────────────┘      thread.c modifications
         │
         ▼
Phase 2: APIC Support
   msr.h ─────────────────────────┐
   lapic.h/c ─────────────────────┼──> Phase 3: AP Bootstrap
   ioapic.h/c ────────────────────┤      trampoline.S
   acpi.h/c ──────────────────────┘      smp_init()
                                              │
                                              ▼
                                   Phase 5: Advanced
                                      TLB shootdown
                                      Console lock
                                              │
                                              ▼
                                   Phase 6: Testing
                                      SMP stress tests
                                      Regression tests
```

---

## Critical Files Reference

| File | Current Role | SMP Changes |
|------|--------------|-------------|
| `src/threads/synch.c` | `intr_disable()` synchronization | Spinlock-based |
| `src/threads/thread.c` | Global ready queues, scheduler | Add `ready_lock`, per-CPU idle |
| `src/threads/interrupt.c` | PIC-based interrupts | LAPIC EOI, per-CPU state |
| `src/threads/init.c` | BSP-only boot | Add `smp_init()`, `ap_main()` |
| `src/threads/start.S` | Real→protected mode | Pattern for trampoline |
| `src/userprog/gdt.c` | Single global GDT | Per-CPU GDT array |
| `src/userprog/tss.c` | Single global TSS | Per-CPU TSS in `struct cpu` |
| `src/devices/timer.c` | PIT timer, global ticks | LAPIC timer, per-CPU ticks |

---

## QEMU Testing Commands

```bash
# Single CPU (baseline)
pintos --qemu -- run test-name

# 2 CPUs
pintos --qemu -- -smp 2 run test-name

# 4 CPUs with debug
pintos --qemu -- -smp 4 -d int run test-name

# 8 CPUs stress test
pintos --qemu -- -smp 8 run smp-spinlock
```

---

## Estimated Scope

| Phase | New Lines | Modified Lines | New Files |
|-------|-----------|----------------|-----------|
| 1. Foundation | ~400 | ~50 | 6 |
| 2. APIC | ~600 | ~100 | 6 |
| 3. AP Bootstrap | ~300 | ~150 | 1 |
| 4. SMP Scheduler | ~200 | ~400 | 0 |
| 5. Advanced | ~150 | ~100 | 0 |
| 6. Testing | ~400 | ~50 | 5 |
| **Total** | **~2050** | **~850** | **18** |

---

## References

- Intel 64 and IA-32 Architectures Software Developer's Manual, Vol 3A (APIC)
- AMD64 Architecture Programmer's Manual, Vol 2 (System Programming)
- xv6 SMP implementation (MIT)
- Linux kernel `arch/x86/kernel/smpboot.c`
