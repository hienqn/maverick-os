#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>

typedef bool (*validate_func)(struct intr_frame* f, uint32_t* args);
typedef void (*handler_func)(struct intr_frame* f, uint32_t* args);

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

/* Helper functions */
static void terminate(struct intr_frame* f, int status) {
  f->eax = status;
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, status);
  process_exit(status);
}

static bool is_valid_pointer(void* pointer, size_t size) {
  if (!is_user_vaddr(pointer) || !is_user_vaddr((char*)pointer + size) ||
      pagedir_get_page(thread_current()->pcb->pagedir, pointer) == NULL ||
      pagedir_get_page(thread_current()->pcb->pagedir, (char*)pointer + size) == NULL) {
    return false;
  }
  return true;
}

static bool is_valid_file(const char* file) {
  // Validate the initial pointer
  if (file == NULL || !is_user_vaddr(file) ||
      pagedir_get_page(thread_current()->pcb->pagedir, file) == NULL) {
    return false;
  }

  // Traverse the string, validating each character
  while (true) {
    if (!is_user_vaddr(file) || pagedir_get_page(thread_current()->pcb->pagedir, file) == NULL) {
      return false;
    }

    // Check for the null terminator
    if (*file == '\0') {
      break;
    }

    file++; // Move to the next character
  }

  return true;
}

static bool is_valid_buffer(void* buffer, size_t size) {
  char* start = (char*)buffer;
  char* end = start + size;
  for (char* addr = start; addr < end; addr += PGSIZE) {
    if (!pagedir_get_page(thread_current()->pcb->pagedir, addr)) {
      return false;
    }
  }
  return true;
}

/* Validation functions */
static bool validate_halt(struct intr_frame* f UNUSED, uint32_t* args UNUSED) {
  return true; // No arguments to validate
}

static bool validate_wait(struct intr_frame* f UNUSED, uint32_t* args UNUSED) {
  return is_valid_pointer(&args[1], sizeof(uint32_t));
}

static bool validate_practice(struct intr_frame* f UNUSED, uint32_t* args) {
  return is_valid_pointer(&args[1], sizeof(uint32_t)); // Validate args[1]
}

static bool validate_exit(struct intr_frame* f UNUSED, uint32_t* args UNUSED) {
  return true; // No validation needed for `SYS_EXIT`
}

static bool validate_exec(struct intr_frame* f UNUSED, uint32_t* args) {
  // Validate args[1] itself as a pointer before using it
  if (!is_valid_pointer(&args[1], sizeof(char*))) {
    return false;
  }

  char* file_name = (char*)args[1];

  // Validate the file name string
  return is_valid_file(file_name);
}

static bool validate_write(struct intr_frame* f UNUSED, uint32_t* args) {
  int fd = args[1];
  void* buffer = (void*)args[2];
  unsigned size = args[3];
  return fd >= 0 && is_valid_buffer(buffer, size);
}

/* Handlers */
static void sys_wait_handler(struct intr_frame* f, uint32_t* args) {
  pid_t pid = args[1]; // Retrieve the PID
  int status = process_wait(pid);
  f->eax = status; // Return the exit status
}

/* System call handlers */
static void sys_halt_handler(struct intr_frame* f UNUSED, uint32_t* args UNUSED) {
  shutdown_power_off(); // Shutdown the OS
}

static void sys_practice_handler(struct intr_frame* f, uint32_t* args) {
  uint32_t value = args[1]; // Retrieve the first argument
  f->eax = value + 1;       // Increment the value and store it in eax
}

static void sys_exit_handler(struct intr_frame* f, uint32_t* args) { terminate(f, args[1]); }

static void sys_exec_handler(struct intr_frame* f, uint32_t* args) {
  char* file_name = (char*)args[1];
  // Note: in case failing to load the file, pid will be -1.
  pid_t pid = process_execute(file_name);

  if (pid != TID_ERROR) {
    f->eax = pid;
  } else {
    f->eax = -1;
  }
}

static void sys_write_handler(struct intr_frame* f, uint32_t* args) {
  int fd = args[1];
  const void* buffer = (void*)args[2];
  unsigned size = args[3];
  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    f->eax = size;
  } else {
    f->eax = -1; // Not implemented yet
  }
}

/* Arrays for syscall validators and handlers */
static validate_func syscall_validators[SYS_CALL_COUNT] = {
    [SYS_HALT] = validate_halt,   [SYS_EXIT] = validate_exit,         [SYS_EXEC] = validate_exec,
    [SYS_WRITE] = validate_write, [SYS_PRACTICE] = validate_practice, [SYS_WAIT] = validate_wait};

static handler_func syscall_handlers[SYS_CALL_COUNT] = {
    [SYS_HALT] = sys_halt_handler,         [SYS_EXIT] = sys_exit_handler,
    [SYS_EXEC] = sys_exec_handler,         [SYS_WRITE] = sys_write_handler,
    [SYS_PRACTICE] = sys_practice_handler, [SYS_WAIT] = sys_wait_handler,

};

/* Main syscall handler */
static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = (uint32_t*)f->esp;

  /* Validate syscall number */
  if (!is_valid_pointer(args, sizeof(uint32_t))) {
    terminate(f, -1);
    return;
  }

  uint32_t syscall_number = args[0];
  if (syscall_number >= SYS_CALL_COUNT || syscall_handlers[syscall_number] == NULL) {
    terminate(f, -1);
    return;
  }

  /* Validate syscall arguments */
  if (syscall_validators[syscall_number] != NULL && !syscall_validators[syscall_number](f, args)) {
    terminate(f, -1);
    return;
  }

  /* Execute the syscall */
  syscall_handlers[syscall_number](f, args);
}