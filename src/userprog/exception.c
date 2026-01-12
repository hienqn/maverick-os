/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                        EXCEPTION HANDLERS                                 ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  This module handles CPU exceptions caused by user programs, such as     ║
 * ║  divide-by-zero, invalid opcode, and page faults.                        ║
 * ║                                                                          ║
 * ║  EXCEPTION vs INTERRUPT:                                                 ║
 * ║  ───────────────────────                                                 ║
 * ║  • Exception: Caused by executing instruction (sync with CPU)            ║
 * ║    - Divide by zero, invalid opcode, page fault, etc.                    ║
 * ║  • Interrupt: Caused by external hardware (async)                        ║
 * ║    - Timer tick, keyboard, disk I/O, etc.                                ║
 * ║                                                                          ║
 * ║  EXCEPTION TYPES:                                                        ║
 * ║  ────────────────                                                        ║
 * ║                                                                          ║
 * ║    ┌─────────┬───────────────────────────────────────────────────────┐   ║
 * ║    │ Vector  │ Exception                                             │   ║
 * ║    ├─────────┼───────────────────────────────────────────────────────┤   ║
 * ║    │    0    │ #DE Divide Error                                      │   ║
 * ║    │    1    │ #DB Debug Exception                                   │   ║
 * ║    │    3    │ #BP Breakpoint (INT3)                                 │   ║
 * ║    │    4    │ #OF Overflow (INTO)                                   │   ║
 * ║    │    5    │ #BR BOUND Range Exceeded                              │   ║
 * ║    │    6    │ #UD Invalid Opcode                                    │   ║
 * ║    │    7    │ #NM Device Not Available (no FPU)                     │   ║
 * ║    │   11    │ #NP Segment Not Present                               │   ║
 * ║    │   12    │ #SS Stack Fault                                       │   ║
 * ║    │   13    │ #GP General Protection (catch-all)                    │   ║
 * ║    │   14    │ #PF Page Fault                                        │   ║
 * ║    │   16    │ #MF x87 FPU Error                                     │   ║
 * ║    │   19    │ #XF SIMD Floating-Point                               │   ║
 * ║    └─────────┴───────────────────────────────────────────────────────┘   ║
 * ║                                                                          ║
 * ║  DPL (Descriptor Privilege Level):                                       ║
 * ║  ─────────────────────────────────                                       ║
 * ║  • DPL=3: User can invoke via INT instruction (e.g., breakpoint)         ║
 * ║  • DPL=0: User cannot invoke directly, but exceptions still trigger      ║
 * ║                                                                          ║
 * ║  PintOS currently kills any process that causes an exception.            ║
 * ║  In a full OS, some exceptions would be delivered as signals.            ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#ifdef VM
#include "vm/vm.h"
#endif

#ifdef ARCH_RISCV64
#include "arch/riscv64/csr.h"
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * EXCEPTION STATISTICS
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Number of page faults processed. */
static long long page_fault_cnt;

/* ═══════════════════════════════════════════════════════════════════════════
 * FORWARD DECLARATIONS
 * ═══════════════════════════════════════════════════════════════════════════*/

