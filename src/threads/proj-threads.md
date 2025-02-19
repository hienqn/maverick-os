# **Project 2: Threads and Synchronization**

## **Task 1: Efficient Alarm Clock**

### **Current Issue**
The existing `timer_sleep(int64_t ticks)` uses a **busy-wait loop**:
```c
while (timer_elapsed(start) < ticks)
    thread_yield();
```
**Problems:**
- **Inefficiency:** The thread remains in the ready queue, wasting CPU cycles.
- **Scalability:** Poor performance with many sleeping threads due to frequent `thread_yield()` calls.

---

### **Proposed Solution**
Replace the busy-wait loop with **non-busy waiting** using `thread_block()` and `thread_unblock()`, managed by a **sleep list**.

#### **Key Components**
1. **Sleep List**  
   - **Purpose:** Track sleeping threads and their scheduled wake-up times.
   - **Structure:** A sorted list (ascending by wake time) stored in `timer.c`.
   - **Entry Format:**  
     ```c
     struct sleeping_thread {
         struct thread *thread;   // Blocked thread
         int64_t wake_tick;       // Absolute tick to wake up
         struct list_elem elem;   // List element
     };
     ```

2. **Modified `timer_sleep()`**  
   - **Steps:**  
     1. Calculate `wake_tick = timer_ticks() + ticks`.  
     2. Add the thread to the sleep list (sorted by `wake_tick`).  
     3. Call `thread_block()` to block the thread.  
   - **Atomicity:** Disable interrupts during list insertion and blocking.

3. **Timer Interrupt Handler**  
   - **Steps (on each timer tick):**  
     1. Iterate through the sleep list.  
     2. Unblock threads where `current_tick >= wake_tick` using `thread_unblock()`.  
     3. Remove unblocked threads from the sleep list.  
   - **Efficiency:** Stop iteration at the first non-expired `wake_tick` (list is sorted).

---

#### **Synchronization**
- **Critical Sections:**  
  - Disable interrupts when modifying the sleep list or changing thread states.  
  ```c
  enum intr_level old_level = intr_disable();
  // Modify sleep list or thread state
  intr_set_level(old_level);
  ```
- **Why Atomicity Matters:**  
  Prevents race conditions (e.g., a thread being scheduled mid-blocking).

---

#### **Edge Cases**
1. **Zero-Duration Sleep (`timer_sleep(0)`):**  
   Handle by yielding immediately without blocking.
2. **Thread Termination:**  
   Remove terminated threads from the sleep list to avoid dangling pointers.
3. **Large `ticks` Values:**  
   Use 64-bit arithmetic to prevent overflow.

---

#### **Testing Plan**
1. **Single Thread Sleep:** Verify a thread sleeps exactly `N` ticks.  
2. **Concurrent Threads:** Test multiple threads with different wake times.  
3. **Stress Test:** Create many sleeping threads to validate scalability.  
4. **Edge Cases:**  
   - `timer_sleep(0)`  
   - Thread termination during sleep  

---

#### **Alternatives Considered**
1. **Semaphores:**  
   Overkill for timed sleeps; direct `thread_block()`/`thread_unblock()` is simpler.  
2. **Min-Heap:**  
   More efficient for large N but adds complexity. Start with a sorted list for simplicity.

---

#### **Implementation Steps**
1. Add `sleep_list` to `timer.c`.  
2. Define `struct sleeping_thread`.  
3. Update `timer_sleep()` to block threads and manage the sleep list.  
4. Modify `timer_interrupt()` to unblock expired threads.  
5. Test edge cases and concurrency.

---

## **Task 2: Strict Priority Scheduler**

### **Current Issue**

Our current FIFO scheduling approach can lead to significant problems. In this model, a long-running task may monopolize the CPU, preventing other tasks from running. This situation results in delayed responses for tasks that need fast processing, giving the system a laggy and unresponsive feel. To address this, we propose switching to a strict priority scheduler. In this system, each thread is assigned a priority, and the scheduler always runs the highest-priority tasks.

