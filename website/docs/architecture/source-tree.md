---
sidebar_position: 3
---

# Source Tree Guide

A file-by-file guide to the PintOS source code.

## Directory Overview

```
src/
├── threads/      # Core kernel (always needed)
├── userprog/     # User program support
├── vm/           # Virtual memory
├── filesys/      # File system
├── devices/      # Hardware drivers
├── lib/          # Libraries
├── tests/        # Test programs
└── utils/        # Build utilities
```

## threads/

The core threading system. Built by all projects.

| File | Lines | Description |
|------|-------|-------------|
| `thread.c` | ~500 | Thread lifecycle, scheduling, `thread_create()`, `thread_exit()` |
| `thread.h` | ~150 | Thread struct, status enum, priorities |
| `synch.c` | ~300 | Locks, semaphores, condition variables |
| `synch.h` | ~50 | Synchronization primitive structs |
| `switch.S` | ~40 | Context switch assembly |
| `switch.h` | ~20 | Switch function declaration |
| `interrupt.c` | ~300 | Interrupt handling, IDT setup |
| `interrupt.h` | ~50 | Interrupt types, frame struct |
| `palloc.c` | ~200 | Page allocator (kernel/user pools) |
| `palloc.h` | ~30 | Allocation flags |
| `malloc.c` | ~300 | Kernel heap allocator |
| `malloc.h` | ~10 | malloc/free declarations |
| `fixed-point.h` | ~40 | 17.14 fixed-point math for MLFQS |
| `init.c` | ~250 | Kernel initialization, `main()` |
| `loader.S` | ~200 | Bootloader (real mode → protected) |
| `start.S` | ~100 | Early kernel setup (GDT, paging) |

## userprog/

User program loading and system calls.

| File | Lines | Description |
|------|-------|-------------|
| `process.c` | ~600 | Process lifecycle, ELF loading, `fork()`, `exec()`, `wait()` |
| `process.h` | ~50 | Process functions |
| `syscall.c` | ~800 | System call handler, all syscall implementations |
| `syscall.h` | ~20 | Handler declaration |
| `exception.c` | ~150 | Page fault handler, kill on bad access |
| `exception.h` | ~10 | Exception setup |
| `pagedir.c` | ~300 | Page directory manipulation |
| `pagedir.h` | ~30 | PD functions |
| `gdt.c` | ~100 | Global Descriptor Table setup |
| `tss.c` | ~80 | Task State Segment |

## vm/

Virtual memory subsystem (Project 3).

| File | Lines | Description |
|------|-------|-------------|
| `page.c` | ~400 | Supplemental page table |
| `page.h` | ~60 | SPT entry struct, page states |
| `frame.c` | ~300 | Frame table, clock eviction |
| `frame.h` | ~40 | Frame functions |
| `swap.c` | ~150 | Swap slot management |
| `swap.h` | ~20 | Swap functions |
| `vm.c` | ~50 | VM initialization |
| `vm.h` | ~20 | VM init declaration |

## filesys/

File system implementation (Project 4).

| File | Lines | Description |
|------|-------|-------------|
| `inode.c` | ~500 | Indexed inode, read/write |
| `inode.h` | ~80 | Inode struct, sector math |
| `file.c` | ~150 | File abstraction |
| `file.h` | ~30 | File struct |
| `directory.c` | ~350 | Directory operations |
| `directory.h` | ~40 | Dir struct, entry |
| `filesys.c` | ~200 | High-level FS operations |
| `filesys.h` | ~20 | FS functions |
| `cache.c` | ~400 | Buffer cache with LRU |
| `cache.h` | ~40 | Cache functions |
| `cache_prefetch.c` | ~100 | Read-ahead |
| `wal.c` | ~600 | Write-ahead logging |
| `wal.h` | ~80 | WAL structures |
| `free-map.c` | ~100 | Sector allocation bitmap |
| `free-map.h` | ~20 | Free-map functions |

## devices/

Hardware drivers.

| File | Lines | Description |
|------|-------|-------------|
| `timer.c` | ~200 | System timer, `timer_sleep()` |
| `timer.h` | ~20 | Timer functions |
| `block.c` | ~300 | Block device abstraction |
| `block.h` | ~40 | Block interface |
| `ide.c` | ~300 | IDE disk driver |
| `ide.h` | ~10 | IDE init |
| `input.c` | ~100 | Input buffer |
| `kbd.c` | ~200 | Keyboard driver |
| `vga.c` | ~100 | VGA text display |
| `serial.c` | ~150 | Serial port |
| `pit.c` | ~100 | Programmable Interval Timer |
| `rtc.c` | ~100 | Real-time clock |
| `shutdown.c` | ~50 | Power off |

## lib/

Shared libraries.

| Directory | Purpose |
|-----------|---------|
| `lib/` | Common utilities (both kernel and user) |
| `lib/kernel/` | Kernel-only utilities |
| `lib/user/` | User program library |

### Key Files

| File | Description |
|------|-------------|
| `lib/debug.c` | `ASSERT()`, `PANIC()`, backtraces |
| `lib/hash.c` | Hash table (used by SPT) |
| `lib/list.c` | Doubly-linked list |
| `lib/bitmap.c` | Bitmap operations |
| `lib/kernel/console.c` | Kernel printf |
| `lib/user/syscall.c` | User syscall wrappers |

## tests/

Test programs organized by project.

| Directory | Count | Examples |
|-----------|-------|----------|
| `tests/threads/` | 27 | `alarm-*`, `priority-*`, `mlfqs-*` |
| `tests/userprog/` | 76 | `args-*`, `exec-*`, `fork-*` |
| `tests/vm/` | 40+ | `page-*`, `mmap-*`, `swap-*` |
| `tests/filesys/` | 50+ | `grow-*`, `dir-*`, `syn-*` |

## Build Artifacts

After `make`:

```
build/
├── kernel.o        # Linked kernel
├── kernel.bin      # Raw binary kernel
├── os.dsk          # Bootable disk image
├── loader.bin      # Bootloader
└── *.o             # Object files
```

## Next Steps

- [Architecture Overview](/docs/architecture/overview) - Visual system architecture
- [Threads Project](/docs/projects/threads/overview) - Start implementing
