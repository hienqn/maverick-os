# Pintos Context Switching: A Complete Visual Guide

## 1. Thread Memory Layout (CRITICAL CONCEPT)

Each thread gets exactly **one 4KB page** that holds both its `struct thread` and its kernel stack:

```
┌─────────────────────────────────────┐ ◄── 4 KB (top of page)
│                                     │
│          KERNEL STACK               │
│              ↓                      │
│         grows downward              │
│              ↓                      │
│                                     │
│   ┌─────────────────────────────┐   │ ◄── thread->stack (saved ESP)
│   │   switch_threads_frame      │   │     Points here when thread
│   │   ┌─────────────────────┐   │   │     is NOT running
│   │   │ edi (saved)         │   │   │
│   │   │ esi (saved)         │   │   │
│   │   │ ebp (saved)         │   │   │
│   │   │ ebx (saved)         │   │   │
│   │   │ eip (return addr)   │   │   │
│   │   │ cur (argument)      │   │   │
│   │   │ next (argument)     │   │   │
│   │   └─────────────────────┘   │   │
│   └─────────────────────────────┘   │
│                                     │
├─────────────────────────────────────┤
│              magic                  │ ◄── Stack overflow detection
│              name[16]               │
│              priority               │
│              status                 │
│              tid                    │
│              elem                   │
│              allelem                │
│              stack ─────────────────┼──► Points to saved ESP
│              pcb (if USERPROG)      │
└─────────────────────────────────────┘ ◄── 0 KB (struct thread starts here)
```

**KEY INSIGHT**: The `running_thread()` function finds the current thread by masking off the lower 12 bits of ESP:

```c
// thread.c:403-411
asm("mov %%esp, %0" : "=g"(esp));
return pg_round_down(esp);  // Masks to page boundary
```

---

## 2. Thread States and Transitions

```
                    ┌──────────────────────────────────────────────────────────────┐
                    │                                                              │
                    ▼                                                              │
        ┌───────────────────┐                                                      │
        │                   │                                                      │
        │   THREAD_BLOCKED  │ ◄──────────────────────────────────────────┐         │
        │                   │                                            │         │
        └─────────┬─────────┘                                            │         │
                  │                                                      │         │
                  │ thread_unblock()                                     │         │
                  │ [Moves to ready_list]                                │         │
                  ▼                                                      │         │
        ┌───────────────────┐     schedule()              ┌──────────────┴───────┐ │
        │                   │ ◄────────────────────────── │                      │ │
        │   THREAD_READY    │     [Selected by            │   THREAD_RUNNING     │ │
        │  (on ready_list)  │      scheduler]             │    (only ONE at      │ │
        │                   │ ────────────────────────►   │     any time)        │ │
        └───────────────────┘     switch_threads()        │                      │ │
                  ▲                                       └──────────┬───────────┘ │
                  │                                                  │             │
                  │                                                  │             │
                  │ thread_yield()                                   │             │
                  │ [Current thread gives up CPU]                    │             │
                  └──────────────────────────────────────────────────┘             │
                                                                                   │
                                                         thread_block()            │
                                                         [e.g., sema_down()]       │
                                                         ─────────────────────────►│
                                                                                   │
                                                         thread_exit()             │
                                                         ─────────────────────────►│
                                                                      ▼            │
                                                            ┌───────────────────┐  │
                                                            │   THREAD_DYING    │  │
                                                            │   (cleaned up by  │  │
                                                            │    next thread)   │  │
                                                            └───────────────────┘  │
```

---

## 3. The Context Switch Call Chain (THE HEART OF THREADING)

