# Pthread Design Document

## 1. Overview

This design enables multiple user-level threads within a single process, sharing the same address space but with independent stacks and execution contexts.

---

## 2. Data Structures

### 2.1 Additions to `struct process` (PCB)

```c
struct process {
    /* Existing fields... */
    
    /* Threading support */
    struct list threads;              /* List of all threads in this process */
    struct list thread_statuses;      /* List of thread_status for join/cleanup */
    
    /* Exit synchronization (Mesa-style monitor) */
    struct lock exit_lock;            /* Protects all exit-related fields */
    struct condition exit_cond;       /* Signals when thread_count changes */
    int thread_count;                 /* Number of active threads (starts at 1) */
    bool is_exiting;                  /* Process termination initiated */
    int exit_code;                    /* Final exit code for parent */
};
```

### 2.2 New Struct: `thread_status`

Mirrors `process_status` but for threads within a process:

```c
struct thread_status {
    tid_t tid;                        /* Thread's TID */
    struct semaphore join_sem;        /* For pthread_join to wait on */
    struct list_elem elem;            /* Element in PCB's thread_statuses list */
    int ref_count;                    /* 2 initially: process + thread */
    bool is_joined;                   /* Prevents multiple joins */
    void* stack_base;                 /* User stack base (for freeing) */
};
```

### 2.3 New Struct: `pthread_load_info`

Passed from `pthread_execute` to `start_pthread`:

```c
struct pthread_load_info {
    stub_fun sf;                      /* Stub function (calls tf, then pthread_exit) */
    pthread_fun tf;                   /* User's thread function */
    void* arg;                        /* Argument to thread function */
    struct semaphore ready_sem;       /* Signals when child is set up */
    bool success;                     /* Did setup succeed? */
    struct thread_status* status;     /* Status struct for this thread */
};
```

---

## 3. Thread Creation

### 3.1 Flow

```
pthread_create(fun, arg)     [user space]
         │
         │ syscall
         ▼
pthread_execute(stub, fun, arg)
    1. Allocate thread_status
    2. Initialize pthread_load_info
    3. thread_create("pthread", start_pthread, &load_info)
    4. sema_down(&load_info.ready_sem)  ← wait for child setup
    5. Return TID or error
         │
         │ creates
         ▼
start_pthread()              [runs in NEW thread]
    1. Allocate user stack pages
    2. Set up stack with sf, tf, arg
    3. Add self to PCB's thread list
    4. Increment thread_count
    5. Set up interrupt frame (eip = stub function)
    6. sema_up(&load_info.ready_sem)  ← signal parent
    7. intr_exit() → jump to user mode
```

### 3.2 Stub Function

The stub (`_pthread_start_stub`) runs in user space:

```c
void _pthread_start_stub(pthread_fun fun, void* arg) {
    fun(arg);         // Run user's function
    pthread_exit();   // Automatic cleanup when function returns
}
```

---

## 4. Thread Join

### 4.1 `pthread_join(tid)`

```
1. Find thread_status for tid in PCB's thread_statuses list
2. If not found → return TID_ERROR
3. If status->is_joined → return TID_ERROR (already joined)
4. Set status->is_joined = true
5. sema_down(&status->join_sem)  ← block until thread exits
6. Cleanup:
   - ref_count--
   - If ref_count == 0: free stack pages, free thread_status
7. Return tid (success)
```

---

## 5. Thread Exit

### 5.1 `pthread_exit()` — Non-Main Thread

```
1. lock_acquire(&pcb->exit_lock)
2. pcb->thread_count--
3. If thread_count == 0:
     cond_signal(&pcb->exit_cond)  ← wake main if waiting
4. lock_release(&pcb->exit_lock)
5. sema_up(&my_status->join_sem)  ← wake joiner
6. my_status->ref_count--
7. If ref_count == 0: free stack, free status
8. thread_exit()
```

### 5.2 `pthread_exit_main()` — Main Thread Calls pthread_exit

Main must wait for all children, then clean up with exit code 0:

```
1. lock_acquire(&pcb->exit_lock)
2. while (pcb->thread_count > 1):     ← wait for all children
     cond_wait(&pcb->exit_cond, &pcb->exit_lock)
3. lock_release(&pcb->exit_lock)
4. pcb->exit_code = 0
5. cleanup_process()  ← free PCB, pagedir, signal parent
6. thread_exit()
```

---

## 6. Process Exit (Multi-threaded)

### 6.1 `process_exit()` — Called via `exit(n)` syscall