### **Proposed Solution**

#### **Basic Scheduling Support**

1. **`thread_schedule_prio`:**  
   - **Current Status:** This function is currently empty.  
   - **Objective:** Implement this function to select the next thread to run based on priority.  
   - **Approach:** Utilize the `list_max` function from `list.c` to retrieve the highest-priority thread. In scenarios where multiple threads share the same priority, `list_max` should return the thread that appears first in the list.  
   - **Note:** The runtime complexity of `list_max` is O(n).

2. **`thread_enqueue`:**  
   - **Current Status:** Priority-based insertion is not yet supported.  
   - **Objective:** Modify the function to insert threads into the queue based on their priority.  
   - **Approach:** Use `list_insert_ordered` to ensure that each element is inserted in its proper order according to its priority.

3. **Modifications to Synchronization Primitives:**  
   - **Objective:** Ensure that lock, condition variable, and semaphore operations adhere to the strict priority scheduling policy.  
   - **Approach:** Update the logic in `cond_broadcast`, `cond_signal`, and `semaphore_up` so that the highest-priority thread is always chosen to run.

#### **Priority Donation**

The current solution does not address the priority donation issue—where a high-priority thread may be forced to wait for a lower-priority thread to release a lock. To mitigate this, priority donation is required.

**Donating Priority:**

- **Lock Owner Tracking:**  
  The lock implementation already includes a `holder` field to track the thread that owns the lock.

- **Tracking Locks and Donors:**  
  In addition, each thread records:
  - The lock it is waiting for (`wait_lock`).  
  - A list of locks it currently holds (`held_locks`).
  - A list of threads that are donating their priority to it (`donors`).

  This is important for handling multiple donation scenarios. For example, suppose:
  - High-priority thread (HP) is waiting for a lock held by MP.
  - Medium-priority thread (MP) is waiting for a lock held by LP.
  
  In order to properly boost the lower-priority chain, we must traverse all the way down to the lowest priority thread that is not waiting for any lock and update their donated priorities accordingly.

```c
struct lock {
  struct thread* holder;      /* Thread holding lock (for debugging). */
  struct semaphore semaphore; /* Binary semaphore controlling access. */
};

struct thread {
  ...
  int priority;              /* Original assigned Priority. */
  int effective_priority;    /* Effective (donated) Priority. */
  struct lock *wait_lock;    /* Current lock that this thread is waiting for. */
  struct list held_locks;    /* Locks currently held by this thread. */
  struct list donors;        /* Threads donating to this thread. */
  ...
}
```

**Implementation Details:**

1. **Acquire Operation**
   - **a. Check Lock Availability:**  
     Call `lock_try_acquire`.  
     If the lock is already held, add the current lock to the thread's waiting list.
   - **b. Donate Priority:**  
     With a reference to the current holder thread, compare priorities.  
     If the holder's priority is lower than the waiting thread's, set (donate) the holder's `effective_priority` accordingly.
   - **c. Recursive Donation:**  
     If the holder thread is itself waiting on a lock (`wait_lock` is set), recursively propagate the donation along the chain, updating `effective_priority` as needed.

2. **Release Operation**
   - **a. Simple Release:**  
     For the simplest case—if the thread holds only one lock—reset its `effective_priority` back to its original `priority` upon releasing the lock.
   - **b. Multi-Lock Scenario:**  
     When multiple locks are held, recalculate the effective priority by:
       - Scanning all held locks to determine the maximum waiter priority.
       - Traversing the list of donors to identify the highest donated priority.
   - **c. Immediate Yield:**  
     If the effective priority drops below that of other threads, yield immediately.
   - **d. Cleanup Donors:**  
     Remove any donor entry if the donating thread no longer exists or is no longer relevant.

Also note that these two operations must be atomic by turning off interrupt.

--

## **Task 3: User Threads **

Here's a concise summary of the requirements and expected behavior:

