---
sidebar_position: 0
slug: /concepts
---

import ConceptMap from '@site/src/components/ConceptMap';

# OS Concepts Overview

This section covers the fundamental operating system concepts implemented in Maverick OS. Use the interactive map below to explore relationships between concepts.

<ConceptMap
  title="Operating System Concepts"
  concepts={[
    // Core Concepts
    { id: 'process', label: 'Process', category: 'concept', description: 'Unit of resource ownership with isolated address space', link: '/docs/concepts/threads-and-processes' },
    { id: 'thread', label: 'Thread', category: 'concept', description: 'Unit of CPU scheduling within a process', link: '/docs/concepts/threads-and-processes' },
    { id: 'scheduling', label: 'Scheduling', category: 'concept', description: 'Deciding which thread runs next', link: '/docs/concepts/scheduling' },
    { id: 'syscall', label: 'System Calls', category: 'concept', description: 'Interface between user and kernel mode', link: '/docs/concepts/system-calls' },
    { id: 'vm', label: 'Virtual Memory', category: 'concept', description: 'Memory abstraction with demand paging', link: '/docs/concepts/virtual-memory' },
    { id: 'sync', label: 'Synchronization', category: 'concept', description: 'Coordinating concurrent threads', link: '/docs/concepts/threads-and-processes' },

    // Code Locations
    { id: 'thread_c', label: 'thread.c', category: 'code', filePath: 'threads/thread.c', description: 'Thread lifecycle and scheduling implementation' },
    { id: 'synch_c', label: 'synch.c', category: 'code', filePath: 'threads/synch.c', description: 'Locks, semaphores, condition variables' },
    { id: 'process_c', label: 'process.c', category: 'code', filePath: 'userprog/process.c', description: 'Process creation, fork, exec, exit' },
    { id: 'syscall_c', label: 'syscall.c', category: 'code', filePath: 'userprog/syscall.c', description: 'System call handler and dispatch' },
    { id: 'page_c', label: 'page.c', category: 'code', filePath: 'vm/page.c', description: 'Supplemental page table' },
    { id: 'frame_c', label: 'frame.c', category: 'code', filePath: 'vm/frame.c', description: 'Physical frame management and eviction' },
  ]}
  relations={[
    // Concept relationships
    { from: 'process', to: 'thread', label: 'contains' },
    { from: 'thread', to: 'scheduling', label: 'managed by' },
    { from: 'process', to: 'vm', label: 'has' },
    { from: 'thread', to: 'sync', label: 'uses' },
    { from: 'syscall', to: 'process', label: 'manages' },

    // Code mappings
    { from: 'thread', to: 'thread_c', label: 'impl' },
    { from: 'sync', to: 'synch_c', label: 'impl' },
    { from: 'process', to: 'process_c', label: 'impl' },
    { from: 'syscall', to: 'syscall_c', label: 'impl' },
    { from: 'vm', to: 'page_c', label: 'impl' },
    { from: 'vm', to: 'frame_c', label: 'impl' },
  ]}
/>

## Concept Categories

### Threads & Processes

| Topic | Description | Link |
|-------|-------------|------|
| **Threads and Processes** | Basic units of execution and resource ownership | [Read more →](/docs/concepts/threads-and-processes) |
| **Context Switching** | How the CPU switches between threads | [Read more →](/docs/concepts/context-switching) |
| **Scheduling** | Priority scheduling and MLFQS algorithms | [Read more →](/docs/concepts/scheduling) |
| **Priority Donation** | Solving priority inversion with locks | [Read more →](/docs/concepts/priority-donation) |

### Memory Management

| Topic | Description | Link |
|-------|-------------|------|
| **Virtual Memory** | Paging, demand loading, and address translation | [Read more →](/docs/concepts/virtual-memory) |

### System Interface

| Topic | Description | Link |
|-------|-------------|------|
| **System Calls** | User-kernel interface via INT 0x30 | [Read more →](/docs/concepts/system-calls) |

## Learning Path

### Beginner

Start with the fundamentals:

1. [Threads and Processes](/docs/concepts/threads-and-processes) - Understand the basic execution units
2. [Context Switching](/docs/concepts/context-switching) - How threads share the CPU

### Intermediate

Build on the basics:

3. [Scheduling](/docs/concepts/scheduling) - Priority scheduling and MLFQS
4. [Priority Donation](/docs/concepts/priority-donation) - Handle priority inversion
5. [System Calls](/docs/concepts/system-calls) - User-kernel boundary

### Advanced

Deep dive into complex topics:

6. [Virtual Memory](/docs/concepts/virtual-memory) - Paging and demand loading

## Deep Dives

For detailed technical analysis:

- [Context Switch Assembly](/docs/deep-dives/context-switch-assembly) - Line-by-line `switch.S` walkthrough
- [Page Fault Handling](/docs/deep-dives/page-fault-handling) - Exception handler flow
- [WAL Crash Recovery](/docs/deep-dives/wal-crash-recovery) - Write-ahead logging details

## Related Projects

Each concept is implemented in a specific project:

| Concept | Project | Key Files |
|---------|---------|-----------|
| Threads, Scheduling, Priority Donation | [Project 1: Threads](/docs/projects/threads/overview) | `thread.c`, `synch.c` |
| Process Lifecycle, System Calls | [Project 2: User Programs](/docs/projects/userprog/overview) | `process.c`, `syscall.c` |
| Virtual Memory, Demand Paging | [Project 3: Virtual Memory](/docs/projects/vm/overview) | `page.c`, `frame.c`, `swap.c` |
| File System, Buffer Cache, WAL | [Project 4: File System](/docs/projects/filesys/overview) | `inode.c`, `cache.c`, `wal.c` |