```
1. lock_acquire(&pcb->exit_lock)
2. pcb->is_exiting = true
3. pcb->exit_code = n
4. Wake all joiners (sema_up on all thread_statuses)
5. pcb->thread_count--
6. If I'm main:
     while (thread_count > 0):
       cond_wait(&exit_cond)
     cleanup_process()
   Else (not main):
     lock_release()
     thread_exit()  ← die, let main handle cleanup
```

### 6.2 Interrupt Return Check

Other threads notice `is_exiting` on syscall/interrupt return:

```c
/* On return to user mode: */
if (pcb->is_exiting && is_trap_from_userspace(frame)) {
    if (is_main_thread())
        process_exit();     // Main takes cleanup duty
    else
        pthread_exit();     // Non-main just dies
}
```

---

## 7. Exit Flow Diagrams

### 7.1 Non-Main Thread Calls `exit(n)`

```
    Thread B                           Main Thread                Thread C
    (calls exit)
         │
         ▼
    process_exit()
    lock_acquire(&exit_lock)
    is_exiting = true
    exit_code = n
    wake_all_joiners()
    thread_count-- (3→2)
         │
         ▼
    am I main? → NO
         │
         ▼
    lock_release()
    thread_exit()
         ✗                             (interrupt)              (interrupt)
                                            │                         │
                                            ▼                         ▼
                                    is_exiting == true?        is_exiting == true?
                                            │                         │
                                            ▼                         ▼
                                    is_main? → YES             is_main? → NO
                                            │                         │
                                            ▼                         ▼
                                    process_exit()             pthread_exit()
                                    lock_acquire()             lock_acquire()
                                    while(count > 0)           count-- (2→1)
                                    cond_wait() 💤             lock_release()
                                            │                  thread_exit()
                                            │                        ✗
                                            │◄───────────────────────┐
                                            ▼                        │
                                    (woken when count==0)      (C signaled)
                                            │
                                            ▼
                                    cleanup_process()
                                    thread_exit()
                                            ✗
```

### 7.2 Main Thread Calls `exit(n)`

```
    Main Thread                        Thread B                 Thread C
    (calls exit)
         │
         ▼
    process_exit()
    lock_acquire(&exit_lock)
    is_exiting = true
    exit_code = n
    wake_all_joiners()
    thread_count-- (3→2)
         │
         ▼
    am I main? → YES
         │
         ▼
    while(count > 0) → TRUE
    cond_wait() 💤
         │                          (notices is_exiting)       (notices is_exiting)
         │                                  │                         │
         │                                  ▼                         ▼
         │                          pthread_exit()             pthread_exit()
         │                          count-- (2→1)              count-- (1→0)
         │                          thread_exit()              cond_signal()────┐
         │                                 ✗                   thread_exit()    │
         │                                                            ✗         │
         │◄─────────────────────────────────────────────────────────────────────┘
         ▼
    (wakes, count==0)
    cleanup_process()
    thread_exit()
         ✗
```

### 7.3 Main Calls `pthread_exit()` (Graceful)

```
    Main Thread                        Thread B                 Thread C
         │
         ▼
    pthread_exit_main()
    lock_acquire(&exit_lock)
         │
         ▼
    is_exiting stays FALSE
    (children run naturally!)
         │
         ▼
    while(count > 1) → TRUE
    cond_wait() 💤
         │                           (finishes work)
         │                                  │
         │                                  ▼
         │                           pthread_exit()
         │                           count-- (3→2)
         │                           thread_exit()
         │                                  ✗                  (finishes work)
         │                                                            │
         │                                                            ▼
         │                                                     pthread_exit()
         │                                                     count-- (2→1)
         │◄────────────────────────────────────────────────────cond_signal()
         │                                                     thread_exit()
         │                                                            ✗
         ▼
    (wakes, count==1)
    lock_release()
    exit_code = 0
    cleanup_process()
    thread_exit()
         ✗
```

---

## 8. Synchronization

### 8.1 Mesa-Style Exit Barrier

```
MONITOR: exit_lock + exit_cond

Invariant: Only one thread modifies thread_count at a time

Main waits:    while (thread_count > 0) cond_wait()
Others signal: thread_count--; if (count==0) cond_signal()

Main always does final cleanup (the "janitor")
```

### 8.2 Thread Creation Sync

```
Parent: sema_down(&ready_sem)   ← wait for child setup
Child:  sema_up(&ready_sem)     ← signal when ready
```

### 8.3 Join Sync

```
Joiner:  sema_down(&join_sem)   ← wait for thread to exit
Exiting: sema_up(&join_sem)     ← wake joiner
```

---

