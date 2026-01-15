<h1 align="center">
  MaverickOS
</h1>

<p align="center">
  <strong>An Advanced Operating System</strong>
</p>

<p align="center">
  <a href="#features">Features</a> •
  <a href="#quick-start">Quick Start</a> •
  <a href="#architecture">Architecture</a> •
  <a href="#testing">Testing</a> •
  <a href="#documentation">Documentation</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-1.4.0-blue.svg" alt="Version">
  <img src="https://img.shields.io/badge/platform-x86-lightgrey.svg" alt="Platform">
  <img src="https://img.shields.io/badge/license-Stanford-green.svg" alt="License">
  <img src="https://img.shields.io/badge/course-CS162-orange.svg" alt="Course">
</p>

---

## Overview

MaverickOS is inspired by PintOS, a simple operating system framework for the x86 architecture, developed at Stanford University for teaching OS fundamentals. This repository contains a **fully-featured implementation** of all four major PintOS projects from UC Berkeley's CS162 course. 

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Programs                            │
├─────────────────────────────────────────────────────────────────┤
│  System Call Interface (exec, fork, wait, open, read, write...) │
├───────────────┬───────────────┬───────────────┬─────────────────┤
│    Process    │    Virtual    │     File      │     Device      │
│   Management  │    Memory     │    System     │    Drivers      │
├───────────────┴───────────────┴───────────────┴─────────────────┤
│              Thread Scheduler (Priority / MLFQS)                │
├─────────────────────────────────────────────────────────────────┤
│                    Hardware Abstraction                         │
└─────────────────────────────────────────────────────────────────┘
```

On top of these projects, there are a lot more features added to this Operating System that makes it quite different from its origin. For example, this project no longer use Perl for its tooling, but the entire tooling is switched to Typescript. It also added a few more advanced schedulers. The filesystem also support journaling. This repo also support RISC-V architecture as well as x86. It has a complete networking stack as well (pending, in future roadmap)

## Features

### Project 1: Threads & Scheduling

| Feature | Description |
|---------|-------------|
| **Priority Scheduler** | Threads scheduled based on priority levels (0-63) |
| **Priority Donation** | Prevents priority inversion through donation chains |
| **MLFQS** | Multi-Level Feedback Queue with dynamic priorities |
| **Alarm Clock** | Efficient sleep without busy-waiting |

### Project 2: User Programs

| Feature | Description |
|---------|-------------|
| **Process Management** | `fork()`, `exec()`, `wait()`, `exit()` |
| **System Calls** | 20+ syscalls for file/process operations |
| **Argument Passing** | Stack-based argument marshalling |
| **Memory Safety** | User pointer validation via page faults |

### Project 3: Virtual Memory

| Feature | Description |
|---------|-------------|
| **Demand Paging** | Lazy loading of executable pages |
| **Frame Management** | Clock algorithm for page eviction |
| **Swap Space** | Disk-backed memory for large workloads |
| **Memory-Mapped Files** | `mmap()` / `munmap()` support |
| **Copy-on-Write Fork** | Efficient process forking |

### Project 4: File System

| Feature | Description |
|---------|-------------|
| **Buffer Cache** | 64-block LRU cache with write-behind |
| **Extensible Files** | Indexed allocation up to 8MB |
| **Subdirectories** | Hierarchical paths with `.` and `..` |
| **Symbolic Links** | `symlink()` and `readlink()` |
| **Write-Ahead Logging** | Crash-consistent transactions |

## Quick Start

### Prerequisites

- GCC cross-compiler for `i386-elf`
- QEMU or Bochs emulator
- GNU Make
- Perl 5.x

### Build & Run

```bash
# Clone the repository
git clone https://github.com/your-repo/pintos.git
cd pintos

# Build the threads project
cd src/threads
make

# Run a test in QEMU
cd build
pintos --qemu -- run alarm-multiple

# Run all tests for a component
cd src/userprog
make check
```

### Build Other Components

```bash
cd src/userprog && make    # User programs
cd src/vm && make          # Virtual memory
cd src/filesys && make     # File system
```

## Architecture

### Project Structure

```
src/
├── threads/          # Core threading and scheduling
│   ├── thread.c      # Thread lifecycle & context switch
│   ├── synch.c       # Locks, semaphores, condition vars
│   └── fixed-point.h # MLFQS arithmetic
│
├── userprog/         # User program support
│   ├── process.c     # ELF loading & process management
│   ├── syscall.c     # System call dispatch
│   └── exception.c   # Page fault handling
│
├── vm/               # Virtual memory
│   ├── page.c        # Supplemental page table
│   ├── frame.c       # Physical frame management
│   └── swap.c        # Swap partition
│
├── filesys/          # File system
│   ├── inode.c       # Indexed file blocks
│   ├── directory.c   # Directory operations
│   ├── cache.c       # Buffer cache
│   └── wal.c         # Write-ahead logging
│
└── devices/          # Hardware drivers
    ├── timer.c       # System timer
    ├── block.c       # Block device abstraction
    └── ide.c         # IDE disk driver
