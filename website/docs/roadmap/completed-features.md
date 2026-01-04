---
sidebar_position: 2
---

# Completed Features

A summary of all implemented features in PintOS.

## Project 1: Threads (v0.1.0 - v0.2.0)

### Alarm Clock
- Efficient thread sleeping without busy-waiting
- Wake-up based on timer interrupts
- Sorted sleep queue for O(1) wake-up checks

### Priority Scheduler
- 64 priority levels (0-63)
- Higher priority threads run first
- Preemption when higher-priority thread becomes ready

### Priority Donation
- Prevents priority inversion
- Nested donation through lock chains
- Multiple donation support (thread holds multiple locks)

### MLFQS Scheduler
- Multi-Level Feedback Queue
- Dynamic priority based on CPU usage
- 17.14 fixed-point arithmetic
- Load average tracking

## Project 2: User Programs (v0.3.0)

### Argument Passing
- Stack-based argument marshalling
- Proper word alignment
- Support for complex argument strings

### System Calls
37+ system calls implemented:

| Category | Syscalls |
|----------|----------|
| Process | `halt`, `exit`, `exec`, `wait`, `fork` |
| File | `create`, `remove`, `open`, `close`, `read`, `write`, `seek`, `tell`, `filesize` |
| Directory | `chdir`, `mkdir`, `readdir`, `isdir`, `inumber` |
| Memory | `mmap`, `munmap` |
| Threading | `pt_create`, `pt_exit`, `pt_join` |
| Sync | `lock_init`, `lock_acquire`, `lock_release`, `sema_*` |

### Memory Safety
- Page fault-based pointer validation
- Graceful handling of bad user pointers
- Process termination with -1 on invalid access

## Project 3: Virtual Memory (v1.3.0 - v1.4.0)

### Demand Paging
- Lazy loading of executable segments
- Pages loaded on first access
- Reduced memory footprint for large programs

### Supplemental Page Table
- Per-process page tracking
- Four page states: ZERO, FILE, SWAP, FRAME
- Hash-based for O(1) lookup

### Frame Table
- Global frame management
- Clock (second-chance) eviction algorithm
- Access bit tracking

### Swap Space
- Disk-backed swap partition
- Bitmap-based slot allocation
- Transparent swap-in on access

### Memory-Mapped Files
- `mmap()` and `munmap()` support
- File-backed pages
- Dirty page write-back

### Copy-on-Write Fork (v1.4.0)
- Shared read-only pages between parent/child
- Copy triggered on write fault
- Significant memory savings

## Project 4: File System (v1.0.0 - v1.2.0)

### Buffer Cache (v1.0.0)
- 64-block LRU cache
- Clock eviction algorithm
- Write-behind with periodic flush
- Read-ahead prefetching

### Extensible Files (v1.0.0)
- Indexed allocation
- 12 direct blocks
- 1 indirect block
- 1 doubly-indirect block
- Up to 8MB file size

### Subdirectories (v1.0.0)
- Hierarchical paths
- `.` and `..` entries
- Relative and absolute paths
- Current working directory

### Symbolic Links (v1.1.0)
- `symlink()` system call
- `readlink()` system call
- Transparent path resolution
- Loop detection

### Write-Ahead Logging (v1.2.0)
- Transaction-based crash recovery
- UNDO/REDO recovery model
- 64-sector log space
- ACID guarantees

## Test Coverage

| Project | Tests | Status |
|---------|-------|--------|
| Threads | 27 | All passing |
| User Programs | 76 | All passing |
| Virtual Memory | 40+ | All passing |
| File System | 50+ | All passing |

## See Also

- [Changelog](/docs/roadmap/changelog) - Detailed version history
- [Future: SMP](/docs/roadmap/future-smp) - Planned multiprocessor support
