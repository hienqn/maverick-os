# MaverickOS Documentation

This folder contains learning guides and future implementation plans for MaverickOS.

## Overview

MaverickOS is an extended implementation of PintOS, the educational operating system from Stanford/Berkeley's CS162 course. Beyond the standard four projects (Threads, User Programs, Virtual Memory, File System), this implementation adds:

- **Crash consistency** via write-ahead logging
- **RISC-V architecture** support (experimental)
- **VGA display** driver
- **Modern tooling** with Bun/TypeScript

Planned features include a TCP/IP network stack, SMP support, VFS layer, and software RAID.

## Concepts

Educational guides covering OS fundamentals:

| Document | Description |
|----------|-------------|
| [context-switching-guide.md](concepts/context-switching-guide.md) | How context switching works in Pintos |
| [INTERRUPT_FLOWS.md](concepts/INTERRUPT_FLOWS.md) | Interrupts, exceptions, and syscall handling |
| [vm-concepts.md](concepts/vm-concepts.md) | Virtual memory concepts and page tables |
| [PROJECT3_FILESYSTEM_GUIDE.md](concepts/PROJECT3_FILESYSTEM_GUIDE.md) | Comprehensive filesystem tutorial |
| [APPEND_ONLY_LOGS.md](concepts/APPEND_ONLY_LOGS.md) | Append-only log and journaling concepts |
| [IO_PATTERNS_AND_CONCEPTS.md](concepts/IO_PATTERNS_AND_CONCEPTS.md) | I/O patterns from blocking to io_uring |
| [UART_SERIAL_COMMUNICATION.md](concepts/UART_SERIAL_COMMUNICATION.md) | UART serial port and terminal I/O |
| [network-stack-guide.md](concepts/network-stack-guide.md) | Network stack layers 1-4 guide |
| [transport-layer-concepts.md](concepts/transport-layer-concepts.md) | UDP/TCP transport layer concepts |

## Future Plans

Implementation plans for features not yet built:

| Document | Description |
|----------|-------------|
| [NETWORK_STACK_PLAN.md](future-plans/NETWORK_STACK_PLAN.md) | TCP/IP network stack implementation |
| [VFS_IMPLEMENTATION_PLAN.md](future-plans/VFS_IMPLEMENTATION_PLAN.md) | Virtual Filesystem Switch layer |
| [RAID_Implementation_Plan.md](future-plans/RAID_Implementation_Plan.md) | Software RAID layer |
| [SMP_IMPLEMENTATION_PLAN.md](future-plans/SMP_IMPLEMENTATION_PLAN.md) | Symmetric multiprocessing support |
| [RISCV_PORT_PLAN.md](future-plans/RISCV_PORT_PLAN.md) | RISC-V architecture port |