```

### Memory Layout

```
┌──────────────────────┐ 0xFFFFFFFF
│    Kernel Space      │
│  (Direct Mapped)     │
├──────────────────────┤ PHYS_BASE (0xC0000000)
│                      │
│    User Stack        │
│         ↓            │
│                      │
│         ↑            │
│    Memory Maps       │
│                      │
├──────────────────────┤
│    User Heap         │
├──────────────────────┤
│    BSS Segment       │
├──────────────────────┤
│    Data Segment      │
├──────────────────────┤
│    Code Segment      │
├──────────────────────┤ 0x08048000
│    Unmapped          │
└──────────────────────┘ 0x00000000
```

## Testing

### Run Test Suites

```bash
# All tests for a component
cd src/threads && make check
cd src/userprog && make check
cd src/filesys && make check

# Individual test
cd src/threads/build
make tests/threads/priority-donate-chain.result

# View test output
cat tests/threads/priority-donate-chain.output
```

### Test Categories

| Component | Tests | Coverage |
|-----------|-------|----------|
| Threads | 27 | Alarm, priority, donation, MLFQS |
| Userprog | 76 | Args, syscalls, processes, robustness |
| VM | 40+ | Page faults, eviction, mmap, swap |
| Filesys | 50+ | Cache, growth, directories, persistence |

## Documentation

- **[CHANGELOG.md](CHANGELOG.md)** - Version history and release notes
- **[docs/](docs/)** - Design documents and implementation guides
  - Project design specs
  - Implementation plans
  - Concept explanations

### Interactive Learning

This repository includes Claude Code commands for exploring OS concepts:

| Command | Description |
|---------|-------------|
| `/explain` | Explain OS concepts in context |
| `/trace` | Trace code execution paths |
| `/debug-learn` | Debug while learning concepts |
| `/quiz` | Test your OS knowledge |
| `/explore` | Guided codebase tours |
| `/challenge` | Coding challenges |

## Development

### Code Style

- K&R indentation (2 spaces)
- 80 character line limit
- Descriptive names
- Comprehensive comments

### Environment Setup

```bash
# Configure shell with PintOS utilities
./setup-shell.sh

# Install pre-commit hooks
cp .pre-commit.sh .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

## Implementation Highlights

<details>
<summary><strong>Priority Donation</strong></summary>

Multi-level priority donation handles nested lock acquisition:

```
Thread H (pri=63) waiting on Lock A held by
  Thread M (pri=31) waiting on Lock B held by
    Thread L (pri=0)

→ L receives donation of 63, runs, releases B
→ M receives donation of 63, runs, releases A
→ H finally acquires A and runs
```
</details>

<details>
<summary><strong>MLFQS Scheduler</strong></summary>

Dynamic priority based on CPU usage:
```
priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice
load_avg = (59/60) * load_avg + (1/60) * ready_threads
```
Uses 17.14 fixed-point arithmetic for precision.
</details>

<details>
<summary><strong>Buffer Cache</strong></summary>

64-block cache with clock eviction:
- **Read-ahead**: Prefetches next block on sequential access
- **Write-behind**: Flushes dirty blocks every 30 seconds
- **Coalescing**: Batches writes to same sector
</details>

<details>
<summary><strong>Indexed Inodes</strong></summary>

Two-level indexed allocation:
```
┌─────────────────┐
│   Inode Block   │
├─────────────────┤
│ 123 Direct      │ → 123 blocks × 512B = 63KB
│ 1 Indirect      │ → 128 blocks × 512B = 64KB
│ 1 Doubly-Indir  │ → 128² blocks × 512B = 8MB
└─────────────────┘
Max file size: ~8MB
```
</details>

## License

Copyright (C) 2004-2006 Board of Trustees, Leland Stanford Jr. University.

This project is licensed under the [Stanford PintOS License](src/LICENSE).

---

<p align="center">
  <sub>
    Built for <strong>CS162: Operating Systems</strong> at UC Berkeley<br>
    For educational and reference purposes only
  </sub>
</p>
