---
description: Debug timeouts and deadlocks in PintOS
---

You are debugging a TIMEOUT or suspected deadlock in PintOS. Follow this systematic workflow.

## Step 1: Confirm the timeout

```bash
maverick-test --test {test-name} --json
```

If verdict is "FAIL" with error "Kernel did not shut down properly", it's likely a deadlock.

## Step 2: Identify lock contention

```bash
maverick-debug --test {test-name} \
  --break lock_acquire \
  --break lock_release \
  --eval "lock" \
  --eval "lock->holder" \
  --eval "thread_current()->name" \
  --max-stops 30
```

Look for patterns:
- Same lock acquired multiple times without release
- Multiple threads trying to acquire different locks in different orders

## Step 3: Check semaphore operations

```bash
maverick-debug --test {test-name} \
  --break sema_down \
  --break sema_up \
  --eval "sema->value" \
  --eval "list_size(&sema->waiters)" \
  --max-stops 30
```

Look for:
- `sema->value` going negative (too many downs)
- Waiters list growing but never shrinking

## Step 4: Use conditional breakpoints

```bash
# Break only when specific thread blocks
maverick-debug --test {test-name} \
  --break-if "sema_down if sema->value == 0" \
  --eval "thread_current()->name" \
  --eval "sema" \
  --max-stops 10
```

## Common Deadlock Patterns

### Pattern 1: Lock Ordering Violation

```
Thread A: acquires lock1, tries to acquire lock2
Thread B: acquires lock2, tries to acquire lock1
```

**Fix:** Always acquire locks in consistent global order.

### Pattern 2: Missing Release

```
Thread A: acquires lock, hits error path, returns without release
Thread B: waits forever for lock
```

**Fix:** Ensure all code paths release acquired locks.

### Pattern 3: Self-Deadlock

```
Thread A: acquires lock, calls function that tries to acquire same lock
```

**Fix:** Use recursive lock or restructure code.

### Pattern 4: Priority Inversion Without Donation

```
High priority waits for lock held by low priority
Medium priority preempts low priority indefinitely
```

**Fix:** Implement priority donation in lock_acquire.

## Advanced: Interrupt-Related Deadlocks

```bash
maverick-debug --test {test-name} \
  --break intr_disable \
  --break intr_enable \
  --eval "intr_get_level()" \
  --max-stops 20
```

Look for:
- Interrupts disabled for too long
- Mismatched disable/enable pairs

## Step 5: Add debug output (if needed)

If the above doesn't reveal the issue, add printf statements:

```c
// In lock_acquire
printf("LOCK_ACQ: %s trying lock %p (holder: %s)\n",
       thread_current()->name, lock,
       lock->holder ? lock->holder->name : "none");
```

Rebuild and run:
```bash
cd src/threads && make
maverick-test --test {test-name}
```

## Analysis Output

Provide:
1. **Deadlock type**: Lock ordering, missing release, self-deadlock, etc.
2. **Threads involved**: Which threads are blocked
3. **Resources**: Which locks/semaphores are contended
4. **Cycle**: The circular wait pattern (if applicable)
5. **Fix**: Specific code changes needed
