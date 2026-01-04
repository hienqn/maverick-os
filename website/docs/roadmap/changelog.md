---
sidebar_position: 1
---

import Timeline from '@site/src/components/Timeline';

# Changelog

All notable changes to PintOS are documented here.

<Timeline
  title="Version History"
  items={[
    {
      version: 'v1.4.0',
      date: '2026-01-04',
      title: 'Copy-on-Write Fork',
      highlight: true,
      items: [
        'Implemented COW fork for efficient process creation',
        'Pages shared read-only between parent and child',
        'Copy deferred until write fault occurs',
        'Added comprehensive header documentation',
      ],
      link: '/docs/projects/vm/cow-fork',
    },
    {
      version: 'v1.3.0',
      date: '2026-01-03',
      title: 'Virtual Memory',
      items: [
        'Supplemental page table (SPT) with 4 page states',
        'Demand paging with lazy loading',
        'Frame table with clock eviction algorithm',
        'Swap partition support for memory pressure',
        'Fork support with SPT cloning',
        'Memory-mapped files (mmap/munmap)',
      ],
      link: '/docs/projects/vm/overview',
    },
    {
      version: 'v1.2.0',
      date: '2026-01-02',
      title: 'Write-Ahead Logging',
      items: [
        'Transaction-based crash recovery',
        'Log record types: BEGIN, COMMIT, ABORT, WRITE',
        'UNDO/REDO recovery model',
        '64-sector log with metadata tracking',
        'Deferred checkpoint implementation',
      ],
      link: '/docs/projects/filesys/wal',
    },
    {
      version: 'v1.1.0',
      date: '2025-12-28',
      title: 'Symbolic Links',
      items: [
        'symlink() and readlink() system calls',
        'Transparent link following in path resolution',
        'Symlink loop detection (max depth)',
        'Comprehensive test suite (9 tests)',
      ],
      link: '/docs/projects/filesys/subdirectories',
    },
    {
      version: 'v1.0.0',
      date: '2025-12-27',
      title: 'Complete File System',
      items: [
        '64-block buffer cache with LRU eviction',
        'Extensible files with indexed allocation (8MB max)',
        'Subdirectory support with . and ..',
        'Read-ahead prefetching',
        'Write-behind dirty block flushing',
      ],
      link: '/docs/projects/filesys/overview',
    },
    {
      version: 'v0.3.0',
      date: '2025-12-26',
      title: 'User Programs',
      items: [
        'Argument passing via stack',
        '20+ system calls (file, process, sync)',
        'Process management (exec, wait, exit)',
        'File descriptor table per process',
        'Page fault-based pointer validation',
      ],
      link: '/docs/projects/userprog/overview',
    },
    {
      version: 'v0.2.0',
      date: '2025-12-25',
      title: 'Advanced Scheduling',
      items: [
        'MLFQS (Multi-Level Feedback Queue)',
        '17.14 fixed-point arithmetic',
        'Dynamic priority calculation',
        'Load average tracking',
      ],
      link: '/docs/concepts/scheduling',
    },
    {
      version: 'v0.1.0',
      date: '2025-12-23',
      title: 'Core Threading',
      items: [
        'Priority scheduler (64 levels)',
        'Priority donation for lock inversion',
        'Nested donation support',
        'Efficient alarm clock (no busy-wait)',
        'Synchronization primitives',
      ],
      link: '/docs/projects/threads/overview',
    },
  ]}
/>

## Detailed Changes

### v1.4.0 - Copy-on-Write Fork

#### Added
- **COW Fork Implementation**
  - Pages marked read-only and shared during fork
  - Write faults trigger page copy
  - Reference counting for shared frames
  - Significant memory savings for fork-heavy workloads

- **Documentation Improvements**
  - Comprehensive header documentation for all public APIs
  - ASCII diagrams in critical headers
  - Thread safety annotations

### v1.3.0 - Virtual Memory

#### Added
- **Supplemental Page Table**
  - Hash-based per-process page tracking
  - Four page states: ZERO, FILE, SWAP, FRAME

- **Demand Paging**
  - Lazy loading of executable segments
  - Zero-fill pages allocated on first access
  - File-backed pages loaded from disk

- **Frame Management**
  - Global frame table
  - Clock (second-chance) eviction algorithm
  - Dirty page write-back before eviction

- **Swap Space**
  - Dedicated swap partition
  - Bitmap-based slot allocation
  - Automatic swap-in on page fault

### v1.2.0 - Write-Ahead Logging

#### Added
- Transaction log for crash consistency
- Recovery on boot after unclean shutdown
- ACID guarantees for filesystem operations

### v1.1.0 - Symbolic Links

#### Added
- `symlink()` system call
- `readlink()` system call
- Symlink-aware path resolution
- Loop detection to prevent infinite recursion

### v1.0.0 - File System

#### Added
- Buffer cache with 64 blocks
- Indexed inode structure (direct, indirect, doubly-indirect)
- Directory hierarchy with relative paths
- File growth up to 8MB

### Earlier Versions

See [Project Guides](/docs/projects/threads/overview) for implementation details of earlier versions.
