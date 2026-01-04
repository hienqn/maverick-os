---
sidebar_position: 1
slug: /
---

# Welcome to PintOS

**PintOS** is a simple operating system framework for the x86 architecture, developed at Stanford University for teaching OS fundamentals. This documentation covers a fully-featured implementation of all four major projects.

## What You'll Learn

Through PintOS, you'll gain hands-on experience with core operating system concepts:

- **Threads & Scheduling**: Priority schedulers, MLFQS, synchronization primitives
- **User Programs**: System calls, process management, memory protection
- **Virtual Memory**: Demand paging, frame management, swap space
- **File Systems**: Buffer caching, indexed allocation, journaling

## Architecture Overview

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

## Features by Project

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
| **System Calls** | 37+ syscalls for file/process operations |
| **Argument Passing** | Stack-based argument marshalling |
| **Memory Safety** | User pointer validation via page faults |

### Project 3: Virtual Memory

| Feature | Description |
|---------|-------------|
| **Demand Paging** | Lazy loading of executable pages |
| **Frame Management** | Clock algorithm for page eviction |
| **Swap Space** | Disk-backed memory for large workloads |
| **Memory-Mapped Files** | `mmap()` / `munmap()` support |
| **Copy-on-Write Fork** | Efficient process forking (v1.4.0) |

### Project 4: File System

| Feature | Description |
|---------|-------------|
| **Buffer Cache** | 64-block LRU cache with write-behind |
| **Extensible Files** | Indexed allocation up to 8MB |
| **Subdirectories** | Hierarchical paths with `.` and `..` |
| **Symbolic Links** | `symlink()` and `readlink()` |
| **Write-Ahead Logging** | Crash-consistent transactions |

## Quick Start

```bash
# Clone the repository
git clone https://github.com/your-repo/pintos.git
cd pintos

# Build the threads project
cd src/threads
make

# Run a test
cd build
pintos --qemu -- run alarm-multiple
```

### Run the Documentation Site

```bash
cd website
bun install
bun start    # Opens at localhost:3000
```

See the [Installation Guide](/docs/getting-started/installation) for detailed setup instructions.

## How to Use This Documentation

- **New to PintOS?** Start with [Getting Started](/docs/getting-started/installation)
- **Learning OS Concepts?** Explore the [OS Concepts](/docs/concepts/threads-and-processes) section
- **Working on a Project?** Check the [Project Guides](/docs/projects/threads/overview)
- **Want Deep Understanding?** Read the [Deep Dives](/docs/deep-dives/context-switch-assembly)
- **Tracking Progress?** See the [Roadmap](/docs/roadmap/changelog)

## Version

This documentation covers **PintOS v1.4.0** with the following major features:

- Copy-on-Write Fork
- Write-Ahead Logging
- Symbolic Links
- Virtual Memory with Demand Paging
- MLFQS Scheduler with Priority Donation
