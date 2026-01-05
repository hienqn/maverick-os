---
sidebar_position: 6
---

import AnimatedFlow from '@site/src/components/AnimatedFlow';

# Priority Donation

Priority donation (also called priority inheritance) solves the **priority inversion** problem in systems with priority scheduling and locks.

## The Priority Inversion Problem

### Scenario

Three threads with different priorities:
- **H** (High priority = 60)
- **M** (Medium priority = 40)
- **L** (Low priority = 20)

### What Goes Wrong

```
Time →
─────────────────────────────────────────────────────

L acquires Lock A
        │
        ▼
L is running (holding Lock A)
        │
        ├── H wakes up, wants Lock A
        │         │
        │         ▼
        │   H blocks (waiting for L to release Lock A)
        │
        ├── M wakes up (no lock needed)
        │         │
        │         ▼
        │   M runs! (higher priority than L)
        │         │
        │         ▼
        │   M keeps running...
        │         │
        │         ▼
        │   H is still blocked!!
```

**Result**: High-priority H is blocked by medium-priority M, even though M has nothing to do with Lock A. This is **unbounded priority inversion**.

### Real-World Impact

This exact bug caused the [Mars Pathfinder reset problem](https://www.cs.unc.edu/~anderson/teach/comp790/papers/mars_pathfinder_long_version.html) in 1997. The spacecraft's high-priority bus management task was blocked by a low-priority weather gathering task, causing watchdog timer resets.

## The Solution: Priority Donation

When a high-priority thread H tries to acquire a lock held by low-priority thread L, H **donates** its priority to L temporarily:

<AnimatedFlow
  title="Priority Donation in Action"
  states={[
    { id: 'l_holds', label: 'L holds lock (pri=20)', description: 'L acquired the lock' },
    { id: 'h_wants', label: 'H wants lock (pri=60)', description: 'H tries to acquire' },
    { id: 'donate', label: 'H donates to L', description: 'L now has effective pri=60' },
    { id: 'l_runs', label: 'L runs (eff_pri=60)', description: 'L can preempt M' },
    { id: 'l_releases', label: 'L releases lock', description: 'L returns to pri=20' },
    { id: 'h_acquires', label: 'H acquires lock', description: 'H runs with pri=60' },
  ]}
  transitions={[
    { from: 'l_holds', to: 'h_wants', label: 'H wakes' },
    { from: 'h_wants', to: 'donate', label: 'blocked' },
    { from: 'donate', to: 'l_runs', label: 'priority boost' },
    { from: 'l_runs', to: 'l_releases', label: 'lock_release' },
    { from: 'l_releases', to: 'h_acquires', label: 'donation ends' },
  ]}
/>

Now L runs with priority 60 (same as H), can preempt M, and releases the lock quickly. H can then proceed.

## Implementation

### Data Structures

```c
struct thread {
  int priority;              /* Base priority (0-63) */
  int eff_priority;          /* Effective priority (with donations) */
  struct lock *waiting_lock; /* Lock this thread is waiting for */
  struct list held_locks;    /* Locks this thread currently holds */
  /* ... */
};

struct lock {
  struct thread *holder;     /* Thread holding the lock */
  struct list waiters;       /* Threads waiting for this lock */
  struct list_elem elem;     /* For holder's held_locks list */
  /* ... */
};
```

### Computing Effective Priority

A thread's effective priority is the max of:
1. Its base priority
2. All donated priorities from threads waiting on locks it holds

```c
int thread_get_effective_priority(struct thread *t) {
  int max_priority = t->priority;  /* Start with base */

  /* Check all held locks */
  for (each lock in t->held_locks) {
    /* Check all waiters on this lock */
    for (each waiter in lock->waiters) {
      int waiter_pri = thread_get_effective_priority(waiter);
      if (waiter_pri > max_priority)
        max_priority = waiter_pri;
    }
  }

  return max_priority;
}
```

### Lock Acquire with Donation

```c
void lock_acquire(struct lock *lock) {
  struct thread *cur = thread_current();

  if (lock->holder != NULL) {
    /* Record what we're waiting for */
    cur->waiting_lock = lock;

    /* Donate our priority to the holder */
    donate_priority(lock->holder, cur->eff_priority);

    /* Block until lock is available */
    sema_down(&lock->semaphore);
  }

  /* Now we have the lock */
  lock->holder = cur;
  cur->waiting_lock = NULL;
  list_push_back(&cur->held_locks, &lock->elem);
}
```

### Lock Release with Priority Restoration

```c
void lock_release(struct lock *lock) {
  struct thread *cur = thread_current();

  /* Remove from held locks */
  list_remove(&lock->elem);
  lock->holder = NULL;

  /* Release the semaphore (wakes highest-priority waiter) */
  sema_up(&lock->semaphore);

  /* Recalculate our effective priority */
  cur->eff_priority = thread_get_effective_priority(cur);

  /* Yield if we're no longer highest priority */
  if (cur->eff_priority < max_ready_priority())
    thread_yield();
}
```

## Nested Donation

Donation must propagate through chains of locks:

```
Thread H (pri=60) waits on Lock A
          ↓ donates to
Thread M (pri=40) holds Lock A, waits on Lock B
          ↓ donates to
Thread L (pri=20) holds Lock B
```

L needs to run with priority 60 so M can run, so H can run.

### Implementation

```c
void donate_priority(struct thread *t, int priority) {
  while (t != NULL) {
    if (priority <= t->eff_priority)
      break;  /* No need to donate further */

    t->eff_priority = priority;

    /* Propagate through waiting chain */
    if (t->waiting_lock != NULL)
      t = t->waiting_lock->holder;
    else
      break;
  }
}
```

## Multiple Donations

A thread can receive donations from multiple sources:

```
     ┌─── Thread A (pri=50) waiting on Lock X
     │
Thread L (pri=20) ──┼─── Thread B (pri=60) waiting on Lock X
holds Lock X        │
                    └─── Thread C (pri=55) waiting on Lock Y
                         (L also holds Lock Y)
```

L's effective priority = max(20, 50, 60, 55) = **60**

When L releases Lock X:
- L's effective priority = max(20, 55) = **55**
- (Still getting donation from C via Lock Y)

When L releases Lock Y:
- L's effective priority = **20** (back to base)

## Synchronization Primitives

### Priority-Aware Semaphores

When `sema_up()` wakes a waiter, pick the highest priority one:

```c
void sema_up(struct semaphore *sema) {
  if (!list_empty(&sema->waiters)) {
    /* Wake highest priority waiter */
    struct list_elem *max = list_max(&sema->waiters,
                                     thread_cmp_priority, NULL);
    list_remove(max);
    thread_unblock(list_entry(max, struct thread, elem));
  }
  sema->value++;
}
```

### Priority-Aware Condition Variables

For condition variables, wake the highest-priority waiter in `cond_signal()`:

```c
void cond_signal(struct condition *cond, struct lock *lock) {
  if (!list_empty(&cond->waiters)) {
    /* Find semaphore with highest-priority waiter */
    struct list_elem *max = list_max(&cond->waiters,
                                     sema_cmp_priority, NULL);
    list_remove(max);
    struct semaphore *sema = list_entry(max, struct semaphore, elem);
    sema_up(sema);
  }
}
```

## Common Pitfalls

### 1. Forgetting to Update Ready Queue

After changing a thread's effective priority, it may need to be moved in the ready queue:

```c
void thread_set_priority(int new_priority) {
  struct thread *cur = thread_current();
  cur->priority = new_priority;
  cur->eff_priority = thread_get_effective_priority(cur);

  /* May need to yield if we're no longer highest */
  if (cur->eff_priority < max_ready_priority())
    thread_yield();
}
```

### 2. Donating to Base Instead of Effective

Always donate to `eff_priority`, not `priority`:

```c
/* WRONG */
t->priority = max(t->priority, donated_pri);

/* CORRECT */
t->eff_priority = max(t->eff_priority, donated_pri);
```

### 3. Not Handling Nested Donation

Donation must follow the `waiting_lock` chain:

```c
/* WRONG: Only donates to immediate holder */
lock->holder->eff_priority = max(...);

/* CORRECT: Follow the chain */
struct thread *t = lock->holder;
while (t != NULL && t->eff_priority < donated_pri) {
  t->eff_priority = donated_pri;
  if (t->waiting_lock)
    t = t->waiting_lock->holder;
  else
    break;
}
```

## Summary

| Concept | Description |
|---------|-------------|
| **Priority Inversion** | High-pri blocked by medium-pri due to lock |
| **Priority Donation** | Temporarily boost lock holder's priority |
| **Effective Priority** | max(base, all donations) |
| **Nested Donation** | Donation propagates through lock chains |
| **Multiple Donations** | Thread can receive from multiple waiters |

## Related Topics

- [Project 1: Threads](/docs/projects/threads/overview) - Implement priority donation
- [Scheduling](/docs/concepts/scheduling) - Priority scheduling basics