## 9. Exit Code Priority

| Priority | Condition | Exit Code |
|----------|-----------|-----------|
| 3 (highest) | Exception | -1 |
| 2 | `exit(n)` called | n |
| 1 (lowest) | Main calls `pthread_exit()` | 0 |

---

## 10. Function Dispatch

```
SYS_EXIT:
    → process_exit()

SYS_PT_EXIT:
    if (is_main_thread())
        → pthread_exit_main()
    else
        → pthread_exit()

ON INTERRUPT RETURN (when is_exiting == true):
    if (is_main_thread())
        → process_exit()  (take over cleanup)
    else
        → pthread_exit()  (just die)
```

---

## 11. Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Main always cleans up | Single owner of PCB, no races on cleanup |
| Mesa-style monitor | Clear, correct synchronization pattern |
| `is_exiting` flag | Lets blocked threads know to die on return |
| Stub function | User code doesn't need to call `pthread_exit()` explicitly |
| `is_joined` flag | Prevents multiple threads joining same target |
| Check on interrupt return | Threads naturally hit kernel, check flag, die |

---

## 12. Simplifying Assumption

> "You may assume that a user thread that enters the kernel never blocks indefinitely."

This means:
- Every thread will eventually return from syscall/interrupt
- We can check `is_exiting` on return to user mode
- No need to track and forcibly wake blocked threads

Without this assumption, we would need to:
- Track every blocking location (semaphore, lock, I/O, etc.)
- Implement cancellation for all blocking operations
- Handle partial operations and cleanup

---

## 13. Function Summary

| Function | Called By | Purpose |
|----------|-----------|---------|
| `pthread_execute()` | Main/any thread | Create new user thread |
| `start_pthread()` | New thread (kernel) | Set up stack, jump to user mode |
| `pthread_join()` | Any thread | Wait for another thread to finish |
| `pthread_exit()` | Non-main thread | Exit current thread |
| `pthread_exit_main()` | Main thread | Wait for children, exit with code 0 |
| `process_exit()` | Any thread via `exit()` | Force-kill all threads, cleanup |

---

## 14. User-Level Synchronization Primitives

### 14.1 Overview

User programs need synchronization primitives (locks and semaphores) to coordinate between threads. Rather than implementing these entirely in user space, we expose kernel-level primitives through syscalls.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           USER SPACE                                    │
│                                                                         │
│   lock_t lock;              sema_t sema;                                │
│   lock_init(&lock);         sema_init(&sema, 1);                        │
│   lock_acquire(&lock);      sema_down(&sema);                           │
│   lock_release(&lock);      sema_up(&sema);                             │
│                                                                         │
│         │                          │                                    │
│         │ syscall                  │ syscall                            │
│         ▼                          ▼                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                          KERNEL SPACE                                   │
│                                                                         │
│   lock_table[lock_id]       sema_table[sema_id]                         │
│        │                          │                                     │
│        ▼                          ▼                                     │
│   struct lock               struct semaphore                            │
│   (kernel primitive)        (kernel primitive)                          │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 14.2 Type Definitions

```c
typedef char lock_t;   /* 1 byte = supports up to 256 locks */
typedef char sema_t;   /* 1 byte = supports up to 256 semaphores */
```

The `lock_t` and `sema_t` are just **identifiers** (indices) that map to kernel-level primitives.

### 14.3 Data Structures in PCB

```c
#define MAX_LOCKS 256
#define MAX_SEMAS 256

struct process {
    /* Existing fields... */
    
    /* User-level synchronization */
    struct lock* lock_table[MAX_LOCKS];     /* Maps lock_t → struct lock* */
    struct semaphore* sema_table[MAX_SEMAS]; /* Maps sema_t → struct semaphore* */
    int next_lock_id;                        /* Next available lock ID */
    int next_sema_id;                        /* Next available semaphore ID */
};
```

### 14.4 Syscall Implementations

#### `lock_init(lock_t* lock)`

```
1. Validate pointer (user memory check)
2. If next_lock_id >= MAX_LOCKS → return false
3. Allocate struct lock in kernel
4. lock_init() on the kernel lock
5. Store in lock_table[next_lock_id]
6. *lock = next_lock_id  ← write ID back to user
7. next_lock_id++
8. Return true
```

#### `lock_acquire(lock_t* lock)`

```
1. Validate pointer
2. id = *lock
3. If id < 0 || id >= next_lock_id → return false
4. If lock_table[id] == NULL → return false
5. lock_acquire(lock_table[id])  ← kernel primitive
6. Return true
```

#### `lock_release(lock_t* lock)`