- **User Threads and 1-1 Mapping:**
  - **Thread Creation:**  
    - Implement a system call `sys_pthread_create` that creates a new user thread.  
    - Each new user thread should have a dedicated kernel thread when it traps into the kernel.
    - Details:
      - 
  - **Thread Termination:**  
    - Implement `sys_pthread_exit` to terminate the calling thread.  
    - If the main thread calls `pthread_exit`, it should wait (join) on all other active threads before the process exits.
  - **Thread Joining:**  
    - Implement `sys_pthread_join` so that one thread can wait for another to finish.
  - **Stub Function:**  
    - Use a stub (e.g., `_pthread_start_stub`) as the entry point for new threads. This function calls the actual thread function and then calls `pthread_exit` when that function returns.

- **User-Level Synchronization:**
  - **Locks:**  
    - Implement `lock_init`, `lock_acquire`, and `lock_release` to provide user-level lock primitives.
  - **Semaphores:**  
    - Implement `sema_init`, `sema_down`, and `sema_up` for user-level semaphores.
  - These synchronization primitives are tied to underlying kernel-level mechanisms and are used by user programs to manage concurrent access.

- **Process Control and Syscall Modifications:**
  - **exec:**  
    - When executing a new process, only the main thread is created. Additional threads must be created using `pthread_create`.
  - **wait:**  
    - Only the calling thread (not the entire process) is suspended while waiting for a child process.
  - **exit:**  
    - When any thread calls `exit`, all user threads should immediately terminate and release their resources.
  - **Exit Codes:**  
    - The process’s exit code is determined based on the termination conditions (e.g., normal exit via `pthread_exit` should result in exit code 0, while exceptions or explicit exit calls can override this).

- **Memory and Stack Management:**
  - Each user thread must have its own stack allocated within the process's virtual address space.  
  - You need to decide how to assign virtual addresses for these multiple stacks, keeping in mind the layout of the process’s memory (user stacks above the heap).

- **Implementation Guidance:**
  - **Incremental Development:**  
    - Start with a basic version that supports thread creation and joining, then add synchronization and process control features.
  - **Resource Management:**  
    - Ensure that all allocated resources are properly freed.
  - **Performance Considerations:**  
    - For switching between threads (as opposed to processes), avoid unnecessary page table changes and TLB flushes.
  - **Console Synchronization:**  
    - The provided console lock (a user-level lock) is essential for ensuring that output from multiple threads does not interleave.

This project essentially requires you to extend Pintos to support multithreaded user programs by implementing both the threading API and the underlying system calls for thread management and synchronization.


----

Let's walk through the full flow with a concrete example—say, when a user program calls `pthread_create`—and see what happens at each step from user space into the kernel and back:

---

### 1. User-Space Initiation

- **User Call:**  
  The user program calls `pthread_create(fun, arg)`.  
  For example:
  ```c
  tid_t tid = pthread_create(thread_function, some_argument);
  ```

- **Wrapper Function:**  
  The `pthread_create` function is a user-level wrapper. It internally calls `sys_pthread_create`, passing in:
  - A pointer to the stub function (`_pthread_start_stub`),
  - The user-specified thread function (`thread_function`),
  - And the argument (`some_argument`).

- **Triggering a System Call:**  
  The wrapper then triggers a system call. In Pintos, this is typically done by issuing a software interrupt or a similar trap mechanism, switching the processor from **user mode** to **kernel mode**.

---

### 2. System Call Entry (User → Kernel Transition)

- **Trap to Kernel:**  
  The CPU switches into kernel mode, and the system call entry point (for example, an interrupt handler in `syscall.c`) is invoked.
  
- **Dispatching the Call:**  
  The system call dispatcher examines the syscall number (which identifies `sys_pthread_create`) and calls the corresponding kernel function, `sys_pthread_create`.

---

### 3. Kernel Thread Creation

