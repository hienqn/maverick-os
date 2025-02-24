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

static thread_func start_process NO_RETURN;
static thread_func start_pthread NO_RETURN;
static bool load(const char* file_name, void (**eip)(void), void** esp);
bool setup_thread(void** esp, int num_threads);

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
  list_init(&t->pcb->all_threads);
  lock_init(&t->pcb->child_lock);
  t->pcb->total_threads = 1;
  t->pcb->p_process = NULL;
  for (int i = 0; i < MAX_FD; i++) {
    t->pcb->fd_table[i] = NULL;
  }
  t->pcb->fd_table[STDIN_FILENO] = NULL;
  t->pcb->fd_table[STDOUT_FILENO] = NULL;
  t->pcb->fd_table[STDERR_FILENO] = NULL;

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
    return TID_ERROR;
  }
  strlcpy(fn_copy, file_name, PGSIZE);
  args.fn_copy = fn_copy;
  args.load_success = false;
  args.parent_process = thread_current()->pcb;

  tid = thread_create(file_name, PRI_DEFAULT, start_process, &args);

  // If thread creation failed, clean up and return error
  if (tid == TID_ERROR) {
    palloc_free_page(fn_copy);
    return TID_ERROR;
  }

  // Allocate memory for the child process metadata
  child_process_t* child_process = (child_process_t*)malloc(sizeof(child_process_t));
  if (child_process == NULL) {
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

  list_push_front(&thread_current()->pcb->child_processes, &child_process->elem);

  // Signal the child that the parent has finished its setup
  sema_up(&args.parent_ready_sem);

  // Wait for the child process to load
  sema_down(&args.load_program_sem);

  if (args.load_success == false) {
    return -1;
  };

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void start_process(void* args) {
  process_args_t* process_args = (process_args_t*)args;

  // Wait until the parent has added this process to its list
  sema_down(&process_args->parent_ready_sem); // Ensure the parent has finished setup

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
    t->pcb = new_pcb;
    t->pcb->p_process = parent_process;

    for (int i = 0; i < MAX_FD; i++) {
      t->pcb->fd_table[i] = NULL;
    }

    t->pcb->fd_table[STDIN_FILENO] = NULL;
    t->pcb->fd_table[STDOUT_FILENO] = NULL;
    t->pcb->fd_table[STDERR_FILENO] = NULL;
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

    if (success) {
      struct file* file = filesys_open(argv[0]);
      if (file == NULL) {
        printf("load: %s: open failed\n", argv[0]);
      }
      file_deny_write(file);
      /* We assign the 3rd index to the currently open file */
      t->pcb->fd_table[DENY_EXECUTABLE] = file;
    }
  }

  // Notify the parent that the load process is complete
  process_args->load_success = success;
  sema_up(&process_args->load_program_sem);

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
    thread_exit();
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}

static child_process_t* get_child_process(struct list* child_processes, const pid_t child_pid) {
  // Check if the list is empty
  if (list_empty(child_processes)) {
    return NULL;
  }

  // Iterate through the list to find the matching child process
  struct list_elem* e;
  for (e = list_begin(child_processes); e != list_end(child_processes); e = list_next(e)) {
    child_process_t* c_process = list_entry(e, child_process_t, elem);
    // Check if the current child process matches the given PID
    if (c_process->child_pid == child_pid) {
      return c_process; // Return the matching child process
    }
  }

  return NULL;
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

  // Acquire the lock to ensure safe access to child_processes list
  lock_acquire(&pcb->child_lock);

  // Validate child_pid and retrieve the child process entry
  child_process_t* c_process = get_child_process(&pcb->child_processes, child_pid);
  if (c_process == NULL) {
    lock_release(&pcb->child_lock);
    return -1;
  }

  if (c_process->waited_on) {
    lock_release(&pcb->child_lock);
    return -1;
  }

  // Mark the child as waited on
  c_process->waited_on = true;

  // Release the lock before waiting
  lock_release(&pcb->child_lock);

  // Wait for the child to exit
  sema_down(&c_process->sem);

  // Retrieve the exit status (already set by process_exit)
  int exit_status = c_process->exit_status;

  return exit_status;
}