```
1. Validate pointer
2. id = *lock
3. If id < 0 || id >= next_lock_id → return false
4. If lock_table[id] == NULL → return false
5. If not held by current thread → return false
6. lock_release(lock_table[id])  ← kernel primitive
7. Return true
```

#### `sema_init(sema_t* sema, int val)`

```
1. Validate pointer
2. If next_sema_id >= MAX_SEMAS → return false
3. Allocate struct semaphore in kernel
4. sema_init(sema, val) on kernel semaphore
5. Store in sema_table[next_sema_id]
6. *sema = next_sema_id  ← write ID back to user
7. next_sema_id++
8. Return true
```

#### `sema_down(sema_t* sema)`

```
1. Validate pointer
2. id = *sema
3. If id < 0 || id >= next_sema_id → return false
4. If sema_table[id] == NULL → return false
5. sema_down(sema_table[id])  ← kernel primitive (may block)
6. Return true
```

#### `sema_up(sema_t* sema)`

```
1. Validate pointer
2. id = *sema
3. If id < 0 || id >= next_sema_id → return false
4. If sema_table[id] == NULL → return false
5. sema_up(sema_table[id])  ← kernel primitive
6. Return true
```

### 14.5 User-Space Wrapper Behavior

The user-space wrappers in `lib/user/syscall.c` handle failures:

```c
void lock_acquire(lock_t* lock) {
    bool success = syscall1(SYS_LOCK_ACQUIRE, lock);
    if (!success)
        exit(1);  // Process terminates on failure
}
```

This means:
- `lock_init` / `sema_init` → return bool to user (user handles failure)
- `lock_acquire` / `lock_release` / `sema_down` / `sema_up` → exit on failure

### 14.6 Cleanup on Process Exit

When process exits, free all allocated locks and semaphores:

```c
void cleanup_sync_primitives(struct process* pcb) {
    for (int i = 0; i < pcb->next_lock_id; i++) {
        if (pcb->lock_table[i] != NULL) {
            free(pcb->lock_table[i]);
        }
    }
    for (int i = 0; i < pcb->next_sema_id; i++) {
        if (pcb->sema_table[i] != NULL) {
            free(pcb->sema_table[i]);
        }
    }
}
```

### 14.7 Design Pattern: Handle/Descriptor Pattern

This uses the same **Handle/Descriptor Pattern** as file descriptors:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                                                                         │
│   USER SPACE                        KERNEL SPACE                        │
│                                                                         │
│   ┌─────────┐                       ┌─────────────────────────┐         │
│   │ handle  │ ───── ID (int) ─────► │  table[ID] → real object│         │
│   │ (opaque)│                       └─────────────────────────┘         │
│   └─────────┘                                                           │
│                                                                         │
│   User can't see                    Kernel owns the actual              │
│   or touch the                      data structure                      │
│   real object                                                           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

**Same pattern used throughout the OS:**

| Domain | Handle | Table | Real Object |
|--------|--------|-------|-------------|
| Files | `int fd` | `fd_table[fd]` | `struct file*` |
| Locks | `lock_t` | `lock_table[id]` | `struct lock*` |
| Semaphores | `sema_t` | `sema_table[id]` | `struct semaphore*` |

**Why this pattern?**
- **Security** — User can't forge pointers to kernel memory
- **Abstraction** — User doesn't know internal representation
- **Validation** — Easy bounds check: `if (id >= max) error`
- **Cleanup** — Kernel iterates table to free everything

### 14.8 Design Rationale

| Decision | Rationale |
|----------|-----------|
| ID-based mapping | Simple 1-byte identifier, user can't access kernel memory directly |
| Kernel primitives | Reuse existing, tested `struct lock` and `struct semaphore` |
| Per-process tables | Each process has isolated synchronization primitives |
| Exit on failure | Simplifies user code, failures indicate bugs |
| No re-init handling | Spec says undefined behavior, simplifies implementation |

### 14.9 Syscall Summary

| Syscall | Returns | On Failure |
|---------|---------|------------|
| `SYS_LOCK_INIT` | `bool` | User checks return value |
| `SYS_LOCK_ACQUIRE` | `bool` | User-space wrapper calls `exit(1)` |
| `SYS_LOCK_RELEASE` | `bool` | User-space wrapper calls `exit(1)` |
| `SYS_SEMA_INIT` | `bool` | User checks return value |
| `SYS_SEMA_DOWN` | `bool` | User-space wrapper calls `exit(1)` |
| `SYS_SEMA_UP` | `bool` | User-space wrapper calls `exit(1)` |