#ifndef ARCH_RISCV64
/* x86-specific exception handlers (use x86 intr_frame fields) */
static void kill(struct intr_frame*);
static void page_fault(struct intr_frame*);
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * EXCEPTION INITIALIZATION
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Registers handlers for interrupts that can be caused by user programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void exception_init(void) {
#ifdef ARCH_RISCV64
  /* RISC-V exception handlers are registered in arch/riscv64/intr.c.
     Page fault handling will be added in Task 3. For now, RISC-V
     exceptions are handled by the default trap handler. */
#else
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int(5, 3, INTR_ON, kill, "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int(7, 0, INTR_ON, kill, "#NM Device Not Available Exception");
  intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int(19, 0, INTR_ON, kill, "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
#endif
}

/* Prints exception statistics. */
void exception_print_stats(void) { printf("Exception: %lld page faults\n", page_fault_cnt); }

/* ═══════════════════════════════════════════════════════════════════════════
 * EXCEPTION HANDLERS (x86-specific)
 * ═══════════════════════════════════════════════════════════════════════════*/

#ifndef ARCH_RISCV64
/* Handler for an exception (probably) caused by a user process.
   Terminates the user process if it caused the exception.
   Panics the kernel if the exception originated in kernel code. */
static void kill(struct intr_frame* f) {
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */

  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs) {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf("%s: dying due to interrupt %#04x (%s).\n", thread_name(), f->vec_no,
             intr_name(f->vec_no));
      intr_dump_frame(f);
      f->eax = -1;
      thread_current()->pcb->my_status->exit_code = -1;
      printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
      process_exit();
      NOT_REACHED();

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame(f);
      PANIC("Kernel bug - unexpected interrupt in kernel");

    default:
      /* Some other code segment? Shouldn't happen. Panic the kernel. */
      printf("Interrupt %#04x (%s) in unknown segment %04x\n", f->vec_no, intr_name(f->vec_no),
             f->cs);
      PANIC("Kernel bug - unexpected interrupt in kernel");
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PAGE FAULT HANDLER
 * ─────────────────────────────────────────────────────────────────────────────
 * Page faults are special - they can be "good" (demand paging, COW) or
 * "bad" (invalid memory access). Currently we treat all as bad.
 *
 * For virtual memory (Project 3), this handler would:
 *   1. Check if the address should be valid (in mmap region, stack growth, etc.)
 *   2. If valid, load the page and return
 *   3. If invalid, kill the process
 *
 * Error code bits (PF_* macros in exception.h):
 *   - PF_P: 0 = not-present page, 1 = protection violation
 *   - PF_W: 0 = read, 1 = write
 *   - PF_U: 0 = kernel access, 1 = user access
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void page_fault(struct intr_frame* f) {
  bool not_present; /* True: not-present page, false: writing r/o page. */
  bool write;       /* True: access was write, false: access was read. */
  bool user;        /* True: access by user, false: access by kernel. */
  void* fault_addr; /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm("movl %%cr2, %0" : "=r"(fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

#ifdef VM
  /* Get the stack pointer. For user faults, use the saved ESP from the
     interrupt frame. For kernel faults (e.g., during syscall), we need
     the user ESP that was saved when entering kernel mode. */
  void* esp = user ? (void*)f->esp : thread_current()->syscall_esp;

  /* Try to handle the fault via the VM system. */
  if (vm_handle_fault(fault_addr, user, write, not_present, esp))
    return; /* Fault handled successfully - return to user. */
#endif

  /* VM couldn't handle the fault (or VM disabled) - this is an invalid access.
     Check if this is kernel code accessing user memory (syscall context).
     In this case, we should kill the user process, not panic the kernel. */
  if (!user && is_user_vaddr(fault_addr)) {
    /* Kernel code tried to access invalid user memory (bad syscall pointer).
       Kill the user process with exit code -1. */
    f->eax = -1;
    thread_current()->pcb->my_status->exit_code = -1;
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
    process_exit();
    NOT_REACHED();
  }

  /* User fault or kernel fault on kernel address - use standard kill. */
  printf("Page fault at %p: %s error %s page in %s context.\n", fault_addr,
         not_present ? "not present" : "rights violation", write ? "writing" : "reading",
         user ? "user" : "kernel");
  kill(f);
}
#endif /* !ARCH_RISCV64 */

/* ═══════════════════════════════════════════════════════════════════════════
 * RISC-V PAGE FAULT HANDLER
 * ═══════════════════════════════════════════════════════════════════════════*/

#ifdef ARCH_RISCV64

/* RISC-V page fault handler.
   Called from intr.c for SCAUSE_LOAD_PAGE_FAULT, SCAUSE_STORE_PAGE_FAULT,
   and SCAUSE_INST_PAGE_FAULT.

   RISC-V provides fault information differently than x86:
   - Fault address in stval (not CR2)
   - Fault type encoded in scause (not error_code bits)
   - User/kernel mode in sstatus.SPP bit */
void riscv_page_fault(struct intr_frame* f) {
  void* fault_addr; /* Faulting virtual address */
  bool not_present; /* True: not-present page */
  bool write;       /* True: access was write */
  bool user;        /* True: access by user mode */

  /* Get faulting address from stval CSR (already saved in frame) */
  fault_addr = (void*)f->stval;

  /* Count page faults */
  page_fault_cnt++;

  /* Determine cause from scause.
     RISC-V distinguishes load/store/instruction page faults directly. */
  write = (f->scause == SCAUSE_STORE_PAGE_FAULT);
  not_present = true; /* RISC-V page faults are always "not present" */

  /* Determine user/kernel from sstatus.SPP (saved in frame).
     SPP=0 means trap from user mode, SPP=1 means supervisor mode. */
  user = ((f->sstatus & SSTATUS_SPP) == 0);

#ifdef VM
  /* Get the stack pointer. For user faults, use the saved SP from the
     interrupt frame. For kernel faults (e.g., during syscall), we need
     the user SP that was saved when entering kernel mode. */
  void* esp = user ? (void*)f->sp : thread_current()->syscall_esp;

  /* Try to handle the fault via the VM system. */
  if (vm_handle_fault(fault_addr, user, write, not_present, esp))
    return; /* Fault handled successfully - return to user. */
#endif

  /* VM couldn't handle the fault (or VM disabled).
     Check if this is kernel code accessing user memory (syscall context). */
  if (!user && is_user_vaddr(fault_addr)) {
    /* Kernel code tried to access invalid user memory (bad syscall pointer).
       Kill the user process with exit code -1. */
    f->a0 = -1;
    thread_current()->pcb->my_status->exit_code = -1;
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
    process_exit();
    NOT_REACHED();
  }

  /* User fault or kernel fault on kernel address - fatal. */
  printf("Page fault at %p: %s error %s page in %s context.\n", fault_addr,
         not_present ? "not present" : "rights violation", write ? "writing" : "reading",
         user ? "user" : "kernel");

  if (user) {
    /* Kill the user process */
    f->a0 = -1;
    thread_current()->pcb->my_status->exit_code = -1;
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
    process_exit();
    NOT_REACHED();
  } else {
    /* Kernel bug - panic */
    intr_dump_frame(f);
    PANIC("Kernel bug - page fault in kernel at %p", fault_addr);
  }
}

#endif /* ARCH_RISCV64 */