void process_exit(const int exit_status) {
  // Log the exit status of the process
  struct thread* cur = thread_current();
  if (cur->pcb == NULL) {
    thread_exit();
    NOT_REACHED();
  }

  struct process* p_process = cur->pcb->p_process;

  for (int fd = 3; fd < MAX_FD; fd++) {
    struct file* file = cur->pcb->fd_table[fd];
    if (file != NULL) {
      file_close(cur->pcb->fd_table[fd]);
      cur->pcb->fd_table[fd] = NULL; // Clear entry.
    }
  }

  if (p_process) {
    lock_acquire(&p_process->child_lock);
    // Get the child process entry in the parent's list
    child_process_t* c_process = get_child_process(&p_process->child_processes, cur->tid);
    if (c_process) {
      // Set exit status and signal the parent
      c_process->exit_status = exit_status;
      c_process->exited = true;
      sema_up(&c_process->sem);
    }

    lock_release(&p_process->child_lock);
  }

  // Free current process's resources
  struct process* pcb_to_free = cur->pcb;
  if (pcb_to_free) {
    // Clear the thread's reference to the PCB.
    cur->pcb = NULL;

    // Free file descriptors.
    for (int fd = 3; fd < MAX_FD; fd++) {
      if (pcb_to_free->fd_table[fd] != NULL) {
        file_close(pcb_to_free->fd_table[fd]);
        pcb_to_free->fd_table[fd] = NULL;
      }
    }

    // Free child process entries.
    while (!list_empty(&pcb_to_free->child_processes)) {
      struct list_elem* e = list_pop_front(&pcb_to_free->child_processes);
      child_process_t* cp = list_entry(e, child_process_t, elem);
      free(cp); // Free the child process structure.
    }

    // Invalidate the page directory if active.
    if (active_pd() == pcb_to_free->pagedir) {
      pagedir_activate(init_page_dir);
    }
    // // Destroy the page directory, if applicable.
    if (pcb_to_free->pagedir != NULL) {
      // printf("[DEBUG] Destroying page directory at %p for process '%s'\n", pcb_to_free->pagedir,
      //        pcb_to_free->process_name);
      pagedir_destroy(pcb_to_free->pagedir); // Frees page directory and associated resources.
      pcb_to_free->pagedir = NULL;           // Prevent dangling pointer.
      // printf("[DEBUG] Page directory destroyed for process '%s'\n", pcb_to_free->process_name);
    }

    // Finally, free the process control block itself.
    free(pcb_to_free);
  }
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

/* Allocates a file descriptor for the process. struct file* is a 
  kernel concept, we can't expose this because this would leak abstraction
*/
int process_allocate_fd(struct file* file) {
  struct process* process = thread_current()->pcb;

  if (!process) {
    return -1;
  }

  /* Must start at 4 because we specify 0 for stdin, 1 for stdout, 2 for stderr, 
  3 for the current executable file to be close after*/
  for (int fd = 4; fd < MAX_FD; fd++) {
    if (process->fd_table[fd] == NULL) {
      process->fd_table[fd] = file;
      return fd;
    }
  }
  return -1;
}

/* Get file size of a file descriptor in the fd_table
*/
int process_get_filesize(int fd) {
  struct process* process = thread_current()->pcb;

  if (!process) {
    return -1;
  }

  if (process->fd_table[fd] != NULL) {
    return file_length(process->fd_table[fd]);
  };

  return -1;
}

/* Get file from fd */
struct file* process_get_file(int fd) {
  struct process* process = thread_current()->pcb;

  if (!process) {
    return NULL;
  }

  if (process->fd_table[fd] != NULL) {
    return process->fd_table[fd];
  };

  return NULL;
}

/* Get file from fd */
void process_close_file(int fd) {
  struct process* process = thread_current()->pcb;

  if (!process) {
    return;
  }

  if (process->fd_table[fd] != NULL) {
    process->fd_table[fd] = NULL;
  };
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
  if (t->pcb->pagedir == NULL) {
    goto done;
  }
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
          if (!load_segment(file, file_page, (void*)mem_page, read_bytes, zero_bytes, writable)) {
            goto done;
          }
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
    if (kpage == NULL) {
      return false;
    }

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
bool setup_thread(void** esp, int num_threads) {
  ASSERT(num_threads > 1);

  uint8_t* kpage;
  bool success = false;

  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL) {
    success = install_page(((uint8_t*)PHYS_BASE) - num_threads * PGSIZE, kpage, true);
    if (success)
      *esp = PHYS_BASE - (num_threads - 1) * PGSIZE;
    else
      palloc_free_page(kpage);
  }
  return success;
}

typedef struct pthread_args {
  pthread_fun tf;
  stub_fun sf;
  void* arg;
  bool success;
  struct semaphore sem;
} pthread_args_t;

/* Starts a new thread with a new user stack running SF, which takes
TF and ARG as arguments on its user stack. This new thread may be
scheduled (and may even exit) before pthread_execute () returns.
Returns the new thread's TID or TID_ERROR if the thread cannot
be created properly.

This function will be implemented in Project 2: Multithreading and
should be similar to process_execute (). For now, it does nothing.
*/
tid_t pthread_execute(stub_fun sf, pthread_fun tf, void* arg) {
  tid_t tid;
  pthread_args_t args;
  args.sf = sf;
  args.tf = tf;
  args.success = false;
  args.arg = arg;
  sema_init(&args.sem, 0);

  tid = thread_create("stuff", PRI_DEFAULT, start_pthread, &args);

  if (tid == TID_ERROR) {
    return TID_ERROR;
  }

  sema_down(&args.sem);

  if (!args.success) {
    return TID_ERROR;
  }

  return tid;
}

static bool prepare_pthread_stack(pthread_fun tf, void* arg, void** esp) {
  // Push arguments in right-to-left order.

  // Step 1: Push the argument.
  *esp -= sizeof(void*);
  memcpy(*esp, &arg, sizeof(void*));

  // Step 2: Push the thread function pointer.
  *esp -= sizeof(void*);
  memcpy(*esp, &tf, sizeof(void*));

  // Step 3: Push a fake return address (0).
  *esp -= sizeof(void*);
  memset(*esp, 0, sizeof(void*));

  // Step 4: Align the stack pointer to a 16-byte boundary.
  uintptr_t sp = (uintptr_t)*esp;
  size_t misalignment = sp % 16;
  if (misalignment != 0) {
    size_t padding = 16 - misalignment;
    *esp -= padding;
    memset(*esp, 0, padding);
  }

  return true;
}

/* A thread function that creates a new user thread and starts it
   running. Responsible for adding itself to the list of threads in
   the PCB.

   This function will be implemented in Project 2: Multithreading and
   should be similar to start_process (). For now, it does nothing. */
static void start_pthread(void* exec_) {
  struct thread* t = thread_current();
  ASSERT(t->pcb != NULL);
  pthread_args_t* pthread_args = (pthread_args_t*)exec_;
  bool success;
  struct intr_frame if_;
  int num_threads;

  // Step 1. Activate the thread to make sure we're in this process context
  process_activate();

  // Step 2. Add the current thread in pcb. //TODO: ADD MAIN TO THE list
  list_push_front(&t->pcb->all_threads, &t->elem);
  t->pcb->total_threads++;

  // Step 3. Set up interrupt stack frame, including stack pointer and instruction pointer.
  memset(&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  if_.eip = pthread_args->sf;

  success = setup_thread(&if_.esp, t->pcb->total_threads);

  if (!success) {
    pthread_args->success = success;
    sema_up(&pthread_args->sem);
    thread_exit();
  }

  success = prepare_pthread_stack(pthread_args->tf, pthread_args->arg, &if_.esp);

  if (!success) {
    pthread_args->success = success;
    sema_up(&pthread_args->sem);
    thread_exit();
  }

  pthread_args->success = success;
  sema_up(&pthread_args->sem);

  /* Start the user pthread by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}

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