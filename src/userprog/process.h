#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 500
#define MAX_FD 150

typedef int fd;
/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

typedef struct child_process {
  pid_t child_pid;       // Process ID of the child
  bool waited_on;        // Whether the parent has waited on this child
  bool parent_exited;    // Whether the parent has exited
  bool exited;           // Whether the child has exited
  int exit_status;       // Exit status of the child
  struct semaphore sem;  // Semaphore to notify the parent of child's exit
  struct list_elem elem; // List element for struct lists
} child_process_t;

// Add these definitions at the top
#define MAX_LOCKS_PER_PROCESS 256
#define MAX_SEMAS_PER_PROCESS 256

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  uint32_t* pagedir;                 // Page directory
  char process_name[16];             // Name of the process
  struct thread* main_thread;        // Pointer to the main thread
  struct list all_threads;           // List of all threads in this process
  struct list child_processes;       // List of child processes
  struct lock child_lock;            // Lock for synchronizing access to child_processes
  struct lock all_threads_lock;      // Lock for synchronizing access to all_threads
  struct condition all_threads_cond; // Condition variable for all_threads
  struct process* p_process;         // Pointer to the process
  struct file* fd_table[MAX_FD];     // File descriptor table
  struct thread_descriptor* threads_descriptor_table[MAX_THREADS]; // Thread descriptor table
  int total_threads;                                // Total number of threads in the process
  struct bitmap* stack_bitmap;                      // Bitmap for tracking stack allocations
  bool terminating;                                 // Whether the process is terminating
  int exit_code;                                    // Exit code of the process
  struct kernel_lock* locks[MAX_LOCKS_PER_PROCESS]; // Array of kernel locks
  struct kernel_semaphore* semaphores[MAX_SEMAS_PER_PROCESS]; // Array of kernel semaphores
  uint8_t lock_map[MAX_LOCKS_PER_PROCESS / 8];                // Bitmap for locks
  uint8_t sema_map[MAX_SEMAS_PER_PROCESS / 8];                // Bitmap for semaphores
};

struct thread_descriptor {
  struct thread* thread;
  bool has_been_joined;
  tid_t tid;
};

void userprog_init(void);

pid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(const int exit_status);
void process_activate(void);
int process_allocate_fd(struct file* file);
int process_get_filesize(int fd);
struct file* process_get_file(int fd);
void process_close_file(int fd);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

#endif /* userprog/process.h */