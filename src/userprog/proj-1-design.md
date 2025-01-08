# Project 1: User Programs Design Documentation

This project has 4 tasks in total:

1. Argument passing
2. Process control syscalls
3. File operation syscalls
4. Floating point operations

The goal of this document is to outline our group's thought process and plans to build the above features.

## Argument Passing

Currently, ```process_execute``` does not support command-line arguments. A lot of the tests written check for the existence of ```argv[0]```. Because ```argv``` doesn't exist, many tests will fail.

For more context, in the current set-up, ```process_execute``` are invoked from two different paths:

1. From calling [```exec```](../tests/userprog/exec-arg.c) system call in user space, ```exec``` will excecute ```process_execute``` with the arguments passed in from the user space.
2. From kernel tests when it calls ```run``` alongside with a program and its argument. The flow is: [run_actions](../threads/init.c#L136) => [run_task](../threads/init.c#L310) => [process_execute](./process.c#L181)

In both of these paths, ```process_execute``` would just accept whatever passes into the argument and assume that this is all it needs to execute the program, but in reality executing a program would needs more than just a filename. This behavior is undesireable, as it does not allow for any arguments of a program to be added.

We propose a solution to address this, outlined below:

In ```process_execute```, we will change the argument name from ```file_name``` to ```program_with_args``` because the current name would longer be fitting if we were to add arguments.

We will make a copy of the ```file_name``` from ```fn_copy``` page, to pass in ```thread_create```, otherwise its name might also contain arguments for the program. For example, passing ```program a b c``` to ```process_excute``` would mean that ```thread_create``` inherit ```program a b c``` as its thread name. This not is desirable as its thread name should be just ```program```.

In ```start_process```, we will parse the string argument that is passed in as ```aux``` pointer to get ```argc``` (argument count) and ```argv``` (argument vector). Below is the function signature of the parser:

```c
static bool parse_file_name(const char* file_name_, char** argv)
```

```argv``` argument is an address to a pointer. Its pointer points to memory region allocated in the ```start_process``` using ```malloc```. 

After loading the file successfully, we will execute ```prepare_stack``` to set up ```if_.esp``` appropriately. Below is the function signature of ```prepare_stack```

```c
# argc is argument count
# argv is argment vector
# esp is the current stack pointer after loading the program
static bool prepare_stack(int argc, char** argv, void** esp)
```

We will return ```false``` if it fails for whatever reason. ```prepare_stack``` performs the following:

* Step 1. Push all the copy of ```argv``` values on the stack (reverse order)
* Step 2. Align the stack on 16 bytes.
* Step 3. Push ```NULL``` terminator for argv.
* Step 4. Push the pointers to each ```argv[]``` in reverse order.
* Step 5. Push the address of ```argv``` (i.e., ```&argv[0]```)
* Step 6. Push ```argc```
* Step 7. Push a fake return address (this is no use but we have to be compliant with calling convention)

After this, we're ready to exectute user program.

We will also need to make sure we free all the allocated resources, especially ```argv```.

## Process Control Syscalls

### Background

In PintOS, every time a syscall is called from the user space, it executes a readily available function located in [syscall.c](../lib/user/syscall.c) file exposed to user program. All of the system calls is a wrapper for one of these functions ```syscall0```, ```syscall1```, ```syscall1f```, ```syscall2```, ```syscall3```. 

These functions then call the ```system_handler``` registered in [syscall.c](../userprog/syscall.c) in kernel space. The goal of this these functions is to help determine the system call number in [syscall-nr.h](../lib/syscall-nr.h) so that the kernel can figure out how to handler each system call appropriately. For example, when ```SYS_EXIT``` is passed in as a system call number, ```system_handler``` in kernel space will use this number to figure out which function to handle this scenario. 

As it stands, we don't have support for any other system calls except for ```SYS_EXIT```. This section will outline our strategy to add support for more system calls.

### Strategy

Currently, ```system_handler``` in ```syscall.c``` is executed in kernel space, we cannot automatically trust the arguments passed in from the user space when a program contains its arguments because they might be malicious, i.e. an argument pointer to kernel space. Therefore, we must validate these arguments to make sure they're valid.

Because all the system call arguments are either a 1) file descriptor 2) pointer to a file or 3) a buffer with a certain size, we will have three separate functions to handler each type of arguments accordingly, namely ```fd_validator```, ```pointer_validator```, ```buffer_validator```. 

Each system call is different and might have different number of arguments, so our validation logic will compose one or more of the validator above. For example, calling ```write``` system call would require us to validate ```fd``` as the first argument and ```buffer``` pointer as the second argument, but for ```open```, we only need to validate ```file``` pointer. To address this, we will implement a function that leverage the system call number passed in as an argument, and compose the validator appropriately to handle each system call. Note that we assume that we will always be a system call number.

We will define a validation dispatcher

