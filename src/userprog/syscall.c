/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                        SYSTEM CALL HANDLER                                ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  This module handles all system calls from user programs. System calls   ║
 * ║  are the interface between user code and kernel services.                ║
 * ║                                                                          ║
 * ║  SYSCALL MECHANISM:                                                      ║
 * ║  ──────────────────                                                      ║
 * ║                                                                          ║
 * ║    User Program                      Kernel                              ║
 * ║    ─────────────                     ──────                              ║
 * ║         │                                                                ║
 * ║    push args onto stack                                                  ║
 * ║    push syscall number                                                   ║
 * ║    int 0x30  ────────────────────►  syscall_handler()                    ║
 * ║         │                                 │                              ║
 * ║         │                           validate args                        ║
 * ║         │                           dispatch to handler                  ║
 * ║         │                           store result in eax                  ║
 * ║         │                                 │                              ║
 * ║    eax = result  ◄────────────────  iret (return from interrupt)         ║
 * ║                                                                          ║
 * ║  ARGUMENT PASSING:                                                       ║
 * ║  ─────────────────                                                       ║
 * ║  Arguments are passed on the user stack. The syscall number is at        ║
 * ║  esp[0], and arguments follow at esp[1], esp[2], etc.                    ║
 * ║                                                                          ║
 * ║    ┌───────────────────┐                                                 ║
 * ║    │   arg3 (esp[3])   │                                                 ║
 * ║    │   arg2 (esp[2])   │                                                 ║
 * ║    │   arg1 (esp[1])   │                                                 ║
 * ║    │ syscall# (esp[0]) │ ← esp points here                               ║
 * ║    └───────────────────┘                                                 ║
 * ║                                                                          ║
 * ║  SECURITY:                                                               ║
 * ║  ─────────                                                               ║
 * ║  • ALL user pointers must be validated before dereferencing              ║
 * ║  • Check: is_user_vaddr() and pagedir_get_page()                         ║
 * ║  • Invalid pointers → terminate process with exit(-1)                    ║
 * ║                                                                          ║
 * ║  SYSCALL CATEGORIES:                                                     ║
 * ║  ────────────────────                                                    ║
 * ║  • Process:  exit, exec, wait, fork, halt                                ║
 * ║  • File:     create, remove, open, close, read, write, seek, tell, size  ║
 * ║  • Directory: chdir, mkdir, readdir, isdir                               ║
 * ║  • Threading: pt_create, pt_exit, pt_join, get_tid                       ║
 * ║  • Sync:     lock_init/acquire/release, sema_init/up/down                ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/malloc.h"
#include "devices/input.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/wal.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * FORWARD DECLARATIONS
 * ═══════════════════════════════════════════════════════════════════════════*/

static void syscall_handler(struct intr_frame*);

/* ═══════════════════════════════════════════════════════════════════════════
 * SYSCALL INITIALIZATION
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Registers the syscall interrupt handler.
   INT 0x30 is the software interrupt used for system calls.
   DPL=3 allows user-mode code to invoke this interrupt. */
void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

/* ═══════════════════════════════════════════════════════════════════════════
 * USER POINTER VALIDATION
 * ─────────────────────────────────────────────────────────────────────────────
 * CRITICAL SECURITY: User programs can pass arbitrary pointers to syscalls.
 * We MUST validate every pointer before dereferencing it in kernel mode.
 *
 * Validation checks:
 *   1. is_user_vaddr: Address is below PHYS_BASE (user space)
 *   2. pagedir_get_page: Address is mapped in the process's page directory
 *
 * If validation fails, the process is terminated with exit code -1.
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Validates a 4-byte pointer (suitable for int, void*, etc.).
   Returns true if all 4 bytes are valid user memory. */
static bool validate_pointer(void* arg) {
  char* byte_ptr = (char*)arg;
  if (!is_user_vaddr(byte_ptr) || !is_user_vaddr(byte_ptr + 3))
    return false;

  /* Check all 4 bytes are mapped (handles cross-page pointers). */
  if (!pagedir_get_page(thread_current()->pcb->pagedir, byte_ptr))
    return false;
  if (!pagedir_get_page(thread_current()->pcb->pagedir, byte_ptr + 1))
    return false;
  if (!pagedir_get_page(thread_current()->pcb->pagedir, byte_ptr + 2))
    return false;
  if (!pagedir_get_page(thread_current()->pcb->pagedir, byte_ptr + 3))
    return false;

  return true;
}

