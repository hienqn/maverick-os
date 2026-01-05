---
sidebar_position: 2
---

import AnimatedFlow from '@site/src/components/AnimatedFlow';
import MemoryLayout from '@site/src/components/MemoryLayout';
import CodeWalkthrough from '@site/src/components/CodeWalkthrough';

# Project 2: User Programs

In this project, you'll implement user program execution, including system calls, process management, and argument passing.

## Learning Goals

- Understand how user programs are loaded and executed
- Implement the system call interface between user and kernel
- Build process lifecycle operations: fork, exec, wait, exit
- Handle argument passing via the user stack

## Tasks Overview

| Task | Difficulty | Key Concepts |
|------|------------|--------------|
| Argument Passing | ★★☆ | Stack layout, word alignment |
| System Call Infrastructure | ★★☆ | Interrupt handling, user pointer validation |
| Process Syscalls | ★★★ | fork, exec, wait synchronization |
| File Syscalls | ★★☆ | File descriptors, I/O operations |

## Key Files

| File | Purpose |
|------|---------|
| `userprog/process.c` | Process lifecycle (exec, fork, wait, exit) |
| `userprog/process.h` | Process struct, PCB definition |
| `userprog/syscall.c` | System call dispatcher and handlers |
| `userprog/exception.c` | Page fault handler for user pointer validation |
| `userprog/pagedir.c` | Page directory management |
| `lib/user/syscall.c` | User-side syscall stubs |

## Getting Started

```bash
cd src/userprog
make

# Run a specific test
cd build
make tests/userprog/args-single.result

# Run all tests
make check
```

## Process Lifecycle

<AnimatedFlow
  title="Process State Transitions"
  states={[
    { id: 'new', label: 'NEW', description: 'Process created via exec() or fork()' },
    { id: 'ready', label: 'READY', description: 'Waiting in run queue' },
    { id: 'running', label: 'RUNNING', description: 'Executing on CPU' },
    { id: 'waiting', label: 'WAITING', description: 'Blocked on I/O or child' },
    { id: 'zombie', label: 'ZOMBIE', description: 'Exited, waiting for parent to wait()' },
  ]}
  transitions={[
    { from: 'new', to: 'ready', label: 'admitted' },
    { from: 'ready', to: 'running', label: 'scheduled' },
    { from: 'running', to: 'ready', label: 'preempted' },
    { from: 'running', to: 'waiting', label: 'wait() / I/O' },
    { from: 'waiting', to: 'ready', label: 'child exits / I/O done' },
    { from: 'running', to: 'zombie', label: 'exit()' },
  ]}
/>

## Task 1: Argument Passing

### The Problem

When a program runs with arguments like `grep foo bar.txt`, those arguments must be passed to `main(argc, argv)`. The kernel must:

1. Parse the command line string
2. Push arguments onto the user stack
3. Set up `argc` and `argv` pointers

### Stack Layout

<MemoryLayout
  title="User Stack After Argument Setup"
  regions={[
    {
      name: 'PHYS_BASE',
      size: '0xC0000000',
      color: '#6b7280',
      description: 'Top of user address space'
    },
    {
      name: 'Argument Strings',
      size: '~20 bytes',
      color: '#3b82f6',
      description: '"grep\\0" "foo\\0" "bar.txt\\0"'
    },
    {
      name: 'Padding',
      size: '0-15 bytes',
      color: '#9ca3af',
      description: '16-byte alignment'
    },
    {
      name: 'argv[3] (NULL)',
      size: '4 bytes',
      color: '#10b981',
      description: 'Sentinel'
    },
    {
      name: 'argv[2]',
      size: '4 bytes',
      color: '#10b981',
      description: 'Pointer to "bar.txt"'
    },
    {
      name: 'argv[1]',
      size: '4 bytes',
      color: '#10b981',
      description: 'Pointer to "foo"'
    },
    {
      name: 'argv[0]',
      size: '4 bytes',
      color: '#10b981',
      description: 'Pointer to "grep"'
    },
    {
      name: 'argv',
      size: '4 bytes',
      color: '#f59e0b',
      description: 'char** (points to argv[0])'
    },
    {
      name: 'argc',
      size: '4 bytes',
      color: '#f59e0b',
      description: 'int = 3'
    },
    {
      name: 'Return Address',
      size: '4 bytes',
      color: '#ef4444',
      description: 'Fake (NULL)'
    },
  ]}
