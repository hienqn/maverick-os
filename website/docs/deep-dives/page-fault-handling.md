---
sidebar_position: 2
---

import AnimatedFlow from '@site/src/components/AnimatedFlow';
import CodeWalkthrough from '@site/src/components/CodeWalkthrough';

# Page Fault Handling Deep Dive

This page provides a detailed walkthrough of the page fault handler, showing how the kernel responds to memory access exceptions.

## What is a Page Fault?

A page fault occurs when the CPU tries to access a virtual address that:
1. Is not mapped in the page table (present bit = 0)
2. Violates permissions (e.g., write to read-only page)
3. Is accessed from wrong privilege level (user accessing kernel)

## Page Fault Flow

<AnimatedFlow
  title="Page Fault Handler Decision Tree"
  states={[
    { id: 'fault', label: 'Page Fault', description: 'CPU raises exception #14' },
    { id: 'read_info', label: 'Read Fault Info', description: 'Get address from CR2, error code' },
    { id: 'kernel_check', label: 'Kernel Access?', description: 'Was CPU in kernel mode?' },
    { id: 'user_ptr', label: 'User Pointer?', description: 'Is fault address in user space?' },
    { id: 'spt_lookup', label: 'SPT Lookup', description: 'Find supplemental page table entry' },
    { id: 'stack_growth', label: 'Stack Growth?', description: 'Is it near the stack pointer?' },
    { id: 'load_page', label: 'Load Page', description: 'Allocate frame, load data' },
    { id: 'kill', label: 'Kill Process', description: 'Invalid access, terminate' },
    { id: 'resume', label: 'Resume', description: 'Re-execute faulting instruction' },
  ]}
  transitions={[
    { from: 'fault', to: 'read_info', label: '' },
    { from: 'read_info', to: 'kernel_check', label: '' },
    { from: 'kernel_check', to: 'user_ptr', label: 'yes' },
    { from: 'kernel_check', to: 'spt_lookup', label: 'no (user mode)' },
    { from: 'user_ptr', to: 'kill', label: 'yes → bad user ptr' },
    { from: 'user_ptr', to: 'spt_lookup', label: 'no' },
    { from: 'spt_lookup', to: 'load_page', label: 'found' },
    { from: 'spt_lookup', to: 'stack_growth', label: 'not found' },
    { from: 'stack_growth', to: 'load_page', label: 'yes' },
    { from: 'stack_growth', to: 'kill', label: 'no' },
    { from: 'load_page', to: 'resume', label: 'success' },
    { from: 'load_page', to: 'kill', label: 'failure' },
  ]}
/>

## Error Code Bits

The CPU provides an error code with information about the fault:

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | P (Present) | 0 = page not present, 1 = protection violation |
| 1 | W (Write) | 0 = read access, 1 = write access |
| 2 | U (User) | 0 = supervisor mode, 1 = user mode |

## The Page Fault Handler

<CodeWalkthrough
  title="page_fault() in userprog/exception.c"
  code={`static void page_fault(struct intr_frame *f) {
  void *fault_addr;  /* Fault address from CR2 */
  bool not_present;  /* True: not-present, false: permissions */
  bool write;        /* True: write fault, false: read */
  bool user;         /* True: user mode, false: kernel mode */

  /* Read faulting address from CR2 register */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Parse the error code */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  /* CASE 1: Kernel accessing bad user pointer
     If we're in kernel mode and fault on a user address,
     the process passed a bad pointer to a syscall. */
  if (!user && is_user_vaddr(fault_addr)) {
    f->eax = -1;
    f->eip = (void *) thread_current()->user_eip_on_fault;
    return;
  }

  /* CASE 2: Check supplemental page table */
  struct spt *spt = &thread_current()->pcb->spt;
  struct spt_entry *entry = spt_find(spt, fault_addr);

  if (entry != NULL) {
    /* Found in SPT - load the page */
    if (spt_load_page(entry)) {
      return;  /* Success - resume execution */
    }
  }

  /* CASE 3: Stack growth
     Heuristic: fault address is below ESP but within 32 bytes
     (PUSHA can access up to 32 bytes below ESP) */
  void *esp = user ? f->esp : thread_current()->user_esp;
  if (fault_addr >= esp - 32 &&
      fault_addr < PHYS_BASE &&
      fault_addr >= STACK_LIMIT) {
    /* Grow the stack */
    void *page = pg_round_down(fault_addr);
    if (spt_create_zero_page(spt, page, true) &&
        spt_load_page(spt_find(spt, page))) {
      return;  /* Success */
    }
  }

  /* CASE 4: Invalid access - kill the process */
  printf("Page fault at %p: %s error %s page in %s context.\\n",
         fault_addr,
         not_present ? "not present" : "rights violation",
         write ? "writing" : "reading",
         user ? "user" : "kernel");

  thread_current()->pcb->exit_code = -1;
  process_exit();
}`}
  steps={[
    { lines: [1, 2, 3, 4, 5, 6], title: 'Local Variables', description: 'Declare variables to hold fault information extracted from CPU state.' },
    { lines: [8, 9], title: 'Read Fault Address', description: 'The CR2 register contains the virtual address that caused the fault. Use inline assembly to read it.' },
    { lines: [11, 12, 13, 14], title: 'Parse Error Code', description: 'Extract fault type from error code: was page present? was it a write? was CPU in user mode?' },
    { lines: [16, 17, 18, 19, 20, 21, 22, 23], title: 'Handle Bad User Pointer', description: 'If kernel mode code faulted on a user address, a syscall received a bad pointer. Set return value to -1 and return to syscall caller.' },
    { lines: [25, 26, 27, 28, 29, 30, 31, 32, 33, 34], title: 'SPT Lookup and Load', description: 'Look up the faulting address in the supplemental page table. If found, load the page (may involve reading from file, swap, or zero-filling).' },
    { lines: [36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48], title: 'Stack Growth', description: 'If address is near ESP (within 32 bytes below), treat as stack growth. Create a zero-filled page and load it.' },
    { lines: [50, 51, 52, 53, 54, 55, 56, 57, 58], title: 'Invalid Access', description: 'If none of the above cases apply, the access is invalid. Print diagnostic message and terminate the process.' },
  ]}
