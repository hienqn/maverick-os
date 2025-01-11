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
#include "filesys/filesys.h"

typedef bool (*validate_func)(struct intr_frame* f, void* args);

typedef void (*handler_func)(struct intr_frame* f, void* args);

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

/* Helper functions */
static bool is_valid_file_pointer(const void* pointer) {
  /* Ensure the pointer itself is in user space and mapped */
  if (pointer == NULL || !is_user_vaddr(pointer) ||
      pagedir_get_page(thread_current()->pcb->pagedir, pointer) == NULL) {
    return false;
  }

  /* Cast the pointer to a char* for character-level access */
  const char* temp_pointer = (const char*)pointer;
  int file_length = 0;

  /* Traverse the string until the null terminator or length limit */
  while (true) {
    /* Check if the current address is valid and mapped */
    if (!is_user_vaddr((void*)temp_pointer) ||
        pagedir_get_page(thread_current()->pcb->pagedir, (void*)temp_pointer) == NULL) {
      return false;
    }

    /* Check if file path length exceeds the limit */
    if (file_length > MAX_PATH_LENGTH) {
      printf("Error: File path length exceeds limit of %d characters!\n", MAX_PATH_LENGTH);
      return false;
    }

    /* Check for the null terminator */
    if (*temp_pointer == '\0') {
      break;
    }

    /* Move to the next character and increment the length */
    temp_pointer++;
    file_length++;
  }

  return true;
}

static bool is_valid_fd(int fd) { return true; }

static bool is_valid_buffer(void* buffer, size_t size) {
  /* Ensure the buffer starts and ends in user space */
  if (!is_user_vaddr(buffer) || !is_user_vaddr((void*)((char*)buffer + size - 1))) {
    return false;
  }

  /* Calculate the start and end of the buffer */
  char* start = (char*)buffer;
  char* end = (char*)buffer + size - 1;

  /* Iterate through all pages spanned by the buffer */
  for (char* addr = start; addr <= end; addr += PGSIZE) {
    if (pagedir_get_page(thread_current()->pcb->pagedir, addr) == NULL) {
      return false;
    }
  }

  /* Final check for the last byte of the range */
  if (pagedir_get_page(thread_current()->pcb->pagedir, end) == NULL) {
    return false;
  }

  return true;
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
static bool validate_exit(struct intr_frame* f, void* args) { return true; };

static bool validate_exec(struct intr_frame* f, void* args) {
  uint32_t* syscall_arguments = (uint32_t*)args;

  char* file = syscall_arguments[1];
  if (!is_valid_file_pointer(file)) {
    return false;
  };

  return true;
};
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
    [SYS_PRACTICE] = NULL,
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
  uint32_t* syscall_arguments = (uint32_t*)args;
  char* file_name = syscall_arguments[1];
  pid_t pid = process_execute(file_name);

  if (pid != TID_ERROR) {
    f->eax = pid;
  } else {
    f->eax = -1;
  }
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

static void sys_practice_handler(struct intr_frame* f, void* args) {
  uint32_t* syscall_arguments = (uint32_t*)args;
  f->eax = syscall_arguments[1] + 1;
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
    [SYS_WRITE] = sys_write_handler,   [SYS_PRACTICE] = sys_practice_handler};

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
