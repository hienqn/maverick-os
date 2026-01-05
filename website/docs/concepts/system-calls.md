---
sidebar_position: 3
---

import AnimatedFlow from '@site/src/components/AnimatedFlow';
import CodeWalkthrough from '@site/src/components/CodeWalkthrough';
import MemoryLayout from '@site/src/components/MemoryLayout';

# System Calls

System calls are the interface between user programs and the operating system kernel. They allow user code to request services that require kernel privileges.

## Why System Calls?

User programs run in **unprivileged mode** and cannot:
- Access hardware directly (disk, network, display)
- Access memory of other processes
- Execute privileged CPU instructions

The kernel runs in **privileged mode** and provides these services. System calls are the controlled gateway between the two worlds.

## The System Call Mechanism

<AnimatedFlow
  title="System Call Flow"
  states={[
    { id: 'user', label: 'User Code', description: 'Running in user mode' },
    { id: 'trap', label: 'Trap (INT 0x30)', description: 'Software interrupt triggers mode switch' },
    { id: 'handler', label: 'Syscall Handler', description: 'Kernel validates and dispatches' },
    { id: 'service', label: 'Kernel Service', description: 'Performs requested operation' },
    { id: 'return', label: 'Return', description: 'Result in EAX, back to user mode' },
  ]}
  transitions={[
    { from: 'user', to: 'trap', label: 'INT 0x30' },
    { from: 'trap', to: 'handler', label: 'mode switch' },
    { from: 'handler', to: 'service', label: 'dispatch' },
    { from: 'service', to: 'return', label: 'IRET' },
    { from: 'return', to: 'user', label: 'continue' },
  ]}
/>

## Stack Layout During Syscall

When a user program makes a system call, arguments are on the user stack:

<MemoryLayout
  title="User Stack at INT 0x30"
  regions={[
    {
      name: '...',
      size: '',
      color: '#9ca3af',
      description: 'Higher addresses'
    },
    {
      name: 'arg3',
      size: '4 bytes',
      color: '#3b82f6',
      description: 'Third argument (at esp+12)'
    },
    {
      name: 'arg2',
      size: '4 bytes',
      color: '#3b82f6',
      description: 'Second argument (at esp+8)'
    },
    {
      name: 'arg1',
      size: '4 bytes',
      color: '#3b82f6',
      description: 'First argument (at esp+4)'
    },
    {
      name: 'syscall_number',
      size: '4 bytes',
      color: '#f59e0b',
      description: 'Syscall ID (at esp+0)'
    },
  ]}
/>

## User-Side Syscall Stub

User programs call wrapper functions that set up the stack and invoke `INT 0x30`:

```c
/* lib/user/syscall.c */

/* Generic 3-argument syscall */
static inline int syscall3(int number, int arg1, int arg2, int arg3) {
  int retval;
  asm volatile(
    "pushl %[arg3]\n"      /* Push arg3 */
    "pushl %[arg2]\n"      /* Push arg2 */
    "pushl %[arg1]\n"      /* Push arg1 */
    "pushl %[number]\n"    /* Push syscall number */
    "int $0x30\n"          /* Trap to kernel */
    "addl $16, %%esp"      /* Clean up stack */
    : "=a" (retval)        /* Output: EAX = return value */
    : [number] "i" (number),
      [arg1] "r" (arg1),
      [arg2] "r" (arg2),
      [arg3] "r" (arg3)
    : "memory"
  );
  return retval;
}

int write(int fd, const void *buffer, unsigned size) {
  return syscall3(SYS_WRITE, fd, (int)buffer, size);
}
```

## Kernel-Side Handler

<CodeWalkthrough
  title="syscall_handler() in userprog/syscall.c"
  code={`static void syscall_handler(struct intr_frame *f) {
  /* Save user ESP for page fault recovery */
  thread_current()->user_esp = f->esp;

  /* Read syscall number and arguments from user stack */
  int *args = (int *)f->esp;
  int syscall_num = args[0];
  int arg1 = args[1];
  int arg2 = args[2];
  int arg3 = args[3];

  switch (syscall_num) {
    case SYS_HALT:
      shutdown_power_off();
      break;

    case SYS_EXIT:
      thread_current()->pcb->exit_code = arg1;
      printf("%s: exit(%d)\\n", thread_name(), arg1);
      process_exit();
      break;

    case SYS_WRITE:
      f->eax = sys_write(arg1, (void *)arg2, (unsigned)arg3);
      break;

    case SYS_READ:
      f->eax = sys_read(arg1, (void *)arg2, (unsigned)arg3);
      break;

    /* ... other syscalls ... */

    default:
      printf("Unknown syscall: %d\\n", syscall_num);
      thread_exit();
  }
}`}
  steps={[
    { lines: [2, 3], title: 'Save User ESP', description: 'Store user stack pointer for page fault handler. If kernel faults on user pointer, this helps identify the situation.' },
    { lines: [5, 6, 7, 8, 9, 10], title: 'Read Arguments', description: 'Arguments are at offsets from ESP. args[0] is syscall number, args[1-3] are arguments.' },
    { lines: [12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31], title: 'Dispatch', description: 'Switch on syscall number and call the appropriate handler. Return value goes in f->eax.' },
  ]}
