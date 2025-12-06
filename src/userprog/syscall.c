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
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"

static void syscall_handler(struct intr_frame*);

static struct lock global_fs_lock;

void syscall_init(void) { 
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); 
  lock_init(&global_fs_lock);
}

static bool validate_pointer(void* arg) {
  char *byte_ptr = (char *)(arg); 
  if (!is_user_vaddr(byte_ptr) || !is_user_vaddr(byte_ptr + 3)) return false;
  
  if (!pagedir_get_page(thread_current()->pcb->pagedir, byte_ptr)) return false;
  if (!pagedir_get_page(thread_current()->pcb->pagedir, byte_ptr + 1)) return false;
  if (!pagedir_get_page(thread_current()->pcb->pagedir, byte_ptr + 2)) return false;
  if (!pagedir_get_page(thread_current()->pcb->pagedir, byte_ptr + 3)) return false;

  return true;
}

static void exit_process(struct intr_frame* f, int exit_code) {
  f->eax = exit_code;
  thread_current()->pcb->my_status->exit_code = exit_code;
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, f->eax);
  process_exit();
}

static bool validate_string(char* str) {
  char *pointer = str;
  
  while (true) {
    // Validate that the current byte is accessible
    if (!is_user_vaddr(pointer)) return false;
    if (!pagedir_get_page(thread_current()->pcb->pagedir, pointer)) return false;
    
    // Check for null terminator
    if (*pointer == '\0') return true;
    
    pointer++;
  }
}

static bool validate_buffer(char* buffer, int size) {
  char *pointer = buffer;
  int count = 0;
  
  while (count < size) {
    // Validate that the current byte is accessible
    if (!is_user_vaddr(pointer)) return false;
    if (!pagedir_get_page(thread_current()->pcb->pagedir, pointer)) return false;
    
    pointer++;
    count++;
  }
  
  return true;
}

static void validate_pointer_and_exit_if_false(struct intr_frame* f, void * arg) {
  if (!validate_pointer(arg)) {
    exit_process(f, -1);
  }
}

static void validate_string_and_exit_if_false(struct intr_frame* f, char* str) {
  if (!validate_string(str)) {
    exit_process(f, -1);
  }
}

static void validate_buffer_and_exit_if_false(struct intr_frame* f, char* buffer, int size) {
  if (!validate_buffer(buffer, size)) {
    exit_process(f, -1);
  }
}