- **Creating the Kernel Thread:**  
  Inside `sys_pthread_create`, the kernel calls its internal thread creation function (e.g., `thread_create`) to allocate a new **kernel thread**. This creates a new Thread Control Block (TCB) for the thread.
  
- **Setting the Entry Point:**  
  The kernel thread is set up so that its entry point is the stub function, `_pthread_start_stub`. This means when the thread starts running, it will begin execution at `_pthread_start_stub`.

- **User Stack Setup:**  
  Before or during the thread creation, `setup_thread` is called. Even though it runs in the kernel, it:
  - Allocates physical pages for a **user stack**,
  - Maps these pages into the process’s virtual address space at a chosen location (e.g., just below `PHYS_BASE`),
  - And stores the initial user-mode context (like the stack pointer) into the TCB or an associated structure.

This linking—the TCB containing both kernel-side scheduling info and a pointer/record of the user stack—is what creates the 1:1 mapping between the kernel thread and the user thread.

---

### 4. Scheduling the New Thread

- **Placing in the Ready Queue:**  
  The newly created kernel thread is added to the scheduler’s ready queue.
  
- **Thread Switch:**  
  When the scheduler picks this thread, it will start executing in kernel mode at `_pthread_start_stub`.

---

### 5. Transition Back to User Mode (Inside the New Thread)

- **Entering the Stub:**  
  The `_pthread_start_stub` function runs first. Its responsibilities are:
  - **Call the Actual Thread Function:**  
    It calls the user-specified thread function (`thread_function`) with the provided argument.  
    For example:
    ```c
    void _pthread_start_stub(pthread_fun fun, void* arg) {
      (*fun)(arg); // Transition into user-space code execution.
      pthread_exit(); // Ensure proper termination when the thread function returns.
    }
    ```
  - **User-Mode Context:**  
    Before calling the user function, the context (including the user stack pointer) is set up so that when the function is executed, it runs in **user mode**.

- **Running in User Space:**  
  The actual thread function now executes in user space using the dedicated user stack set up earlier. During its execution, if it makes any system calls, similar traps occur, moving it into the kernel for that duration.

---

### 6. Returning from Kernel to User Space (During Syscalls)

- **Syscall Handling:**  
  Whenever the thread in user space needs to perform an operation that requires kernel privileges (e.g., I/O or memory management), it triggers a system call.
  - The CPU traps into the kernel,
  - The syscall handler processes the request (possibly using the TCB and user context information),
  - And then returns a result.
  
- **Resuming User Execution:**  
  After handling the syscall, the kernel uses the stored user context (including the user stack pointer and registers) to return the thread back to user mode.

---

### 7. Thread Termination

- **Function Returns:**  
  Once `thread_function` completes its execution, control returns to `_pthread_start_stub`.

- **Calling `pthread_exit`:**  
  `_pthread_start_stub` calls `pthread_exit()`, which triggers another system call—`sys_pthread_exit`.
  
- **Cleanup in Kernel:**  
  In `sys_pthread_exit`, the kernel:
  - Cleans up the thread's resources (reclaims the kernel stack, unmaps the user stack, updates the TCB, etc.),
  - Signals any thread that is waiting (via `pthread_join`) for this thread to finish.
  
- **Final Transition:**  
  The kernel then fully terminates the kernel thread, which in turn ends the associated user thread.

---

### 8. Joining Threads (If Another Thread Waits)

- **Calling `pthread_join`:**  
  If another thread calls `pthread_join` on this thread’s ID:
  - It will issue a syscall (e.g., `sys_pthread_join`).
  - The kernel will block the calling thread until the target thread has terminated.
  - Once the target thread is cleaned up, the waiting thread is unblocked and resumes execution in user space.

---

### Summary Flow Diagram

1. **User Space:**
   - `pthread_create(thread_function, arg)`  
     ⮕ calls `sys_pthread_create` (via wrapper)
2. **Transition to Kernel:**
   - Trap → System call handler → `sys_pthread_create`
