---
sidebar_position: 1
---

import CodeWalkthrough from '@site/src/components/CodeWalkthrough';
import MemoryLayout from '@site/src/components/MemoryLayout';

# Context Switch Assembly Deep Dive

This page provides a line-by-line analysis of `switch.S`, the assembly code that performs thread context switches in PintOS.

## Overview

Context switching happens when the scheduler decides to run a different thread. The key challenge: how do we save one thread's execution state and restore another's?

The answer is elegantly simple: **save registers on the stack, switch stacks, restore registers**.

## The switch_threads Function

<CodeWalkthrough
  title="switch_threads in threads/switch.S"
  code={`.globl switch_threads
.func switch_threads
switch_threads:
    # Save caller's register state.
    #
    # Note that the SVR4 ABI allows us to destroy %eax, %ecx, %edx,
    # but requires us to preserve %ebx, %ebp, %esi, %edi.
    #
    pushl %ebx
    pushl %ebp
    pushl %esi
    pushl %edi

    # Get offsetof (struct thread, stack).
    mov thread_stack_ofs, %edx

    # Save current stack pointer to old thread's stack, if any.
    movl SWITCH_CUR(%esp), %eax
    movl %esp, (%eax,%edx,1)

    # Restore stack pointer from new thread's stack.
    movl SWITCH_NEXT(%esp), %ecx
    movl (%ecx,%edx,1), %esp

    # Restore caller's register state.
    popl %edi
    popl %esi
    popl %ebp
    popl %ebx
    ret
.endfunc`}
  steps={[
    { lines: [1, 2, 3], title: 'Function Entry', description: 'switch_threads takes two arguments: cur (current thread) and next (thread to switch to). These are at esp+20 and esp+24 after register pushes.' },
    { lines: [4, 5, 6, 7, 8, 9, 10, 11, 12], title: 'Save Callee-Saved Registers', description: 'The x86 ABI requires functions to preserve ebx, ebp, esi, edi. We push them onto the current stack. After this, esp points 16 bytes lower.' },
    { lines: [14, 15], title: 'Get Stack Offset', description: 'Load the offset of the stack field within struct thread. This is computed at compile time and stored in thread_stack_ofs.' },
    { lines: [17, 18, 19], title: 'Save Current ESP', description: 'Load cur pointer into eax, then store current esp into cur->stack. This saves where we are on the current stack.' },
    { lines: [21, 22, 23], title: 'Load New ESP', description: 'Load next pointer into ecx, then load next->stack into esp. Now we are on the new thread stack!' },
    { lines: [25, 26, 27, 28, 29], title: 'Restore Registers', description: 'Pop the saved registers from the new stack. These were pushed by the new thread when it last called switch_threads.' },
    { lines: [30], title: 'Return', description: 'The ret pops the return address from the new stack and jumps there. We are now running in the new thread context!' },
  ]}
/>

## Stack Layout During Switch

### Before the Switch

<MemoryLayout
  title="Current Thread's Stack (Before Save)"
  regions={[
    {
      name: '...',
      size: '',
      color: '#9ca3af',
      description: 'Higher addresses'
    },
    {
      name: 'return address',
      size: '4 bytes',
      color: '#f59e0b',
      description: 'Where to return after switch_threads'
    },
    {
      name: 'arg: cur',
      size: '4 bytes',
      color: '#3b82f6',
      description: 'First argument (SWITCH_CUR)'
    },
    {
      name: 'arg: next',
      size: '4 bytes',
      color: '#3b82f6',
      description: 'Second argument (SWITCH_NEXT)'
    },
    {
      name: 'ESP →',
      size: '',
      color: '#ef4444',
      description: 'Stack pointer on entry'
    },
  ]}
/>

### After Pushing Registers

<MemoryLayout
  title="Current Thread's Stack (After Push)"
  regions={[
    {
      name: '...',
      size: '',
      color: '#9ca3af',
      description: 'Higher addresses'
    },
    {
      name: 'return address',
      size: '4 bytes',
      color: '#f59e0b',
      description: 'Where to return'
    },
    {
      name: 'arg: cur',
      size: '4 bytes',
      color: '#3b82f6',
      description: 'SWITCH_CUR = esp+20'
    },
    {
      name: 'arg: next',
      size: '4 bytes',
      color: '#3b82f6',
      description: 'SWITCH_NEXT = esp+24'
    },
    {
      name: 'saved %ebx',
      size: '4 bytes',
      color: '#10b981',
      description: 'Callee-saved register'
    },
    {
      name: 'saved %ebp',
      size: '4 bytes',
      color: '#10b981',
      description: 'Callee-saved register'
    },
    {
      name: 'saved %esi',
      size: '4 bytes',
      color: '#10b981',
      description: 'Callee-saved register'
    },
    {
      name: 'saved %edi',
      size: '4 bytes',
      color: '#10b981',
      description: 'Callee-saved register'
    },
    {
      name: 'ESP →',
      size: '',
      color: '#ef4444',
      description: 'Stack pointer after pushes'
    },
  ]}
/>

## The Magic Moment

The key insight is at lines 21-23:

```asm
movl SWITCH_NEXT(%esp), %ecx    # ecx = next (new thread)
movl (%ecx,%edx,1), %esp        # esp = next->stack
```