/* Terminates the current process with the given exit code. */
static void exit_process(struct intr_frame* f, int exit_code) {
  f->eax = exit_code;
  thread_current()->pcb->my_status->exit_code = exit_code;
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, f->eax);
  process_exit();
}

/* Validates a null-terminated string.
   Checks each byte until we find '\0' or an invalid address. */
static bool validate_string(char* str) {
  char* pointer = str;

  while (true) {
    if (!is_user_vaddr(pointer))
      return false;
    if (!pagedir_get_page(thread_current()->pcb->pagedir, pointer))
      return false;

    if (*pointer == '\0')
      return true;
    pointer++;
  }
}

/* Validates a buffer of SIZE bytes starting at BUFFER. */
static bool validate_buffer(char* buffer, int size) {
  char* pointer = buffer;
  int count = 0;

  while (count < size) {
    if (!is_user_vaddr(pointer))
      return false;
    if (!pagedir_get_page(thread_current()->pcb->pagedir, pointer))
      return false;

    pointer++;
    count++;
  }

  return true;
}

/* Convenience wrappers that exit on validation failure. */
static void validate_pointer_and_exit_if_false(struct intr_frame* f, void* arg) {
  if (!validate_pointer(arg)) {
    exit_process(f, -1);
  }
}

static void validate_string_and_exit_if_false(struct intr_frame* f, char* str) {
  if (!validate_string(str)) {
    exit_process(f, -1);
  }
}