3. **Kernel Thread Creation:**
   - `thread_create` allocates TCB with `_pthread_start_stub` as entry
   - `setup_thread` allocates and maps a user stack
4. **Thread Scheduling:**
   - Kernel schedules the new thread
5. **Kernel → User Transition in New Thread:**
   - `_pthread_start_stub` runs
   - Sets up user context and calls `thread_function(arg)` in user mode
6. **User Execution:**
   - `thread_function` runs and makes further syscalls as needed
7. **Thread Exit:**
   - `thread_function` returns → `_pthread_start_stub` calls `pthread_exit()`
   - System call `sys_pthread_exit` cleans up and terminates the thread
8. **Joining (if applicable):**
   - Another thread’s call to `pthread_join` triggers a syscall
   - Kernel blocks/unblocks threads accordingly

---

This complete flow ensures that:
- **Each user thread has a dedicated kernel thread** (via the TCB and dedicated user stack setup),
- **System calls transition correctly between user and kernel modes**, and
- **Resources are managed properly during thread creation, execution, and termination**.

Let me know if you need further details on any of these steps!

That’s a common and effective approach. By associating a semaphore with each thread, you can have the joining thread block until the target thread signals its completion. Here are some points to consider:

- **Initialization:**  
  When a thread is created, initialize its semaphore to 0. This semaphore acts as a signal for when the thread has finished execution.

- **sys_pthread_join Behavior:**  
  In `sys_pthread_join`, locate the semaphore associated with the target thread (using its tid). The joining thread then calls `sema_down` on that semaphore, which blocks until the target thread signals.

- **Thread Exit Handling:**  
  When a thread exits (via `sys_pthread_exit`), it should call `sema_up` on its own semaphore. This unblocks any thread waiting in `sys_pthread_join`.

- **Edge Cases:**  
  - Ensure that a thread isn’t joined on more than once if that’s disallowed in your design.  
  - Handle cases where the thread has already terminated—its semaphore might already be upped, so `sema_down` should return immediately.  
  - Consider proper cleanup for the semaphore once it’s no longer needed.

Overall, your idea to use per-thread semaphores to synchronize `pthread_join` is solid and aligns well with common synchronization patterns.

Yes, that's essentially it. Since the core synchronization mechanisms (locks, semaphores, atomic operations, etc.) are already implemented in the kernel, your syscall implementations primarily act as wrappers. Your responsibilities include:

- **Argument Validation:**  
  Ensuring that the user-space pointers or handles provided are valid and correctly mapped.

- **Mapping User Handles to Kernel Objects:**  
  Using the lightweight user-space data structures (handles like `lock_t` or `sema_t`) to reference the actual kernel-level synchronization objects.

- **Calling the Appropriate Kernel Functions:**  
  Once validated, your syscalls simply delegate to the kernel's existing synchronization functions.

So, while the underlying functionality is already in place, you must carefully manage the interface between user space and kernel space, ensuring safe and correct operations.

Exactly. In a multithreaded system, you often need to modify or extend the semantics of process control syscalls to account for multiple threads. Here’s what that might involve:

- **Wait Semantics:**  
  Traditionally, the `wait` syscall waits for a child process (using a process ID, or pid) to terminate. In a multithreaded program, you must ensure that when a thread calls `wait`, only that particular thread is blocked while waiting for the child process, not the entire process.  
  - If you intend to wait for a thread’s termination rather than an entire process, you would use `pthread_join`, which works with thread IDs (tids) instead of pids.  
  - So, if your design calls for waiting on individual threads, you need to make sure you use and validate tids (via `pthread_join`) rather than reusing `wait` for that purpose.

- **Exit Logic:**  
  In a multithreaded process, if any thread calls `exit`, the semantics require that all threads terminate immediately. This means:  
  - You need to propagate the exit event to every thread in the process.  
  - The exit status of the process might depend on the exit codes of individual threads. For example, if the main thread calls `pthread_exit` (which normally would result in an exit code of 0), but another thread terminates with an exception or calls `exit(n)`, the process should exit with the code reflecting that critical event (often a negative code like -1 for exceptions).
  - You must modify the logic so that the kernel collects and manages the statuses of all threads to determine the overall process exit code according to the specified priorities.

