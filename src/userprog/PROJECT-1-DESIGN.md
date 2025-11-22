**Author:** Hien Nguyen

**Date:** 11/17/2025

**Status:** In Progress

---

# Problem Statement

This project has three main tasks:

1. Providing `process_execute` with arguments so it can execute the loaded program
2. Implementing the missing system calls: `practice`, `halt`, `exit`, `exec`, `wait`, `fork`, `create`, `remove`, `open`, `filesize`, `read`, `write`, `seek`, `tell`, `close`
3. Providing additional test cases not covered in the provided repository

This design document will address each of these points.

---

# 1. Argument Passing

## Current Implementation Issues

Currently, `process_execute` treats the input string `file_name` strictly as the executable file's name. It passes this string directly to `thread_create`, which subsequently attempts to load a file with that exact name.

This approach has two major deficiencies:
1.  **File Loading Failure**: If `file_name` contains arguments (e.g., `"grep foo bar"`), `filesys_open` fails because no file named `"grep foo bar"` exists.
2.  **Missing Arguments**: Even if the file loaded, the user program would have no access to the command-line arguments, as they are not parsed or placed on the user stack.

## Proposed Design

We will modify `process_execute` and `start_process` to support command-line arguments. This design integrates with the process control structures described in Section 2.

1.  **Command Line Copy**: 
    - Inside `process_execute`, we will allocate a new page of memory (using `palloc_get_page`) and copy the full command line string into it. This ensures the child thread has access to the arguments.

2.  **Thread Name Extraction**:
    - We will parse the first token of the command line to obtain the **executable name**.
    - This name will be passed as the first argument to `thread_create`.

