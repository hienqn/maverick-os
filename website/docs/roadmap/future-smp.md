---
sidebar_position: 3
---

# Future: SMP Support

This page outlines the planned implementation of Symmetric Multi-Processing (SMP) support in PintOS.

## Overview

SMP support will enable PintOS to run on multiple CPU cores, providing:

- **Parallelism**: True concurrent execution of threads
- **Scalability**: Better performance on multi-core systems
- **Educational Value**: Understanding of real-world multiprocessor challenges

## Target Configuration

| Parameter | Value |
|-----------|-------|
| Architecture | x86 32-bit |
| Max CPUs | 2-8 (via QEMU `-smp`) |
| APIC Mode | Legacy xAPIC |
| Timer | Local APIC timer per CPU |

## Implementation Phases

### Phase 1: Spinlocks & Atomics

**Goal**: Replace interrupt disabling with proper spinlocks.

```c
struct spinlock {
  volatile int locked;
  struct cpu *cpu;    /* Holding CPU (for debugging) */
  char *name;
};

void spinlock_acquire(struct spinlock *lock) {
  pushcli();  /* Disable interrupts */
  while (xchg(&lock->locked, 1) != 0)
    ;  /* Spin */
  __sync_synchronize();  /* Memory barrier */
  lock->cpu = mycpu();
}
```

**Key Changes**:
- Implement atomic operations (`xchg`, `cmpxchg`, `xadd`)
- Add memory barriers
- Convert all `intr_disable()` to spinlocks

### Phase 2: Per-CPU Data

**Goal**: Each CPU needs its own:
- GDT and TSS
- Kernel stack for interrupts
- Current thread pointer
- Idle thread

```c
struct cpu {
  uint8_t id;                    /* CPU ID (from APIC) */
  struct gdt_entry gdt[SEL_CNT]; /* Per-CPU GDT */
  struct tss tss;                /* Per-CPU TSS */
  struct thread *thread;         /* Current thread */
  struct thread *idle;           /* Idle thread */
};

struct cpu cpus[MAX_CPU];
struct cpu *mycpu(void);  /* Get current CPU struct */
```

### Phase 3: APIC Support

**Goal**: Configure Local APIC and I/O APIC.

```c
/* Local APIC registers */
#define LAPIC_ID      0x020
#define LAPIC_EOI     0x0B0
#define LAPIC_SVR     0x0F0
#define LAPIC_TIMER   0x320

void lapic_init(void) {
  /* Enable Local APIC */
  lapic_write(LAPIC_SVR, LAPIC_ENABLE | (T_IRQ0 + IRQ_SPURIOUS));

  /* Configure timer */
  lapic_write(LAPIC_TIMER, T_IRQ0 + IRQ_TIMER);
  lapic_write(LAPIC_TDCR, LAPIC_X1);
  lapic_write(LAPIC_TICR, 10000000);  /* Initial count */
}
```

### Phase 4: AP Bootstrap

**Goal**: Start Application Processors (APs) using INIT-SIPI-SIPI sequence.

```c
void start_ap(int apicid, void *stack) {
  /* Send INIT IPI */
  lapic_write(LAPIC_ICRHI, apicid << 24);
  lapic_write(LAPIC_ICRLO, LAPIC_INIT | LAPIC_ASSERT);
  microdelay(200);

  /* Send SIPI twice */
  for (int i = 0; i < 2; i++) {
    lapic_write(LAPIC_ICRHI, apicid << 24);
    lapic_write(LAPIC_ICRLO, LAPIC_STARTUP | (TRAMPOLINE >> 12));
    microdelay(200);
  }
}
```

### Phase 5: SMP Scheduler

**Goal**: Make the scheduler work with multiple CPUs.

**Approach 1: Global Run Queue** (Simpler)
```c
struct spinlock sched_lock;
struct list ready_list;  /* Shared by all CPUs */

struct thread *next_thread_to_run(void) {
  spinlock_acquire(&sched_lock);
  struct thread *t = list_empty(&ready_list)
    ? mycpu()->idle
    : list_entry(list_pop_front(&ready_list), struct thread, elem);
  spinlock_release(&sched_lock);
  return t;
}
```

**Approach 2: Per-CPU Run Queues** (Better scaling)
```c
struct cpu {
  struct spinlock lock;
  struct list ready_list;
};

/* Work stealing when local queue empty */
struct thread *steal_thread(void);
```

### Phase 6: TLB Shootdown

**Goal**: Invalidate TLB entries across CPUs when page tables change.

```c
void tlb_shootdown(void *va) {
  /* Send IPI to all other CPUs */
  for (int i = 0; i < ncpu; i++) {
    if (cpus[i].id != mycpu()->id) {
      lapic_ipi(cpus[i].id, T_TLBFLUSH);
    }
  }
  invlpg(va);  /* Invalidate local TLB */
}

/* Handler on receiving CPU */
void tlb_ipi_handler(void) {
  invlpg(current_shootdown_addr);
  lapic_eoi();
}
```

## Estimated Scope

| Phase | New Lines | Modified Lines |
|-------|-----------|----------------|
| Spinlocks | 200 | 300 |
| Per-CPU | 300 | 150 |
| APIC | 400 | 100 |
| AP Bootstrap | 350 | 50 |
| Scheduler | 300 | 200 |
| TLB Shootdown | 200 | 50 |
| **Total** | **~1,750** | **~850** |

## New Files

| File | Purpose |
|------|---------|
| `threads/spinlock.c` | Spinlock implementation |
| `threads/spinlock.h` | Spinlock declarations |
| `threads/cpu.c` | Per-CPU data structures |
| `threads/cpu.h` | CPU struct definition |
| `devices/lapic.c` | Local APIC driver |
| `devices/ioapic.c` | I/O APIC driver |
| `threads/mp.c` | MP table parsing |
| `threads/trampoline.S` | AP startup code |

## Challenges

1. **Race Conditions**: Any shared data needs locking
2. **Deadlocks**: Lock ordering must be consistent
3. **Cache Coherence**: x86 handles automatically, but awareness needed
4. **Debugging**: Much harder with multiple CPUs

## Testing Strategy

1. **Spinlock contention tests**
2. **Parallel thread execution**
3. **Priority scheduling on SMP**
4. **TLB shootdown correctness**
5. **Stress tests with high CPU counts**

## References

- Intel Software Developer's Manual, Vol. 3A (System Programming)
- xv6 SMP implementation
- Linux kernel SMP code

## See Also

- [Completed Features](/docs/roadmap/completed-features)
- [Changelog](/docs/roadmap/changelog)
