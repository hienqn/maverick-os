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