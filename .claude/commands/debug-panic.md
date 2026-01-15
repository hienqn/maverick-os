---
description: Debug kernel panics with systematic backtrace analysis
---

You are debugging a kernel PANIC in PintOS. Follow this systematic workflow.

## Step 1: Get the panic information

If the user provides panic output, extract:
- The PANIC message (e.g., `assertion 'lock->holder == NULL' failed`)
- The file:line from the panic
- The call stack addresses

If they have a test name, run it:
```bash
maverick-test --test {test-name} --json
```

Look for the `panic` field in the JSON output.

## Step 2: Translate the call stack

```bash
backtrace --json kernel.o {space-separated addresses}
```

This gives you structured output:
```json
{
  "frames": [
    {"address": "0x...", "function": "lock_release", "file": "synch.c", "line": 123},
    {"address": "0x...", "function": "sema_up", "file": "synch.c", "line": 89}
  ]
}
```

## Step 3: Understand common panic patterns

| Panic Message | Likely Cause | Investigation |
|---------------|--------------|---------------|
| `is_thread(t)` | Corrupted thread struct | Stack overflow, bad pointer |
| `lock->holder == NULL` | Releasing unheld lock | Double release, wrong thread |
| `!lock_held_by_current_thread` | Lock not held | Missing acquire |
| `sec_no < d->capacity` | Invalid sector | Freed inode access |
| `intr_get_level() == INTR_OFF` | Interrupts wrong state | Scheduling with interrupts on |
| `elem != NULL` | List corruption | Double insert/remove |

## Step 4: Set breakpoints to investigate

```bash
# Break at the panicking function
maverick-debug --test {test-name} \
  --break {function_from_frame_0} \
  --eval "relevant_struct_fields" \
  --step 3 \
  --max-stops 10
```

## Step 5: Check the code

Read the source at the panic location:
- What assertion failed?
- What are the preconditions for this code?
- What state would violate those preconditions?

## Analysis Output

Provide:
1. **Panic type**: What assertion/condition failed
2. **Call chain**: How execution got there (from backtrace)
3. **Root cause**: What incorrect state caused this
4. **Code fix**: Where and what to change

## Example

```
PANIC at synch.c:123: assertion `lock->holder == thread_current()' failed.
Call stack: 0xc0107abc 0xc0108def

Analysis:
- Frame 0: lock_release() at synch.c:123
- Frame 1: process_exit() at process.c:456

Root cause: Process is trying to release a lock it doesn't hold.
This happens when a child process inherits parent's lock state
but the lock->holder still points to parent.

Fix: In process_exit(), only release locks that this process acquired.
```
