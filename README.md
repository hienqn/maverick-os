# PintOS - CS162 Operating System Implementation

A complete implementation of the PintOS educational operating system for UC Berkeley's CS162 (Operating Systems and Systems Programming) course. This project implements core operating system components including thread scheduling, process management, file systems, and virtual memory.

## Table of Contents

- [Overview](#overview)
- [Project Structure](#project-structure)
- [Features](#features)
- [Prerequisites](#prerequisites)
- [Building and Running](#building-and-running)
- [Testing](#testing)
- [Components](#components)
- [Learning Resources](#learning-resources)
- [Development](#development)
- [License](#license)

## Overview

PintOS is a simple operating system framework for the x86 architecture that runs on both real hardware and the QEMU and Bochs emulators. This repository contains a fully-featured implementation of all major PintOS projects:

- **Project 1**: Threads and Scheduling
- **Project 2**: User Programs
- **Project 3**: Virtual Memory
- **Project 4**: File Systems

## Project Structure

```
cs162-pintos/
├── src/
│   ├── threads/        # Thread management and scheduling
│   ├── userprog/       # User program loading and system calls
│   ├── vm/             # Virtual memory management
│   ├── filesys/        # File system implementation
│   ├── devices/        # Device drivers (timer, keyboard, disk, etc.)
│   ├── lib/            # Standard library implementations
│   ├── tests/          # Test suites for all components
│   ├── examples/       # Example user programs
│   └── utils/          # Utility programs
├── .claude/            # Claude Code learning commands
└── README.md
```

## Features

### Threads & Scheduling
- **Priority Scheduling**: Threads scheduled based on priority levels
- **Priority Donation**: Prevents priority inversion through priority donation chains
- **MLFQS Scheduler**: Multi-Level Feedback Queue Scheduler with advanced load balancing
- **Synchronization Primitives**: Semaphores, locks, and condition variables
- **Alarm Clock**: Efficient thread sleeping without busy-waiting

### User Programs
- **Program Loading**: ELF binary loading and execution
- **System Calls**: Complete syscall interface for file operations, process control, and I/O
- **Process Management**: Process creation, execution, waiting, and termination
- **Argument Passing**: Command-line argument parsing and stack setup
- **Memory Safety**: User memory access validation and protection

### File System
- **Buffer Cache**: LRU-based disk block caching with write-behind and read-ahead
- **Prefetching**: Sequential access pattern detection and optimization
- **Extensible Files**: Dynamic file growth with indexed allocation
- **Subdirectories**: Hierarchical directory structure with absolute and relative paths
- **Synchronization**: Fine-grained locking for concurrent file access
- **File Operations**: Create, open, read, write, seek, close, and remove

### Virtual Memory
- **Lazy Loading**: Pages loaded on-demand via page faults
- **Page Management**: Efficient page table and frame management
- **Swap Space**: Disk-backed swap for memory-intensive workloads
- **Memory-Mapped Files**: mmap/munmap system call support

## Prerequisites

- GCC cross-compiler for i386-elf target
- QEMU or Bochs emulator
- Make build system
- Perl (for testing utilities)

## Building and Running

### Build the kernel

```bash
cd src/threads
make
```

### Run in QEMU

```bash
cd src/threads/build
pintos --qemu -- run alarm-multiple
```

### Run specific tests

```bash
cd src/threads
make check
```

### Build other components

```bash
# User programs
cd src/userprog
make

# File system
cd src/filesys
make

# Virtual memory
cd src/vm
make
```

## Testing

Each component includes comprehensive test suites:

```bash
# Run all thread tests
cd src/threads
make check

# Run specific test
cd src/threads/build
make tests/threads/alarm-multiple.result

# Run all userprog tests
cd src/userprog
make check

# Run all filesys tests
cd src/filesys
make check
```

Test results are stored in `*.result` files and can be compared against expected outputs in `*.ck` files.

## Components

### Thread System (`src/threads/`)

- **thread.c/h**: Thread creation, scheduling, and context switching
- **synch.c/h**: Synchronization primitives (locks, semaphores, condition variables)
- **fixed-point.h**: Fixed-point arithmetic for MLFQS calculations
- **interrupt.c/h**: Interrupt handling framework
- **malloc.c/h**: Kernel memory allocation

### User Programs (`src/userprog/`)

- **process.c/h**: Process loading, execution, and management
- **syscall.c/h**: System call handler and implementations
- **exception.c/h**: Page fault and exception handling
- **pagedir.c/h**: Page directory management

### File System (`src/filesys/`)

- **filesys.c/h**: High-level file system operations
- **inode.c/h**: Inode structure with indexed allocation
- **directory.c/h**: Directory operations and path resolution
- **cache.c/h**: Buffer cache implementation
- **cache_prefetch.c/h**: Read-ahead prefetching logic
- **file.c/h**: File descriptor operations
- **free-map.c/h**: Free block bitmap management

### Devices (`src/devices/`)

- **timer.c/h**: System timer and alarm clock
- **kbd.c/h**: Keyboard driver
- **block.c/h**: Block device abstraction
- **ide.c/h**: IDE disk driver

## Learning Resources

This repository includes interactive Claude Code commands for learning OS concepts:

- `/explain` - Explain OS concepts in the context of the Pintos codebase
- `/debug-learn` - Debug code while learning underlying OS concepts
- `/quiz` - Test your OS knowledge with interactive quizzes
- `/explore` - Take guided tours through parts of the OS
- `/challenge` - Get coding challenges to practice OS concepts
- `/trace` - Trace code execution paths to understand control flow

To use these commands, type them in Claude Code when working with this repository.

## Development

### Code Style

This project follows the CS162 coding standards:
- K&R indentation style (2 spaces)
- Comprehensive comments and documentation
- Descriptive variable and function names
- Maximum 80 characters per line

### Pre-commit Hooks

A pre-commit script is available to ensure code quality:

```bash
# Set up the pre-commit hook
cp .pre-commit.sh .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

### Shell Environment

Set up the development environment:

```bash
./setup-shell.sh
```

This configures the shell with helpful aliases and environment variables for PintOS development.

## Implementation Highlights

### Priority Donation
The thread scheduler implements multi-level priority donation to handle nested resource acquisition and prevent priority inversion. Donations propagate through chains of locks and are properly released when locks are freed.

### MLFQS Scheduler
The Multi-Level Feedback Queue Scheduler uses fixed-point arithmetic to calculate priorities dynamically based on recent CPU usage and niceness values, ensuring fair scheduling without starvation.

### Buffer Cache
The file system buffer cache uses a clock algorithm (second-chance) for eviction, implements write-behind to batch disk writes, and includes read-ahead prefetching for sequential access patterns.

### Indexed Inodes
Files use a two-level indexed allocation scheme with direct blocks, indirect blocks, and doubly-indirect blocks, supporting files up to 8MB in size while maintaining efficient access for small files.

## License

Copyright (C) 2004-2006 Board of Trustees, Leland Stanford Jr. University.
All rights reserved.

This project is licensed under the Stanford PintOS License. See [src/LICENSE](src/LICENSE) for full details.

PintOS was originally developed at Stanford University and has been adapted for educational use in operating systems courses worldwide.

---

**Note**: This is an educational project completed as part of CS162. The code is provided for reference and learning purposes. If you are currently taking CS162 or a similar course, please adhere to your institution's academic integrity policies.
