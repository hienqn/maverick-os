# CLAUDE.md

This file provides guidance for AI assistants working with the PintOS codebase.

## Project Overview

PintOS is an educational operating system for x86 (i686) architecture, developed at Stanford University and used in UC Berkeley's CS162 course. This repository contains a complete implementation of all four major projects: Threads, User Programs, Virtual Memory, and File System.

## Directory Structure

```
src/
├── threads/      # Core threading, scheduling, synchronization
├── userprog/     # User programs, syscalls, process management
├── vm/           # Virtual memory, paging, swap, mmap
├── filesys/      # File system, buffer cache, directories, WAL
├── devices/      # Hardware drivers (timer, disk, keyboard, etc.)
├── lib/          # C library (kernel/ and user/ subdirectories)
├── tests/        # Test suites for each component
├── examples/     # Sample user programs
└── utils/        # Build and test utilities
    ├── pintos, Pintos.pm   # Perl utilities (default)
    └── bun/                # Bun/TypeScript ports (USE_BUN=1)
```

## Build Commands

```bash
# Build a specific component (from src/ directory)
cd threads && make      # Kernel with threading only
cd userprog && make     # Add user program support
cd vm && make           # Add virtual memory
cd filesys && make      # Add file system

# Clean build
make clean

# Format code
make format
```

## Testing

```bash
# Run all tests for a component
cd src/userprog && make check
cd src/vm && make check
cd src/filesys && make check

# Run tests in parallel (much faster)
make check -j$(nproc)    # Use all CPU cores
make check -j20          # Or specify core count

# Run single test (from build directory)
pintos --qemu -- run alarm-multiple

# View test output
cat tests/threads/alarm-multiple.output

# Use Bun/TypeScript tooling with colored output
make check USE_BUN=1
make check USE_BUN=1 -j20  # Parallel + colored output
```

### Tooling Toggle (Perl vs Bun)

The build system supports both Perl and Bun/TypeScript implementations of utilities:

| Variable | Default | Description |
|----------|---------|-------------|
| `USE_BUN=0` | Yes | Use original Perl scripts (`utils/pintos`) |
| `USE_BUN=1` | No | Use Bun/TypeScript ports (`utils/bun/bin/pintos`) |

**Affected components:**
- `pintos` - Simulator launcher
- `pintos-mkdisk` - Disk image creation
- `backtrace` - Stack trace translation
- `check-test` - Test verification (parses `.ck` files)
- `pintos-test` - Interactive test runner (fuzzy search with fzf)

Note: Complex tests (MLFQS, archive/persistence) fall back to Perl automatically.

## Code Style

- **Indentation**: 2 spaces (no tabs)
- **Line length**: 100 characters max
- **Naming**: snake_case for functions and variables, UPPER_SNAKE_CASE for macros
- **Format tool**: clang-format (run `make format`)

## Key Source Files

### Threading (`src/threads/`)
- `thread.h/c` - Thread struct, lifecycle, scheduling
- `synch.h/c` - Locks, semaphores, condition variables
- `interrupt.h/c` - Interrupt handling
- `palloc.h/c` - Page allocator
- `malloc.h/c` - Subpage allocator

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

## Architecture Notes

### Memory Layout
- `PHYS_BASE` (0xC0000000): Kernel/user boundary
- User code starts at 0x08048000
- User stack grows down from PHYS_BASE

### Synchronization Primitives
- Interrupts must be disabled for certain operations (use `intr_disable()`)
- Never hold locks across blocking operations
- Priority donation prevents priority inversion

### Page Fault Handling
- User pointer validation happens via page faults in `exception.c`
- SPT in `vm/page.c` tracks page locations (frame, swap, file, or zero)

### System Calls
- Entry point: `syscall_handler()` in `userprog/syscall.c`
- Arguments passed on user stack
- Always validate user pointers before dereferencing

## Common Patterns

### Disabling Interrupts
```c
enum intr_level old_level = intr_disable();
// Critical section
intr_set_level(old_level);
```

### Lock Usage
```c
lock_acquire(&some_lock);
// Protected section
lock_release(&some_lock);
```

### Hash Table Iteration (SPT, etc.)
```c
struct hash_iterator i;
hash_first(&i, &table);
while (hash_next(&i)) {
  struct entry *e = hash_entry(hash_cur(&i), struct entry, hash_elem);
  // Process entry
}
```

## Debugging

- Use `printf()` or `PANIC()` for debugging
- Run with `--qemu` flag: `pintos --qemu -- run test-name`
- GDB support: `pintos-gdb` wrapper script
- Backtrace utility: `utils/backtrace`

## Commit Guidelines

- Do not add Co-Authored-By lines to commits

## Test Structure

Each test has:
- `.c` file: Test source code
- `.ck` file: Expected output checker script
- Tests run in QEMU/Bochs emulator via `pintos` Perl script