In summary, while the kernel already has mechanisms for process control, in a multithreaded context you must adjust:

- **For waiting:**  
  Use thread-specific waiting (with tids and `pthread_join`) rather than process-wide waiting via pids if you’re targeting thread termination.

- **For exit:**  
  Ensure that when one thread signals exit, all threads are terminated and that the final process exit code is chosen by considering the status of each thread.

This separation and careful handling allow the system to correctly manage multithreaded processes.


----
Design

Below is a design summary that outlines the overall approach and key points without going into code details. You can use this as a blueprint to fill in the implementation details.

---

## **Task 3: User Threads**

### **Overview**
Extend Pintos to support multithreaded user programs by implementing both the threading API and the corresponding kernel-level system calls. The goal is to maintain a 1:1 mapping between user threads and kernel threads while providing safe synchronization and proper process control in a multithreaded environment.

---

### **Design Components**

#### **1. User Threads and 1:1 Mapping**

- **Thread Creation:**
  - **Requirement:** Implement a system call (`sys_pthread_create`) to create a new user thread.
  - **Behavior:**
    - Each call creates a new kernel thread (via the internal thread creation mechanism).
    - The new kernel thread’s entry point is set to a stub function (e.g., `_pthread_start_stub`).
    - A dedicated user stack is allocated and mapped into the process’s virtual address space (using a helper like `setup_thread`).
  - **Key Considerations:**
    - The Thread Control Block (TCB) must store both the kernel-level context and a pointer to the user-mode context (including the user stack).

- **Thread Termination:**
  - **Requirement:** Implement `sys_pthread_exit` to terminate the calling thread.
  - **Behavior:**
    - When a thread finishes executing (or the thread function returns), it calls the stub which in turn calls `pthread_exit()`.
    - The exit system call cleans up kernel resources, unmaps the user stack, and signals any waiting threads.
  - **Special Case:**  
    - If the main thread calls `pthread_exit`, it should wait (join) on all other active threads before the process terminates.

- **Thread Joining:**
  - **Requirement:** Implement `sys_pthread_join` so one thread can wait for another’s termination.
  - **Behavior:**
    - Use per-thread semaphores (or similar synchronization mechanisms) to block the joining thread until the target thread completes.
    - Ensure that a thread isn’t joined on more than once and handle the case where the target thread has already terminated.

- **Stub Function (_pthread_start_stub):**
  - **Purpose:** Acts as the entry point for new threads.
  - **Responsibilities:**
    - Transition the thread from kernel-mode (post-creation) to user-mode.
    - Invoke the actual user-specified thread function.
    - Ensure that after the user function returns, `pthread_exit` is called for proper cleanup.

---

#### **2. User-Level Synchronization**

- **Synchronization Primitives:**
  - **Locks:** Implement system calls for `lock_init`, `lock_acquire`, and `lock_release`.
  - **Semaphores:** Implement system calls for `sema_init`, `sema_down`, and `sema_up`.
- **Design Considerations:**
  - User-space synchronization objects (e.g., `lock_t`, `sema_t`) act as lightweight handles or identifiers.
  - The kernel maintains the actual synchronization structures.
  - Your syscall implementations will validate user-space pointers/handles and map them to the kernel-level objects.
  
- **Application (e.g., for pthread_join):**
  - Associate a semaphore with each thread upon creation (initialized to 0).
  - In `sys_pthread_join`, the calling thread performs a `sema_down` on the target thread’s semaphore.
  - Upon termination in `sys_pthread_exit`, the thread signals its semaphore (calls `sema_up`) to unblock joiners.

---

#### **3. Process Control and Syscall Modifications**