3.  **Context Passing via `process_load_info`**:
    - We will use the `struct process_load_info` (defined in [Process Control System Calls](#1-process-control-system-calls)) to pass arguments to the child.
    - We will initialize this struct on the parent's stack, setting its `cmd_line` member to the allocated copy and its `child_status` member to the new process status.
    - We will pass the address of this struct as the `aux` argument to `thread_create`.

4.  **Process Loading and Stack Setup**:
    - Inside `start_process`, we access the `process_load_info` via the `aux` parameter.
    - We use the `cmd_line` to setup the stack and the executable name to call `load`.
    - We parse the `cmd_line` string (tokenizing by spaces) to determine `argc` and `argv` and push them onto the stack.
    - After loading (success or failure), we signal the parent using the semaphore in `process_load_info`.

## Stack Layout Strategy

We must strictly adhere to the 80x86 calling convention for function arguments. The stack will be prepared as follows, growing downwards from `PHYS_BASE`:

1.  **Argument Data**: Push the actual string content of each argument (including the null terminator) onto the stack.
2.  **Alignment**: Decrement the stack pointer to align it to a 16-byte boundary (padding with null bytes if necessary).
3.  **Argument Pointers (`argv[]`)**: Push the addresses of the argument strings onto the stack.
    - `argv[argc]` (NULL pointer)
    - ...
    - `argv[1]`
    - `argv[0]`
4.  **`argv`**: Push the address of `argv[0]` (the address of the stack location where `argv[0]` is stored).
5.  **`argc`**: Push the argument count (integer).
6.  **Return Address**: Push a fake return address (`0`) to accommodate the `main` function's stack frame.

**Visual Representation:**

| Address | Name | Type | Description |
| :--- | :--- | :--- | :--- |
| `0xC0000000` | `PHYS_BASE` | | Bottom of User Stack |
| ... | `argv[n][...]` | `char` | Argument strings data |
| ... | `argv[0][...]` | `char` | |
| `Align` | | `uint8_t` | Word alignment padding |
| `ESP+12` | `argv[n]` | `char *` | Null Pointer Sentinel |
| ... | ... | ... | ... |
| `ESP+8` | `argv[0]` | `char *` | Address of 1st arg string |
| `ESP+4` | `argv` | `char **` | Address of argv[0] |
| `ESP` | `argc` | `int` | Number of arguments |
| `ESP-4` | `ret addr` | `void *` | Fake return address (0) |

---

# 2. Implementing System Calls

[See separate document for Interrupt Flow details: `INTERRUPT_FLOW_DOC.md`]

## System Call Invocation Flow

When a system call is invoked from user space, several steps occur:

1. The wrapper function in `lib/user/syscall.c` pushes the system call arguments onto the user stack, followed by the system call number
2. It executes the instruction `int $0x30`

This instruction triggers the following:

1. **Privilege Switch:** The CPU transitions from User Mode (Ring 3) to Kernel Mode (Ring 0)
2. **Context Save:** The CPU pushes the hardware context (SS, ESP, EFLAGS, CS, EIP) onto the kernel stack (specified in the TSS)
3. **Jump to Handler:** The CPU jumps to the interrupt handler registered for vector `0x30` (defined in the IDT)

Before the C-based `syscall_handler` runs, an assembly stub (`intr-stubs.S`) pushes the remaining registers (general-purpose registers) onto the stack to complete the `intr_frame`.

The `syscall_handler` can then dispatch execution based on the system call number. The arguments for the system call are located on the user stack. We can access them via the saved user stack pointer, which is stored in the `esp` member of the interrupt frame (`f-&gt;esp`).

## Argument Validation

Since these arguments reside in user space, we must validate them before access. We will use a **software validation** approach (Verify-Before-Access) instead of relying on the MMU exception handler.

For every pointer argument, we will perform the following checks:

1. **User Space Check:** Ensure the pointer points to user address space (below `PHYS_BASE`). We will use the `is_user_vaddr()` helper for this
2. **Mapping Check:** Verify that the address is actually mapped to a physical frame in the current process's page table. We will use `pagedir_get_page(thread_current()->pagedir, addr)` from `userprog/pagedir.c`, which returns a kernel virtual address if mapped, or `NULL` if not
3. **Boundary Check:** Ensure that the data structure being pointed to (e.g., a 4-byte integer or a buffer) does not span across a page boundary into unmapped memory. We will validate both `ptr` and `ptr + size - 1`

## System Call Implementations

### 1. Process Control System Calls

These system calls (`exec`, `wait`, `exit`) require complex synchronization to ensure the parent can retrieve the child's exit status even if the child dies first.

**Data Structures**

We will define a shared structure to track the status of a child process. This structure is allocated on the heap and reference-counted.

```c
struct process_status {
  tid_t tid;                  /* Child's thread ID */
  int exit_code;              /* Exit code (default -1) */
  struct semaphore wait_sem;  /* Semaphore for the parent to wait on */
  struct list_elem elem;      /* Element for the parent's children list */
  int ref_count;              /* Reference count (2 initially: parent + child) */
  bool is_waited_on;          /* Prevents waiting twice */
};

struct process {
  /* Existing members... */
  struct list children;             /* List of struct process_status (children) */
  struct process_status *my_status; /* My own status shared with parent */
};

struct process_load_info {
  const char *cmd_line;             /* Command line to execute */
  struct semaphore *loaded_signal;  /* Semaphore for loading synchronization */
  bool load_success;                /* Result of loading */
  struct process_status *child_status; /* Status struct created by parent */
};
```

**Algorithms**

**A. `exec` (implemented in `process_execute`)**

1.  **Prepare Synchronization**: Allocate a `struct process_load_info` and a `semaphore` on the **stack**.
2.  **Prepare Status**: `malloc` a new `struct process_status`. Initialize `ref_count = 2` (one for parent, one for child), `exit_code = -1`, and `sema_init(&wait_sem, 0)`.
3.  **Create Thread**: Call `thread_create`, passing the `process_load_info` (which points to the new `process_status`).
4.  **Wait for Load**: Parent calls `sema_down` on the stack semaphore.
5.  **Check Result**:
    *   Inside `start_process` (child), if load fails: set `load_success = false`, `sema_up`, and exit.
    *   If load succeeds: child links `my_status` to the passed status, `sema_up`.
6.  **Parent Resume**: If load failed, free `process_status` and return -1. If successful, add `process_status` to `children` list and return the PID.

**B. `wait`**

1.  **Find Child**: Iterate through the current thread's `children` list to find the `process_status` with the matching PID.
    *   If not found, return -1.
2.  **Check Double Wait**: If `is_waited_on` is true, return -1 immediately. Set `is_waited_on = true`.
3.  **Wait**: Call `sema_down(&status->wait_sem)`. This blocks until the child calls `exit`.
4.  **Retrieve Result**: Save `status->exit_code`.
5.  **Cleanup Reference**: Decrement `status->ref_count`. If it hits 0, `free(status)`. Remove the element from the `children` list.
6.  Return the exit code.

**C. `exit`**

1.  **Save Status**: If `my_status` is not NULL (kernel threads might not have it), lock it.
    *   Set `my_status->exit_code = status`.
    *   `sema_up(&my_status->wait_sem)` to unblock parent.
    *   Decrement `my_status->ref_count`. If 0, `free(my_status)`.
2.  **Release Children**: Iterate through `children` list. For each child status:
    *   Decrement its `ref_count`. If 0, `free(child_status)`.
3.  **Process Cleanup**: Close files, destroy page directory, etc.
4.  **Thread Exit**: Call `thread_exit()`.

**D. `fork`**

We will implement `fork` using two main functions: `process_fork` (running in the parent) and `fork_process` (running in the child).

```c
pid_t process_fork(const char *name, struct intr_frame *if_);
```

1.  **`process_fork` (Parent Context)**:
    - Called by the `fork` syscall handler.
    - Receives the parent's current `intr_frame` (snapshot of registers at the time of syscall).
    - Calls `thread_create`, passing the `intr_frame` via a shared struct (on the stack) to the new thread.
    - **Waits** (using a semaphore) for the child to successfully clone the address space.
    - Returns the **child's PID** if successful, or -1 on failure.

2.  **`fork_process` (Child Context)**:
    - Functions similarly to `start_process` but for cloning.
    - **Memory Space**: Calls a helper `duplicate_pagedir` to copy the parent's entire address space (code, data, stack, heap) to the child.
    - **Context Restore**: Copies the parent's passed `intr_frame` into its own local struct.
    - **Return Value**: Manually sets the `eax` register in its `intr_frame` to **0**.
    - **Resume**: Performs an `intr_exit` to jump to user mode, effectively "returning" from the fork syscall as the child.

**Memory Duplication Algorithm (`duplicate_pagedir`)**

To clone the address space, we must iterate over every mapped page in the parent's page directory, allocate a corresponding page for the child, and copy the data.

**Key Address Concepts**:
*   **Parent's Data**: Accessed via the **User Virtual Address** (`upage`). Since the parent's page directory is currently active, dereferencing `upage` reads the parent's data.
*   **Child's Data**: Accessed via the **Kernel Virtual Address** (`kpage`). `palloc_get_page` returns a pointer in kernel space that maps directly to the new physical frame.

**Logic Flow**:
1.  Iterate through all Page Directory Entries (PDEs) in the parent's page directory (up to `PHYS_BASE`).
2.  For each present PDE, iterate through its Page Table Entries (PTEs).
3.  For each present PTE:
    - **Allocate**: Call `palloc_get_page(PAL_USER)` to get a new frame (`kpage`) for the child.
    - **Copy**: `memcpy(kpage, upage, PGSIZE)` to copy data from the parent's user page to the child's new frame.
    - **Map**: Call `pagedir_set_page` to map `upage` to `kpage` in the child's page directory, preserving the writable bit.
4.  **Error Handling**: If allocation fails at any point, return `false`. The caller is responsible for calling `pagedir_destroy` on the child's page directory to free any partial allocations.

```c
/* Helper in pagedir.c */
bool duplicate_pagedir(uint32_t *parent_pd, uint32_t *child_pd) {
  uint32_t *pde;

  /* Iterate over user space pages */
  for (pde = parent_pd; pde < parent_pd + pd_no(PHYS_BASE); pde++) {
    if (*pde & PTE_P) {
      uint32_t *pt = pde_get_pt(*pde);
      uint32_t *pte;

      for (pte = pt; pte < pt + PGSIZE / sizeof *pte; pte++) {
        if (*pte & PTE_P) {
          /* 1. Calculate User Virtual Address (upage) from indices */
          void *upage = (void *) (((pde - parent_pd) << 22) | ((pte - pt) << 12));
          bool writable = (*pte & PTE_W) != 0;

          /* 2. Allocate new frame for child */
          void *kpage = palloc_get_page(PAL_USER);
          if (kpage == NULL) return false;

          /* 3. Copy data: Parent(upage) -> Child(kpage) */
          /* Note: This works because parent_pd is currently active */
          memcpy(kpage, upage, PGSIZE);

          /* 4. Map page in child's PD */
          if (!pagedir_set_page(child_pd, upage, kpage, writable)) {
            palloc_free_page(kpage);
            return false;
          }
        }
      }
    }
  }
  return true;
}
```

### 2. Simple System Calls

These system calls are straightforward and do not require complex synchronization or data structures.

**A. `halt`**

*   **Action**: Shuts down the system.
*   **Implementation**: Call `shutdown_power_off()` (declared in `devices/shutdown.h`).

**B. `practice`**

*   **Action**: Returns the input integer incremented by 1.
*   **Implementation**: Return `args[1] + 1`.
