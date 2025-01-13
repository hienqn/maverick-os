#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static struct semaphore temporary;
static thread_func start_process NO_RETURN;
static thread_func start_pthread NO_RETURN;
static bool load(const char* file_name, void (**eip)(void), void** esp);
bool setup_thread(void (**eip)(void), void** esp);

typedef struct process_args {
  struct semaphore load_program_sem;
  struct semaphore parent_ready_sem;
  bool load_success;
  void* fn_copy;
  struct process* parent_process;
} process_args_t;

/* Get the number of arguments from a file name */
static int get_argc(const char* file_name_) {
  int argc = 0;
  char* save_ptr;
  char* token;

  int file_name_length = strlen(file_name_) + 1;

  char* file_name_copy = (char*)malloc(file_name_length);
  if (!file_name_copy) {
    return 0;
  }

  strlcpy(file_name_copy, file_name_, file_name_length);

  for (token = strtok_r(file_name_copy, " ", &save_ptr); token != NULL;
       token = strtok_r(NULL, " ", &save_ptr)) {
    argc++;
  }

  free(file_name_copy);
  return argc;
}

/* Parse a file name to construct argv to the next stage to prepare the stack */
static bool parse_file_name(const char* file_name_, char** argv) {
  int file_name_length = strlen(file_name_) + 1;
  int argc = 0;
  char* save_ptr;
  char* token;

  char* file_name_copy = (char*)malloc(file_name_length);
  if (!file_name_copy) {
    printf("parse_file_name: Failed to allocate memory");
    return false;
  }

  strlcpy(file_name_copy, file_name_, file_name_length);

  for (token = strtok_r(file_name_copy, " ", &save_ptr); token != NULL;
       token = strtok_r(NULL, " ", &save_ptr)) {
    argv[argc] = (char*)malloc(strlen(token) + 1);
    if (!argv[argc]) {
      printf("parse_file_name: Failed to allocate memory for token");
      free(file_name_copy);
      return false;
    }
    memcpy(argv[argc], token, strlen(token) + 1);
    argc++;
  }

  free(file_name_copy);

  return true;
}

static bool debug_prepare_stack = false; // Off by default

