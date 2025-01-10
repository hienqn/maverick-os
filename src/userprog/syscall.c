#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include <string.h>
#include <stdio.h>
#include <syscall-nr.h>

typedef bool (*validate_func)(struct intr_frame* f, void* args);

typedef void (*handler_func)(struct intr_frame* f, void* args);

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

/* Helper functions */

static bool is_valid_pointer(void* pointer) { return true; }
static bool is_valid_fd(void* pointer) { return true; }
static bool is_valid_buffer(void* buffer, size_t size) {
  /* First of all, this pointer needs to be in user space */

  /* */

  /* The data it points to must also */
}

// Array of syscall handlers
static void terminate(struct intr_frame* f, int status) {
  f->eax = status;
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, status);
  process_exit();
};

/* Function to validate if a syscall number is valid. */
static bool validate_syscall_number(struct intr_frame* f, int syscall_number) {
  if (syscall_number >= SYS_HALT && syscall_number <= SYS_INUMBER) {
    return true;
  }

  printf("%s", "Invalid syscall number");

  terminate(f, -1);
};

/* Validation functions */
static bool validate_exit(struct intr_frame* f, void* args) {
  /* We need to validate the first arguments */
  return true;
};

static bool validate_exec(struct intr_frame* f, void* args) { return true; };
static bool validate_create(struct intr_frame* f, void* args) { return true; };
static bool validate_wait(struct intr_frame* f, void* args) { return true; };
static bool validate_remove(struct intr_frame* f, void* args) { return true; };
static bool validate_open(struct intr_frame* f, void* args) { return true; };
static bool validate_read(struct intr_frame* f, void* args) { return true; };

static bool validate_write(struct intr_frame* f, void* args) {
  /* int fd, const void* buffer, unsigned length */
  uint32_t* syscall_arguments = (uint32_t*)args;

  int fd = syscall_arguments[1];
  void* buffer = syscall_arguments[2];
  uint32_t size = syscall_arguments[3];

  if (!is_valid_fd(fd)) {
    return false;
  };

  if (!is_valid_buffer(buffer, size)) {
    return false;
  };

  return true;
};

static validate_func syscall_validators[SYS_CALL_COUNT] = {
    [SYS_HALT] = NULL,
    [SYS_EXIT] = validate_exit,
    [SYS_EXEC] = validate_exec,
    [SYS_CREATE] = validate_create,
    [SYS_WAIT] = validate_wait,
    [SYS_REMOVE] = validate_remove,
    [SYS_OPEN] = validate_open,
    [SYS_READ] = validate_read,
    [SYS_WRITE] = validate_write,
};

/* System call handlers */

static void sys_halt_handler(struct intr_frame* f, void* args) { shutdown_power_off(); };

static void sys_exec_handler(struct intr_frame* f, void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_wait_handler(struct intr_frame* f, void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_create_handler(struct intr_frame* f, void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_remove_handler(struct intr_frame* f, void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_open_handler(struct intr_frame* f, void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_read_handler(struct intr_frame* f, void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_write_handler(struct intr_frame* f, void* args) {
  uint32_t* syscall_arguments = (uint32_t*)args;

  int fd = syscall_arguments[1];
  const void* buffer = (void*)syscall_arguments[2];
  unsigned size = syscall_arguments[3];

  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    f->eax = size; // Return the number of bytes written
  } else {
    // Handle other file descriptors (e.g., actual files)
    f->eax = -1; // Not implemented yet
  }
};

static void sys_exit_handler(struct intr_frame* f, void* args) {
  uint32_t* syscall_arguments = (uint32_t*)args;
  terminate(f, syscall_arguments[1]);
};

static handler_func syscall_handlers[SYS_CALL_COUNT] = {
    [SYS_HALT] = sys_halt_handler,     [SYS_EXIT] = sys_exit_handler,
    [SYS_EXEC] = sys_exec_handler,     [SYS_WAIT] = sys_wait_handler,
    [SYS_CREATE] = sys_create_handler, [SYS_REMOVE] = sys_remove_handler,
    [SYS_OPEN] = sys_open_handler,     [SYS_READ] = sys_read_handler,
    [SYS_WRITE] = sys_write_handler,
};

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = ((uint32_t*)f->esp);

  /* We assume that args[0] will always be available for us */
  uint32_t syscall_number = args[0];

  /* Make sure the syscall_number is valid */
  validate_syscall_number(f, syscall_number);

  /* Look up validator table based on syscall_number */
  if (syscall_validators[syscall_number] != NULL) {
    bool is_valid = syscall_validators[syscall_number](f, args);
    if (!is_valid) {
      // Handle invalid syscall arguments (e.g., terminate process, return error)
      printf("Syscall %d validation failed!\n", syscall_number);
      terminate(f, -1);
    }
  }

  /* After validation, we call the system call handler */
  if (syscall_handlers[syscall_number] != NULL) {
    syscall_handlers[syscall_number](f, args);
  } else {
    printf("Syscall %d not implemented.\n", syscall_number);
  }
}
