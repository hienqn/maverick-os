# CLAUDE.md

This file provides comprehensive guidance for AI assistants working with the PintOS codebase.

## Project Overview

PintOS is an educational operating system developed at Stanford University and used in UC Berkeley's CS162 course. This repository contains a complete implementation of all four major projects: Threads, User Programs, Virtual Memory, and File System. It supports two architectures: x86 (i386) and RISC-V (riscv64, experimental).

## Directory Structure

```
src/
├── threads/      # Core threading, scheduling, synchronization
├── userprog/     # User programs, syscalls, process management
├── vm/           # Virtual memory, paging, swap, mmap
├── filesys/      # File system, buffer cache, directories, WAL
├── devices/      # Hardware drivers (timer, disk, keyboard, etc.)
├── arch/         # Architecture-specific code
│   ├── i386/     # x86 32-bit (start.S, intr.c, pagedir.c, loader.S)
│   ├── riscv64/  # RISC-V 64-bit (start.S, mmu.c, trap.S, virtio)
│   └── common/   # Shared abstractions (arch.h, cpu.h, intr.h)
├── lib/          # C library
│   ├── kernel/   # Kernel-only: bitmap, hash, list, debug
│   └── user/     # User-space C library
├── tests/        # Test suites for each component
├── examples/     # Sample user programs
└── utils/        # Build and test utilities (Bun/TypeScript)
```

Build artifacts are placed in architecture-specific directories:
```
<component>/build/
├── i386/         # x86 build artifacts (kernel.bin, loader.bin, etc.)
└── riscv64/      # RISC-V build artifacts (kernel.bin, etc.)
```

## Build Commands

```bash
# Build a specific component (from src/ directory)
cd threads && make      # Kernel with threading only
cd userprog && make     # Add user program support
cd vm && make           # Add virtual memory
cd filesys && make      # Add file system

# Build for a specific architecture (default: i386)
make                    # Build for i386
make ARCH=riscv64       # Build for RISC-V

# Clean build
make clean              # Clean current architecture (default: i386)
make clean ARCH=riscv64 # Clean RISC-V build
make clean-all          # Clean all architectures

# Format code
make format
```

## Testing

### Running Tests

```bash
# Run all tests for a component
cd src/userprog && make check
cd src/vm && make check
cd src/filesys && make check

# Run tests for a specific architecture
make check                    # Test i386 (default)
make check ARCH=riscv64       # Test RISC-V

# Run tests in parallel (much faster)
make check -j$(nproc)    # Use all CPU cores
make check -j20          # Or specify core count

# Run single test (from build/i386 or build/riscv64 directory)
pintos --qemu -- run alarm-multiple

# View test output
cat tests/threads/alarm-multiple.output
```

### Test File Structure

Each test consists of complementary files:
- **`.c`**: Test source code (user program or kernel code)
- **`.ck`**: Perl checker script for output verification
- **`.test.json`**: Native JSON format (faster than `.ck`)

Test utilities (`tests/lib.h`):
- `msg()` - Print test output
- `fail()` - Fail test with error message
- `CHECK(condition, "message")` - Assert macro

### Debugging Test Failures

```bash
# Check test output vs expected
cat tests/userprog/args-single.output   # Actual output
cat tests/userprog/args-single.result   # Pass/fail result

# Run with timeout and verbose output
pintos --qemu -T 60 -- run args-single

# Run with specific scheduler
pintos --qemu -- -sched=prio run priority-donate-one
```

## Code Style

- **Indentation**: 2 spaces (no tabs)
- **Line length**: 100 characters max
- **Naming**: snake_case for functions and variables, UPPER_SNAKE_CASE for macros
- **Format tool**: clang-format (run `make format`)

## Key Source Files

### Threading (`src/threads/`)
- `thread.h/c` - Thread struct, lifecycle, scheduling
- `synch.h/c` - Locks, semaphores, condition variables, rw_locks
- `interrupt.h/c` - Interrupt handling
- `palloc.h/c` - Page allocator
- `malloc.h/c` - Subpage allocator
- `fixed-point.h` - 17.14 fixed-point arithmetic for MLFQS

### User Programs (`src/userprog/`)
- `process.h/c` - ELF loading, process management, fork/exec
- `syscall.h/c` - System call handlers
- `exception.h/c` - Page fault handling
- `pagedir.h/c` - Page directory manipulation

