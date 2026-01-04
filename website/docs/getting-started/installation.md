---
sidebar_position: 1
---

# Installation

This guide walks you through setting up the PintOS development environment.

## Prerequisites

You'll need the following tools:

| Tool | Version | Purpose |
|------|---------|---------|
| GCC | 4.x+ | Cross-compiler for i386-elf |
| QEMU | 2.x+ | x86 emulator |
| GNU Make | 3.81+ | Build system |
| Perl | 5.x | Test harness scripts |
| GDB | 7.x+ | Debugging (optional) |

## Platform-Specific Setup

### Ubuntu/Debian

```bash
# Install build tools
sudo apt update
sudo apt install build-essential gcc-multilib qemu-system-x86 perl

# Install debugging tools (optional)
sudo apt install gdb cgdb
```

### macOS

```bash
# Install Homebrew if not present
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install tools
brew install qemu i386-elf-gcc i386-elf-gdb perl

# Add to PATH
echo 'export PATH="/usr/local/opt/i386-elf-gcc/bin:$PATH"' >> ~/.zshrc
```

### Arch Linux

```bash
sudo pacman -S base-devel qemu perl
```

## Building PintOS

### Clone the Repository

```bash
git clone https://github.com/your-org/pintos.git
cd pintos
```

### Configure the Shell

Add PintOS utilities to your PATH:

```bash
# Add to ~/.bashrc or ~/.zshrc
export PATH="$PATH:/path/to/pintos/src/utils"
```

### Build a Project

Each project (threads, userprog, vm, filesys) is built independently:

```bash
# Build the threads project
cd src/threads
make

# Build user programs project
cd ../userprog
make

# Build virtual memory project
cd ../vm
make

# Build file system project
cd ../filesys
make
```

### Verify Installation

Run a simple test to verify everything works:

```bash
cd src/threads/build
pintos --qemu -- run alarm-single
```

You should see output like:

```
Booting from Hard Disk...
Pintos booting with 4,096 kB RAM...
...
(alarm-single) begin
(alarm-single) Creating 5 threads to sleep 1 time each.
(alarm-single) Thread 0 sleeps 10 ticks each time.
...
(alarm-single) end
```

## Common Issues

### "pintos: command not found"

Make sure `src/utils` is in your PATH:

```bash
export PATH="$PATH:$(pwd)/src/utils"
```

### QEMU won't start

Try specifying the QEMU binary explicitly:

```bash
pintos --qemu-path=/usr/bin/qemu-system-i386 -- run alarm-single
```

### Build errors with GCC

Ensure you have a 32-bit capable GCC. On 64-bit systems:

```bash
# Ubuntu/Debian
sudo apt install gcc-multilib

# Verify 32-bit support
echo 'int main(){}' | gcc -m32 -x c - -o /dev/null && echo "32-bit OK"
```

## Next Steps

- [First Run](/docs/getting-started/first-run) - Run your first test
- [Debugging](/docs/getting-started/debugging) - Learn to debug with GDB
- [Project 1: Threads](/docs/projects/threads/overview) - Start the first project
