# Changelog

All notable changes to this PintOS implementation are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased]

### Planned
- Symmetric Multiprocessing (SMP) support

---

## [1.4.0] - 2026-01-04

### Added
- **Copy-on-Write (COW) Fork**: Implemented lazy page copying for `fork()` system call
  - Pages marked read-only and shared between parent/child
  - Actual copy deferred until write fault occurs
  - Significantly reduces fork overhead for large processes

### Changed
- Comprehensive documentation added to critical header files
  - `pagedir.h`: Page directory management (13 functions)
  - `inode.h`: Inode lifecycle and multi-level indexing (10 functions)
  - `directory.h`: Directory operations and iterator pattern (8 functions)
  - `free-map.h`: Sector allocation and bitmap management (7 functions)
  - `exception.h`: Page fault handling algorithm flowchart

### Fixed
- Priority donation now re-sorts ready queue for `THREAD_READY` recipients
- Syscall robustness issues causing test failures and hangs
- Replaced explicit syscall pointer validation with page-fault based approach

---

## [1.3.0] - 2026-01-03

### Added
- **Virtual Memory Subsystem**: Complete demand paging implementation
  - Lazy loading of executable pages
  - Supplemental page table (SPT) for tracking virtual pages
  - Frame table with clock algorithm eviction
  - Swap partition support for memory-intensive workloads
  - Fork support with SPT cloning

### Fixed
- Threads build compatibility with `#ifdef VM` guards for VM-specific code

---

## [1.2.0] - 2026-01-02

### Added
- **Write-Ahead Logging (WAL)**: Crash-consistent file system operations
  - Transaction-based logging for atomic multi-block updates
  - Deferred checkpoint implementation for better performance
  - Recovery mechanism for system crashes

### Fixed
- Critical bugs in WAL implementation
- Deferred checkpoint now actually runs when flag is set
- Stack overflow and sector conflict bugs in WAL
- syn-rw timeout and WAL persistence test infrastructure
- Refactored WAL skip logic to use inode flag instead of sector check

### Documentation
- Added essay on append-only sequential logs as powerful primitives
- WAL documentation updated to reflect deferred checkpoint implementation

---

## [1.1.0] - 2025-12-28

### Added
- **Symbolic Links**: `symlink()` and `readlink()` system calls
  - Support for creating and resolving symbolic links
  - Loop detection to prevent infinite symlink chains
  - Comprehensive test suite for symbolic link operations

---

## [1.0.0] - 2025-12-27

### Added
- **Buffer Cache**: LRU-based disk block caching
  - Clock algorithm (second-chance) for eviction
  - Write-behind for batched disk writes
  - Read-ahead prefetching for sequential access patterns

- **Extensible Files**: Dynamic file growth with indexed allocation
  - Direct blocks for small files
  - Single indirect blocks
  - Doubly-indirect blocks
  - Support for files up to 8MB

- **Subdirectories**: Hierarchical directory structure
  - Absolute and relative path resolution
  - Current working directory support
  - Directory traversal operations

- **Multi-Level Feedback Queue Scheduler (MLFQS)**
  - Dynamic priority calculation based on CPU usage
  - Fixed-point arithmetic for precise calculations
  - Load average tracking
  - Niceness support

- **Priority Donation**: Complete priority inversion prevention
  - Nested donation through lock chains
  - Multiple donation support
  - Proper donation release on lock release

- **User Programs**: Full system call interface
  - Process management: `exec`, `wait`, `exit`, `fork`
  - File operations: `open`, `close`, `read`, `write`, `seek`, `tell`, `filesize`
  - File system: `create`, `remove`, `mkdir`, `chdir`, `readdir`, `isdir`
  - Memory mapping: `mmap`, `munmap`

- **Alarm Clock**: Efficient thread sleeping
  - No busy-waiting implementation
  - Timer interrupt-based wakeup

### Changed
- Refactored file system for improved code quality and thread safety
- Formatted all source files for consistency

---

## [0.1.0] - 2025-12-23

### Added
- Initial project setup
- Basic thread scheduling infrastructure
- Priority scheduler foundation
- Semaphore and condition variable implementations
- Timer interrupt handling

---

## Contributors

This implementation was developed as part of UC Berkeley's CS162 Operating Systems course.

---

## Legend

- **Added**: New features
- **Changed**: Changes in existing functionality
- **Deprecated**: Soon-to-be removed features
- **Removed**: Removed features
- **Fixed**: Bug fixes
- **Security**: Vulnerability fixes
- **Documentation**: Documentation improvements