### Virtual Memory (`src/vm/`)
- `page.h/c` - Supplemental page table (SPT)
- `frame.h/c` - Physical frame management, eviction
- `swap.h/c` - Swap partition management
- `mmap.h/c` - Memory-mapped files

### File System (`src/filesys/`)
- `inode.h/c` - Indexed file blocks (direct + indirect)
- `directory.h/c` - Directory operations
- `cache.h/c` - 64-block buffer cache with LRU
- `wal.h/c` - Write-ahead logging

## Architecture Support

### x86 (i386) - Primary

**Memory Layout:**
- `PHYS_BASE` = 0xC0000000 (3GB boundary)
- User space: 0x00000000 - 0xBFFFFFFF
- Kernel space: 0xC0000000 - 0xFFFFFFFF
- User code starts at 0x08048000

**Key Files:**
- `arch/i386/start.S` - Bootstrap code
- `arch/i386/intr.c` - Interrupt handling
- `arch/i386/pagedir.c` - Page directory manipulation
- `arch/i386/loader.S` - Bootloader

**System Calls:** Via `INT 0x30` interrupt, arguments on user stack.

### RISC-V (riscv64) - Experimental

**Memory Layout:**
- Sv39 MMU (39-bit virtual address space)
- `PHYS_BASE` = 0xFFFFFFFF80000000
- User space: Lower 256GB
- Kernel space: Upper half

**Key Files:**
- `arch/riscv64/start.S` - Entry from OpenSBI
- `arch/riscv64/mmu.c` - Page table management
- `arch/riscv64/trap.S` - Trap entry/exit
- `arch/riscv64/virtio*.c` - VirtIO device drivers

**System Calls:** Via `ECALL` instruction, arguments in a0-a7 registers.

### Architecture-Neutral Patterns

Use these macros for portable code:
```c
#ifdef ARCH_RISCV64
  // RISC-V specific code
#else
  // x86 specific code (default)
#endif

// Use arch-neutral functions
is_user_vaddr(addr)   // Check if address is in user space
is_kernel_vaddr(addr) // Check if address is in kernel space
ptov(paddr)           // Physical to virtual address
vtop(vaddr)           // Virtual to physical address
```

## Core Code Patterns

### Embedded Container Pattern

PintOS uses embedded elements instead of pointers to avoid dynamic allocation overhead. This pattern appears throughout `list.h` and `hash.h`:

```c
// Define structure with embedded element
struct thread {
  struct list_elem elem;      // For ready queue
  struct list_elem allelem;   // For all_list
  // ... other fields
};

// Convert from element back to containing structure
struct list_elem *e;
for (e = list_begin(&ready_list); e != list_end(&ready_list); e = list_next(e)) {
  struct thread *t = list_entry(e, struct thread, elem);
  // Process t...
}
```

For hash tables (used in SPT):
```c
struct spt_entry {
  void *upage;
  struct hash_elem hash_elem;
  // ... other fields
};

// Lookup and convert
struct hash_elem *h = hash_find(&spt->pages, &lookup.hash_elem);
struct spt_entry *entry = hash_entry(h, struct spt_entry, hash_elem);
```

### Synchronization Primitives

**Hierarchy (bottom to top):**
```
Semaphore (foundation) → Lock (mutex) → Condition Variable → RW Lock
```

**Disabling Interrupts:**
```c
enum intr_level old_level = intr_disable();
// Critical section - atomic on uniprocessor
intr_set_level(old_level);  // Restore previous state
```

**Lock Usage:**
```c
lock_acquire(&some_lock);
// Protected section
lock_release(&some_lock);
```

**Condition Variables (Mesa semantics):**
```c
lock_acquire(&lock);
while (!condition)              // WHILE, not IF (spurious wakeup)
  cond_wait(&cond, &lock);      // Releases lock, blocks, reacquires
// Condition is now true
lock_release(&lock);
```

**Readers-Writers Lock (writer-preferring):**
```c
rw_lock_acquire_read(&cache_lock);   // Multiple readers allowed
read_from_cache();
rw_lock_release_read(&cache_lock);

rw_lock_acquire_write(&cache_lock);  // Exclusive access
update_cache();
rw_lock_release_write(&cache_lock);
```

### Memory Management

