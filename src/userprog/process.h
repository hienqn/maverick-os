/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                         PROCESS MODULE                                    ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  This module manages user processes in PintOS. A process is a resource   ║
 * ║  container (address space, file descriptors, etc.) that may contain      ║
 * ║  multiple threads.                                                       ║
 * ║                                                                          ║
 * ║  THREAD vs PROCESS:                                                      ║
 * ║  ─────────────────                                                       ║
 * ║                                                                          ║
 * ║    ┌───────────────────────────────────────────────────────────────┐     ║
 * ║    │                    PROCESS (struct process)                   │     ║
 * ║    │                                                               │     ║
 * ║    │  ┌─────────────────────────────────────────────────────────┐  │     ║
 * ║    │  │              ADDRESS SPACE (pagedir)                    │  │     ║
 * ║    │  │  • Code, data, heap, stack(s)                           │  │     ║
 * ║    │  │  • Shared by all threads in process                     │  │     ║
 * ║    │  └─────────────────────────────────────────────────────────┘  │     ║
 * ║    │                                                               │     ║
 * ║    │  ┌─────────────────────────────────────────────────────────┐  │     ║
 * ║    │  │              FILE DESCRIPTORS (fd_table)                │  │     ║
 * ║    │  │  • Shared by all threads in process                     │  │     ║
 * ║    │  │  • Inherited by fork() children                         │  │     ║
 * ║    │  └─────────────────────────────────────────────────────────┘  │     ║
 * ║    │                                                               │     ║
 * ║    │  ┌─────────┐   ┌─────────┐   ┌─────────┐                      │     ║
 * ║    │  │ Thread  │   │ Thread  │   │ Thread  │   ← Multiple threads │     ║
 * ║    │  │ (main)  │   │  (t1)   │   │  (t2)   │     share resources  │     ║
 * ║    │  └─────────┘   └─────────┘   └─────────┘                      │     ║
 * ║    │                                                               │     ║
 * ║    └───────────────────────────────────────────────────────────────┘     ║
 * ║                                                                          ║
 * ║  PROCESS LIFECYCLE:                                                      ║
 * ║  ──────────────────                                                      ║
 * ║                                                                          ║
 * ║    process_execute("prog args")                                          ║
 * ║           │                                                              ║
 * ║           ▼                                                              ║
 * ║    ┌──────────────┐                                                      ║
 * ║    │ Allocate PCB │ ──► start_process()                                  ║
 * ║    │ Create thread│                │                                     ║
 * ║    └──────────────┘                ▼                                     ║
 * ║           │                  ┌──────────────┐                            ║
 * ║           │                  │ Load ELF     │                            ║
 * ║           │                  │ Setup stack  │                            ║
 * ║           │                  └──────┬───────┘                            ║
 * ║           │                         │                                    ║
 * ║    sema_down() ◄────── success ─────┤                                    ║
 * ║    (wait for load)                  │                                    ║
 * ║           │                         ▼                                    ║
 * ║           ▼                   ┌──────────────┐                           ║
 * ║    return child PID           │ Jump to user │                           ║
 * ║                               │    mode      │                           ║
 * ║                               └──────────────┘                           ║
 * ║                                                                          ║
 * ║  PARENT-CHILD RELATIONSHIP:                                              ║
 * ║  ──────────────────────────                                              ║
 * ║                                                                          ║
 * ║    Parent Process           process_status           Child Process       ║
 * ║    ┌─────────────┐         ┌─────────────┐          ┌─────────────┐      ║
 * ║    │             │         │ tid         │          │             │      ║
 * ║    │ children ───┼────────►│ exit_code   │◄─────────┼─ my_status  │      ║
 * ║    │   _status   │         │ wait_sem    │          │             │      ║
 * ║    │             │         │ ref_count=2 │          │             │      ║
 * ║    └─────────────┘         └─────────────┘          └─────────────┘      ║
 * ║                                                                          ║
 * ║    ref_count ensures struct survives until both parent and child         ║
 * ║    have finished with it (parent calls wait, child exits).               ║
 * ║                                                                          ║
 * ║  MULTI-THREADING:                                                        ║
 * ║  ────────────────                                                        ║
 * ║  • Main thread: Created by process_execute(), owns the process           ║
 * ║  • Worker threads: Created by pthread_execute(), share process resources ║
 * ║  • Exit synchronization: Main thread waits for all threads before exit   ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "userprog/filedesc.h"
#include "vm/page.h"
#include <stdint.h>

/* Forward declarations. */
struct file;
struct dir;

/* ═══════════════════════════════════════════════════════════════════════════
 * LIMITS AND CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Maximum stack pages (8 MB total stack space). */
#define MAX_STACK_PAGES (1 << 11)

/* Maximum threads per process. */
#define MAX_THREADS 127

