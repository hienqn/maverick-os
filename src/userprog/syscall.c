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

typedef bool (*validate_func)(void* args);

typedef void (*handler_func)(void* args);

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

static bool is_valid_pointer(void* pointer) { return true; }

static bool is_valid_buffer(void* buffer, size_t size) { return true; }

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

  terminate(f, -1);
};

static bool validate_exit(void* args) { return true; };
static bool validate_exec(void* args) { return true; };
static bool validate_create(void* args) { return true; };
static bool validate_wait(void* args) { return true; };
static bool validate_remove(void* args) { return true; };
static bool validate_open(void* args) { return true; };
static bool validate_read(void* args) { return true; };
static bool validate_write(void* args) { return true; };

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

static void sys_halt_handler(void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_exec_handler(void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_wait_handler(void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_create_handler(void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_remove_handler(void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_open_handler(void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_read_handler(void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_write_handler(void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static void sys_exit_handler(void* args) {
  printf("Executing a new process.\n");
  // Logic for exec syscall
};

static handler_func syscall_handlers[SYS_CALL_COUNT] = {
    [SYS_HALT] = sys_halt_handler,     [SYS_EXIT] = sys_exit_handler,
    [SYS_EXEC] = sys_exec_handler,     [SYS_WAIT] = sys_wait_handler,
    [SYS_CREATE] = sys_create_handler, [SYS_REMOVE] = sys_remove_handler,
    [SYS_OPEN] = sys_open_handler,     [SYS_READ] = sys_read_handler,
    [SYS_WRITE] = sys_write_handler,
};

static void dispatch_syscall(int syscall_number, void* args) {
  if (syscall_handlers[syscall_number] != NULL) {
    syscall_handlers[syscall_number](args);
  } else {
    printf("Syscall %d not implemented.\n", syscall_number);
  }
}

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = ((uint32_t*)f->esp);

  uint32_t syscall_number = args[0];

  validate_syscall_number(f, syscall_number);

  if (syscall_validators[syscall_number] != NULL) {
    bool is_valid = syscall_validators[syscall_number](args);
    if (!is_valid) {
      // Handle invalid syscall arguments (e.g., terminate process, return error)
      printf("Syscall %d validation failed!\n", syscall_number);
      terminate(f, -1);
    }
  }

  dispatch_syscall(syscall_number, args);
}