**Page Allocation:**
```c
enum palloc_flags {
  PAL_ASSERT = 001,  // Panic on failure
  PAL_ZERO = 002,    // Zero page contents
  PAL_USER = 004     // User page (not kernel)
};

void *page = palloc_get_page(PAL_USER | PAL_ZERO);
// ... use page ...
palloc_free_page(page);
```

**SPT Page States:**
- `PAGE_ZERO` - Not yet allocated, will be zero-filled on access
- `PAGE_FILE` - Backed by file (executable or mmap)
- `PAGE_SWAP` - Swapped out to disk
- `PAGE_FRAME` - Currently in physical memory

### Hash Table Iteration

```c
struct hash_iterator i;
hash_first(&i, &table);
while (hash_next(&i)) {
  struct entry *e = hash_entry(hash_cur(&i), struct entry, hash_elem);
  // Process entry
}
```

### User Pointer Validation

User pointers are NOT manually validated before dereferencing. Instead, page faults catch invalid accesses:

```c
// In page fault handler (exception.c)
if (!user && is_user_vaddr(fault_addr)) {
  // Kernel tried to access invalid user address
  thread_current()->pcb->my_status->exit_code = -1;
  process_exit();
}
```

## Debugging Guide

### Debug Macros

Located in `lib/debug.h`:

```c
// Halt kernel with message (always enabled)
PANIC("message: %d", value);
// Output: "PANIC in function_name (file.c:123): message: 42"

// Assert condition (disabled with NDEBUG)
ASSERT(ptr != NULL);
// Panics with: "assertion `ptr != NULL' failed."

// Unreachable code check
NOT_REACHED();
// Panics with: "executed an unreachable statement"
```

### Kernel Panics

When a panic occurs, you'll see:
```
PANIC in thread_schedule (thread.c:456): assertion `is_thread(t)' failed.
Call stack: 0x8004a1 0x8003b2 0x8002c3
```

Use the backtrace utility:
```bash
cd build/i386
../../utils/bun/bin/backtrace kernel.o 0x8004a1 0x8003b2 0x8002c3
```

### Page Fault Debugging

Page faults are handled in `userprog/exception.c`:
- **User fault + valid SPT entry**: Load page via `spt_load_page()`
- **User fault + invalid address**: Terminate process with exit code -1
- **Kernel fault on user address**: Bug in kernel code

Common causes:
- Invalid user pointer passed to syscall
- Stack overflow (access below stack pointer)
- Use-after-free

### GDB Usage

```bash
# Start pintos with GDB support
pintos --qemu --gdb -- run test-name

# In another terminal
pintos-gdb kernel.o
(gdb) target remote localhost:1234
(gdb) break thread_create
(gdb) continue
```

### Agent-Friendly Debugging with debug-pintos

The `debug-pintos` tool provides machine-readable debugging output for AI agents. It runs GDB in batch mode and returns structured JSON with registers, backtraces, memory dumps, and custom command output.

**Location:** `src/utils/bun/bin/debug-pintos`

**Basic Usage:**
```bash
# Debug a test with a breakpoint
debug-pintos --test alarm-single --break thread_create

# Multiple breakpoints with custom commands
debug-pintos --test priority-donate-one \
  --break lock_acquire \
  --break thread_set_priority \
  --commands "bt,print lock->holder->priority" \
  --max-stops 5

# With memory dumps
debug-pintos --test alarm-single \
  --break thread_create \
  --memory '$esp:16' \
  --commands "print name,print function"

# Conditional breakpoint
debug-pintos --test priority-donate-one \
  --break-if "lock_acquire if lock->holder != 0"

# For RISC-V (when toolchain is available)
debug-pintos --test alarm-single --arch riscv64 --break thread_create
```

**CLI Options:**
| Option | Description |
|--------|-------------|
| `--test NAME` | Test to debug (e.g., "alarm-single") |
| `--break LOC` | Set breakpoint (function, file:line, or *address) |
| `--break-if "LOC if COND"` | Conditional breakpoint |
| `--watch EXPR` | Write watchpoint |
| `--rwatch EXPR` | Read watchpoint |
| `--commands CMDS` | Comma-separated GDB commands to run at each stop |
| `--memory SPEC` | Memory dump spec (e.g., "$esp:16" or "0xc0000000:32") |
| `--max-stops N` | Max breakpoint hits before returning (default: 10) |
| `--timeout SECS` | Execution timeout (default: 60) |
| `--arch ARCH` | Architecture: i386 (default) or riscv64 |
| `--output FILE` | Write JSON to file instead of stdout |

