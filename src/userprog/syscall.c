#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

bool static validate_pointer(void* arg) {
  char *pointer = (char *) arg;
  if (!is_user_vaddr(pointer) || !is_user_vaddr(pointer + 3)) return false;
  
  if (!pagedir_get_page(thread_current()->pcb->pagedir, pointer)) return false;
  if (!pagedir_get_page(thread_current()->pcb->pagedir, pointer + 1)) return false;
  if (!pagedir_get_page(thread_current()->pcb->pagedir, pointer + 2)) return false;
  if (!pagedir_get_page(thread_current()->pcb->pagedir, pointer + 3)) return false;

  return true;
}

void static validate_and_exit_if_false(struct intr_frame* f, void * arg) {
  if (!validate_pointer(arg)) {
    f->eax = -1;
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
    process_exit();
  }
}

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = ((uint32_t*)f->esp);
  
  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  // printf("System call number: %d\n", args[0]);

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
    process_exit();
  }

  if (args[0] == SYS_WRITE) {
    void *buffer = (void *)args[2];
    validate_and_exit_if_false(f, buffer);
    uint32_t size = args[3];
    putbuf(buffer, size);
    f->eax = size;
  }

  if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
  }
}
