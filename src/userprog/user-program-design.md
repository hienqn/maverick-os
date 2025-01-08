
# **Project 1: User Programs Design Documentation**

This project involves the following tasks:

1. **Argument Passing**  
2. **Process Control Syscalls**  
3. **File Operation Syscalls**  
4. **Floating Point Operations**

This document outlines our thought process and plans for implementing these features.

---

## **1. Argument Passing**

### **Problem**

Currently, `process_execute` does not support command-line arguments. Many tests fail because `argv[0]` is not properly set.  

### **Existing Behavior**

`process_execute` is invoked from two paths:  

1. **From User Space (`exec`):** Arguments are passed directly by the program.  
2. **From Kernel Tests (`run`):** Arguments are passed indirectly via the flow:  
   ```
   run_actions → run_task → process_execute
   ```

In both cases, `process_execute` assumes it only needs the filename, which is insufficient for handling program arguments.

### **Proposed Solution**

#### **Modifications to `process_execute`**
1. **Update Argument Name:** Rename `file_name` to `program_with_args` to reflect its new purpose.  
2. **Extract Program Name:** Use only the program name (`argv[0]`) as the thread name in `thread_create`.

#### **Argument Parsing**
- Implement `parse_file_name` to extract `argc` (argument count) and `argv` (argument vector):  
  ```c
  static bool parse_file_name(const char* program_with_args, char** argv);
  ```
- Allocate memory for `argv` dynamically in `start_process` using `malloc`.

#### **Stack Setup**
- Use `prepare_stack` to set up the stack with arguments:
  ```c
  static bool prepare_stack(int argc, char** argv, void** esp);
  ```
- Steps:
  1. Push argument strings onto the stack in reverse order.  
  2. Align the stack to 16 bytes.  
  3. Push `argv` pointers and `argc`.  
  4. Push a fake return address (required by the calling convention).

#### **Resource Management**
- Ensure all allocated memory for `argv` is freed after use.

---

## **2. Process Control Syscalls**

### **Background**

System calls are handled by `syscall_handler` in `syscall.c`, which identifies the appropriate kernel function based on the syscall number (e.g., `SYS_EXIT`). Currently, only `SYS_EXIT` is supported.

---

### **Strategy**

#### **Validation Dispatcher**
Arguments from user space cannot be trusted and must be validated. We abstract validation into a dispatcher that maps syscall numbers to validation functions.

**Dispatcher Table:**
```c
typedef bool (*validate_func)(void* args);

static validate_func syscall_validators[SYS_CALL_COUNT] = {
    [SYS_HALT] = NULL,                  // No validation needed
    [SYS_EXIT] = validate_exit,         // Custom validation for exit
    [SYS_EXEC] = validate_exec,         // Custom validation for exec
    [SYS_WAIT] = validate_wait,         // Custom validation for wait
    [SYS_CREATE] = validate_create,     // Custom validation for create
    [SYS_REMOVE] = validate_remove,     // Custom validation for remove
    [SYS_OPEN] = validate_open,         // Custom validation for open
    [SYS_READ] = validate_read,         // Custom validation for read
    [SYS_WRITE] = validate_write,       // Custom validation for write
};
```

**Common Validators:**
| Validator               | Purpose                                                                 |
|-------------------------|-------------------------------------------------------------------------|
| **`fd_validator`**      | Validates that the file descriptor (FD) is valid and open.             |
| **`pointer_validator`** | Validates that the pointer is in user space and properly aligned.       |
| **`buffer_validator`**  | Ensures the buffer is valid, non-null, and within user memory bounds.   |

---

### **Process Control Signals**

#### **Implemented Syscalls**
1. **`practice`:** Returns the argument incremented by one.  
2. **`halt`:** Calls `shutdown_power_off`.  
3. **`exit`:** Already implemented; extend as needed.

#### **`exec` Implementation**
- Use a semaphore (`program_loading_sem`) to synchronize parent and child processes:
  - Parent waits (`sema_down`) until the child signals (`sema_up`) that the program is loaded.

---

### **`wait` Implementation**

#### **Validation Steps**
1. **Check `child_pid`:** Ensure it corresponds to a valid child process:
   - Maintain a `child_processes` list to track children.
2. **Prevent Double Waits:**  
   - Use a `waited_on` flag to ensure a child is only waited on once.
   - Protect `waited_on` with a lock to avoid race conditions.

#### **Waiting Mechanism**
- Use a semaphore in `child_process_t` to block the parent until the child exits.
- Return the child’s `exit_status` once it signals completion.

#### **Edge Cases**
- **Parent Exits Before Child:**  
  - Mark the child as orphaned and clean it up when it exits.

---

## **3. Updated Data Structures**

### **Child Process**
```c
typedef struct child_process {
    pid_t child_pid;            // Process ID of the child
    bool waited_on;             // Whether the parent has waited on this child
    bool parent_exited;         // Whether the parent has exited
    bool exited;                // Whether the child has exited
    int exit_status;            // Exit status of the child
    struct semaphore sem;       // Semaphore to notify the parent of child's exit
    struct list_elem elem;      // List element for struct list
} child_process_t;
```

### **Process Control Block**
```c
struct process {
    uint32_t* pagedir;          // Page directory
    char process_name[16];      // Name of the process
    struct thread* main_thread; // Pointer to the main thread
    struct list child_processes; // List of child processes
    struct lock child_lock;     // Lock for synchronizing access to child_processes
};
```

---

## **4. File Operation Syscalls**

### **Planned Implementation**
- Add system calls for `open`, `read`, `write`, and `close`.
- Extend the validation dispatcher for file operations:
  - Use `fd_validator` for file descriptors.
  - Use `buffer_validator` for buffers.

---

## **5. Floating Point Operations**

### **Planned Implementation**
- Enable floating-point instructions during process initialization.
- Add tests to ensure correctness.

---

## **6. Testing Strategy**

### **Test Plan**
1. **Argument Passing:**  
   - Test with simple and complex arguments.  
   - Validate stack layout after `prepare_stack`.

2. **Process Control Syscalls:**  
   - Test `wait` with valid and invalid child PIDs.  
   - Simulate edge cases like orphaned processes.

3. **File Operations:**  
   - Test opening, reading, writing, and closing files.  
   - Validate behavior with invalid file descriptors.

4. **Floating Point Operations:**  
   - Test computations using floating-point instructions.

---

## **Conclusion**