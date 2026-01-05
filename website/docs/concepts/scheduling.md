---
sidebar_position: 5
---

import AnimatedFlow from '@site/src/components/AnimatedFlow';
import Quiz from '@site/src/components/Quiz';

# CPU Scheduling

CPU scheduling determines which thread runs next when multiple threads are ready. The scheduler balances fairness, responsiveness, and throughput.

## Scheduling Goals

| Goal | Description |
|------|-------------|
| **Fairness** | All threads get reasonable CPU time |
| **Responsiveness** | Interactive tasks respond quickly |
| **Throughput** | Maximize work completed per unit time |
| **Efficiency** | Minimize scheduling overhead |

## Priority Scheduling

### Basic Concept

Threads have priorities from 0 (lowest) to 63 (highest). The scheduler always runs the highest-priority ready thread.

```c
/* Priority constants */
#define PRI_MIN 0      /* Lowest priority */
#define PRI_DEFAULT 31 /* Default priority */
#define PRI_MAX 63     /* Highest priority */
```

### Ready Queue Organization

```
Priority 63: [Thread A] → [Thread B]
Priority 62: [Thread C]
Priority 61: (empty)
...
Priority 31: [Thread D] → [Thread E] → [Thread F]
...
Priority 0:  [Thread G]
```

The scheduler picks from the highest non-empty priority level, using FIFO within each level.

### Preemption

When a higher-priority thread becomes ready, it preempts the current thread:

<AnimatedFlow
  title="Priority Preemption"
  states={[
    { id: 'low_running', label: 'Low Running', description: 'Low-priority thread executing' },
    { id: 'high_wakes', label: 'High Wakes', description: 'High-priority thread unblocked' },
    { id: 'preempt', label: 'Preempt', description: 'Low yields to high' },
    { id: 'high_running', label: 'High Running', description: 'High-priority thread executing' },
  ]}
  transitions={[
    { from: 'low_running', to: 'high_wakes', label: 'unblock' },
    { from: 'high_wakes', to: 'preempt', label: 'check priority' },
    { from: 'preempt', to: 'high_running', label: 'context switch' },
  ]}
/>

### Implementation

```c
/* In thread_unblock() */
void thread_unblock(struct thread *t) {
  t->status = THREAD_READY;
  list_push_back(&ready_list, &t->elem);

  /* Preempt if unblocked thread has higher priority */
  if (t->priority > thread_current()->priority)
    intr_yield_on_return();  /* Yield after interrupt returns */
}

/* In schedule() */
struct thread *next_thread_to_run(void) {
  if (list_empty(&ready_list))
    return idle_thread;

  /* Find highest priority thread */
  struct list_elem *max = list_max(&ready_list, thread_cmp_priority, NULL);
  list_remove(max);
  return list_entry(max, struct thread, elem);
}
```

## Multi-Level Feedback Queue (MLFQS)

MLFQS automatically adjusts priorities based on CPU usage. CPU-bound threads gradually lower in priority, while I/O-bound threads stay responsive.

### Key Metrics

| Metric | Formula | Update Frequency |
|--------|---------|------------------|
| `nice` | Set by thread | Manual |
| `recent_cpu` | `(2*load_avg)/(2*load_avg+1) * recent_cpu + nice` | Every second |
| `load_avg` | `(59/60)*load_avg + (1/60)*ready_threads` | Every second |
| `priority` | `PRI_MAX - (recent_cpu/4) - (nice*2)` | Every 4 ticks |

### Nice Values

Threads can hint at their priority with `nice`:
- **nice = -20**: Highest priority (greedy)
- **nice = 0**: Default
- **nice = +20**: Lowest priority (generous)

### Fixed-Point Arithmetic

Since the kernel lacks floating-point support, we use 17.14 fixed-point:

```c
/* 17.14 fixed-point format:
   17 bits integer part, 14 bits fractional part
   Value = raw_value / 2^14 */

#define F (1 << 14)  /* 2^14 = 16384 */

/* Convert integer to fixed-point */
#define INT_TO_FP(n) ((n) * F)

/* Convert fixed-point to integer (truncate) */
#define FP_TO_INT(x) ((x) / F)

/* Convert fixed-point to integer (round) */
#define FP_TO_INT_ROUND(x) ((x) >= 0 ? ((x) + F/2) / F : ((x) - F/2) / F)

/* Multiply two fixed-point values */
#define FP_MUL(x, y) (((int64_t)(x)) * (y) / F)

/* Divide two fixed-point values */
#define FP_DIV(x, y) (((int64_t)(x)) * F / (y))
```