/>

## Loading a Page (spt_load_page)

When a valid SPT entry is found, `spt_load_page` brings the data into memory:

```c
bool spt_load_page(struct spt_entry *entry) {
  /* Allocate a physical frame */
  void *kpage = frame_alloc(entry->upage, entry->writable);
  if (kpage == NULL)
    return false;

  /* Load data based on page type */
  switch (entry->status) {
    case PAGE_ZERO:
      /* Zero-fill the frame */
      memset(kpage, 0, PGSIZE);
      break;

    case PAGE_FILE:
      /* Read from file */
      file_seek(entry->file, entry->file_offset);
      if (file_read(entry->file, kpage, entry->read_bytes)
          != (int) entry->read_bytes) {
        frame_free(kpage);
        return false;
      }
      /* Zero the rest */
      memset(kpage + entry->read_bytes, 0, entry->zero_bytes);
      break;

    case PAGE_SWAP:
      /* Read from swap partition */
      swap_in(entry->swap_slot, kpage);
      entry->pinned_dirty = true;  /* Must go back to swap */
      break;

    default:
      PANIC("Invalid page status");
  }

  /* Install in page table */
  if (!pagedir_set_page(thread_current()->pagedir,
                        entry->upage, kpage, entry->writable)) {
    frame_free(kpage);
    return false;
  }

  /* Update SPT entry */
  entry->status = PAGE_FRAME;
  entry->kpage = kpage;

  return true;
}
```

## Stack Growth Heuristic

### Why 32 Bytes?

The x86 `PUSHA` instruction pushes all 8 general-purpose registers (32 bytes) in one atomic operation. The stack pointer isn't decremented until all pushes complete, so faults can occur up to 32 bytes below ESP.

```asm
PUSHA    ; Pushes EAX, ECX, EDX, EBX, original ESP, EBP, ESI, EDI
         ; All 8 registers = 32 bytes
```

### Stack Limit

We also check that the fault address is above `STACK_LIMIT` to prevent unbounded stack growth:

```c
#define STACK_LIMIT (PHYS_BASE - (8 * 1024 * 1024))  /* 8 MB max stack */
```

## Handling Kernel-Mode Faults on User Addresses

This is crucial for syscall safety:

```c
/* In syscall_handler */
void syscall_handler(struct intr_frame *f) {
  /* Save info for page fault handler */
  thread_current()->user_esp = f->esp;

  /* If we fault while accessing user buffer... */
  int *args = (int *) f->esp;  /* Might fault here */
  /* ...the page fault handler will catch it */
}
```

If the user passes an invalid pointer:
1. Kernel tries to read from it
2. Page fault occurs in kernel mode
3. Handler sees kernel-mode fault on user address
4. Handler sets `eax = -1` and returns to syscall
5. Syscall returns `-1` to user

## Frame Allocation and Eviction

`frame_alloc` may need to evict a page if memory is full:

```c
void *frame_alloc(void *upage, bool writable) {
  /* Try to get a free frame */
  void *kpage = palloc_get_page(PAL_USER);

  if (kpage == NULL) {
    /* No free frames - must evict */
    kpage = frame_evict();
    if (kpage == NULL)
      return NULL;  /* All frames pinned */
  }

  /* Track in frame table */
  struct frame_entry *fe = malloc(sizeof *fe);
  fe->kpage = kpage;
  fe->upage = upage;
  fe->owner = thread_current();
  fe->pinned = true;  /* Pin during setup */
  list_push_back(&frame_list, &fe->elem);

  return kpage;
}
```

## Common Bugs and Debugging

### Infinite Page Fault Loop

**Symptom**: Same address faults repeatedly

**Cause**: Page loaded but not installed in page table, or page table not flushed

**Fix**: Verify `pagedir_set_page` succeeds, check TLB invalidation

### Stack Growth Not Working

**Symptom**: Stack faults not handled, process killed

**Cause**: ESP not saved correctly, or heuristic too strict

**Fix**: Ensure `user_esp` is saved in syscall handler; check bounds

### Swap Read Failure

**Symptom**: Page loads garbage after swap-in

**Cause**: Wrong swap slot, or slot freed prematurely

**Fix**: Verify `swap_slot` saved correctly when swapping out

## Related Topics

- [Virtual Memory Concepts](/docs/concepts/virtual-memory) - Theory behind paging
- [Project 3: Virtual Memory](/docs/projects/vm/overview) - Implementation guide
- [System Calls](/docs/concepts/system-calls) - User pointer validation