static int read_from_input(char *buffer, unsigned size) {
  for (unsigned i = 0; i < size; i++) {
    buffer[i] = input_getc();
  }
  return size;
}

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = ((uint32_t*)f->esp);

  if (!validate_pointer(&args[0])) {
    thread_current()->pcb->my_status->exit_code = -1;
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
    process_exit();
    NOT_REACHED();
  }
  
  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  if (args[0] == SYS_EXIT) {
    validate_pointer_and_exit_if_false(f, &args[1]);
    exit_process(f, args[1]);
  }

  if (args[0] == SYS_WRITE) {
    validate_pointer_and_exit_if_false(f, &args[1]);
    int fd = args[1];
    validate_pointer_and_exit_if_false(f, &args[2]);
    void* buffer = (void*) args[2];
    uint32_t size = args[3];
    validate_buffer_and_exit_if_false(f, buffer, size);

    if (fd == STDIN_FILENO) {
      f->eax = -1;
      return;
    }

    if (fd < 0 || fd >= MAX_FILE_DESCRIPTOR) {
      f->eax = -1;
      return;
    }

    if (fd == STDOUT_FILENO) {
      putbuf(buffer, size);
      f->eax = size;
      return;
    }

    if (thread_current()->pcb->fd_table[fd] == NULL) {
      f->eax = -1;
      return;
    }

    lock_acquire(&global_fs_lock);

    struct file* file_to_write = thread_current()->pcb->fd_table[fd];

    f->eax = file_write(file_to_write, buffer, size);

    lock_release(&global_fs_lock);
  }

  if (args[0] == SYS_PRACTICE) {
    validate_pointer_and_exit_if_false(f, &args[1]);
    f->eax = args[1] + 1;
  }

  if (args[0] == SYS_HALT) {
    shutdown_power_off();
  }

  if (args[0] == SYS_EXEC) {
    validate_pointer_and_exit_if_false(f, &args[1]);
    char* cmd_line = (char *)args[1];
    validate_string_and_exit_if_false(f, cmd_line);
    pid_t pid = process_execute(cmd_line);
    f->eax = pid == TID_ERROR ? -1 : pid;
  }

  if (args[0] == SYS_WAIT) {
    pid_t child_pid = args[1];
    int exit_code = process_wait(child_pid);
    f->eax = exit_code;
  }

  if (args[0] == SYS_CREATE) {
    lock_acquire(&global_fs_lock);
    validate_pointer_and_exit_if_false(f, &args[1]);
    char* file_name = (char *)args[1];
    validate_string_and_exit_if_false(f, file_name);

    unsigned initial_size = args[2];
    bool success = filesys_create(file_name, initial_size);
    f->eax = success;
    lock_release(&global_fs_lock);
  }

  if (args[0] == SYS_READ) {
    validate_pointer_and_exit_if_false(f, &args[1]);
    int fd = args[1];
    validate_pointer_and_exit_if_false(f, &args[2]);
    char *buffer = (char *)args[2];
    int size = args[3];
    validate_buffer_and_exit_if_false(f, buffer, size);

    lock_acquire(&global_fs_lock);

    if (fd == STDIN_FILENO) {
      f->eax = read_from_input(buffer, size);
      lock_release(&global_fs_lock);
      return;
    }

    if (fd == STDOUT_FILENO) {
      f->eax = -1;
      lock_release(&global_fs_lock);
      return;
    }

    if (fd < 0 || fd >= MAX_FILE_DESCRIPTOR) {
      f->eax = -1;
      lock_release(&global_fs_lock);
      return;
    }

    struct file **fd_table = thread_current()->pcb->fd_table;
    if (fd_table[fd] == NULL) {
      f->eax = -1;
      lock_release(&global_fs_lock);
      return;
    }
  
    f->eax = file_read(fd_table[fd], buffer, size);
    lock_release(&global_fs_lock);
    return;
  }

  if (args[0] == SYS_OPEN) {
    validate_pointer_and_exit_if_false(f, &args[1]);
    char* file_name = (char *)args[1];
    validate_string_and_exit_if_false(f, file_name);
    
    struct thread* curr_thread = thread_current();
    
    lock_acquire(&global_fs_lock);
    struct file* open_file = filesys_open(file_name);

    if (open_file == NULL) {
      f->eax = -1;
      lock_release(&global_fs_lock);
      return;
    }

    int free_fd = is_fd_table_full();
    
    if (free_fd == -1) {
      file_close(open_file);
      f->eax = -1;
      lock_release(&global_fs_lock);
      return;
    }

    curr_thread->pcb->fd_table[free_fd] = open_file;
    
    f->eax = free_fd;
    lock_release(&global_fs_lock);
  }

  if (args[0] == SYS_FILESIZE) {
    validate_pointer_and_exit_if_false(f, &args[1]);
    int fd = args[1];

    lock_acquire(&global_fs_lock);

    if (fd < 2 || fd >= MAX_FILE_DESCRIPTOR) {
      f->eax = -1;
      lock_release(&global_fs_lock);
      return;
    }

    struct file **fd_table = thread_current()->pcb->fd_table;
    if (fd_table[fd] == NULL) {
      f->eax = -1;
      lock_release(&global_fs_lock);
      return;
    }

    f->eax = file_length(fd_table[fd]);
    lock_release(&global_fs_lock);
  }

  if (args[0] == SYS_CLOSE) {
    validate_pointer_and_exit_if_false(f, &args[1]);
  
    int fd = args[1];

    lock_acquire(&global_fs_lock);

    if (fd < 0 || fd >= MAX_FILE_DESCRIPTOR) {
      f->eax = -1;
      lock_release(&global_fs_lock);
      return;
    }
    
    struct thread *t = thread_current();
    if (t->pcb->fd_table[fd] != NULL) {
      file_close(t->pcb->fd_table[fd]);
      t->pcb->fd_table[fd] = NULL;
    }

    lock_release(&global_fs_lock);
  }

  if (args[0] == SYS_TELL) {
    validate_pointer_and_exit_if_false(f, &args[1]);
    int fd = args[1];

    if (fd < 2 || fd >= MAX_FILE_DESCRIPTOR) {
      f->eax = -1;
      return;
    }

    struct file* current_file = thread_current()->pcb->fd_table[fd];

    if (current_file == NULL) {
      f->eax = -1;
      return;
    }
    
    f->eax = file_tell(current_file);
  }

  if (args[0] == SYS_SEEK) {
    validate_pointer_and_exit_if_false(f, &args[1]);
    int fd = args[1];

    if (fd < 2 || fd >= MAX_FILE_DESCRIPTOR) {
      f->eax = -1;
      return;
    }

    validate_pointer_and_exit_if_false(f, &args[2]);

    int pos = args[2];

    struct file* current_file = thread_current()->pcb->fd_table[fd];

    if (current_file == NULL) {
      f->eax = -1;
      return;
    }
    
    file_seek(current_file, pos);
  }

  if (args[0] == SYS_REMOVE) {
    validate_pointer_and_exit_if_false(f, &args[1]);
    char* file_name = (char *)args[1];
    validate_string_and_exit_if_false(f, file_name);

    lock_acquire(&global_fs_lock);

    f->eax = filesys_remove(file_name);

    lock_release(&global_fs_lock);
  }

  if (args[0] == SYS_FORK) {
    lock_acquire(&global_fs_lock);

    pid_t pid = process_fork(f);  // Pass the interrupt frame
    f->eax = pid == TID_ERROR ? -1 : pid;
    
    lock_release(&global_fs_lock);
  }
}
