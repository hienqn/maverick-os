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
static thread_func fork_process NO_RETURN;
static thread_func start_pthread NO_RETURN;
static bool load(char* cmd_line, void (**eip)(void), void** esp, int argc, char** argv);
bool setup_thread(void (**eip)(void), void** esp);
int is_fd_table_full(void);
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

  /* Kill the kernel if we did not succeed */
  ASSERT(success);

  /* Initialize the children status list for the main process */
  list_init(&t->pcb->children_status);
}

static void parse_cmd_line(char *cmd_line, char** file_name, int* argc, char** argv) {
  char *token, *save_ptr;
  int count = 0;
  
  // First call: pass the string
  token = strtok_r(cmd_line, " ", &save_ptr);
  *file_name = token;  // Set the caller's file_name pointer
  argv[count] = token;
  count++;
  
  // Subsequent calls: pass NULL
  while (token != NULL) {
    token = strtok_r(NULL, " ", &save_ptr);
    if (token != NULL) {
      argv[count] = token;
      count++;
    } else {
      argv[count] = NULL;  // Null terminator
    }
  }
  
  *argc = count;  // Set the caller's argc
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   process id, or TID_ERROR if the thread cannot be created. */
pid_t process_execute(const char* cmd_line) {
  char* fn_copy;
  tid_t tid;
  struct process_load_info load_info;
  memset(&load_info, 0, sizeof(load_info));  // Zero everything first
  load_info.load_success = false;
  // loaded_signal is allocated on stack which is fine because its lifetime is in this function scope
  sema_init(&load_info.loaded_signal, 0);
  // Child staus is null for now, we'll implement later
  load_info.child_status = NULL;
  load_info.argc = 0;
  load_info.parent_process = thread_current()->pcb;
  
  struct process_status* child_status = malloc(sizeof(struct process_status));
  if (!child_status) {
    return TID_ERROR;
  }
  load_info.child_status = child_status;
  load_info.child_status->exit_code = -1;
  load_info.child_status->is_waited_on = false;
  load_info.child_status->ref_count = 0;
  load_info.child_status->tid = -1;
  sema_init(&load_info.child_status->wait_sem, 0);

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page(0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy(fn_copy, cmd_line, PGSIZE);
  load_info.cmd_line = fn_copy;

  parse_cmd_line(fn_copy, &load_info.file_name, &load_info.argc, load_info.argv);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create(load_info.file_name, PRI_DEFAULT, start_process, &load_info);
  if (tid == TID_ERROR)
    palloc_free_page(fn_copy);

  // Wait until the program is loaded successfully
  sema_down(&load_info.loaded_signal);

  // This mean that it's not loaded successfully, we should consider this as failure
  // If it's not success, a thread is scheduled to be exited already
  if (load_info.load_success == false) {
    return TID_ERROR;  
  }

  return tid;
}


/* A thread function that loads a user process and starts it
   running. */
static void start_process(void* aux) {
  struct process_load_info* load_info = (struct process_load_info*)aux;
  char* file_name = (char*)load_info->file_name;

  struct thread* t = thread_current();
  struct intr_frame if_;
  bool success, pcb_success;

  /* Allocate process control block */
  struct process* new_pcb = malloc(sizeof(struct process));
  success = pcb_success = new_pcb != NULL;

  /* Initialize process control block */
  if (success) {
    // Ensure that timer_interrupt() -> schedule() -> process_activate()
    // does not try to activate our uninitialized pagedir
    new_pcb->pagedir = NULL;
    t->pcb = new_pcb;
    list_init(&t->pcb->children_status);

    // Initialize fd_table to NULL
    for (int i = 0; i < MAX_FILE_DESCRIPTOR; i++) {
      t->pcb->fd_table[i] = NULL;
    }
    t->pcb->executable = NULL;

    // Continue initializing the PCB as normal
    t->pcb->main_thread = t;
    strlcpy(t->pcb->process_name, load_info->file_name, sizeof t->pcb->process_name);
  }

  /* Initialize interrupt frame and load executable. */
  if (success) {
    memset(&if_, 0, sizeof if_);
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;

    success = load(file_name, &if_.eip, &if_.esp, load_info->argc, load_info->argv);
  }

  /* Handle failure with succesful PCB malloc. Must free the PCB */
  if (!success && pcb_success) {
    // Avoid race where PCB is freed before t->pcb is set to NULL
    // If this happens, then an unfortuantely timed timer interrupt
    // can try to activate the pagedir, but it is now freed memory
    struct process* pcb_to_free = t->pcb;
    t->pcb = NULL;
    free(pcb_to_free);
  }

  /* Clean up. Exit on failure or jump to userspace */
  palloc_free_page(load_info->cmd_line);
  if (!success) {
    load_info->load_success = success;
    sema_up(&load_info->loaded_signal);
    thread_exit();
  }

  /* At this stage, the process has successfully initialized */
  /* Assigned this process its own status to the memory that the parent process created */
  t->pcb->my_status = load_info->child_status;
  /* Increment reference count because this current thread points to this*/
  t->pcb->my_status->ref_count += 1;
  /* Initialized my status (this current thread) */
  t->pcb->my_status->tid = t->tid;
  /* This thread hasn't been waited on */
  t->pcb->my_status->is_waited_on = false;

  struct process *parent_process = load_info->parent_process;
  list_push_front(&parent_process->children_status, &t->pcb->my_status->elem);
  t->pcb->my_status->ref_count += 1;
  
  t->pcb->parent_process = parent_process;

  load_info->load_success = success;
  sema_up(&load_info->loaded_signal);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}

pid_t process_fork(struct intr_frame* parent_interrupt_frame) {
  tid_t tid;
  struct process_load_info load_info;
  memset(&load_info, 0, sizeof(load_info));  // Zero everything first
  load_info.load_success = false;
  // loaded_signal is allocated on stack which is fine because its lifetime is in this function scope
  sema_init(&load_info.loaded_signal, 0);
  // Child staus is null for now, we'll implement later
  load_info.child_status = NULL;
  load_info.argc = 0;
  load_info.parent_process = thread_current()->pcb;
  
  struct process_status* child_status = malloc(sizeof(struct process_status));
  if (!child_status) {
    return TID_ERROR;
  }
  load_info.child_status = child_status;
  load_info.child_status->exit_code = -1;
  load_info.child_status->is_waited_on = false;
  load_info.child_status->ref_count = 0;
  load_info.child_status->tid = -1;
  load_info.parent_if = parent_interrupt_frame;
  sema_init(&load_info.child_status->wait_sem, 0);
  load_info.file_name = thread_current()->pcb->process_name;
  
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create(load_info.file_name, PRI_DEFAULT, fork_process, &load_info);

  // Wait until the program is loaded successfully
  sema_down(&load_info.loaded_signal);

  // This mean that it's not loaded successfully, we should consider this as failure
  // If it's not success, a thread is scheduled to be exited already
  if (load_info.load_success == false) {
    return TID_ERROR;  
  }

  return tid;
}

static void fork_process(void*aux) {
  struct process_load_info* load_info = (struct process_load_info*)aux;

  struct thread* t = thread_current();
  struct intr_frame if_;
  bool success, pcb_success, pagedir_success, pagedup_success, fdcopy_success;

  /* Allocate process control block */
  struct process* new_pcb = malloc(sizeof(struct process));
  success = pcb_success = pagedup_success = fdcopy_success= new_pcb != NULL;
  
  /* Initialize process control block */
  if (success) {
    // Ensure that timer_interrupt() -> schedule() -> process_activate()
    // does not try to activate our uninitialized pagedir
    new_pcb->pagedir = NULL;
    t->pcb = new_pcb;
    list_init(&t->pcb->children_status);

    // Initialize fd_table to NULL
    for (int i = 0; i < MAX_FILE_DESCRIPTOR; i++) {
      t->pcb->fd_table[i] = NULL;
    }
    t->pcb->executable = NULL;

    // Continue initializing the PCB as normal
    t->pcb->main_thread = t;
    strlcpy(t->pcb->process_name, load_info->file_name, sizeof t->pcb->process_name);
  }

  /* Duplicate process address space */
  if (success) {
    t->pcb->pagedir = pagedir_create();

    if (t->pcb->pagedir == NULL) {
      success = pagedir_success = false;
    } else {
      /* Only call pagedir_dup if pagedir_create succeeded */
      uint32_t* child_pagedir = t->pcb->pagedir;
      uint32_t* parent_pagedir = load_info->parent_process->pagedir;
      success = pagedup_success = pagedir_dup(child_pagedir, parent_pagedir);
    }
  }

  /* Duplicate all file descriptor */
  /* Use file_dup() to share file pointers between parent and child.
     This implements POSIX fork semantics where parent and child share
     the same file description (position, flags, etc.). */
  if (success) {
    struct file** parent_fd = load_info->parent_process->fd_table;
    for (int i = 2; i < MAX_FILE_DESCRIPTOR; i++) {
      if (parent_fd[i] != NULL) {
        /* Share the same file struct - increments reference count */
        t->pcb->fd_table[i] = file_dup(parent_fd[i]);
      }
    }
  }
  /* Failed to duplicate file descriptor, close all the files already opened */
  /* Only cleanup if PCB was successfully allocated (t->pcb is not NULL) */
  if (!success && pcb_success) {
    for (int i = 2; i < MAX_FILE_DESCRIPTOR; i++) {
      if (t->pcb->fd_table[i] != NULL) {
        file_close(t->pcb->fd_table[i]);
      }
    }
  }

  /* If we failed but created a pagedir, destroy it to free resources */
  if (!success && pcb_success && t->pcb->pagedir != NULL) {
    pagedir_destroy(t->pcb->pagedir);
  }

  /* Handle failure with succesful PCB malloc. Must free the PCB */
  if (!success && pcb_success) {
    // Avoid race where PCB is freed before t->pcb is set to NULL
    // If this happens, then an unfortuantely timed timer interrupt
    // can try to activate the pagedir, but it is now freed memory
    struct process* pcb_to_free = t->pcb;
    t->pcb = NULL;
    free(pcb_to_free);
  }

  /* Last step, signal parent process they can go and exit */
  if (!success) {
    load_info->load_success = success;
    sema_up(&load_info->loaded_signal);
    thread_exit();
  }
  
  /* Store executable in PCB and deny writes while running */
  /* Use file_reopen to get a separate file handle for the child */
  /* This ensures that when the child closes the file, it doesn't affect the parent */
  if (load_info->parent_process->executable != NULL) {
    t->pcb->executable = file_reopen(load_info->parent_process->executable);
    if (t->pcb->executable != NULL) {
      file_deny_write(t->pcb->executable);
    }
  } else {
    t->pcb->executable = NULL;
  }

   // Copy the parent's interrupt frame to the child
   memcpy(&if_, load_info->parent_if, sizeof(if_));
   // CRITICAL: Child process must return 0 from fork()
   // The parent's interrupt frame may have a different value in eax
   if_.eax = 0;

  /* At this stage, the process has successfully initialized */
  /* Assigned this process its own status to the memory that the parent process created */
  t->pcb->my_status = load_info->child_status;
  /* Increment reference count because this current thread points to this*/
  t->pcb->my_status->ref_count += 1;
  /* Initialized my status (this current thread) */
  t->pcb->my_status->tid = t->tid;
  /* This thread hasn't been waited on */
  t->pcb->my_status->is_waited_on = false;

  struct process *parent_process = load_info->parent_process;
  list_push_front(&parent_process->children_status, &t->pcb->my_status->elem);
  t->pcb->my_status->ref_count += 1;
  
  t->pcb->parent_process = parent_process;

  load_info->load_success = success;
  sema_up(&load_info->loaded_signal);

  /* Activate the child's page directory before jumping to user space.
     This ensures that when the child starts executing, it uses its own
     page directory with the duplicated memory mappings. */
  process_activate();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
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
  struct process *cur_pcb = thread_current()->pcb;
  struct list *children = &cur_pcb->children_status;
  struct process_status *child_status = NULL;

  /* Find the child with matching tid in our children list */
  struct list_elem *e;
  for (e = list_begin(children); e != list_end(children); e = list_next(e)) {
    struct process_status *status = list_entry(e, struct process_status, elem);
    if (status->tid == child_pid) {
      child_status = status;
      break;
    }
  }

  /* There's no children */
  if (child_status == NULL) {
    return -1;
  };
  
  /* Child process is already awaited */
  if (child_status->is_waited_on) {
    return -1;
  };

  child_status->is_waited_on = true;

  sema_down(&child_status->wait_sem);

  int exit_code = child_status->exit_code;

  child_status->ref_count--;

  if (child_status->ref_count == 0) {
    list_remove(&child_status->elem);
    free(child_status);
  }

  return exit_code;
}

/* Free the current process's resources. */
void process_exit(void) {
  struct thread* cur = thread_current();
  uint32_t* pd;

  /* If this thread does not have a PCB, don't worry */
  if (cur->pcb == NULL) {
    thread_exit();
    NOT_REACHED();
  }

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pcb->pagedir;
  if (pd != NULL) {
    /* Correct ordering here is crucial.  We must set
         cur->pcb->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
    cur->pcb->pagedir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(pd);
  }

  /* Free the PCB of this process and kill this thread
     Avoid race where PCB is freed before t->pcb is set to NULL
     If this happens, then an unfortuantely timed timer interrupt
     can try to activate the pagedir, but it is now freed memory */
  struct process* pcb_to_free = cur->pcb;

  /* Close all open file descriptors */
  for (int i = 2; i < MAX_FILE_DESCRIPTOR; i++) {
    if (pcb_to_free->fd_table[i] != NULL) {
      file_close(pcb_to_free->fd_table[i]);
      pcb_to_free->fd_table[i] = NULL;
    }
  }

  /* Close the executable (also re-enables writes via file_allow_write) */
  if (pcb_to_free->executable != NULL) {
    file_close(pcb_to_free->executable);
  }

  if (pcb_to_free->my_status != NULL) {
    sema_up(&pcb_to_free->my_status->wait_sem);

    pcb_to_free->my_status->ref_count--;
    if (pcb_to_free->my_status->ref_count == 0) {
      list_remove(&pcb_to_free->my_status->elem);
      free(pcb_to_free->my_status);
    }
  }
  
  cur->pcb = NULL;
  free(pcb_to_free);

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

int is_fd_table_full(void) {
  struct thread* t = thread_current();
  struct file** fd_table = t->pcb->fd_table;
  for (int i = 2; i < MAX_FILE_DESCRIPTOR; i++) {
    if (fd_table[i] == NULL) {
      return i;
    }
  }
  return -1;
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

static bool setup_stack(void** esp, int argc, char** argv);
static bool validate_segment(const struct Elf32_Phdr*, struct file*);
static void parse_cmd_line(char *cmd_line, char** file_name, int* argc, char** argv);
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load(char* file_name, void (**eip)(void), void** esp, int argc, char**argv) {
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
  if (!setup_stack(esp, argc, argv))
    goto done;

  /* Start address. */
  *eip = (void (*)(void))ehdr.e_entry;

  success = true;

done:
  if (success) {
    /* Store executable in PCB and deny writes while running */
    file_deny_write(file);
    t->pcb->executable = file;
  } else {
    file_close(file);
  }
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
static bool setup_stack(void** esp, int argc, char **argv) {
  uint8_t* kpage;
  bool success = false;
  uint8_t *saved[128];

  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL) {
    success = install_page(((uint8_t*)PHYS_BASE) - PGSIZE, kpage, true);
    if (success) {
      uint8_t* stack_ptr = (uint8_t*)PHYS_BASE;
      
      for (int i = argc - 1; i >= 0; i--) {
        unsigned len = strlen(argv[i]) + 1;  // +1 for null terminator
        stack_ptr -= len;
        memcpy(stack_ptr, argv[i], len);
        saved[i] = stack_ptr;
      }

      uint8_t* future_stack_ptr_before_alignment = stack_ptr - sizeof(char *) * (argc + 1) - sizeof(char**) - sizeof(argc);
      uint8_t* future_stack_ptr_after_alignment = (uint8_t*) ROUND_DOWN((uintptr_t)future_stack_ptr_before_alignment, 16);
      size_t padding = (size_t)(future_stack_ptr_before_alignment - future_stack_ptr_after_alignment);
      
      stack_ptr -= padding;
      memset(stack_ptr, 0, padding);

      stack_ptr -= sizeof(char *);
      memset(stack_ptr, 0, sizeof(char *));

      for (int i = argc - 1; i >= 0; i--) {
        stack_ptr -= sizeof(char *);
        memcpy(stack_ptr, &saved[i], sizeof(char *));
      }

      // Save the address of argv[0] before decrementing stack_ptr
      uint8_t* argv_start = stack_ptr;
      stack_ptr -= sizeof(char**);
      // Store the ADDRESS of argv[0], not its value
      memcpy(stack_ptr, &argv_start, sizeof(char**));

      stack_ptr -= sizeof(int);
      memcpy(stack_ptr, &argc, sizeof(int));

      // at this point it should be aligned;
      stack_ptr -= sizeof(void *);
      memset(stack_ptr, 0, sizeof(void*));

      *esp = stack_ptr;
    } else {
      palloc_free_page(kpage);
    }
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