/* Maximum command-line arguments. */
#define MAX_ARGS 64

/* Maximum file descriptors per process. */
#define MAX_FILE_DESCRIPTOR 128

/* ═══════════════════════════════════════════════════════════════════════════
 * USER-LEVEL SYNCHRONIZATION LIMITS
 * ═══════════════════════════════════════════════════════════════════════════*/

/* User-level lock/semaphore handle types.
   Small types to minimize space in user programs. */
typedef char lock_t; /* Supports up to 256 locks per process. */
typedef char sema_t; /* Supports up to 256 semaphores per process. */

#define MAX_LOCKS 256
#define MAX_SEMAS 256

/* ═══════════════════════════════════════════════════════════════════════════
 * PID AND THREAD FUNCTION TYPES
 * ═══════════════════════════════════════════════════════════════════════════*/

/* PIDs and TIDs are the same type. PID = TID of main thread. */
typedef tid_t pid_t;

/* Thread function types for pthread_execute. */
typedef void (*pthread_fun)(void*);           /* User thread function. */
typedef void (*stub_fun)(pthread_fun, void*); /* Stub wrapper function. */

/* ═══════════════════════════════════════════════════════════════════════════
 * FILE DESCRIPTOR TABLE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Each process has a file descriptor table indexed 0 to MAX_FILE_DESCRIPTOR-1.
 * FDs 0, 1, 2 are reserved for STDIN/STDOUT/STDERR.
 *
 * File descriptors point to Open File Descriptions (OFDs) in a global table.
 * Multiple FDs can share the same OFD (via dup/dup2/fork), meaning they share
 * the file position and flags. See userprog/filedesc.h for OFD details.
 *
 * Types (enum fd_type, enum console_mode) and struct fd_entry are defined
 * in userprog/filedesc.h.
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

/* ═══════════════════════════════════════════════════════════════════════════
 * PROCESS CONTROL BLOCK (PCB)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * The PCB contains all per-process state:
 *   • Address space (page directory)
 *   • File descriptors
 *   • Child process list (for wait/exit coordination)
 *   • Thread management (for multi-threading)
 *   • User-level synchronization primitives
 *
 * All threads in a process share the same PCB via their thread->pcb pointer.
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

struct process {
  /* ═══════════════════════════════════════════════════════════════════════
   * ADDRESS SPACE
   * ═══════════════════════════════════════════════════════════════════════*/
  uint32_t* pagedir;     /* Page directory (virtual memory mappings). */
  struct spt spt;        /* Supplemental page table (VM metadata). */
  char process_name[16]; /* Process name (for debug output). */

  /* ═══════════════════════════════════════════════════════════════════════
   * PROCESS HIERARCHY
   * ─────────────────────────────────────────────────────────────────────────
   * Each process tracks its children and its own status for wait/exit.
   * ═══════════════════════════════════════════════════════════════════════*/
  struct thread* main_thread;       /* Main thread (owns the process). */
  struct list children_status;      /* List of children's process_status. */
  struct process_status* my_status; /* Our status (shared with parent). */
  struct process* parent_process;   /* Parent process pointer. */

  /* ═══════════════════════════════════════════════════════════════════════
   * FILE SYSTEM STATE
   * ═══════════════════════════════════════════════════════════════════════*/
  struct fd_entry fd_table[MAX_FILE_DESCRIPTOR]; /* File descriptor table. */
  struct file* executable;                       /* Executable file (write-denied while running). */

  /* ═══════════════════════════════════════════════════════════════════════
   * MULTI-THREADING SUPPORT
   * ─────────────────────────────────────────────────────────────────────────
   * Supports pthread_create, pthread_join, pthread_exit.
   * ═══════════════════════════════════════════════════════════════════════*/
  struct list threads;         /* All threads in this process. */
  struct list thread_statuses; /* pthread_status list for join/cleanup. */

  /* ═══════════════════════════════════════════════════════════════════════
   * EXIT SYNCHRONIZATION (Mesa-style Monitor)
   * ─────────────────────────────────────────────────────────────────────────
   * Coordinates process exit when multiple threads are running.
   * Main thread waits for all worker threads to exit before cleanup.
   * ═══════════════════════════════════════════════════════════════════════*/
  struct lock exit_lock;      /* Protects exit-related fields. */
  struct condition exit_cond; /* Signals when thread_count changes. */
  int thread_count;           /* Number of active threads (≥1). */
  bool is_exiting;            /* True when process termination initiated. */
  int exit_code;              /* Exit code for parent's wait(). */

  /* ═══════════════════════════════════════════════════════════════════════
   * USER-LEVEL SYNCHRONIZATION
   * ─────────────────────────────────────────────────────────────────────────
   * Kernel-managed locks and semaphores for user programs.
   * User code gets a handle (lock_t/sema_t), kernel stores actual struct.
   * ═══════════════════════════════════════════════════════════════════════*/
  struct lock* lock_table[MAX_LOCKS];      /* Maps lock_t → struct lock*. */
  struct semaphore* sema_table[MAX_SEMAS]; /* Maps sema_t → struct semaphore*. */
  int next_lock_id;                        /* Next available lock ID. */
  int next_sema_id;                        /* Next available semaphore ID. */

  /* ═══════════════════════════════════════════════════════════════════════
   * PTHREAD STACK MANAGEMENT
   * ─────────────────────────────────────────────────────────────────────────
   * Each pthread needs its own user stack. We allocate stack pages at
   * fixed offsets from PHYS_BASE to avoid collisions.
   * ═══════════════════════════════════════════════════════════════════════*/
  bool stack_slots[MAX_THREADS]; /* true = slot in use. */

  /* ═══════════════════════════════════════════════════════════════════════
   * MEMORY-MAPPED FILES
   * ─────────────────────────────────────────────────────────────────────────
   * List of mmap_region structs tracking all memory-mapped files.
   * See vm/mmap.h for details.
   * ═══════════════════════════════════════════════════════════════════════*/
  struct list mmap_list; /* List of mmap_region structs. */
  struct lock mmap_lock; /* Protects mmap_list and mmap operations. */
};

