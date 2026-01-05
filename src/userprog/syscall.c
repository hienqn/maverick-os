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
 * ║  • Invalid user pointers are caught by the page fault handler            ║
 * ║  • When kernel faults on user address, process exits with -1             ║
 * ║  • This approach is simpler and handles lazy-loaded pages correctly      ║
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
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/wal.h"
#include "vm/mmap.h"

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

/* Terminates the current process with the given exit code. */
static void exit_process(struct intr_frame* f, int exit_code) {
  f->eax = exit_code;
  thread_current()->pcb->my_status->exit_code = exit_code;
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, f->eax);
  process_exit();
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
  /* Save user ESP for page fault handler. When a fault occurs in kernel mode
     while accessing user memory, the handler needs the user stack pointer
     (not the kernel stack pointer) to check for valid stack growth. */
  thread_current()->syscall_esp = (void*)f->esp;

  uint32_t* args = ((uint32_t*)f->esp);

  /* Basic check: ensure syscall arguments are in user space.
     Page faults catch unmapped addresses, but kernel addresses are mapped
     and readable - we must explicitly reject them to prevent reading
     kernel memory. Check that args[0..3] are all in user space
     (4 args covers all syscalls). */
  if (!is_user_vaddr(&args[3])) {
    thread_current()->pcb->my_status->exit_code = -1;
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
    process_exit();
    NOT_REACHED();
  }

  uint32_t syscall_num = args[0]; /* May fault - page_fault() handles it */

  switch (syscall_num) {

      /* ═══════════════════════════════════════════════════════════════════════
   * PROCESS SYSCALLS
   * ═══════════════════════════════════════════════════════════════════════*/

    case SYS_EXIT:
      exit_process(f, args[1]);
      break;

    case SYS_HALT:
      shutdown_power_off();
      break;

    case SYS_EXEC: {
      char* cmd_line = (char*)args[1];
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
      f->eax = args[1] + 1;
      break;

    /* ═══════════════════════════════════════════════════════════════════════
   * FILE SYSCALLS
   * ═══════════════════════════════════════════════════════════════════════*/
    case SYS_CREATE: {
      char* file_name = (char*)args[1];
      unsigned initial_size = args[2];
      /* NULL pointer should cause process to exit with -1 */
      if (file_name == NULL) {
        exit_process(f, -1);
        break;
      }
      f->eax = filesys_create(file_name, initial_size);
      break;
    }
    case SYS_REMOVE: {
      char* file_name = (char*)args[1];
      f->eax = filesys_remove(file_name);
      break;
    }
    case SYS_OPEN: {
      char* file_name = (char*)args[1];

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
      int fd = args[1];
      char* buffer = (char*)args[2];
      int size = args[3];

      if (size == 0) {
        f->eax = 0;
        break;
      }

      /* Validate buffer is in user space (reject kernel addresses) */
      if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size - 1)) {
        exit_process(f, -1);
        break;
      }

      if (fd == STDIN_FILENO) {
        f->eax = read_from_input(buffer, size);
        break;
      }
      if (fd == STDOUT_FILENO) {
        f->eax = -1;
        break;
      }

      struct file* file = get_file_from_fd(fd);
      if (file == NULL) {
        f->eax = -1;
        break;
      }

      /* Read into kernel buffer first, then copy to user buffer.
         This ensures we don't hold filesystem locks if user buffer is bad. */
      char* kbuf = malloc(size);
      if (kbuf == NULL) {
        f->eax = -1;
        break;
      }
      int bytes_read = file_read(file, kbuf, size);
      if (bytes_read > 0) {
        memcpy(buffer, kbuf, bytes_read); /* May fault on bad user ptr - no locks held */
      }
      free(kbuf);
      f->eax = bytes_read;
      break;
    }
    case SYS_WRITE: {
      int fd = args[1];
      void* buffer = (void*)args[2];
      uint32_t size = args[3];

      if (size == 0) {
        f->eax = 0;
        break;
      }

      /* Validate buffer is in user space (reject kernel addresses) */
      if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size - 1)) {
        exit_process(f, -1);
        break;
      }

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
      if (file == NULL) {
        f->eax = -1;
        break;
      }

      /* Copy user buffer to kernel buffer first.
         This ensures we don't hold filesystem locks if user buffer is bad. */
      char* kbuf = malloc(size);
      if (kbuf == NULL) {
        f->eax = -1;
        break;
      }
      memcpy(kbuf, buffer, size); /* May fault on bad user ptr - no locks held */
      int bytes_written = file_write(file, kbuf, size);
      free(kbuf);
      f->eax = bytes_written;
      break;
    }
    case SYS_SEEK: {
      int fd = args[1];
      int pos = args[2];

      struct file* file = get_file_from_fd(fd);
      if (file != NULL)
        file_seek(file, pos);
      break;
    }
    case SYS_TELL: {
      int fd = args[1];

      struct file* file = get_file_from_fd(fd);
      f->eax = (file != NULL) ? file_tell(file) : -1;
      break;
    }
    case SYS_FILESIZE: {
      int fd = args[1];

      struct file* file = get_file_from_fd(fd);
      f->eax = (file != NULL) ? file_length(file) : -1;
      break;
    }
    case SYS_INUMBER: {
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
      char* dir_path = (char*)args[1];
      f->eax = filesys_chdir(dir_path);
      break;
    }

    case SYS_MKDIR: {
      char* dir_path = (char*)args[1];
      f->eax = filesys_mkdir(dir_path);
      break;
    }

    case SYS_READDIR: {
      int fd = args[1];
      char* name = (char*)args[2];

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
      int fd = args[1];

      struct fd_entry* entry = get_fd_entry(fd);
      f->eax = (entry != NULL && entry->type == FD_DIR);
      break;
    }

      /* ═══════════════════════════════════════════════════════════════════════
   * THREADING SYSCALLS
   * ═══════════════════════════════════════════════════════════════════════*/

    case SYS_PT_CREATE: {
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
      lock_t* user_lock = (lock_t*)args[1];

      /* Check for NULL pointer - return false instead of crashing */
      if (user_lock == NULL) {
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

      *user_lock = (lock_t)id; /* Safe: no locks held, validated non-NULL */
      f->eax = true;
      break;
    }

    case SYS_LOCK_ACQUIRE: {
      lock_t* user_lock = (lock_t*)args[1];
      if (user_lock == NULL) {
        f->eax = false;
        break;
      }

      /* Read user memory before acquiring any locks */
      int id = (int)(unsigned char)*user_lock;

      struct process* pcb = thread_current()->pcb;
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
      lock_t* user_lock = (lock_t*)args[1];
      if (user_lock == NULL) {
        f->eax = false;
        break;
      }

      /* Read user memory before acquiring any locks */
      int id = (int)(unsigned char)*user_lock;
      struct process* pcb = thread_current()->pcb;

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
      sema_t* user_sema = (sema_t*)args[1];
      int val = (int)args[2];

      /* Check for NULL pointer or negative value - return false instead of crashing */
      if (user_sema == NULL || val < 0) {
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

      *user_sema = (sema_t)id; /* Safe: no locks held, validated non-NULL */
      f->eax = true;
      break;
    }

    case SYS_SEMA_DOWN: {
      sema_t* user_sema = (sema_t*)args[1];
      if (user_sema == NULL) {
        f->eax = false;
        break;
      }

      /* Read user memory before acquiring any locks */
      int id = (int)(unsigned char)*user_sema;

      struct process* pcb = thread_current()->pcb;
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
      sema_t* user_sema = (sema_t*)args[1];
      if (user_sema == NULL) {
        f->eax = false;
        break;
      }

      /* Read user memory before acquiring any locks */
      int id = (int)(unsigned char)*user_sema;

      struct process* pcb = thread_current()->pcb;
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

      /* ═══════════════════════════════════════════════════════════════════════
     * MEMORY-MAPPED FILES
     * ═══════════════════════════════════════════════════════════════════════*/

    case SYS_MMAP: {
      int fd = (int)args[1];
      void* addr = (void*)args[2];

      /* Basic security check: ensure address is in user space */
      if (!is_user_vaddr(addr)) {
        f->eax = (uint32_t)MAP_FAILED;
        break;
      }

      /* Get file length for the mapping */
      struct file* file = get_file_from_fd(fd);
      if (file == NULL) {
        f->eax = (uint32_t)MAP_FAILED;
        break;
      }
      off_t file_size = file_length(file);

      /* Let mmap_create handle all validation (page-alignment, file validity, etc.) */
      void* result = mmap_create(addr, (size_t)file_size, fd, 0);
      f->eax = (uint32_t)result;
      break;
    }

    case SYS_MUNMAP: {
      /* mapid_t is actually the address returned from mmap */
      void* addr = (void*)args[1];

      /* Validate address is in user space */
      if (!is_user_vaddr(addr)) {
        f->eax = -1;
        break;
      }

      /* Find the region containing this address */
      struct mmap_region* region = mmap_find_region(addr);
      if (region == NULL) {
        f->eax = -1;
        break;
      }

      /* Unmap the region using its start address and length */
      int result = mmap_destroy(region->start_addr, region->length);
      f->eax = result;
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