```c
typedef bool (*validate_func)(void* args);

/* Validation dispatcher table */
static validate_func syscall_validators[SYS_CALL_COUNT] = {
    [SYS_HALT] = NULL,                  // No validation needed for halt
    [SYS_EXIT] = validate_exit,         // Custom validation for exit
    [SYS_EXEC] = validate_exec,         // Custom validation for exec
    [SYS_WAIT] = validate_wait,         // Custom validation for wait
    [SYS_CREATE] = validate_create,     // Custom validation for create
    [SYS_REMOVE] = validate_remove,     // Custom validation for remove
    [SYS_OPEN] = validate_open,         // Custom validation for open
    [SYS_READ] = validate_read,         // Custom validation for read
    [SYS_WRITE] = validate_write,       // Custom validation for write
    // Add other syscalls as needed
};
```

Then implement validation for specific syscalls based from the three validators above.

To expand further into the validation logic,

| **Validator Name**              | **Checks**                                                                                           |
|------------------------|-----------------------------------------------------------------------------------------------------|
| **`fd_validator`**    | - Ensures the file descriptor (FD) is within the valid range.                                        |
|                        | - Confirms the FD corresponds to an open file in the process's file descriptor table.               |
| **`pointer_validator`** | - Validates that the pointer is within the user address space.                                      |
|                        | - Checks that the memory region pointed to is accessible only in user space and aligned.                               |
| **`buffer_validator`** | - Ensures the buffer is non-null.                                                                   |
|                        | - Validates that the buffer does not exceed the bounds of user memory.                              |
|                        | - Confirms proper alignment.                                                          |

#### Process Control Signals

For ```practice``` syscall, we return the result of the integer argument with one added.

For ```halt```, we simple call ```shutdown_power_off```

For ```exit```, we already had the implementation, and it will be extended to support other system calls when necessary.

For ```exec```, beside validating the argument as mentioned above, we need to keep synchronization in mind. Speficically, the parent process **must** wait for the child process it finish loading the program before returning. To ensure this, we will add a semaphore called ```program_loading_sem``` in ```process_execute``` function to help with synchronization. We will create declare and initiate this semaphore and pass down as ```aux```, then ```start_process``` will have access to this semaphore to signal when finished loading. 

This semaphore will be initiated in ```process_execute``` at the start before creating a new thread. ```program_loading_sem.signal()``` will be called at ```start_process``` after successfully loading the program. ```program_loading_sem.wait()``` will be called in process_excecute before returning ```pid_t```. This will ensure that the parent process will wait until the program is loaded succesfull by the child process.

To implement, ```wait``` system call, we rely on the scaffolding function ```process_wait.c``` in ```process.c```. Assuming ```process_wait.c``` is implemented and works properly, we can pass the argument from ```wait``` system call to ```process_wait.c```. Therefore, we need to design ```process_wait.c``` properly.

Below are some of the conditions that we need to validate before we even wait for the child process:

1. check if ```child_pid``` is *invalid*, meaning if it's not corresponding to any processes, or if it's not a child of this current process, then terminate with ```-1```. Currently, there's no way to the parent process to know whether a ```child_pid``` is indeed its child. To track this, when the process was created upon calling ```execute```, we will add the ```child_pid``` into a list of the child processes for the current parent process.

2. check if this ```child_pid``` is alway waited on. If it is, then terminate with ```-1``` immediately as well. To determine whether or not this child has already been waited on, we will add a ```waited_on``` flag to the child process in ```pcb``` data structure as showned above. We also need to be careful about race condition when multiple threads could potentially have access to ```waited_on``` at the same time. To ensure only one thread is allowed to update ```waited_on``` at any given time, a lock is needed to ensure proper synchronization. This lock is also used to ensure no data race when adding new child processes or remove one. 

If all of the above conditions are met, we "wait" for a child process to exit with an ```exit_status``` code. To facilitate this, we will add a semaphore in ```struct child_process``` to notify its parent process when it exits.

One more scenario to consider is when the parent process might exit before all of the child processes ever finish. In user program, a ```wait``` system call might be called, but then unexpectedly exits, i.e. forced termination by sending SIGINT, or a user program might not even call ```wait``` at all but spawn many child processes via ```exec```, but then ```exit``` before the child processes exit. We have to handle these scenarios by cleaning up the child processes if the parent process is already exited. 

The new data structure will look something like below

```C
/* Represents a single child process. */
typedef struct child_process {
    pid_t child_pid;            // Process ID of the child
    bool waited_on;             // Whether the parent has waited on this child
    bool parent_exited;         // Whether the parent has exited
    bool exited;                // Whether the child has exited
    int exit_status;            // Exit status of the child
    struct semaphore sem;       // Semaphore to notify the parent of child's exit
    struct list_elem elem;      // List element for struct list
} child_process_t;

/* Represents a process control block. */
struct process {
    uint32_t* pagedir;          // Page directory
    char process_name[16];      // Name of the process
    struct thread* main_thread; // Pointer to the main thread
    struct list child_processes; // List of child processes
    struct lock child_lock;     // Lock for synchronizing access to child_processes
};

```