static void debug_print(const char* fmt, ...) {
  if (!debug_prepare_stack) {
    return; // No-op if the flag is off
  }
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

static bool prepare_stack(int argc, char** argv, void** esp) {
  uint32_t* argv_address[argc];

  debug_print("[DEBUG] Step 0: Initial esp = %p\n", (void*)*esp);

  /* Step 1. Push all the argv values on the stack (reverse order) */
  for (int i = argc - 1; i >= 0; i--) {
    size_t len = strlen(argv[i]) + 1;
    *esp -= len;
    memcpy(*esp, argv[i], len);
    argv_address[i] = (uint32_t*)*esp;

    debug_print("[DEBUG] Step 1: Pushed argv[%d] = \"%s\"\n", i, argv[i]);
    debug_print("[DEBUG]          Copied %zu bytes at esp = %p\n", len, (void*)*esp);
    debug_print("[DEBUG]          argv_address[%d] = %p\n", i, (void*)argv_address[i]);
  }

  /* Step 2. Align the stack on 16 bytes. */
  size_t pointer_size = sizeof(void*);
  size_t total_size = (argc + 3) * pointer_size;
  size_t misalignment = ((uintptr_t)*esp - total_size) % 16;
  *esp -= misalignment;
  memset(*esp, 0, misalignment);

  debug_print("[DEBUG] Step 2: Applied %zu bytes of padding; esp now = %p\n", misalignment,
              (void*)*esp);

  /* Step 3. Push NULL terminator for argv. */
  *esp = (uint32_t*)*esp - 1;
  *(uint32_t*)*esp = 0;
  debug_print("[DEBUG] Step 3: Pushed NULL terminator; esp now = %p\n", (void*)*esp);

  /* Step 4. Push the pointers to each argv[] in reverse order. */
  for (int i = argc - 1; i >= 0; i--) {
    *esp = (uint32_t*)*esp - 1;
    *(uint32_t*)*esp = (uint32_t)argv_address[i];

    debug_print("[DEBUG] Step 4: Pushed pointer argv[%d] (%p) at esp=%p\n", i,
                (void*)argv_address[i], (void*)*esp);
  }

  /* Step 5. Push the address of argv (i.e., &argv[0]) */
  *esp = (uint32_t*)*esp - 1;
  *(uint32_t*)*esp = (uint32_t)((uint32_t*)*esp + 1);
  debug_print("[DEBUG] Step 5: Pushed &argv; esp=%p, content=%p\n", (void*)*esp,
              (void*)(*(uint32_t*)*esp));

  /* Step 6. Push argc */
  *esp = (uint32_t*)*esp - 1;
  *(uint32_t*)*esp = argc;
  debug_print("[DEBUG] Step 6: Pushed argc=%d; esp=%p\n", argc, (void*)*esp);

  /* Step 7. Push a fake return address (0) */
  *esp = (uint32_t*)*esp - 1;
  *(uint32_t*)*esp = 0;
  debug_print("[DEBUG] Step 7: Pushed fake return address; esp=%p\n", (void*)*esp);

  return true;
}

/* Initializes user programs in the system by ensuring the main
   thread has a minimal PCB so that it can execute and wait for
   the first user process. Any additions to the PCB should be also
   initialized here if main needs those members */
void userprog_init(void) {
  struct thread* t = thread_current();
  bool success;

  /* Allocate process control block
     It is imoprtant that this is a call to calloc and not malloc,
     so that t->pcb->pagedir is guaranteed to be NULL (the kernel's
     page directory) when t->pcb is assigned, because a timer interrupt
     can come at any time and activate our pagedir */
  t->pcb = calloc(sizeof(struct process), 1);
  success = t->pcb != NULL;
  list_init(&t->pcb->child_processes);
  lock_init(&t->pcb->child_lock);
  t->pcb->p_process = NULL;
  /* Kill the kernel if we did not succeed */
  ASSERT(success);
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   process id, or TID_ERROR if the thread cannot be created. */
pid_t process_execute(const char* file_name) {
  char* fn_copy;
  tid_t tid;
  process_args_t args;

  sema_init(&args.load_program_sem, 0);
  sema_init(&args.parent_ready_sem, 0);

  fn_copy = palloc_get_page(0);
  if (fn_copy == NULL) {
    printf("[DEBUG] Failed to allocate memory for file name copy.\n");
    return TID_ERROR;
  }
  strlcpy(fn_copy, file_name, PGSIZE);
  args.fn_copy = fn_copy;
  args.load_success = false;
  args.parent_process = thread_current()->pcb;

  printf("[DEBUG] Starting process_execute for file: %s\n", file_name);

  tid = thread_create(file_name, PRI_DEFAULT, start_process, &args);

  // If thread creation failed, clean up and return error
  if (tid == TID_ERROR) {
    printf("[DEBUG] Thread creation failed for file: %s\n", file_name);
    palloc_free_page(fn_copy);
    return TID_ERROR;
  }

  // Allocate memory for the child process metadata
  child_process_t* child_process = (child_process_t*)malloc(sizeof(child_process_t));
  if (child_process == NULL) {
    printf("[DEBUG] Failed to allocate memory for child process metadata.\n");
    palloc_free_page(fn_copy);
    return TID_ERROR;
  }

  /* Initialize child process metadata */
  child_process->child_pid = tid;
  child_process->waited_on = false;
  child_process->parent_exited = false;
  child_process->exited = false;
  child_process->exit_status = -1;
  sema_init(&child_process->sem, 0);

  printf("[DEBUG] Before adding child process PID: %d to parent's child list.\n", tid);
  list_push_front(&thread_current()->pcb->child_processes, &child_process->elem);
  printf("[DEBUG] After adding child process PID: %d. Parent's child list size: %d\n", tid,
         list_size(&thread_current()->pcb->child_processes));

  // Signal the child that the parent has finished its setup
  printf("[DEBUG] Signaling child that parent setup is complete.\n");
  sema_up(&args.parent_ready_sem);

  // Wait for the child process to load
  printf("[DEBUG] Waiting for child process to load.\n");
  sema_down(&args.load_program_sem);

  if (!args.load_success) {
    printf("[DEBUG] Failed to load program: %s\n", file_name);
    palloc_free_page(fn_copy);
    return TID_ERROR;
  }

  printf("[DEBUG] Process_execute: Successfully created child process with TID %d\n", tid);
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void start_process(void* args) {
  process_args_t* process_args = (process_args_t*)args;

  // Wait until the parent has added this process to its list
  printf("[DEBUG] Waiting for parent to complete setup.\n");
  sema_down(&process_args->parent_ready_sem); // Ensure the parent has finished setup
  printf("[DEBUG] Parent setup complete. Continuing child initialization.\n");

  char* file_name = (char*)process_args->fn_copy;
  struct process* parent_process = (struct process*)process_args->parent_process;
  struct thread* t = thread_current();
  struct intr_frame if_;
  bool success, pcb_success, argv_success, argc_success;

  /* Figure how argc, success is true only if argc > 0 */
  int argc = get_argc(file_name);
  success = argc_success = argc > 0;

  if (!success) {
    printf("argc cannot be less than 1\n");
  }

  /* Allocate memory for argv */
  char** argv = (char**)malloc(argc * sizeof(void*));
  success = argv_success = argv != NULL;

  if (!argv_success) {
    printf("Failed to allocate memory for argv!");
  }

  if (success) {
    success = parse_file_name(file_name, argv);
  }

  /* Allocate process control block */
  struct process* new_pcb = malloc(sizeof(struct process));
  success = pcb_success = new_pcb != NULL;

  /* Initialize process control block */
  if (success) {
    // Ensure that timer_interrupt() -> schedule() -> process_activate()
    // does not try to activate our uninitialized pagedir
    new_pcb->pagedir = NULL;
    printf("[DEBUG] Initialized new PCB for thread TID: %d\n", t->tid);

    t->pcb = new_pcb;
    printf("[DEBUG] Assigned new PCB to thread TID: %d\n", t->tid);

    t->pcb->p_process = parent_process;
    if (parent_process != NULL) {
      printf("[DEBUG] Linked child process TID: %d to parent process TID: %s\n", t->tid,
             parent_process->process_name);
    } else {
      printf("[DEBUG] Parent process is NULL for child process TID: %d\n", t->tid);
    }

    // Continue initializing the PCB as normal
    t->pcb->main_thread = t;
    strlcpy(t->name, argv[0], strlen(argv[0]) + 1);
    strlcpy(t->pcb->process_name, argv[0], strlen(argv[0]) + 1);

    list_init(&t->pcb->child_processes);
    lock_init(&t->pcb->child_lock);
  }

  /* Initialize interrupt frame and load executable. */
  if (success) {
    memset(&if_, 0, sizeof if_);
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;

    success = load(argv[0], &if_.eip, &if_.esp);
  }

  // Notify the parent that the load process is complete
  process_args->load_success = success;
  sema_up(&process_args->load_program_sem);
  printf("[DEBUG] Notified parent about load status. Success: %d\n", success);

  if (success) {
    success = prepare_stack(argc, argv, &if_.esp);
  }

  /* Free argv under all circumstances because we no longer use it */
  free(argv);

  /* Handle failure with successful PCB malloc. Must free the PCB */
  if (!success && pcb_success) {
    // Avoid race where PCB is freed before t->pcb is set to NULL
    // If this happens, then an unfortunately timed timer interrupt
    // can try to activate the pagedir, but it is now freed memory
    struct process* pcb_to_free = t->pcb;
    t->pcb = NULL;
    free(pcb_to_free);
  }

  /* Clean up. Exit on failure or jump to userspace */
  palloc_free_page(file_name);
  if (!success) {
    sema_up(&temporary); // Ensure any waiting parent is unblocked
    printf("[DEBUG] Child process TID: %d failed to initialize. Exiting.\n", t->tid);
    thread_exit();
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  printf("[DEBUG] Child process TID: %d successfully initialized. Jumping to user process.\n",
         t->tid);
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}

static child_process_t* get_child_process(struct list* child_processes, const pid_t child_pid) {
  // Check if the list is empty
  if (list_empty(child_processes)) {
    printf("[DEBUG] Child process list is empty. No process found for PID: %d\n", child_pid);
    return NULL;
  }

  // Iterate through the list to find the matching child process
  struct list_elem* e;
  for (e = list_begin(child_processes); e != list_end(child_processes); e = list_next(e)) {
    child_process_t* c_process = list_entry(e, child_process_t, elem);
    printf("[DEBUG] Checking child process PID: %d (Looking for PID: %d)\n", c_process->child_pid,
           child_pid);

    // Check if the current child process matches the given PID
    if (c_process->child_pid == child_pid) {
      printf("[DEBUG] Found matching child process for PID: %d\n", child_pid);
      return c_process; // Return the matching child process
    }
  }

  // If no match is found, log the failure
  printf("[DEBUG] No matching child process found for PID: %d\n", child_pid);
  return NULL;
}

static bool validate_child_pid(const pid_t child_pid) {
  // Use get_child_process to check if the child_pid exists
  return get_child_process(&thread_current()->pcb->child_processes, child_pid) != NULL;
}

/* Waits for process with PID child_pid to die and returns its exit status.
   If it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If child_pid is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given PID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int process_wait(pid_t child_pid) {
  struct process* pcb = thread_current()->pcb;

  printf("[DEBUG] Parent process '%s' (TID: %d) is calling process_wait for child PID: %d.\n",
         pcb->process_name, thread_current()->tid, child_pid);

  // Acquire the lock to ensure safe access to child_processes list
  printf("[DEBUG] Acquiring lock for child process list.\n");
  lock_acquire(&pcb->child_lock);

  // Validate child_pid and retrieve the child process entry
  child_process_t* c_process = get_child_process(&pcb->child_processes, child_pid);
  if (c_process == NULL) {
    printf("[DEBUG] Child process with PID %d not found. Possible reasons: invalid PID or not a "
           "child of this process.\n",
           child_pid);
    lock_release(&pcb->child_lock);
    return -1;
  }

  if (c_process->waited_on) {
    printf("[DEBUG] Child process with PID %d has already been waited on.\n", child_pid);
    lock_release(&pcb->child_lock);
    return -1;
  }

  printf("[DEBUG] Child process with PID %d found. Marking as waited on.\n", child_pid);
  // Mark the child as waited on
  c_process->waited_on = true;

  // Release the lock before waiting
  printf("[DEBUG] Releasing lock before waiting for child process to exit.\n");
  lock_release(&pcb->child_lock);

  // Wait for the child to exit
  printf("[DEBUG] Waiting on semaphore for child process with PID %d.\n", child_pid);
  sema_down(&c_process->sem);
  printf("[DEBUG] Child process with PID %d has exited. Resuming execution.\n", child_pid);

  // Retrieve the exit status (already set by process_exit)
  int exit_status = c_process->exit_status;
  printf("[DEBUG] Retrieved exit status (%d) for child process with PID %d.\n", exit_status,
         child_pid);

  // No manual cleanup of c_process; process_exit handles it
  printf("[DEBUG] process_wait successfully completed for child PID %d with exit status %d.\n",
         child_pid, exit_status);

  return exit_status;
}

/* Free the current process's resources. */
void process_exit(const int exit_status) {
  // Log the exit status of the process
  printf("[DEBUG] Process exiting with status: %d\n", exit_status);

  struct thread* cur = thread_current();
  if (cur->pcb == NULL) {
    printf("[DEBUG] Current thread has no PCB. Exiting thread.\n");
    thread_exit();
    NOT_REACHED();
  }

  // Log the current process's thread ID and name
  printf("[DEBUG] Current process TID: %d\n", cur->pcb->main_thread->tid);
  printf("[DEBUG] Current process name: %s\n", cur->pcb->process_name);

  struct process* p_process = cur->pcb->p_process;

  // Log the parent's ID (if parent exists)
  if (p_process) {
    lock_acquire(&p_process->child_lock);
    printf("[DEBUG] Acquired lock on parent's child list.\n");

    // Get the child process entry in the parent's list
    child_process_t* c_process = get_child_process(&p_process->child_processes, cur->tid);
    if (c_process) {
      printf("[DEBUG] Found child process entry for TID: %d\n", cur->pcb->main_thread->tid);
      printf("[DEBUG] Child process PID: %d\n", c_process->child_pid);

      // Set exit status and signal the parent
      c_process->exit_status = exit_status;
      c_process->exited = true;
      printf("[DEBUG] Marked process as exited with status: %d\n", exit_status);

      sema_up(&c_process->sem);
      printf("[DEBUG] Signaled parent process that child process has exited.\n");
    } else {
      printf("[DEBUG] No child process entry found for TID: %d\n", cur->pcb->main_thread->tid);
    }

    lock_release(&p_process->child_lock);
    printf("[DEBUG] Released lock on parent's child list.\n");
  } else {
    printf("[DEBUG] No parent process found for current process.\n");
  }

  // Free current process's resources
  struct process* pcb_to_free = cur->pcb;
  if (pcb_to_free) {
    printf("[DEBUG] Freeing current process's PCB resources.\n");
    cur->pcb = NULL;
    free(pcb_to_free);
  } else {
    printf("[DEBUG] No PCB resources to free.\n");
  }

  printf("[DEBUG] Thread exiting.\n");
  thread_exit();
}

/* Sets up the CPU for running user code in the current
   thread. This function is called on every context switch. */
void process_activate(void) {
  struct thread* t = thread_current();

  /* Activate thread's page tables. */
  if (t->pcb != NULL && t->pcb->pagedir != NULL)
    pagedir_activate(t->pcb->pagedir);
  else
    pagedir_activate(NULL);

  /* Set thread's kernel stack for use in processing interrupts.
     This does nothing if this is not a user process. */
  tss_update();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr {
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr {
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack(void** esp);
static bool validate_segment(const struct Elf32_Phdr*, struct file*);
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable);
static bool validate_child_pid(const pid_t child_pid);
static child_process_t* get_child_process(struct list* child_processes, const pid_t child_pid);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load(const char* file_name, void (**eip)(void), void** esp) {
  struct thread* t = thread_current();
  struct Elf32_Ehdr ehdr;
  struct file* file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pcb->pagedir = pagedir_create();
  if (t->pcb->pagedir == NULL)
    goto done;
  process_activate();

  /* Open executable file. */
  file = filesys_open(file_name);
  if (file == NULL) {
    printf("load: %s: open failed\n", file_name);
    goto done;
  }

  /* Read and verify executable header. */
  if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr ||
      memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 3 ||
      ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024) {
    printf("load: %s: error loading executable\n", file_name);
    goto done;
  }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) {
    struct Elf32_Phdr phdr;

    if (file_ofs < 0 || file_ofs > file_length(file))
      goto done;
    file_seek(file, file_ofs);

    if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
      goto done;
    file_ofs += sizeof phdr;
    switch (phdr.p_type) {
      case PT_NULL:
      case PT_NOTE:
      case PT_PHDR:
      case PT_STACK:
      default:
        /* Ignore this segment. */
        break;
      case PT_DYNAMIC:
      case PT_INTERP:
      case PT_SHLIB:
        goto done;
      case PT_LOAD:
        if (validate_segment(&phdr, file)) {
          bool writable = (phdr.p_flags & PF_W) != 0;
          uint32_t file_page = phdr.p_offset & ~PGMASK;
          uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
          uint32_t page_offset = phdr.p_vaddr & PGMASK;
          uint32_t read_bytes, zero_bytes;
          if (phdr.p_filesz > 0) {
            /* Normal segment.
                     Read initial part from disk and zero the rest. */
            read_bytes = page_offset + phdr.p_filesz;
            zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
          } else {
            /* Entirely zero.
                     Don't read anything from disk. */
            read_bytes = 0;
            zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
          }
          if (!load_segment(file, file_page, (void*)mem_page, read_bytes, zero_bytes, writable))
            goto done;
        } else
          goto done;
        break;
    }
  }

  /* Set up stack. */
  if (!setup_stack(esp))
    goto done;

  /* Start address. */
  *eip = (void (*)(void))ehdr.e_entry;

  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  file_close(file);
  return success;
}

/* load() helpers. */

static bool install_page(void* upage, void* kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Elf32_Phdr* phdr, struct file* file) {
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length(file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr((void*)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr((void*)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable) {
  ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT(pg_ofs(upage) == 0);
  ASSERT(ofs % PGSIZE == 0);

  file_seek(file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) {
    /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /* Get a page of memory. */
    uint8_t* kpage = palloc_get_page(PAL_USER);
    if (kpage == NULL)
      return false;

    /* Load this page. */
    if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes) {
      palloc_free_page(kpage);
      return false;
    }
    memset(kpage + page_read_bytes, 0, page_zero_bytes);

    /* Add the page to the process's address space. */
    if (!install_page(upage, kpage, writable)) {
      palloc_free_page(kpage);
      return false;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
  }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool setup_stack(void** esp) {
  uint8_t* kpage;
  bool success = false;

  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL) {
    success = install_page(((uint8_t*)PHYS_BASE) - PGSIZE, kpage, true);
    if (success)
      *esp = PHYS_BASE;
    else
      palloc_free_page(kpage);
  }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool install_page(void* upage, void* kpage, bool writable) {
  struct thread* t = thread_current();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page(t->pcb->pagedir, upage) == NULL &&
          pagedir_set_page(t->pcb->pagedir, upage, kpage, writable));
}

/* Returns true if t is the main thread of the process p */
bool is_main_thread(struct thread* t, struct process* p) { return p->main_thread == t; }

/* Gets the PID of a process */
pid_t get_pid(struct process* p) { return (pid_t)p->main_thread->tid; }

/* Creates a new stack for the thread and sets up its arguments.
   Stores the thread's entry point into *EIP and its initial stack
   pointer into *ESP. Handles all cleanup if unsuccessful. Returns
   true if successful, false otherwise.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. You may find it necessary to change the
   function signature. */
bool setup_thread(void (**eip)(void) UNUSED, void** esp UNUSED) { return false; }

/* Starts a new thread with a new user stack running SF, which takes
   TF and ARG as arguments on its user stack. This new thread may be
   scheduled (and may even exit) before pthread_execute () returns.
   Returns the new thread's TID or TID_ERROR if the thread cannot
   be created properly.

   This function will be implemented in Project 2: Multithreading and
   should be similar to process_execute (). For now, it does nothing.
   */
tid_t pthread_execute(stub_fun sf UNUSED, pthread_fun tf UNUSED, void* arg UNUSED) { return -1; }

/* A thread function that creates a new user thread and starts it
   running. Responsible for adding itself to the list of threads in
   the PCB.

   This function will be implemented in Project 2: Multithreading and
   should be similar to start_process (). For now, it does nothing. */
static void start_pthread(void* exec_ UNUSED) {}

/* Waits for thread with TID to die, if that thread was spawned
   in the same process and has not been waited on yet. Returns TID on
   success and returns TID_ERROR on failure immediately, without
   waiting.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
tid_t pthread_join(tid_t tid UNUSED) { return -1; }

/* Free the current thread's resources. Most resources will
   be freed on thread_exit(), so all we have to do is deallocate the
   thread's userspace stack. Wake any waiters on this thread.

   The main thread should not use this function. See
   pthread_exit_main() below.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
void pthread_exit(void) {}

/* Only to be used when the main thread explicitly calls pthread_exit.
   The main thread should wait on all threads in the process to
   terminate properly, before exiting itself. When it exits itself, it
   must terminate the process in addition to all necessary duties in
   pthread_exit.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
void pthread_exit_main(void) {}
