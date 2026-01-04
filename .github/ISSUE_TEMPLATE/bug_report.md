---
name: Bug Report
about: Report a bug or unexpected behavior
title: '[BUG] '
labels: bug
assignees: ''
---

## Description

A clear and concise description of the bug.

## Component

Which part of PintOS is affected?

- [ ] Threads (scheduling, synchronization)
- [ ] User Programs (syscalls, process management)
- [ ] Virtual Memory (paging, swap, mmap)
- [ ] File System (cache, inodes, directories)
- [ ] Other: ___

## Steps to Reproduce

1. Build component with `cd src/... && make`
2. Run test with `pintos --qemu -- ...`
3. Observe error

## Expected Behavior

What you expected to happen.

## Actual Behavior

What actually happened.

## Test Output

```
Paste relevant test output or error messages here
```

## Environment

- **Emulator**: QEMU / Bochs
- **Host OS**: Linux / macOS / WSL
- **GCC Version**: `i386-elf-gcc --version`

## Additional Context

Any other information that might be helpful.