/>

### Implementation Steps

1. **Tokenize** the command line by spaces
2. **Push strings** onto the stack (right-to-left)
3. **Align** the stack to 16 bytes
4. **Push pointers** to each string (argv array)
5. **Push argv** (pointer to argv[0])
6. **Push argc** (argument count)
7. **Push fake return address** (0)

### Code Example

```c
/* In start_process(), after load() succeeds */
static bool setup_stack(void **esp, int argc, char *argv[]) {
  uint8_t *stack = (uint8_t *) PHYS_BASE;
  char *arg_ptrs[argc];

  /* 1. Push argument strings (right-to-left) */
  for (int i = argc - 1; i >= 0; i--) {
    size_t len = strlen(argv[i]) + 1;
    stack -= len;
    memcpy(stack, argv[i], len);
    arg_ptrs[i] = (char *) stack;
  }

  /* 2. Word-align to 16 bytes */
  stack = (uint8_t *) ((uintptr_t) stack & ~0xF);

  /* 3. Push NULL sentinel */
  stack -= sizeof(char *);
  *(char **) stack = NULL;

  /* 4. Push argv pointers */
  for (int i = argc - 1; i >= 0; i--) {
    stack -= sizeof(char *);
    *(char **) stack = arg_ptrs[i];
  }

  /* 5. Push argv, argc, fake return */
  char **argv_ptr = (char **) stack;
  stack -= sizeof(char **);
  *(char ***) stack = argv_ptr;
  stack -= sizeof(int);
  *(int *) stack = argc;
  stack -= sizeof(void *);
  *(void **) stack = NULL;

  *esp = stack;
  return true;
}
```

## Task 2: System Call Infrastructure

### How System Calls Work

```
User Program                         Kernel
────────────                         ──────
     │
 push args onto stack
 push syscall number
 int 0x30  ─────────────────────►  syscall_handler()
     │                                   │
     │                             validate args
     │                             dispatch to handler
     │                             store result in eax
     │                                   │
 eax = result  ◄─────────────────  iret
```

### The Syscall Dispatcher

<CodeWalkthrough
  title="syscall_handler() in userprog/syscall.c"
  code={`static void syscall_handler(struct intr_frame *f) {
  /* Save user ESP for page fault handler */
  thread_current()->user_esp = f->esp;

  /* Read syscall number from user stack */
  int syscall_num = *(int *) f->esp;

  /* Read arguments from esp[1], esp[2], etc. */
  int arg1 = *((int *) f->esp + 1);
  int arg2 = *((int *) f->esp + 2);
  int arg3 = *((int *) f->esp + 3);

  /* Dispatch based on syscall number */
  switch (syscall_num) {
    case SYS_EXIT:
      f->eax = arg1;
      process_exit();
      break;
    case SYS_WRITE:
      f->eax = sys_write(arg1, arg2, arg3);
      break;
    /* ... other syscalls ... */
  }
}`}
  steps={[
    { lines: [2, 3], title: 'Save User ESP', description: 'Store user stack pointer so page fault handler can identify kernel faults on user addresses' },
    { lines: [5, 6], title: 'Read Syscall Number', description: 'The syscall number is at esp[0], pushed by user code before int 0x30' },
    { lines: [8, 9, 10, 11], title: 'Read Arguments', description: 'Arguments follow the syscall number on the stack: esp[1], esp[2], esp[3]' },
    { lines: [13, 14, 15, 16, 17, 18, 19, 20, 21, 22], title: 'Dispatch', description: 'Switch on syscall number and call the appropriate handler. Return value goes in f->eax' },
  ]}
/>

### User Pointer Validation

The kernel must **never trust user pointers**. Two approaches:

| Approach | Description | Used By |
|----------|-------------|---------|
| **Check-then-use** | Validate pointer before dereferencing | Linux (older) |
| **Page-fault-based** | Let faults happen, handle in exception handler | This OS, Linux (modern) |