When a context switch happens, here's the exact sequence:

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  THREAD A (Running)                                                             │
│                                                                                 │
│  1. thread_yield() or thread_block() or thread_exit() called                   │
│     ↓                                                                           │
│  2. intr_disable()  [Disable interrupts - CRITICAL for atomicity]              │
│     ↓                                                                           │
│  3. Set status to THREAD_READY/BLOCKED/DYING                                   │
│     ↓                                                                           │
│  4. schedule() called                                                           │
│     ├─── cur = running_thread()                                                │
│     ├─── next = next_thread_to_run()  [Pick from ready_list]                   │
│     └─── if (cur != next) prev = switch_threads(cur, next)                     │
│                                │                                                │
└────────────────────────────────┼────────────────────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│  switch_threads() in switch.S (ASSEMBLY - THE MAGIC HAPPENS HERE)              │
│                                                                                 │
│  # SAVE Thread A's state to Thread A's stack                                   │
│  pushl %ebx          # Save callee-saved registers                             │
│  pushl %ebp                                                                    │
│  pushl %esi                                                                    │
│  pushl %edi                                                                    │
│                                                                                 │
│  # SAVE Thread A's stack pointer into Thread A's struct                        │
│  mov thread_stack_ofs, %edx                                                    │
│  movl SWITCH_CUR(%esp), %eax      # eax = cur (Thread A)                       │
│  movl %esp, (%eax,%edx,1)         # cur->stack = esp  ◄── CRITICAL LINE!       │
│                                                                                 │
│  ════════════════════════ STACK SWITCH ════════════════════════                │
│                                                                                 │
│  # LOAD Thread B's stack pointer from Thread B's struct                        │
│  movl SWITCH_NEXT(%esp), %ecx     # ecx = next (Thread B)                      │
│  movl (%ecx,%edx,1), %esp         # esp = next->stack  ◄── THE BIG SWITCH!     │
│                                                                                 │
│  # RESTORE Thread B's state from Thread B's stack                              │
│  popl %edi           # Restore callee-saved registers                          │
│  popl %esi                                                                     │
│  popl %ebp                                                                     │
│  popl %ebx                                                                     │
│  ret                 # Return to Thread B's saved EIP                          │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│  THREAD B (Now Running)                                                         │
│                                                                                 │
│  5. thread_switch_tail(prev) called                                            │
│     ├─── cur->status = THREAD_RUNNING                                          │
│     ├─── thread_ticks = 0  [Reset time slice]                                  │
│     ├─── process_activate() [If USERPROG - switch page tables]                 │
│     └─── If prev is DYING, free its page                                       │
│                                                                                 │
│  6. Returns back through schedule() → thread_yield() → ...                     │
│                                                                                 │
│  7. intr_set_level(old_level)  [Re-enable interrupts]                          │
│                                                                                 │
│  Thread B continues execution from where it left off!                          │
└─────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. How a NEW Thread Starts (thread_create)

When you create a new thread, the stack is set up **artificially** to look like it was in the middle of `switch_threads`:

```
thread_create() sets up the stack:

     4 KB ─► ┌─────────────────────────────────────┐
             │                                     │
             │      (empty stack space)            │
             │                                     │
             ├─────────────────────────────────────┤ ◄── kernel_thread_frame
             │  aux      (argument to function)    │
             │  function (thread_func to execute)  │
             │  eip = NULL (fake return address)   │
             ├─────────────────────────────────────┤ ◄── switch_entry_frame
             │  eip = kernel_thread                │
             ├─────────────────────────────────────┤ ◄── switch_threads_frame
             │  ebp = 0                            │
             │  eip = switch_entry ────────────────┼──► First "return" goes here!
     t->stack├─────────────────────────────────────┤
             │                                     │
             │     struct thread fields            │
             │                                     │
       0 KB ─► └─────────────────────────────────────┘
```

**Execution flow for a NEW thread:**

```
1. switch_threads() does its ret instruction
   └── Returns to switch_entry (the fake return address)

2. switch_entry() in switch.S:
   ├── addl $8, %esp          # Discard cur/next arguments
   ├── pushl %eax             # Push prev thread as argument
   ├── call thread_switch_tail # Clean up previous thread
   ├── addl $4, %esp          # Pop argument
   └── ret                    # Returns to kernel_thread

3. kernel_thread():
   ├── intr_enable()          # Enable interrupts (scheduler runs with them OFF)
   ├── function(aux)          # ◄── YOUR THREAD CODE RUNS HERE!
   └── thread_exit()          # If function returns, kill thread
```

---

## 5. Timer-Driven Preemption (How Threads Get Interrupted)

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│                            HARDWARE TIMER (8254 PIT)                             │
│                                                                                  │
│   Fires interrupt 0x20 every 1/TIMER_FREQ seconds (default: 100 Hz = 10ms)      │
└──────────────────────────────────┬───────────────────────────────────────────────┘
                                   │
                                   ▼
┌──────────────────────────────────────────────────────────────────────────────────┐
│  timer_interrupt() [devices/timer.c:129-132]                                     │
│                                                                                  │
│    ticks++;                                                                      │
│    thread_tick();  ──────────────────────────────────────────────────────┐       │
│                                                                          │       │
└──────────────────────────────────────────────────────────────────────────┼───────┘
                                                                           │
                                                                           ▼
