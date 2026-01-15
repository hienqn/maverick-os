---
description: Add printf debugging statements to trace execution
---

You are adding printf-style debugging to PintOS. This is often the fastest way to understand what's happening.

## When to Use Printf Debugging

- Understanding execution order
- Tracing variable values over time
- Finding which code path is taken
- Quick iteration without GDB setup

## Step 1: Add debug output

### Basic printf

```c
#include <stdio.h>

printf("DEBUG: reached function_name\n");
printf("DEBUG: value=%d, ptr=%p\n", value, ptr);
```

### Thread-aware output

```c
#include "threads/thread.h"

printf("[%s] lock_acquire: lock=%p holder=%s\n",
       thread_current()->name,
       lock,
       lock->holder ? lock->holder->name : "none");
```

### Hex dump for memory inspection

```c
#include "lib/debug.h"

// Dump 64 bytes starting at buffer
hex_dump(0, buffer, 64, true);
```

## Step 2: Strategic placement

### For scheduling issues

```c
// In thread.c:thread_yield()
printf("YIELD: %s -> ", thread_current()->name);
// after schedule()
printf("%s\n", thread_current()->name);
```

### For synchronization issues

```c
// In synch.c:lock_acquire()
printf("LOCK_TRY: %s wants %p (holder=%s)\n",
       thread_current()->name, lock,
       lock->holder ? lock->holder->name : "none");

// After acquiring
printf("LOCK_GOT: %s acquired %p\n",
       thread_current()->name, lock);
```

### For priority issues

```c
// In thread.c:thread_set_priority()
printf("PRI_SET: %s %d -> %d\n",
       thread_current()->name,
       thread_current()->priority,
       new_priority);
```

### For syscall issues

```c
// In syscall.c:syscall_handler()
printf("SYSCALL: %s called %d\n",
       thread_current()->name, syscall_num);
```

## Step 3: Rebuild and test

```bash
# Rebuild kernel
cd src/threads && make

# Run test and capture output
maverick-test --test {test-name} --json
```

The output will be in the `output` array of the JSON result.

## Step 4: Analyze output

Look for:
- Missing expected prints (code path not taken)
- Unexpected order (race condition)
- Wrong values (logic error)
- Repeated prints (infinite loop)

## Printf Patterns

### Bracketed output for filtering

```c
printf("[LOCK] acquire %p\n", lock);
printf("[SEMA] down %p value=%d\n", sema, sema->value);
```

Then grep the output:
```bash
maverick-test --test {test-name} 2>&1 | grep "\[LOCK\]"
```

### Entry/exit tracing

```c
void complex_function(int arg) {
  printf(">>> complex_function(%d)\n", arg);
  // ... code ...
  printf("<<< complex_function\n");
}
```

### State snapshots

```c
void print_thread_state(struct thread *t) {
  printf("Thread %s: status=%d priority=%d\n",
         t->name, t->status, t->priority);
}
```

## Common Debugging Macros

Add to your code temporarily:

```c
#define DBG(fmt, ...) \
  printf("[%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

// Usage:
DBG("value=%d", x);  // Prints: [function_name:42] value=5
```

## Step 5: Clean up

After debugging, remove or comment out printf statements:

```c
// #define DEBUG_LOCKS
#ifdef DEBUG_LOCKS
  printf("LOCK: ...\n");
#endif
```

Or use the existing DEBUG macro:

```c
#include "lib/debug.h"
// Only prints when DEBUG is defined
ASSERT(condition);  // Panics if false
```

## Limitations

- Printf in interrupt handlers can cause issues
- Too much output can hide the problem
- Output ordering may not reflect execution order (buffering)
- Cannot inspect post-mortem (use GDB for crashes)

## When to Switch to GDB

Use `maverick-debug` instead when:
- System crashes before printf executes
- Need to inspect memory/registers
- Need to set conditional breakpoints
- Printf output is too voluminous