We use **page-fault-based validation**:

```c
/* In exception.c page_fault() */
if (is_kernel_mode && fault_addr < PHYS_BASE) {
  /* Kernel tried to access bad user address */
  f->eax = -1;
  process_exit();  /* Kill the process */
}
```

## Task 3: Process System Calls

### exec()

Creates a new process running a different program:

```c
pid_t exec(const char *cmd_line);

/* Example: exec("grep foo bar.txt") */
```

Implementation flow:
1. Copy command line to kernel
2. Create new thread with `thread_create(start_process, ...)`
3. Wait for child to signal load success/failure
4. Return child PID or -1 on failure

### fork()

Creates a copy of the current process:

```c
pid_t fork(void);

/* Parent gets child PID, child gets 0 */
```

Implementation requires:
1. **Duplicate page directory** (with copy-on-write)
2. **Copy file descriptor table**
3. **Copy interrupt frame** (so child resumes at same point)
4. **Set child's return value to 0** in the copied frame

### wait()

Waits for a child process to exit:

```c
int wait(pid_t child_pid);

/* Returns child's exit code, or -1 if invalid */
```

Key synchronization:
- Parent blocks on `wait_sema` in child's `process_status`
- Child signals parent when calling `exit()`
- Exit code stored in `process_status->exit_code`

### exit()

Terminates the current process:

```c
void exit(int status);
```

Cleanup steps:
1. Set exit code in `process_status`
2. Close all open files
3. Destroy page directory
4. Signal parent via `wait_sema`
5. Free resources and call `thread_exit()`

## Task 4: File System Calls

### File Descriptor Table

Each process has an array of open files:

```c
struct process {
  struct fd_entry fd_table[MAX_FD];  /* 128 entries */
  /* ... */
};

struct fd_entry {
  enum { FD_NONE, FD_FILE, FD_DIR } type;
  union {
    struct file *file;
    struct dir *dir;
  };
};
```

| FD | Purpose |
|----|---------|
| 0 | stdin (keyboard input) |
| 1 | stdout (console output) |
| 2+ | User files |

### Common File Syscalls

```c
int open(const char *file);       /* Returns fd or -1 */
void close(int fd);               /* Close file descriptor */
int read(int fd, void *buf, unsigned size);
int write(int fd, const void *buf, unsigned size);
int filesize(int fd);             /* Get file size */
void seek(int fd, unsigned pos);  /* Set position */
unsigned tell(int fd);            /* Get position */
```

## Testing

### Run All User Program Tests

```bash
cd src/userprog
make check
```

### Key Test Categories

```bash
# Argument passing
make tests/userprog/args-single.result
make tests/userprog/args-multiple.result

# Basic process ops
make tests/userprog/exit.result
make tests/userprog/exec-once.result
make tests/userprog/wait-simple.result

# File operations
make tests/userprog/open-normal.result
make tests/userprog/read-normal.result
make tests/userprog/write-normal.result

# Error handling
make tests/userprog/bad-read.result
make tests/userprog/bad-write.result
```

## Common Issues

### Stack Overflow in Argument Passing

If you push too many arguments or don't track the stack pointer correctly:
- **Symptom**: Random crashes, page faults
- **Fix**: Check that `esp` stays above stack page boundary

### wait() Returning Wrong Value

- **Symptom**: Parent gets wrong exit code or hangs
- **Fix**: Ensure child stores exit code *before* signaling parent

### File Descriptor Leaks

- **Symptom**: Running out of FDs
- **Fix**: Close FDs in `process_exit()`, handle `fork()` fd_table duplication

### Page Fault on User Pointer

- **Symptom**: Kernel crashes when user passes bad pointer
- **Fix**: Use page-fault-based validation; save `user_esp` in syscall_handler

## Next Steps

After completing this project:

- [Project 3: Virtual Memory](/docs/projects/vm/overview) - Demand paging and memory-mapped files
- [System Calls Concept](/docs/concepts/system-calls) - Deep dive into syscall mechanism