┌──────────────────────────────────────────────────────────────────────────────────┐
│  thread_tick() [thread.c:137-153]                                                │
│                                                                                  │
│    Update statistics (idle_ticks, kernel_ticks, user_ticks)                      │
│                                                                                  │
│    if (++thread_ticks >= TIME_SLICE) {   // TIME_SLICE = 4 ticks (40ms)         │
│        intr_yield_on_return();  ◄── Sets yield_on_return = true                 │
│    }                                                                             │
└──────────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌──────────────────────────────────────────────────────────────────────────────────┐
│  intr_handler() [interrupt.c:348-357] - At END of interrupt handling            │
│                                                                                  │
│    if (external) {                                                               │
│        ...                                                                       │
│        if (yield_on_return)                                                      │
│            thread_yield();  ◄── PREEMPTION HAPPENS HERE!                        │
│    }                                                                             │
│                                                                                  │
│    This causes a context switch BEFORE returning to the interrupted thread!     │
└──────────────────────────────────────────────────────────────────────────────────┘
```

---

## 6. KEY CONCEPTS for Project 2 Threading

### What Gets Saved/Restored During Context Switch

| Saved BY switch_threads() | Saved BY CPU on interrupt | NOT saved (caller-saved) |
|---------------------------|---------------------------|--------------------------|
| `%ebx`, `%ebp`, `%esi`, `%edi` | `%eip`, `%cs`, `%eflags`, `%esp`, `%ss` | `%eax`, `%ecx`, `%edx` |
| (callee-saved registers) | (pushed to stack automatically) | (destroyed across calls) |

### Critical Invariants

1. **Interrupts OFF during schedule()**: `switch_threads` and `schedule` run with interrupts disabled. This prevents recursive scheduling.

2. **Only one RUNNING thread**: At any moment, exactly one thread has `status == THREAD_RUNNING`.

3. **Thread finds itself via ESP**: `running_thread()` works by masking ESP to page boundary.

4. **Stack pointer in `thread->stack`**: When NOT running, a thread's ESP is saved in its `stack` field.

### The "Magic" Field

```c
#define THREAD_MAGIC 0xcd6abf4b
```

Located at offset `magic` in struct thread. If stack overflows downward into this, the magic value gets corrupted, and `is_thread()` will fail. This is your **stack overflow detector**.

---

## 7. Visual Timeline of a Context Switch

```
Time ──────────────────────────────────────────────────────────────────────►

Thread A        Thread B                              Thread A        Thread B
[RUNNING]       [READY]                               [READY]         [RUNNING]
    │               │                                     │               │
    │ thread_yield()│                                     │               │
    ▼               │                                     │               │
    ├─ intr_disable()                                     │               │
    ├─ enqueue(A)   │                                     │               │
    ├─ A.status=READY                                     │               │
    ├─ schedule()   │                                     │               │
    │   │           │                                     │               │
    │   ├─ cur=A    │                                     │               │
    │   ├─ next=B ──┘                                     │               │
    │   │                                                 │               │
    │   ▼                                                 │               │
    │  switch_threads(A, B)                               │               │
    │   │                                                 │               │
    │   ├─ push %ebx,%ebp,%esi,%edi  [save A's regs]      │               │
    │   ├─ A->stack = %esp           [save A's stack]     │               │
    │   │                                                 │               │
    │   │══════════════ CONTEXT SWITCH ══════════════════ │               │
    │   │                                                 │               │
    │   ├─ %esp = B->stack           [load B's stack]     │               │
    │   ├─ pop %edi,%esi,%ebp,%ebx   [restore B's regs]   │               │
    │   └─ ret                        [jump to B's eip]   │               │
    │                                                     │               │
    │                                 thread_switch_tail()│               │
    │                                   │                 │               │
    │                                   ├─ B.status=RUNNING               │
    │                                   ├─ thread_ticks=0 │               │
    │                                   └─ (maybe free dying thread)      │
    │                                                     │               │
    ▼                                                     │               ▼
   [A is frozen here,                                   [B continues from
    waiting in ready_list]                               where it left off]
```

---

## 8. Summary: What You Need to Know for Project 2

1. **struct thread** lives at the bottom of a 4KB page; stack grows down from top
2. **thread->stack** stores the saved ESP when a thread is not running
3. **switch_threads()** is pure assembly that saves/restores registers and swaps ESP
4. **Interrupts must be OFF** during the actual switch
5. **New threads** have fake stack frames set up to make them "look like" they were already running
6. **Preemption** happens via timer interrupt → `intr_yield_on_return()` → `thread_yield()`
7. **The magic field** detects stack overflow

This foundation is essential for implementing user-level threading, where you'll need to manage context switches between user processes and handle the transition between user mode and kernel mode.

---

## Key Source Files Reference

| File | Purpose |
|------|---------|
| `src/threads/thread.h` | Thread structure definition and states |
| `src/threads/thread.c` | Thread management functions (create, yield, block, exit) |
| `src/threads/switch.S` | Assembly code for context switching |
| `src/threads/switch.h` | Stack frame structures for context switch |
| `src/threads/interrupt.c` | Interrupt handling and preemption |
| `src/devices/timer.c` | Timer interrupt that drives preemption |