/>

## User Pointer Validation

**Critical security rule**: The kernel must never trust user-provided pointers.

### Two Approaches

| Approach | Description | Pros | Cons |
|----------|-------------|------|------|
| **Verify-then-use** | Check pointer before access | Explicit | Race conditions possible |
| **Page-fault-based** | Handle faults gracefully | Simpler, handles lazy pages | Requires careful fault handling |

### Page-Fault-Based Validation

This implementation uses page-fault-based validation:

```c
/* In exception.c */
static void page_fault(struct intr_frame *f) {
  void *fault_addr = read_cr2();
  bool in_kernel = (f->error_code & PF_U) == 0;

  /* Kernel accessing invalid user pointer? */
  if (in_kernel && !is_kernel_vaddr(fault_addr)) {
    /* Kill the process gracefully */
    f->eax = -1;
    process_exit();
    return;
  }

  /* Handle legitimate page faults (lazy loading, stack growth) */
  /* ... */
}
```

### Why This Works

1. User passes a bad pointer to `read(fd, bad_ptr, size)`
2. Kernel tries to write to `bad_ptr`
3. Page fault occurs in kernel mode
4. Fault handler sees kernel accessing user address
5. Handler sets `eax = -1` and kills process

## Common System Calls

### Process Calls

| Syscall | Signature | Description |
|---------|-----------|-------------|
| `exit` | `void exit(int status)` | Terminate with exit code |
| `exec` | `pid_t exec(const char *cmd)` | Run new program |
| `wait` | `int wait(pid_t pid)` | Wait for child |
| `fork` | `pid_t fork(void)` | Duplicate process |

### File Calls

| Syscall | Signature | Description |
|---------|-----------|-------------|
| `open` | `int open(const char *file)` | Open file, return fd |
| `close` | `void close(int fd)` | Close file descriptor |
| `read` | `int read(int fd, void *buf, unsigned size)` | Read from file |
| `write` | `int write(int fd, const void *buf, unsigned size)` | Write to file |
| `seek` | `void seek(int fd, unsigned pos)` | Set position |
| `tell` | `unsigned tell(int fd)` | Get position |

### Directory Calls

| Syscall | Signature | Description |
|---------|-----------|-------------|
| `mkdir` | `bool mkdir(const char *dir)` | Create directory |
| `chdir` | `bool chdir(const char *dir)` | Change working directory |
| `readdir` | `bool readdir(int fd, char *name)` | Read directory entry |

## Syscall Numbers

Syscall numbers are defined in `lib/syscall-nr.h`:

```c
/* System call numbers */
enum {
  SYS_HALT,      /* 0: Halt the OS */
  SYS_EXIT,      /* 1: Terminate process */
  SYS_EXEC,      /* 2: Start new process */
  SYS_WAIT,      /* 3: Wait for child */
  SYS_CREATE,    /* 4: Create file */
  SYS_REMOVE,    /* 5: Delete file */
  SYS_OPEN,      /* 6: Open file */
  SYS_FILESIZE,  /* 7: Get file size */
  SYS_READ,      /* 8: Read from file */
  SYS_WRITE,     /* 9: Write to file */
  SYS_SEEK,      /* 10: Set position */
  SYS_TELL,      /* 11: Get position */
  SYS_CLOSE,     /* 12: Close file */
  /* ... more syscalls ... */
};
```

## Error Handling

System calls return `-1` on error (except `void` calls which just fail silently or kill the process):

```c
static int sys_write(int fd, const void *buffer, unsigned size) {
  /* Validate fd */
  if (fd < 0 || fd >= MAX_FD)
    return -1;

  /* Validate buffer pointer (will fault if bad) */
  /* Note: no explicit check, rely on page fault handler */

  /* fd 1 = stdout */
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }

  /* Regular file */
  struct file *f = get_file(fd);
  if (f == NULL)
    return -1;

  return file_write(f, buffer, size);
}
```

## Summary

1. **User code** pushes syscall number and arguments onto stack
2. **INT 0x30** triggers trap to kernel mode
3. **Syscall handler** reads arguments and dispatches to service
4. **Kernel service** validates inputs and performs operation
5. **Return value** stored in `EAX` register
6. **IRET** returns to user mode

## Related Topics

- [Project 2: User Programs](/docs/projects/userprog/overview) - Implement system calls
- [Context Switching](/docs/concepts/context-switching) - How CPU switches between user/kernel mode
