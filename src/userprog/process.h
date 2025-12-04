#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127
#define MAX_ARGS 64
#define MAX_FILE_DESCRIPTOR 128

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. */
  char process_name[16];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */
  struct list children_status;        /* List of children process */
  struct process_status *my_status; /* My own status shared with parent */
  struct process* parent_process; /* Point to parent process */
  struct file *fd_table[MAX_FILE_DESCRIPTOR]; /* Array of file pointers */
  struct file* executable;    /* Executable file (deny writes while running) */
};

struct process_status {
   tid_t tid;                  /* Child's thread ID */
   int exit_code;              /* Exit code (default -1) */
   struct semaphore wait_sem;  /* Semaphore for the parent to wait on */
   struct list_elem elem;      /* Element for the parent's children list */
   int ref_count;              /* Reference count (2 initially: parent + child) */
   bool is_waited_on;          /* Prevents waiting twice */
 };

struct process_load_info {
   char *cmd_line;             /* Command line to execute */
   char *file_name;
   int argc;
   char *argv[MAX_ARGS];       /* Argument array (fixed size) */
   struct semaphore loaded_signal;  /* Semaphore for loading synchronization */
   bool load_success;                /* Result of loading */
   struct process_status *child_status; /* Status struct created by parent */
   struct process *parent_process; /* A pointer to its parent */
 };

void userprog_init(void);

pid_t process_execute(const char* cmd_line);
int process_wait(pid_t);
void process_exit(void);
void process_activate(void);
pid_t process_fork(void);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);
int is_fd_table_full(void);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

#endif /* userprog/process.h */
