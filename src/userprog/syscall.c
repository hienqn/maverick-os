#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "lib/float.h"

void syscall_init(void);
struct lock global_lock;
typedef bool (*validate_func)(struct intr_frame* f, uint32_t* args);
typedef void (*handler_func)(struct intr_frame* f, uint32_t* args);

static void syscall_handler(struct intr_frame*);

void syscall_init(void) {
  lock_init(&global_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Helper functions */
static void terminate(struct intr_frame* f, int status) {
  f->eax = status;
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, status);
  process_exit(status);
}

static bool is_valid_pointer(void* pointer, size_t size) {
  uintptr_t start = (uintptr_t)pointer;
  uintptr_t end = start + size;

  // Check for pointer overflow
  if (end < start) {
    return false;
  }

  // Check that start and end are within user address space
  if (!is_user_vaddr((void*)start) || !is_user_vaddr((void*)end)) {
    return false;
  }

  // Check that both start and end are mapped
  if (pagedir_get_page(thread_current()->pcb->pagedir, (void*)start) == NULL ||
      pagedir_get_page(thread_current()->pcb->pagedir, (void*)end) == NULL) {
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

static bool validate_pt_create(struct intr_frame* f UNUSED, uint32_t* args) {
  // the args are: sfun, tfun, arg. These validations are wrong
  // we need to validate the args[1], args[2], args[3]
  if (!is_valid_pointer(&args[1], sizeof(stub_fun))) {
    return false;
  }
  if (!is_valid_pointer(&args[2], sizeof(pthread_fun))) {
    return false;
  }
  if (!is_valid_pointer(&args[3], sizeof(void*))) {
    return false;
  }

  return true;
}

static bool validate_compute_e(struct intr_frame* f UNUSED, uint32_t* args) { return true; }

static bool validate_seek(struct intr_frame* f UNUSED, uint32_t* args) {
  // Validate args[1] itself as a pointer before using it
  int fd = args[1];
  // Validate the file name string
  return fd >= 0 && fd < MAX_FD ? true : false;
}

static bool validate_tell(struct intr_frame* f UNUSED, uint32_t* args) {
  // Validate args[1] itself as a pointer before using it
  int fd = args[1];

  // Validate the file name string
  return fd >= 0 && fd < MAX_FD && fd < MAX_FD ? true : false;
}

static bool validate_close(struct intr_frame* f UNUSED, uint32_t* args) {
  // Validate args[1] itself as a pointer before using it
  int fd = args[1];

  // Validate the file name string
  return fd >= 0 && fd < MAX_FD ? true : false;
}

static bool validate_read(struct intr_frame* f UNUSED, uint32_t* args) {
  int fd = args[1];
  void* buffer = (void*)args[2];
  unsigned size = args[3];

  // Check if the file descriptor is valid
  if (fd < 0 || fd >= MAX_FD) {
    return false;
  }

  // Check if the buffer pointer is valid
  if (!is_valid_pointer(buffer, sizeof(char*))) {
    return false;
  }

  // Check if the buffer range is valid
  if (!is_valid_buffer(buffer, size)) {
    return false;
  }

  return true;
}

static bool validate_filesize(struct intr_frame* f UNUSED, uint32_t* args) {
  // Validate args[1] itself as a pointer before using it
  int fd = args[1];

  // Validate the file name string
  return fd >= 0 && fd < MAX_FD ? true : false;
}

static bool validate_open(struct intr_frame* f UNUSED, uint32_t* args) {
  // Validate args[1] itself as a pointer before using it
  if (!is_valid_pointer(&args[1], sizeof(char*))) {
    return false;
  }

  char* file_name = (char*)args[1];

  // Validate the file name string
  return is_valid_file(file_name);
}

static bool validate_remove(struct intr_frame* f UNUSED, uint32_t* args) {
  // Validate args[1] itself as a pointer before using it
  if (!is_valid_pointer(&args[1], sizeof(char*))) {
    return false;
  }

  char* file_name = (char*)args[1];

  // Validate the file name string
  return is_valid_file(file_name);
}

/* Validation functions */
static bool validate_create(struct intr_frame* f UNUSED, uint32_t* args) {
  // Validate args[1] itself as a pointer before using it
  if (!is_valid_pointer(&args[1], sizeof(char*))) {
    return false;
  }

  char* file_name = (char*)args[1];

  // Validate the file name string
  return is_valid_file(file_name);
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

  // Check if the file descriptor is valid
  if (fd < 0 || fd >= MAX_FD) {
    return false;
  }

  // Check if the buffer pointer is valid
  if (!is_valid_pointer(buffer, sizeof(char*))) {
    return false;
  }

  // Check if the buffer range is valid
  if (!is_valid_buffer(buffer, size)) {
    return false;
  }

  return true;
}

static void sys_tell_handler(struct intr_frame* f, uint32_t* args) {
  int fd = args[1];

  struct file* open_file = process_get_file(fd);
  if (open_file != NULL) {
    off_t pos = file_tell(open_file);
    f->eax = pos;
  } else {
    f->eax = -1;
  }
}

static void sys_compute_e_handler(struct intr_frame* f, uint32_t* args) {
  if (args == NULL) {
    printf("Error: Invalid arguments\n");
    return;
  }

  int number = args[1];
  double result = sys_sum_to_e(number); // Use a function that returns double
  f->eax = result;
}

static void sys_seek_handler(struct intr_frame* f, uint32_t* args) {
  lock_acquire(&global_lock);
  int fd = args[1];
  unsigned position = args[2];

  struct file* open_file = process_get_file(fd);
  if (open_file != NULL) {
    file_seek(open_file, position);
    f->eax = 0;
  } else {
    f->eax = -1;
  }
  lock_release(&global_lock);
}

static void sys_close_handler(struct intr_frame* f, uint32_t* args) {
  lock_acquire(&global_lock);
  int fd = args[1];

  struct file* open_file = process_get_file(fd);
  if (open_file != NULL) {
    file_close(open_file);
    process_close_file(fd);
    f->eax = 0;
  } else {
    f->eax = -1;
  }
  lock_release(&global_lock);
}

/* Will handle stdin later */
static int read_helper(void* buffer, int size) {
  int byte_size = 0;
  uint8_t* buf = (uint8_t*)buffer;

  while (byte_size < size) {
    *(buf + byte_size) = input_getc();
    byte_size++;
  };

  *(buf + byte_size) = '\0';
  return byte_size;
}

static void sys_read_handler(struct intr_frame* f, uint32_t* args) {
  lock_acquire(&global_lock);
  int fd = args[1];
  void* buffer = (void*)args[2];
  int size = args[3];

  /* Missing stdin tests */
  if (fd == STDIN_FILENO) {
    // read size character from input_getc
    int byte_size = read_helper(buffer, size);
    f->eax = byte_size;
  }

  struct file* open_file = process_get_file(fd);

  if (open_file != NULL) {
    off_t byte_read = file_read(open_file, buffer, size);
    f->eax = byte_read;
  } else {
    f->eax = -1;
  }
  lock_release(&global_lock);
}

static void sys_filesize_handler(struct intr_frame* f, uint32_t* args) {
  int fd = args[1];
  f->eax = process_get_filesize(fd);
}

static void sys_open_handler(struct intr_frame* f, uint32_t* args) {
  lock_acquire(&global_lock);

  char* file_name = (char*)args[1];
  struct file* open_file = filesys_open(file_name);

  if (open_file) {
    int fd = process_allocate_fd(open_file);
    f->eax = fd;
  } else {
    f->eax = -1;
  }

  lock_release(&global_lock);
}

static void sys_remove_handler(struct intr_frame* f, uint32_t* args) {
  lock_acquire(&global_lock);
  char* file_name = (char*)args[1];
  if (filesys_remove(file_name)) {
    f->eax = 1;
  } else {
    f->eax = 0;
  }
  lock_release(&global_lock);
}

static void sys_create_handler(struct intr_frame* f, uint32_t* args) {
  lock_acquire(&global_lock);
  char* file_name = (char*)args[1];
  unsigned initial_size = args[2];
  if (filesys_create(file_name, initial_size)) {
    f->eax = 1;
  } else {
    f->eax = 0;
  }
  lock_release(&global_lock);
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
  lock_acquire(&global_lock);
  int fd = args[1];
  const void* buffer = (void*)args[2];
  off_t size = args[3];
  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    f->eax = size;
  } else {
    struct file* open_file = process_get_file(fd);
    if (open_file != NULL) {
      off_t byte_write = file_write(open_file, buffer, size);
      f->eax = byte_write;
    } else {
      f->eax = -1;
    }
  }
  lock_release(&global_lock);
}

static void sys_pt_create_handler(struct intr_frame* f, uint32_t* args) {
  // extract out all 3 arguments from args
  stub_fun sfun = (stub_fun)args[1];
  pthread_fun tfun = (pthread_fun)args[2];
  void* arg = (void*)args[3];

  tid_t tid = pthread_execute(sfun, tfun, arg);
  if (tid != TID_ERROR) {
    f->eax = tid;
  } else {
    f->eax = -1;
  }
}

static void sys_pt_exit_handler(struct intr_frame* f, uint32_t* args) {
  if (is_main_thread(thread_current(), thread_current()->pcb)) {
    pthread_exit_main();
  } else {
    pthread_exit();
  }
}

static bool validate_pt_exit(struct intr_frame* f, uint32_t* args) { return true; }

static void sys_pthread_join_handler(struct intr_frame* f, uint32_t* args) {
  tid_t tid = args[1];
  tid_t result = pthread_join(tid);
  f->eax = result;
}

static bool validate_pthread_join(struct intr_frame* f, uint32_t* args) {
  return is_valid_pointer(&args[1], sizeof(tid_t));
}

static bool validate_get_tid(struct intr_frame* f, uint32_t* args) { return true; }

static void sys_get_tid_handler(struct intr_frame* f, uint32_t* args) {
  tid_t tid = thread_current()->tid;
  f->eax = tid;
}

/* Arrays for syscall validators and handlers */
static validate_func syscall_validators[SYS_CALL_COUNT] = {
    [SYS_HALT] = validate_halt,           [SYS_EXIT] = validate_exit,
    [SYS_EXEC] = validate_exec,           [SYS_WRITE] = validate_write,
    [SYS_PRACTICE] = validate_practice,   [SYS_WAIT] = validate_wait,
    [SYS_CREATE] = validate_create,       [SYS_REMOVE] = validate_remove,
    [SYS_OPEN] = validate_open,           [SYS_FILESIZE] = validate_filesize,
    [SYS_READ] = validate_read,           [SYS_CLOSE] = validate_close,
    [SYS_SEEK] = validate_seek,           [SYS_TELL] = validate_tell,
    [SYS_COMPUTE_E] = validate_compute_e, [SYS_PT_CREATE] = validate_pt_create,
    [SYS_PT_EXIT] = validate_pt_exit,     [SYS_PT_JOIN] = validate_pthread_join,
    [SYS_GET_TID] = validate_get_tid};

static handler_func syscall_handlers[SYS_CALL_COUNT] = {
    [SYS_HALT] = sys_halt_handler,           [SYS_EXIT] = sys_exit_handler,
    [SYS_EXEC] = sys_exec_handler,           [SYS_WRITE] = sys_write_handler,
    [SYS_PRACTICE] = sys_practice_handler,   [SYS_WAIT] = sys_wait_handler,
    [SYS_CREATE] = sys_create_handler,       [SYS_REMOVE] = sys_remove_handler,
    [SYS_OPEN] = sys_open_handler,           [SYS_FILESIZE] = sys_filesize_handler,
    [SYS_READ] = sys_read_handler,           [SYS_CLOSE] = sys_close_handler,
    [SYS_SEEK] = sys_seek_handler,           [SYS_TELL] = sys_tell_handler,
    [SYS_COMPUTE_E] = sys_compute_e_handler, [SYS_PT_CREATE] = sys_pt_create_handler,
    [SYS_PT_EXIT] = sys_pt_exit_handler,     [SYS_PT_JOIN] = sys_pthread_join_handler,
    [SYS_GET_TID] = sys_get_tid_handler};

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