### Priority Calculation Example

```c
void mlfqs_update_priority(struct thread *t) {
  /* priority = PRI_MAX - (recent_cpu / 4) - (nice * 2) */
  int priority = PRI_MAX;
  priority -= FP_TO_INT(t->recent_cpu / 4);
  priority -= t->nice * 2;

  /* Clamp to valid range */
  if (priority < PRI_MIN) priority = PRI_MIN;
  if (priority > PRI_MAX) priority = PRI_MAX;

  t->priority = priority;
}
```

### Update Schedule

```c
void timer_interrupt(struct intr_frame *f) {
  ticks++;

  /* Every tick: increment recent_cpu for running thread */
  if (thread_current() != idle_thread)
    thread_current()->recent_cpu += INT_TO_FP(1);

  /* Every 4 ticks: recalculate all priorities */
  if (ticks % 4 == 0)
    mlfqs_update_all_priorities();

  /* Every second (100 ticks): update load_avg and recent_cpu */
  if (ticks % TIMER_FREQ == 0) {
    mlfqs_update_load_avg();
    mlfqs_update_all_recent_cpu();
  }
}
```

## Scheduler Comparison

| Feature | Priority | MLFQS |
|---------|----------|-------|
| **Priority source** | Manual (or donation) | Automatic from CPU usage |
| **Responsiveness** | High-priority threads dominate | I/O-bound stay responsive |
| **Starvation** | Possible for low-priority | Prevented by aging |
| **Complexity** | Simple | More complex |
| **Fairness** | Depends on priority assignment | Generally fair |

## Context Switching

When the scheduler picks a new thread, it performs a context switch:

```c
void schedule(void) {
  struct thread *cur = running_thread();
  struct thread *next = next_thread_to_run();
  struct thread *prev = NULL;

  if (cur != next)
    prev = switch_threads(cur, next);

  thread_schedule_tail(prev);
}
```

The actual register save/restore happens in `switch.S`:

```asm
switch_threads:
  # Save old thread's registers
  pushl %ebx
  pushl %ebp
  pushl %esi
  pushl %edi
  movl %esp, (%eax)    # Save old ESP

  # Switch stacks
  movl (%edx), %esp    # Load new ESP

  # Restore new thread's registers
  popl %edi
  popl %esi
  popl %ebp
  popl %ebx
  ret
```

<Quiz
  title="Scheduling Quiz"
  questions={[
    {
      question: "What happens when a priority 60 thread unblocks while a priority 40 thread is running?",
      options: [
        "Priority 40 continues running",
        "Priority 60 preempts immediately",
        "They share the CPU equally",
        "Depends on nice values"
      ],
      correctIndex: 1,
      explanation: "In priority scheduling, a higher-priority thread always preempts a lower-priority one when it becomes ready."
    },
    {
      question: "In MLFQS, what happens to a CPU-bound thread's priority over time?",
      options: [
        "Increases (becomes higher priority)",
        "Stays the same",
        "Decreases (becomes lower priority)",
        "Fluctuates randomly"
      ],
      correctIndex: 2,
      explanation: "CPU-bound threads accumulate recent_cpu, which lowers their calculated priority. This allows I/O-bound threads to remain responsive."
    },
    {
      question: "Why does MLFQS use fixed-point arithmetic?",
      options: [
        "It's faster than integer math",
        "The kernel doesn't have floating-point support",
        "Fixed-point is more accurate",
        "It uses less memory"
      ],
      correctIndex: 1,
      explanation: "The PintOS kernel doesn't support floating-point operations. Fixed-point arithmetic (17.14 format) allows fractional calculations using only integer operations."
    },
  ]}
/>

## Summary

- **Priority scheduling** runs highest-priority thread, supports preemption
- **MLFQS** automatically adjusts priorities based on CPU usage
- **Fixed-point arithmetic** enables fractional calculations without FPU
- **Context switches** save/restore thread state when switching

## Related Topics

- [Project 1: Threads](/docs/projects/threads/overview) - Implement scheduling
- [Priority Donation](/docs/concepts/priority-donation) - Handle priority inversion
- [Context Switching](/docs/concepts/context-switching) - Low-level switch mechanism
