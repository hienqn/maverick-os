#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include <string.h>

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

static void terminate(struct intr_frame* f, int status) {
  f->eax = status;
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, status);
  process_exit();
}

static int get_arg_count(uint32_t syscall_number) {
  switch (syscall_number) {
    case SYS_EXIT: // System call requires 1 argument
      return 1;

    case SYS_WRITE: // System call requires 3 arguments
      return 3;

    case SYS_EXEC: // System call requires 3 arguments
      return 1;

    case SYS_READ: // System call requires 3 arguments
      return 3;

    case SYS_OPEN: // System call requires 1 argument
      return 1;

    case SYS_CREATE: // System call requires 2 arguments
      return 2;

    default:     // Default case for unsupported syscalls
      return -1; // Return -1 for invalid syscall number
  }
}

static bool validate_pointer(void* pointer, size_t size) {
  // Check if the base pointer is a valid user address
  if (!is_user_vaddr(pointer)) {
    return false;
  }

  // Check if the end of the range is a valid user address
  if (!is_user_vaddr(pointer + size)) {
    return false;
  }

  // Check if the base pointer is mapped
  if (!pagedir_get_page(thread_current()->pcb->pagedir, pointer)) {
    return false;
  }

  // Check if the end of the range is mapped
  if (!pagedir_get_page(thread_current()->pcb->pagedir, pointer + size)) {
    return false;
  }

  return true;
}

bool validate_buffer(void* pointer, size_t size) {
  ASSERT(validate_pointer(pointer, 0) == true);

  for (int i = 0; i <= size; i++) {
    if (!pagedir_get_page(thread_current()->pcb->pagedir, (char*)(pointer + i))) {
      return false;
    }
  }

  return true;
}

bool validate_file(char* file) {
  int i = 0;
  while (true) {
    const char* current_address = file + i;

    // Validate that the current address is a valid user address
    if (!is_user_vaddr(current_address)) {
      return false;
    }

    // Validate that the current address is mapped
    if (!pagedir_get_page(thread_current()->pcb->pagedir, current_address)) {
      return false;
    }

    // Dereference the content at the current address
    char content = *current_address;

    // Check for null terminator
    if (content == '\0') {
      break;
    }

    i++;
  }

  return true;
}

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  if (!validate_pointer(args, sizeof(uint32_t))) {
    terminate(f, -1);
  };

  /* syscal_number will always be here */
  uint32_t syscall_number = args[0];

  uint32_t arg_count = get_arg_count(syscall_number);

  if (arg_count == -1) {
    terminate(f, -1);
  }

  if (!validate_pointer(args, (arg_count + 1) * (sizeof(uint32_t)))) {
    terminate(f, -1);
  };

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  if (syscall_number == SYS_EXIT) {
    terminate(f, args[1]);
  }

  if (syscall_number == SYS_PRACTICE) {
    f->eax = (int)args[1] + 1;
  }

  if (syscall_number == SYS_WRITE) {
    int fd = args[1];
    const void* buffer = (void*)args[2];
    unsigned size = args[3];

    if (fd == STDOUT_FILENO) {
      putbuf(buffer, size);
      f->eax = size; // Return the number of bytes written
    } else {
      // Handle other file descriptors (e.g., actual files)
      f->eax = -1; // Not implemented yet
    }
  }

  if (syscall_number == SYS_HALT) {
    shutdown_power_off();
  }

  if (syscall_number == SYS_EXEC) {
    char* file = args[1];
    if (!validate_file(file)) {
      terminate(f, -1);
    };
    // process_execute(file);
  }
}