static void validate_buffer_and_exit_if_false(struct intr_frame* f, char* buffer, int size) {
  if (!validate_buffer(buffer, size)) {
    exit_process(f, -1);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * I/O HELPERS
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Reads SIZE bytes from keyboard input into BUFFER. */
static int read_from_input(char* buffer, unsigned size) {
  for (unsigned i = 0; i < size; i++) {
    buffer[i] = input_getc();
  }
  return size;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FILE DESCRIPTOR HELPERS
 * ─────────────────────────────────────────────────────────────────────────────
 * Common patterns for file descriptor validation and access.
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Validates that FD is a valid file descriptor index (not stdin/stdout).
   Returns true if valid, false otherwise. */
static bool is_valid_fd(int fd) { return fd >= 2 && fd < MAX_FILE_DESCRIPTOR; }

/* Gets the fd_entry for the given FD, or NULL if invalid.
   Does NOT check the entry type - caller must do that. */
static struct fd_entry* get_fd_entry(int fd) {
  if (!is_valid_fd(fd))
    return NULL;
  return &thread_current()->pcb->fd_table[fd];
}

/* Gets a file from fd_table, or NULL if fd is invalid or not a file. */
static struct file* get_file_from_fd(int fd) {
  struct fd_entry* entry = get_fd_entry(fd);
  if (entry == NULL || entry->type != FD_FILE || entry->file == NULL)
    return NULL;
  return entry->file;
}

/* Gets a directory from fd_table, or NULL if fd is invalid or not a dir. */
static struct dir* get_dir_from_fd(int fd) {
  struct fd_entry* entry = get_fd_entry(fd);
  if (entry == NULL || entry->type != FD_DIR || entry->dir == NULL)
    return NULL;
  return entry->dir;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN SYSCALL HANDLER
 * ─────────────────────────────────────────────────────────────────────────────
 * Dispatches system calls to their handlers based on syscall number.
 * Arguments are read from user stack (f->esp).
 * Return value is stored in f->eax.
 * ═══════════════════════════════════════════════════════════════════════════*/

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = ((uint32_t*)f->esp);

  /* First, validate the syscall number pointer itself. */
  if (!validate_pointer(&args[0])) {
    thread_current()->pcb->my_status->exit_code = -1;
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
    process_exit();
    NOT_REACHED();
  }

  uint32_t syscall_num = args[0];

  switch (syscall_num) {

      /* ═══════════════════════════════════════════════════════════════════════
   * PROCESS SYSCALLS
   * ═══════════════════════════════════════════════════════════════════════*/

    case SYS_EXIT:
      validate_pointer_and_exit_if_false(f, &args[1]);
      exit_process(f, args[1]);
      break;

    case SYS_HALT:
      shutdown_power_off();
      break;

    case SYS_EXEC: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      char* cmd_line = (char*)args[1];
      validate_string_and_exit_if_false(f, cmd_line);
      pid_t pid = process_execute(cmd_line);
      f->eax = (pid == TID_ERROR) ? -1 : pid;
      break;
    }
    case SYS_WAIT: {
      pid_t child_pid = args[1];
      f->eax = process_wait(child_pid);
      break;
    }
    case SYS_FORK: {
      pid_t pid = process_fork(f);
      f->eax = (pid == TID_ERROR) ? -1 : pid;
      break;
    }
    case SYS_PRACTICE:
      validate_pointer_and_exit_if_false(f, &args[1]);
      f->eax = args[1] + 1;
      break;

    /* ═══════════════════════════════════════════════════════════════════════
   * FILE SYSCALLS
   * ═══════════════════════════════════════════════════════════════════════*/
    case SYS_CREATE: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      char* file_name = (char*)args[1];
      validate_string_and_exit_if_false(f, file_name);
      unsigned initial_size = args[2];
      f->eax = filesys_create(file_name, initial_size);
      break;
    }
    case SYS_REMOVE: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      char* file_name = (char*)args[1];
      validate_string_and_exit_if_false(f, file_name);
      f->eax = filesys_remove(file_name);
      break;
    }
    case SYS_OPEN: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      char* file_name = (char*)args[1];
      validate_string_and_exit_if_false(f, file_name);

      struct file* open_file = filesys_open(file_name);
      if (open_file == NULL) {
        f->eax = -1;
        break;
      }

      int free_fd = find_free_fd();
      if (free_fd == -1) {
        file_close(open_file);
        f->eax = -1;
        break;
      }

      /* Check if it's a directory or regular file */
      struct inode* inode = file_get_inode(open_file);
      struct fd_entry* entry = get_fd_entry(free_fd);

      if (inode_is_dir(inode)) {
        struct dir* open_dir = dir_open(inode_reopen(inode));
        file_close(open_file);
        if (open_dir == NULL) {
          f->eax = -1;
          break;
        }
        entry->type = FD_DIR;
        entry->dir = open_dir;
      } else {
        entry->type = FD_FILE;
        entry->file = open_file;
      }
      f->eax = free_fd;
      break;
    }
    case SYS_CLOSE: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      int fd = args[1];

      /* Allow closing stdin/stdout (no-op) but validate range */
      if (fd < 0 || fd >= MAX_FILE_DESCRIPTOR) {
        f->eax = -1;
        break;
      }

      struct fd_entry* entry = get_fd_entry(fd);
      if (entry == NULL)
        break;

      if (entry->type == FD_FILE && entry->file != NULL) {
        file_close(entry->file);
        entry->type = FD_NONE;
        entry->file = NULL;
      } else if (entry->type == FD_DIR && entry->dir != NULL) {
        dir_close(entry->dir);
        entry->type = FD_NONE;
        entry->dir = NULL;
      }
      break;
    }
    case SYS_READ: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      int fd = args[1];
      validate_pointer_and_exit_if_false(f, &args[2]);
      char* buffer = (char*)args[2];
      int size = args[3];
      validate_buffer_and_exit_if_false(f, buffer, size);

      if (fd == STDIN_FILENO) {
        f->eax = read_from_input(buffer, size);
        break;
      }
      if (fd == STDOUT_FILENO) {
        f->eax = -1;
        break;
      }

      struct file* file = get_file_from_fd(fd);
      f->eax = (file != NULL) ? file_read(file, buffer, size) : -1;
      break;
    }
    case SYS_WRITE: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      int fd = args[1];
      validate_pointer_and_exit_if_false(f, &args[2]);
      void* buffer = (void*)args[2];
      uint32_t size = args[3];
      validate_buffer_and_exit_if_false(f, buffer, size);

      if (fd == STDIN_FILENO || fd < 0 || fd >= MAX_FILE_DESCRIPTOR) {
        f->eax = -1;
        break;
      }
      if (fd == STDOUT_FILENO) {
        putbuf(buffer, size);
        f->eax = size;
        break;
      }

      struct file* file = get_file_from_fd(fd);
      if (file != NULL) {
        /* File data writes go directly through the buffer cache.
           WAL transactions are used for atomic metadata operations (file creation,
           directory updates) at the filesys layer, not for every data write. */
        int bytes_written = file_write(file, buffer, size);
        f->eax = bytes_written;
      } else {
        f->eax = -1;
      }
      break;
    }
    case SYS_SEEK: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      validate_pointer_and_exit_if_false(f, &args[2]);
      int fd = args[1];
      int pos = args[2];

      struct file* file = get_file_from_fd(fd);
      if (file != NULL)
        file_seek(file, pos);
      break;
    }
    case SYS_TELL: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      int fd = args[1];

      struct file* file = get_file_from_fd(fd);
      f->eax = (file != NULL) ? file_tell(file) : -1;
      break;
    }
    case SYS_FILESIZE: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      int fd = args[1];

      struct file* file = get_file_from_fd(fd);
      f->eax = (file != NULL) ? file_length(file) : -1;
      break;
    }
    case SYS_INUMBER: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      int fd = args[1];

      struct fd_entry* entry = get_fd_entry(fd);
      if (entry == NULL) {
        f->eax = -1;
        break;
      }

      struct inode* inode = NULL;
      if (entry->type == FD_FILE && entry->file != NULL)
        inode = file_get_inode(entry->file);
      else if (entry->type == FD_DIR && entry->dir != NULL)
        inode = dir_get_inode(entry->dir);

      f->eax = (inode != NULL) ? (int)inode_get_inumber(inode) : -1;
      break;
    }

      /* ═══════════════════════════════════════════════════════════════════════
   * DIRECTORY SYSCALLS
   * ═══════════════════════════════════════════════════════════════════════*/

    case SYS_CHDIR: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      char* dir_path = (char*)args[1];
      validate_string_and_exit_if_false(f, dir_path);
      f->eax = filesys_chdir(dir_path);
      break;
    }

    case SYS_MKDIR: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      char* dir_path = (char*)args[1];
      validate_string_and_exit_if_false(f, dir_path);
      f->eax = filesys_mkdir(dir_path);
      break;
    }

    case SYS_READDIR: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      int fd = args[1];
      validate_pointer_and_exit_if_false(f, &args[2]);
      char* name = (char*)args[2];
      validate_buffer_and_exit_if_false(f, name, NAME_MAX + 1);

      struct dir* dir = get_dir_from_fd(fd);
      if (dir == NULL) {
        f->eax = false;
        break;
      }

      /* Read entries, skipping . and .. */
      bool success = false;
      char entry_name[NAME_MAX + 1];
      while (dir_readdir(dir, entry_name)) {
        if (strcmp(entry_name, ".") != 0 && strcmp(entry_name, "..") != 0) {
          strlcpy(name, entry_name, NAME_MAX + 1);
          success = true;
          break;
        }
      }
      f->eax = success;
      break;
    }

    case SYS_ISDIR: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      int fd = args[1];

      struct fd_entry* entry = get_fd_entry(fd);
      f->eax = (entry != NULL && entry->type == FD_DIR);
      break;
    }

      /* ═══════════════════════════════════════════════════════════════════════
   * THREADING SYSCALLS
   * ═══════════════════════════════════════════════════════════════════════*/

    case SYS_PT_CREATE: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      validate_pointer_and_exit_if_false(f, &args[2]);
      validate_pointer_and_exit_if_false(f, &args[3]);
      stub_fun sfun = (stub_fun)args[1];
      pthread_fun tfun = (pthread_fun)args[2];
      void* arg = (void*)args[3];
      f->eax = pthread_execute(sfun, tfun, arg);
      break;
    }

    case SYS_PT_EXIT:
      pthread_exit(NULL);
      break;

    case SYS_PT_JOIN: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      tid_t tid = args[1];
      f->eax = pthread_join(tid, NULL);
      break;
    }

    case SYS_GET_TID:
      f->eax = thread_current()->tid;
      break;

      /* ═══════════════════════════════════════════════════════════════════════
   * USER-LEVEL SYNCHRONIZATION SYSCALLS
   * ═══════════════════════════════════════════════════════════════════════*/

    case SYS_LOCK_INIT: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      lock_t* user_lock = (lock_t*)args[1];

      if (!validate_pointer(user_lock)) {
        f->eax = false;
        break;
      }

      struct process* pcb = thread_current()->pcb;
      lock_acquire(&pcb->exit_lock);

      if (pcb->next_lock_id >= MAX_LOCKS) {
        lock_release(&pcb->exit_lock);
        f->eax = false;
        break;
      }

      struct lock* k_lock = malloc(sizeof(struct lock));
      if (k_lock == NULL) {
        lock_release(&pcb->exit_lock);
        f->eax = false;
        break;
      }

      lock_init(k_lock);
      int id = pcb->next_lock_id++;
      pcb->lock_table[id] = k_lock;
      lock_release(&pcb->exit_lock);

      *user_lock = (lock_t)id;
      f->eax = true;
      break;
    }

    case SYS_LOCK_ACQUIRE: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      lock_t* user_lock = (lock_t*)args[1];
      struct process* pcb = thread_current()->pcb;
      int id = (int)(unsigned char)*user_lock;

      lock_acquire(&pcb->exit_lock);
      struct lock* k_lock = (id >= 0 && id < MAX_LOCKS) ? pcb->lock_table[id] : NULL;
      lock_release(&pcb->exit_lock);

      if (k_lock == NULL || lock_held_by_current_thread(k_lock)) {
        f->eax = false;
      } else {
        lock_acquire(k_lock);
        f->eax = true;
      }
      break;
    }

    case SYS_LOCK_RELEASE: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      lock_t* user_lock = (lock_t*)args[1];
      struct process* pcb = thread_current()->pcb;
      int id = (int)(unsigned char)*user_lock;

      lock_acquire(&pcb->exit_lock);
      struct lock* k_lock = (id >= 0 && id < MAX_LOCKS) ? pcb->lock_table[id] : NULL;
      lock_release(&pcb->exit_lock);

      if (k_lock == NULL || !lock_held_by_current_thread(k_lock)) {
        f->eax = false;
      } else {
        lock_release(k_lock);
        f->eax = true;
      }
      break;
    }

    case SYS_SEMA_INIT: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      sema_t* user_sema = (sema_t*)args[1];
      int val = (int)args[2];

      if (!validate_pointer(user_sema) || val < 0) {
        f->eax = false;
        break;
      }

      struct process* pcb = thread_current()->pcb;
      lock_acquire(&pcb->exit_lock);

      if (pcb->next_sema_id >= MAX_SEMAS) {
        lock_release(&pcb->exit_lock);
        f->eax = false;
        break;
      }

      struct semaphore* k_sema = malloc(sizeof(struct semaphore));
      if (k_sema == NULL) {
        lock_release(&pcb->exit_lock);
        f->eax = false;
        break;
      }

      sema_init(k_sema, val);
      int id = pcb->next_sema_id++;
      pcb->sema_table[id] = k_sema;
      lock_release(&pcb->exit_lock);

      *user_sema = (sema_t)id;
      f->eax = true;
      break;
    }

    case SYS_SEMA_DOWN: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      sema_t* user_sema = (sema_t*)args[1];
      struct process* pcb = thread_current()->pcb;
      int id = (int)(unsigned char)*user_sema;

      lock_acquire(&pcb->exit_lock);
      struct semaphore* k_sema = (id >= 0 && id < MAX_SEMAS) ? pcb->sema_table[id] : NULL;
      lock_release(&pcb->exit_lock);

      if (k_sema == NULL) {
        f->eax = false;
      } else {
        sema_down(k_sema);
        f->eax = true;
      }
      break;
    }

    case SYS_SEMA_UP: {
      validate_pointer_and_exit_if_false(f, &args[1]);
      sema_t* user_sema = (sema_t*)args[1];
      struct process* pcb = thread_current()->pcb;
      int id = (int)(unsigned char)*user_sema;

      lock_acquire(&pcb->exit_lock);
      struct semaphore* k_sema = (id >= 0 && id < MAX_SEMAS) ? pcb->sema_table[id] : NULL;
      lock_release(&pcb->exit_lock);

      if (k_sema == NULL) {
        f->eax = false;
      } else {
        sema_up(k_sema);
        f->eax = true;
      }
      break;
    }

    default:
      /* Unknown syscall - do nothing (return value undefined) */
      break;
  }

  /* Check if process is exiting - thread should exit instead of returning to user */
  struct thread* cur = thread_current();
  if (cur->pcb != NULL && cur->pcb->is_exiting) {
    if (is_main_thread(cur, cur->pcb)) {
      process_exit();
    } else {
      pthread_exit(NULL);
    }
    NOT_REACHED();
  }
}