After this single instruction, **we are on a completely different stack**. Everything below this line operates on the new thread's data.

## The switch_entry Function

When a thread runs for the first time, it doesn't return from `switch_threads` normally. Instead, `thread_create()` sets up the stack so that `switch_threads` "returns" to `switch_entry`:

<CodeWalkthrough
  title="switch_entry in threads/switch.S"
  code={`.globl switch_entry
.func switch_entry
switch_entry:
    # Discard switch_threads() arguments.
    addl $8, %esp

    # Call thread_switch_tail(prev).
    pushl %eax
    call thread_switch_tail
    addl $4, %esp

    # Start thread proper.
    ret
.endfunc`}
  steps={[
    { lines: [1, 2, 3], title: 'Entry Point', description: 'New threads start here after their first context switch.' },
    { lines: [4, 5], title: 'Clean Up Arguments', description: 'Remove the cur and next arguments from the stack (8 bytes total).' },
    { lines: [7, 8, 9, 10], title: 'Call Tail Function', description: 'Call thread_switch_tail with the previous thread (in eax) as argument. This handles cleanup like releasing the scheduler lock.' },
    { lines: [12, 13], title: 'Start the Thread', description: 'The ret instruction pops the kernel_thread function address and jumps there to start the thread.' },
  ]}
/>

## Initial Stack Layout

`thread_create()` sets up a new thread's stack to look like it just called `switch_threads`:

```c
/* In thread_create() */
struct switch_entry_frame *ef;
struct switch_threads_frame *sf;

/* Allocate space on new thread's stack */
ef = alloc_frame(t, sizeof *ef);
ef->eip = (void (*)(void)) kernel_thread;  /* Where to "return" */

sf = alloc_frame(t, sizeof *sf);
sf->eip = switch_entry;  /* "Return address" for switch_threads */
sf->ebp = 0;
sf->ebx = sf->esi = sf->edi = 0;

t->stack = (uint8_t *) sf;  /* Save for switch_threads */
```

<MemoryLayout
  title="New Thread's Initial Stack"
  regions={[
    {
      name: 'struct thread',
      size: '~200 bytes',
      color: '#9ca3af',
      description: 'Thread control block at top of page'
    },
    {
      name: '...',
      size: '',
      color: '#6b7280',
      description: ''
    },
    {
      name: 'kernel_thread addr',
      size: '4 bytes',
      color: '#f59e0b',
      description: 'Where switch_entry returns to'
    },
    {
      name: 'switch_entry addr',
      size: '4 bytes',
      color: '#f59e0b',
      description: 'Where switch_threads "returns" to'
    },
    {
      name: 'ebp = 0',
      size: '4 bytes',
      color: '#10b981',
      description: 'Fake saved ebp'
    },
    {
      name: 'ebx = 0',
      size: '4 bytes',
      color: '#10b981',
      description: 'Fake saved ebx'
    },
    {
      name: 'esi = 0',
      size: '4 bytes',
      color: '#10b981',
      description: 'Fake saved esi'
    },
    {
      name: 'edi = 0',
      size: '4 bytes',
      color: '#10b981',
      description: 'Fake saved edi'
    },
    {
      name: 't->stack →',
      size: '',
      color: '#ef4444',
      description: 'Where switch_threads loads ESP from'
    },
  ]}
/>

## Step-by-Step First Context Switch

1. **Scheduler calls** `switch_threads(cur, new_thread)`
2. **Registers pushed** onto current thread's stack
3. **ESP saved** to `cur->stack`
4. **ESP loaded** from `new_thread->stack` (pointing to fake frame)
5. **Registers popped** (all zeros from fake frame)
6. **ret** pops `switch_entry` address
7. **switch_entry** cleans up, calls `thread_switch_tail`
8. **ret** pops `kernel_thread` address
9. **kernel_thread** calls the actual thread function

## Why This Works

The elegance of this design:

1. **Symmetry**: Every thread that calls `switch_threads` will eventually return from it (when switched back to)
2. **Minimal state**: Only 4 registers + stack pointer needed
3. **No special cases**: New threads work the same as resumed threads
4. **ABI compliance**: Preserves exactly the registers the ABI requires

## Register Responsibilities

| Register | Role in switch_threads |
|----------|------------------------|
| `%eax` | Return value (previous thread) |
| `%ecx` | Temporary for next thread pointer |
| `%edx` | Temporary for stack offset |
| `%ebx`, `%ebp`, `%esi`, `%edi` | Saved/restored across switch |
| `%esp` | Stack pointer (the key!) |

## Common Debugging Issues

### Stack Corruption

If a thread's stack is corrupted, the saved registers or return address may be garbage. Symptoms:
- Random crashes after context switch
- EIP pointing to invalid address
- Stack trace makes no sense

### Magic Number Check

PintOS puts a magic number at the end of each thread struct to detect stack overflow:

```c
/* In thread.h */
#define THREAD_MAGIC 0xcd6abf4b

/* In thread_current() */
ASSERT(t->magic == THREAD_MAGIC);  /* Catch overflow */
```

## Related Topics

- [Context Switching](/docs/concepts/context-switching) - High-level overview
- [Threads and Processes](/docs/concepts/threads-and-processes) - Thread structure
- [Project 1: Threads](/docs/projects/threads/overview) - Implementation guide
