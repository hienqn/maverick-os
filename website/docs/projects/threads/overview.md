---
sidebar_position: 1
---

# Project 1: Threads

In this project, you'll implement core threading functionality including scheduling and synchronization.

## Learning Goals

- Understand how threads are created and scheduled
- Implement priority scheduling with donation
- Build an advanced MLFQS scheduler
- Design efficient alarm clock without busy-waiting

## Tasks Overview

| Task | Difficulty | Key Concepts |
|------|------------|--------------|
| Alarm Clock | ★☆☆ | Timer interrupts, thread blocking |
| Priority Scheduling | ★★☆ | Ready queue ordering, preemption |
| Priority Donation | ★★★ | Lock holders, nested donation |
| MLFQS | ★★★ | Fixed-point math, dynamic priority |

## Key Files

| File | Purpose |
|------|---------|
| `threads/thread.c` | Thread lifecycle, scheduling |
| `threads/thread.h` | Thread struct definition |
| `threads/synch.c` | Locks, semaphores, condition variables |
| `threads/fixed-point.h` | Fixed-point arithmetic for MLFQS |
| `devices/timer.c` | Timer interrupt handler |

## Getting Started

```bash
cd src/threads
make

# Run a specific test
cd build
make tests/threads/alarm-multiple.result

# Run all tests
make check
```

## Task 1: Alarm Clock

### Current Problem

The current `timer_sleep()` implementation busy-waits:

```c
/* BAD: Wastes CPU cycles */
void timer_sleep(int64_t ticks) {
  int64_t start = timer_ticks();
  while (timer_ticks() - start < ticks)
    thread_yield();  /* Spin! */
}
```

### Your Goal

Block the thread and wake it after the specified time:

```c
/* GOOD: Thread sleeps efficiently */
void timer_sleep(int64_t ticks) {
  int64_t wake_time = timer_ticks() + ticks;
  /* Add to sleeping list, block, wake on timer interrupt */
}
```

### Hints

1. Add a `wake_up_tick` field to `struct thread`
2. Maintain a sorted list of sleeping threads
3. In `timer_interrupt()`, wake threads whose time has come

## Task 2: Priority Scheduling

### Goal

Schedule threads based on priority (0-63, higher = more important).

### Key Points

1. **Ready Queue**: Keep sorted by priority
2. **Preemption**: When a higher-priority thread becomes ready, preempt current
3. **Semaphores/Locks**: Wake highest-priority waiter first

### Tests

```bash
make tests/threads/priority-fifo.result
make tests/threads/priority-preempt.result
make tests/threads/priority-change.result
```

## Task 3: Priority Donation

### The Problem

Priority inversion occurs when a high-priority thread waits for a lock held by a low-priority thread:

```
Thread H (priority 63) wants Lock L
Thread L (priority 0) holds Lock L
Thread M (priority 31) is ready

→ M runs instead of L
→ H is blocked indefinitely!
```

### The Solution

Donate H's priority to L so L can finish and release the lock:

```
H donates priority 63 to L
L runs (with priority 63)
L releases Lock L
H acquires Lock L and runs
```

### Nested Donation

Handle chains: H → Lock A → M → Lock B → L

```c
struct thread {
  int priority;              /* Base priority */
  int eff_priority;          /* Effective (with donations) */
  struct lock *waiting_lock; /* Lock we're waiting for */
  struct list held_locks;    /* Locks we hold */
};
```

## Task 4: MLFQS Scheduler

### Goal

Implement a multi-level feedback queue scheduler that dynamically adjusts priorities based on CPU usage.

### Formulas

```
priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)

recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice

load_avg = (59/60) * load_avg + (1/60) * ready_threads
```

### Fixed-Point Arithmetic

Use 17.14 fixed-point format (17 integer bits, 14 fractional bits):

```c
#define F (1 << 14)  /* 2^14 */

/* Convert int to fixed-point */
#define INT_TO_FP(n) ((n) * F)

/* Convert fixed-point to int (round toward zero) */
#define FP_TO_INT(x) ((x) / F)

/* Multiply fixed-point numbers */
#define FP_MUL(x, y) (((int64_t)(x)) * (y) / F)

/* Divide fixed-point numbers */
#define FP_DIV(x, y) (((int64_t)(x)) * F / (y))
```

## Testing

### Run All Thread Tests

```bash
cd src/threads
make check
```

### Expected Output

```
pass tests/threads/alarm-single
pass tests/threads/alarm-multiple
pass tests/threads/alarm-simultaneous
pass tests/threads/alarm-priority
pass tests/threads/priority-fifo
pass tests/threads/priority-preempt
...
All 27 tests passed.
```

## Common Issues

### Stack Overflow

If tests crash randomly, you might be overflowing the 4KB thread stack:
- Reduce local variable sizes
- Avoid deep recursion

### Priority Donation Not Working

- Check that you update `eff_priority`, not just `priority`
- Handle nested donation (follow the waiting chain)
- Recalculate priority when releasing locks

### MLFQS Failing

- Verify fixed-point arithmetic
- Update priorities every 4 ticks
- Update `recent_cpu` and `load_avg` every second

## Next Steps

After completing this project:

- [Project 2: User Programs](/docs/projects/userprog/overview) - System calls and processes
- [Priority Donation Deep Dive](/docs/concepts/priority-donation) - Understand the theory