**Output JSON Structure:**
```json
{
  "status": "completed|timeout|panic|error",
  "test": "alarm-single",
  "arch": "i386",
  "stops": [
    {
      "stopNumber": 1,
      "reason": "breakpoint-hit",
      "location": {
        "function": "thread_create",
        "file": "../../../threads/thread.c",
        "line": 387,
        "address": "0xc00214dd"
      },
      "registers": { "eax": "0x...", "esp": "0x...", "eip": "0x...", ... },
      "backtrace": [
        { "frame": 0, "function": "thread_create", "file": "thread.c", "line": 387 },
        { "frame": 1, "function": "main", "file": "init.c", "line": 179 }
      ],
      "memoryDumps": { "$esp:8": ["0xc0026b9b", "0xc00543a0", ...] },
      "commandOutputs": { "print name": "$1 = 0xc00543a0 \"kbd-worker\"" }
    }
  ],
  "breakpointsSet": [...],
  "serialOutput": "Pintos booting...",
  "errors": []
}
```

**When to Use debug-pintos:**
- Investigating test failures that need stepping through code
- Understanding execution flow at specific functions
- Examining register/memory state at breakpoints
- Debugging kernel panics (set breakpoint at panic location)

**Example: Debugging a Priority Donation Bug:**
```bash
debug-pintos --test priority-donate-one \
  --break lock_acquire \
  --break lock_release \
  --commands "print lock->holder->priority,print thread_current()->priority" \
  --max-stops 10
```

This captures priority values at each lock operation to trace donation behavior.

### Printf Debugging

```c
// In kernel code
printf("DEBUG: value=%d, ptr=%p\n", value, ptr);

// Use hex_dump for memory inspection
hex_dump(0, buffer, 64, true);
```

## Common Pitfalls

### Thread Stack Overflow

Each thread has a 4KB page with struct thread at the bottom and stack growing down:
```
┌─────────────────────┐ 4KB
│   Kernel Stack ↓    │
│                     │
│  (< 4KB available!) │
├─────────────────────┤
│   magic (canary)    │
│   struct thread     │
└─────────────────────┘ 0
```

**Avoid:** Large local arrays or deep recursion in kernel code.

### Lock Misuse

```c
// WRONG: Holding lock across blocking operation
lock_acquire(&lock);
sema_down(&sema);  // May block!
lock_release(&lock);

// CORRECT: Release lock before blocking
lock_acquire(&lock);
// ... critical section ...
lock_release(&lock);
sema_down(&sema);  // Safe to block
```

### Interrupt Level Restoration

```c
// WRONG: Always enables interrupts
intr_disable();
// critical section
intr_enable();  // What if interrupts were already disabled?

// CORRECT: Restore previous state
enum intr_level old_level = intr_disable();
// critical section
intr_set_level(old_level);  // Restore to previous state
```

### Priority Donation

When high-priority thread H waits for lock held by low-priority L:
- L inherits H's priority temporarily
- L runs at elevated priority until it releases the lock
- Without donation: priority inversion (H starves)

The `lock->max_donation` and `thread->held_locks` fields support this.

### User Pointer Bugs

```c
// WRONG: Deference user pointer directly
int value = *user_ptr;  // May page fault!

// CORRECT: Let page fault handler validate
// (PintOS approach - simpler than manual validation)
// If fault occurs in kernel mode on user address,
// exception handler terminates the process.
```

### Fixed-Point Arithmetic

The kernel cannot use floating-point (FPU not initialized). MLFQS uses 17.14 fixed-point:

```c
#include "threads/fixed-point.h"

fixed_point_t half = fix_frac(1, 2);   // 0.5
fixed_point_t two = fix_int(2);        // 2.0
fixed_point_t one = fix_mul(half, two); // 1.0
int result = fix_round(one);           // 1
```

## Commit Guidelines

- Do not add Co-Authored-By lines to commits

## Test Structure

Each test has:
- `.c` file: Test source code
- `.ck` file: Expected output checker script
- Tests run in QEMU/Bochs emulator via `pintos` utility

Test categories by component:
- **threads/**: alarm-*, priority-*, mlfqs-*, fair-*
- **userprog/**: args-*, exec-*, fork-*, syscall-*
- **vm/**: pt-*, page-*, mmap-*
- **filesys/**: grow-*, dir-*, syn-*, cache-*