/* ═══════════════════════════════════════════════════════════════════════════
 * PROCESS STATUS (Parent-Child Coordination)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * This struct is shared between parent and child for wait/exit synchronization.
 * It outlives the child process so the parent can still retrieve exit code.
 *
 * Lifetime management via ref_count:
 *   • Created by parent during process_execute (ref_count = 0)
 *   • Parent adds to children_status (ref_count++)
 *   • Child starts and sets my_status (ref_count++)
 *   • Child exits and signals wait_sem (ref_count--)
 *   • Parent calls wait and retrieves exit_code (ref_count--)
 *   • When ref_count reaches 0, struct is freed
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

struct process_status {
  tid_t tid;                 /* Child's thread/process ID. */
  int exit_code;             /* Exit code (-1 if killed, else from exit()). */
  struct semaphore wait_sem; /* Parent waits on this; child ups on exit. */
  struct list_elem elem;     /* Element in parent's children_status list. */
  int ref_count;             /* Reference count (freed when 0). */
  bool is_waited_on;         /* Prevents double-wait. */
};

/* ═══════════════════════════════════════════════════════════════════════════
 * PTHREAD STATUS (Thread Join Coordination)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Similar to process_status, but for threads within a process.
 * Allows pthread_join to wait for a specific thread and get its return value.
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

struct pthread_status {
  tid_t tid;                  /* Thread ID. */
  struct semaphore exit_sema; /* Joiner waits on this. */
  struct list_elem elem;      /* Element in pcb->thread_statuses. */
  bool is_joined;             /* Prevents double-join. */
  bool has_exited;            /* Thread has called pthread_exit. */
  void* retval;               /* Return value from pthread_exit. */
};

/* ═══════════════════════════════════════════════════════════════════════════
 * PROCESS LOAD INFO (Exec Synchronization)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Passed from parent to child during process_execute/fork.
 * Contains command line, arguments, and synchronization for load completion.
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

struct process_load_info {
  /* Command line and arguments. */
  char* cmd_line;       /* Full command line string. */
  char* file_name;      /* Executable file name. */
  int argc;             /* Argument count. */
  char* argv[MAX_ARGS]; /* Argument array. */

  /* Load synchronization. */
  struct semaphore loaded_signal; /* Signaled when load completes. */
  bool load_success;              /* True if load succeeded. */

  /* Parent-child linkage. */
  struct process_status* child_status; /* Status struct for this child. */
  struct process* parent_process;      /* Parent's PCB. */

  /* For fork: parent's interrupt frame to copy. */
  struct intr_frame* parent_if;
};

/* ═══════════════════════════════════════════════════════════════════════════
 * PROCESS LIFECYCLE FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Initialization (called once during kernel startup). */
void userprog_init(void);

/* Process creation and execution. */
pid_t process_execute(const char* cmd_line);
pid_t process_fork(struct intr_frame* interrupt_frame);

/* Process termination. */
int process_wait(pid_t);
void process_exit(void);

/* Context switch support. */
void process_activate(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * PROCESS UTILITIES
 * ═══════════════════════════════════════════════════════════════════════════*/

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);
int find_free_fd(void);
int is_fd_table_full(void); /* Deprecated: use find_free_fd() */

/* ═══════════════════════════════════════════════════════════════════════════
 * PTHREAD FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════*/

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t, void**);
void pthread_exit(void*);
void pthread_exit_main(void);

#endif /* userprog/process.h */
