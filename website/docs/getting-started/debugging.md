---
sidebar_position: 3
---

# Debugging

PintOS supports debugging with GDB. This guide covers the essentials.

## Starting a Debug Session

### Terminal 1: Start PintOS with GDB Support

```bash
cd src/threads/build
pintos --qemu --gdb -- run alarm-multiple
```

The `--gdb` flag tells QEMU to wait for a GDB connection.

### Terminal 2: Connect GDB

```bash
cd src/threads/build
pintos-gdb kernel.o
```

At the GDB prompt, connect to QEMU:

```gdb
(gdb) target remote localhost:1234
(gdb) continue
```

## Essential GDB Commands

### Breakpoints

```gdb
# Break at function
(gdb) break thread_create

# Break at file:line
(gdb) break thread.c:150

# Break with condition
(gdb) break thread_create if priority > 31

# List breakpoints
(gdb) info breakpoints

# Delete breakpoint
(gdb) delete 1
```

### Execution Control

```gdb
# Continue execution
(gdb) continue
(gdb) c

# Step one line (into functions)
(gdb) step
(gdb) s

# Step over (don't enter functions)
(gdb) next
(gdb) n

# Step out of current function
(gdb) finish

# Run until specific line
(gdb) until 200
```

### Inspecting State

```gdb
# Print variable
(gdb) print thread_current()->priority
(gdb) p *lock

# Print with format
(gdb) print/x 0xc0000000      # hex
(gdb) print/t flags           # binary

# View struct members
(gdb) ptype struct thread

# Backtrace
(gdb) backtrace
(gdb) bt

# Switch stack frames
(gdb) frame 2
(gdb) up
(gdb) down
```

### Memory Inspection

```gdb
# Examine memory (x/FMT ADDRESS)
(gdb) x/10x $esp          # 10 hex words at stack pointer
(gdb) x/20i $eip          # 20 instructions at instruction pointer
(gdb) x/s 0x08048000      # string at address

# View registers
(gdb) info registers
(gdb) print $eax
```

## PintOS-Specific Debugging

### View Thread List

```gdb
(gdb) dumplist &all_list thread allelem
```

### View Current Thread

```gdb
(gdb) print *thread_current()
```

### View Lock Holders

```gdb
(gdb) print *lock->holder
```

### View Ready Queue

```gdb
(gdb) dumplist &ready_list thread elem
```

## Debugging User Programs

When debugging user programs, you need to load symbols:

```gdb
# Load user program symbols
(gdb) loadusersyms tests/userprog/exec-once

# Set breakpoint in user code
(gdb) break _start
```

## Common Debugging Scenarios

### Page Fault Analysis

When you hit a page fault:

```gdb
(gdb) print fault_addr          # Faulting address
(gdb) print not_present         # Page not present?
(gdb) print write               # Write access?
(gdb) print user                # User mode access?
(gdb) bt                        # See how we got here
```

### Triple Fault (Immediate Reboot)

Usually caused by:
1. Stack overflow (thread stack overwritten)
2. Invalid page table
3. Corrupted GDT/IDT

Debug by:
```gdb
(gdb) break intr_handler
(gdb) continue
# When it breaks, check:
(gdb) print $esp                # Stack in valid range?
(gdb) x/10x $esp                # Stack contents
```

### Kernel Panic

```gdb
(gdb) break debug_panic
(gdb) continue
# When panic hits:
(gdb) bt                        # Full backtrace
```

## Using printf Debugging

Sometimes printf is simpler:

```c
#include <stdio.h>

void my_function(struct thread *t) {
  printf("Thread %s, priority %d\n", t->name, t->priority);

  // Hex dump
  hex_dump(0, t, sizeof *t, true);
}
```

:::tip
For timing-sensitive code, use `ASSERT()` instead of printf to avoid affecting behavior.
:::

## Debug Macros

PintOS includes helpful macros in `debug.h`:

```c
// Assertion (removed in release builds)
ASSERT(condition);

// Always-active assertion
PANIC("Something went wrong: %d", error_code);

// Print if DEBUG defined
DEBUG_PRINT("value = %d\n", value);
```

## Next Steps

- [Contributing](/docs/getting-started/contributing) - Code style and PR process
- [Context Switching Deep Dive](/docs/deep-dives/context-switch-assembly) - Understand the assembly