- **exec:**
  - **Behavior:** Only create the main thread when a new process is executed.
  - **Implication:** Additional threads must be created via `pthread_create` after process startup.

- **wait:**
  - **Behavior:** Only the calling thread is blocked when waiting for a child process, not the entire process.
  - **Implication:** Use thread-specific waiting (i.e., `pthread_join`) when waiting for thread termination.

- **exit:**
  - **Behavior:** If any thread calls `exit`, all user threads must immediately terminate.
  - **Implication:** Propagate the exit event to all threads and ensure the kernel cleans up resources for every thread.

- **Exit Codes:**
  - **Behavior:** The final process exit code depends on the termination conditions.
    - Normal termination via `pthread_exit` should result in an exit code of 0.
    - Exceptions or explicit exit calls can override this with a different exit code (e.g., -1).
  - **Design:** Collect and prioritize termination statuses from all threads.

---

#### **4. Memory and Stack Management**

- **User Stack Allocation:**
  - Each user thread requires its own stack allocated in the process's virtual address space.
  - Decide on a layout for multiple stacks (typically placed above the heap).
- **Kernel Responsibilities:**
  - Use helper functions (like `setup_thread`) to allocate physical pages.
  - Map these pages to the appropriate virtual addresses for user threads.
  - Store the initial user context (e.g., stack pointer) in the thread's TCB.

---

#### **5. Implementation Guidance**

- **Incremental Development:**
  - Start with a minimal implementation supporting thread creation and joining.
  - Gradually add synchronization primitives and modify process control syscalls.
- **Resource Management:**
  - Ensure that every allocated resource (kernel thread, user stack, synchronization objects) is properly cleaned up on thread termination.
- **Performance Considerations:**
  - Minimize overhead when switching between threads (avoid unnecessary page table switches/TLB flushes when possible).
- **Console Synchronization:**
  - Use the provided console lock (a user-level synchronization object) to prevent output interleaving from multiple threads.

---

### **Execution Flow Summary**

1. **User-Space Initiation:**
   - A user program calls `pthread_create(thread_function, arg)`.
   - The wrapper function invokes `sys_pthread_create`, passing the thread function, argument, and a pointer to `_pthread_start_stub`.

2. **System Call Transition:**
   - A trap (e.g., software interrupt) transitions execution from user mode to kernel mode.
   - The system call dispatcher calls `sys_pthread_create`.

3. **Kernel Thread Creation:**
   - `sys_pthread_create` creates a new kernel thread (allocates a TCB) with its entry point set to `_pthread_start_stub`.
   - `setup_thread` allocates a dedicated user stack and initializes user-mode context in the TCB.

4. **Thread Scheduling:**
   - The new kernel thread is added to the scheduler’s ready queue.
   - When scheduled, the thread starts executing at `_pthread_start_stub` in kernel mode.

5. **Transition to User Mode:**
   - `_pthread_start_stub` sets up the user-mode context and calls the user thread function.
   - The thread executes in user mode and may perform further system calls as needed.

6. **Thread Termination:**
   - After the user thread function completes, control returns to `_pthread_start_stub`, which calls `pthread_exit()`.
   - `sys_pthread_exit` is invoked to clean up the thread's resources and signal waiting threads (via the per-thread semaphore).

7. **Thread Joining:**
   - A thread calling `pthread_join` triggers `sys_pthread_join`.
   - The system call uses the target thread’s semaphore to block until the target thread signals termination.
   - Once unblocked, the waiting thread resumes execution in user mode.

---

This design ensures that:
- Each user thread is directly mapped to a unique kernel thread.
- Transitions between user and kernel modes occur cleanly and efficiently.
- Synchronization between threads (including joining, locking, and semaphore operations) is handled via kernel-mediated system calls.
- Process control (exec, wait, exit) is adjusted to accommodate the nuances of multithreaded programs.

You now have a high-level design that covers all major components and interactions. Use this summary as your guide to implement and fill in the details as needed.