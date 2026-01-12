/* arch/riscv64/userprog.c - RISC-V user program support.
 *
 * Implements user page table management, ELF64 loading,
 * and user mode transitions for RISC-V.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "arch/riscv64/userprog.h"
#include "arch/riscv64/pte.h"
#include "arch/riscv64/csr.h"
#include "arch/riscv64/memlayout.h"
#include "arch/riscv64/vaddr.h"
#include "arch/riscv64/mmu.h"

/* Forward declarations for console output */
extern void sbi_console_putchar(int c);

static void console_putchar(char c) { sbi_console_putchar(c); }

static void console_puts(const char* s) {
  while (*s) {
    if (*s == '\n')
      console_putchar('\r');
    console_putchar(*s++);
  }
}

static void console_puthex(uint64_t val) {
  static const char hex[] = "0123456789abcdef";
  char buf[19];
  buf[0] = '0';
  buf[1] = 'x';
  for (int i = 0; i < 16; i++) {
    buf[17 - i] = hex[val & 0xf];
    val >>= 4;
  }
  buf[18] = '\0';
  console_puts(buf);
}

/* ==========================================================================
 * Simple Page Allocator (for Phase 6 testing)
 *
 * This is a minimal allocator for user page tables. In a full implementation,
 * this would use the kernel's palloc. For Phase 6 bootstrap, we use a
 * statically allocated pool.
 * ========================================================================== */

#define USER_PAGE_POOL_SIZE 64 /* Number of pages in pool */

/* Statically allocated page pool */
static uint8_t user_page_pool[USER_PAGE_POOL_SIZE][PGSIZE] __attribute__((aligned(PGSIZE)));
static bool user_page_used[USER_PAGE_POOL_SIZE];

/* Allocate a zeroed page from the pool */
static void* alloc_user_page(void) {
  for (int i = 0; i < USER_PAGE_POOL_SIZE; i++) {
    if (!user_page_used[i]) {
      user_page_used[i] = true;
      memset(user_page_pool[i], 0, PGSIZE);
      return user_page_pool[i];
    }
  }
  console_puts("ERROR: User page pool exhausted\n");
  return NULL;
}

/* Free a page back to the pool */
static void free_user_page(void* page) {
  uintptr_t addr = (uintptr_t)page;
  uintptr_t pool_start = (uintptr_t)user_page_pool;
  uintptr_t pool_end = pool_start + sizeof(user_page_pool);

  if (addr >= pool_start && addr < pool_end) {
    int idx = (addr - pool_start) / PGSIZE;
    user_page_used[idx] = false;
  }
}

/* ==========================================================================
 * User Page Table Management
 * ========================================================================== */

/* ASID counter for address space IDs */
static uint64_t next_asid = 1;

/*
 * upt_create - Create a new user page table.
 *
 * Allocates a 3-level Sv39 page table for a user process.
 * The kernel mapping is copied to allow kernel access during syscalls.
 *
 * Returns NULL on allocation failure.
 */
struct user_page_table* upt_create(void) {
  struct user_page_table* upt = (struct user_page_table*)alloc_user_page();
  if (!upt)
    return NULL;

  /* The upt struct is stored at the start of its allocation, but we need
   * a separate page for the actual root page table */
  uint64_t* root = (uint64_t*)alloc_user_page();
  if (!root) {
    free_user_page(upt);
    return NULL;
  }

  upt->root = root;
  upt->asid = next_asid++;
  if (next_asid > 0xFFFF) /* ASID is 16 bits */
    next_asid = 1;

  /* Copy kernel mappings from the kernel page table.
   * Kernel entries are in the upper half of the address space.
   * For Sv39, VPN[2] indices 256-511 are kernel space. */
  uint64_t* kernel_pt = mmu_get_kernel_pt();
  for (int i = 256; i < PT_ENTRIES; i++) {
    root[i] = kernel_pt[i];
  }

  return upt;
}

/*
 * upt_destroy - Free a user page table and all its pages.
 *
 * Frees all page table levels and mapped user pages.
 */
void upt_destroy(struct user_page_table* upt) {
  if (!upt || !upt->root)
    return;

  uint64_t* l2 = upt->root;

  /* Free user space page tables (indices 0-255) */
  for (int i2 = 0; i2 < 256; i2++) {
    if (!pte_is_valid(l2[i2]))
      continue;
    if (pte_is_leaf(l2[i2]))
      continue; /* Gigapage - don't recurse */

    uint64_t* l1 = pte_get_page(l2[i2]);
    for (int i1 = 0; i1 < PT_ENTRIES; i1++) {
      if (!pte_is_valid(l1[i1]))
        continue;
      if (pte_is_leaf(l1[i1]))
        continue; /* Megapage */

      uint64_t* l0 = pte_get_page(l1[i1]);
      for (int i0 = 0; i0 < PT_ENTRIES; i0++) {
        if (pte_is_valid(l0[i0]) && pte_is_leaf(l0[i0])) {
          /* Free the mapped page */
          void* page = pte_get_page(l0[i0]);
          free_user_page(page);
        }
      }
      free_user_page(l0);
    }
    free_user_page(l1);
  }

  free_user_page(l2);
  free_user_page(upt);
}

/*
 * upt_map_page - Map a page in the user address space.
 *
 * @upt: User page table
 * @va: Virtual address (must be page-aligned, in user space)
 * @pa: Physical address (must be page-aligned)
 * @writable: Whether the page is writable
 * @executable: Whether the page is executable
 *
 * Returns true on success, false on failure.
 */
bool upt_map_page(struct user_page_table* upt, uint64_t va, uint64_t pa, bool writable,
                  bool executable) {
  if (!upt || !upt->root)
    return false;
  if ((va & PGMASK) != 0 || (pa & PGMASK) != 0)
    return false;
  if (va >= USER_VIRT_TOP)
    return false; /* Must be user address */

  uint64_t* l2 = upt->root;
  unsigned idx2 = vpn2((void*)va);
  unsigned idx1 = vpn1((void*)va);
  unsigned idx0 = vpn0((void*)va);

  /* Get or create L1 table */
  uint64_t* l1;
  if (!pte_is_valid(l2[idx2])) {
    l1 = (uint64_t*)alloc_user_page();
    if (!l1)
      return false;
    l2[idx2] = pte_create_pointer(l1);
  } else if (pte_is_leaf(l2[idx2])) {
    return false; /* Conflict with gigapage */
  } else {
    l1 = pte_get_page(l2[idx2]);
  }

  /* Get or create L0 table */
  uint64_t* l0;
  if (!pte_is_valid(l1[idx1])) {
    l0 = (uint64_t*)alloc_user_page();
    if (!l0)
      return false;
    l1[idx1] = pte_create_pointer(l0);
  } else if (pte_is_leaf(l1[idx1])) {
    return false; /* Conflict with megapage */
  } else {
    l0 = pte_get_page(l1[idx1]);
  }

  /* Set the L0 entry */
  uint64_t flags = PTE_V | PTE_R | PTE_U | PTE_A;
  if (writable)
    flags |= PTE_W | PTE_D;
  if (executable)
    flags |= PTE_X;

  l0[idx0] = pte_create(pa, flags);
  return true;
}

/*
 * upt_unmap_page - Unmap a page from the user address space.
 *
 * Returns true if the page was mapped and is now unmapped.
 */
bool upt_unmap_page(struct user_page_table* upt, uint64_t va) {
  if (!upt || !upt->root)
    return false;

  uint64_t* l2 = upt->root;
  unsigned idx2 = vpn2((void*)va);

  if (!pte_is_valid(l2[idx2]) || pte_is_leaf(l2[idx2]))
    return false;

  uint64_t* l1 = pte_get_page(l2[idx2]);
  unsigned idx1 = vpn1((void*)va);

  if (!pte_is_valid(l1[idx1]) || pte_is_leaf(l1[idx1]))
    return false;

  uint64_t* l0 = pte_get_page(l1[idx1]);
  unsigned idx0 = vpn0((void*)va);

  if (!pte_is_valid(l0[idx0]))
    return false;

  l0[idx0] = 0;
  sfence_vma_va(va);
  return true;
}

/*
 * upt_lookup - Look up the physical address for a virtual address.
 *
 * Returns the physical address, or 0 if not mapped.
 */
uint64_t upt_lookup(struct user_page_table* upt, uint64_t va) {
  if (!upt || !upt->root)
    return 0;

  uint64_t* l2 = upt->root;
  unsigned idx2 = vpn2((void*)va);

  if (!pte_is_valid(l2[idx2]))
    return 0;
  if (pte_is_leaf(l2[idx2])) {
    /* Gigapage */
    return pte_get_pa(l2[idx2]) + (va & (GIGAPAGE_SIZE - 1));
  }

  uint64_t* l1 = pte_get_page(l2[idx2]);
  unsigned idx1 = vpn1((void*)va);

  if (!pte_is_valid(l1[idx1]))
    return 0;
  if (pte_is_leaf(l1[idx1])) {
    /* Megapage */
    return pte_get_pa(l1[idx1]) + (va & (MEGAPAGE_SIZE - 1));
  }

  uint64_t* l0 = pte_get_page(l1[idx1]);
  unsigned idx0 = vpn0((void*)va);

  if (!pte_is_valid(l0[idx0]))
    return 0;

  return pte_get_pa(l0[idx0]) + (va & PGMASK);
}

/*
 * upt_activate - Switch to a user page table.
 *
 * Sets SATP to use this page table with its ASID.
 */
void upt_activate(struct user_page_table* upt) {
  if (!upt || !upt->root)
    return;

  uintptr_t root_pa = vtop(upt->root);
  uint64_t satp = SATP_VALUE(SATP_MODE_SV39, upt->asid, root_pa >> PGBITS);

  fence();
  csr_write(satp, satp);
  sfence_vma_all();
}

/* ==========================================================================
 * ELF64 Loading
 * ========================================================================== */

/*
 * elf64_validate - Validate an ELF64 header for RISC-V execution.
 *
 * Checks magic number, class (64-bit), machine (RISC-V), and type (executable).
 */
bool elf64_validate(const struct elf64_ehdr* ehdr) {
  /* Check magic number */
  if (ehdr->e_ident[EI_MAG0] != 0x7F || ehdr->e_ident[EI_MAG1] != 'E' ||
      ehdr->e_ident[EI_MAG2] != 'L' || ehdr->e_ident[EI_MAG3] != 'F') {
    console_puts("ELF: Invalid magic\n");
    return false;
  }

  /* Check 64-bit */
  if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
    console_puts("ELF: Not 64-bit\n");
    return false;
  }

  /* Check little-endian */
  if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
    console_puts("ELF: Not little-endian\n");
    return false;
  }

  /* Check machine type */
  if (ehdr->e_machine != EM_RISCV) {
    console_puts("ELF: Not RISC-V\n");
    return false;
  }

  /* Check executable type */
  if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
    console_puts("ELF: Not executable\n");
    return false;
  }

  return true;
}

/*
 * elf64_load - Load an ELF64 program into a user page table.
 *
 * @elf_data: Pointer to ELF file contents in memory
 * @elf_size: Size of the ELF file
 * @upt: User page table to load into
 * @entry: Output parameter for entry point address
 *
 * Returns true on success, false on failure.
 */
bool elf64_load(const void* elf_data, size_t elf_size, struct user_page_table* upt,
                uint64_t* entry) {
  const struct elf64_ehdr* ehdr = (const struct elf64_ehdr*)elf_data;

  if (elf_size < sizeof(struct elf64_ehdr))
    return false;

  if (!elf64_validate(ehdr))
    return false;

  /* Check program header offset and count */
  if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
    console_puts("ELF: No program headers\n");
    return false;
  }

  if (ehdr->e_phoff + ehdr->e_phnum * sizeof(struct elf64_phdr) > elf_size) {
    console_puts("ELF: Program headers out of bounds\n");
    return false;
  }

  const struct elf64_phdr* phdr =
      (const struct elf64_phdr*)((const uint8_t*)elf_data + ehdr->e_phoff);

  console_puts("ELF: Loading ");
  console_puthex(ehdr->e_phnum);
  console_puts(" program headers\n");

  /* Load each PT_LOAD segment */
  for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD)
      continue;

    uint64_t vaddr = phdr[i].p_vaddr;
    uint64_t filesz = phdr[i].p_filesz;
    uint64_t memsz = phdr[i].p_memsz;
    uint64_t offset = phdr[i].p_offset;
    uint32_t flags = phdr[i].p_flags;

    console_puts("  Segment: VA=");
    console_puthex(vaddr);
    console_puts(" filesz=");
    console_puthex(filesz);
    console_puts(" memsz=");
    console_puthex(memsz);
    console_puts("\n");

    if (offset + filesz > elf_size) {
      console_puts("ELF: Segment out of file bounds\n");
      return false;
    }

    /* Allocate and map pages for this segment */
    uint64_t va_start = (uint64_t)pg_round_down((void*)vaddr);
    uint64_t va_end = (uint64_t)pg_round_up((void*)(vaddr + memsz));

    for (uint64_t va = va_start; va < va_end; va += PGSIZE) {
      void* page = alloc_user_page();
      if (!page) {
        console_puts("ELF: Out of memory\n");
        return false;
      }

      bool writable = (flags & PF_W) != 0;
      bool executable = (flags & PF_X) != 0;

      if (!upt_map_page(upt, va, vtop(page), writable, executable)) {
        console_puts("ELF: Map failed\n");
        free_user_page(page);
        return false;
      }

      /* Copy file data into page */
      uint64_t page_start = va;
      uint64_t page_end = va + PGSIZE;

      /* Determine overlap with file data */
      uint64_t data_start = vaddr;
      uint64_t data_end = vaddr + filesz;

      if (page_end > data_start && page_start < data_end) {
        /* This page has file data */
        uint64_t copy_start = (data_start > page_start) ? data_start : page_start;
        uint64_t copy_end = (data_end < page_end) ? data_end : page_end;
        uint64_t copy_len = copy_end - copy_start;

        uint64_t file_offset = offset + (copy_start - vaddr);
        uint64_t page_offset = copy_start - page_start;

        memcpy((uint8_t*)page + page_offset, (const uint8_t*)elf_data + file_offset, copy_len);
      }
      /* BSS (memsz > filesz) is already zeroed by alloc_user_page */
    }
  }

  *entry = ehdr->e_entry;
  console_puts("ELF: Entry point at ");
  console_puthex(*entry);
  console_puts("\n");

  return true;
}

/* ==========================================================================
 * User Mode Transitions
 * ========================================================================== */

/*
 * user_entry - Enter user mode.
 *
 * Sets up the interrupt frame and uses sret to jump to user mode.
 * This function does not return.
 *
 * @f: Interrupt frame with user registers set up
 */
void user_entry(struct intr_frame* f) {
  console_puts("Entering user mode at ");
  console_puthex(f->sepc);
  console_puts(" sp=");
  console_puthex(f->sp);
  console_puts("\n");

  /*
   * Set up sstatus for user mode return:
   * - SPP = 0 (return to User mode)
   * - SPIE = 1 (enable interrupts on sret)
   * - SUM = 1 (allow supervisor access to user pages during syscalls)
   */
  f->sstatus = SSTATUS_SPIE | SSTATUS_SUM;

  /*
   * Jump to user mode using sret.
   * We need to:
   * 1. Set sscratch to point to kernel stack (for trap entry)
   * 2. Restore all registers from the frame
   * 3. Execute sret
   */
  asm volatile(
      /* Save kernel stack pointer in sscratch */
      "csrw sscratch, sp\n\t"

      /* Point sp to the interrupt frame */
      "mv sp, %0\n\t"

      /* Restore CSRs */
      "ld t0, 256(sp)\n\t" /* sepc */
      "csrw sepc, t0\n\t"
      "ld t0, 264(sp)\n\t" /* sstatus */
      "csrw sstatus, t0\n\t"

      /* Restore GPRs */
      "ld ra,   0(sp)\n\t"
      "ld gp,  16(sp)\n\t"
      "ld tp,  24(sp)\n\t"
      "ld t0,  32(sp)\n\t"
      "ld t1,  40(sp)\n\t"
      "ld t2,  48(sp)\n\t"
      "ld s0,  56(sp)\n\t"
      "ld s1,  64(sp)\n\t"
      "ld a0,  72(sp)\n\t"
      "ld a1,  80(sp)\n\t"
      "ld a2,  88(sp)\n\t"
      "ld a3,  96(sp)\n\t"
      "ld a4, 104(sp)\n\t"
      "ld a5, 112(sp)\n\t"
      "ld a6, 120(sp)\n\t"
      "ld a7, 128(sp)\n\t"
      "ld s2, 136(sp)\n\t"
      "ld s3, 144(sp)\n\t"
      "ld s4, 152(sp)\n\t"
      "ld s5, 160(sp)\n\t"
      "ld s6, 168(sp)\n\t"
      "ld s7, 176(sp)\n\t"
      "ld s8, 184(sp)\n\t"
      "ld s9, 192(sp)\n\t"
      "ld s10, 200(sp)\n\t"
      "ld s11, 208(sp)\n\t"
      "ld t3, 216(sp)\n\t"
      "ld t4, 224(sp)\n\t"
      "ld t5, 232(sp)\n\t"
      "ld t6, 240(sp)\n\t"

      /* Restore user sp last */
      "ld sp,   8(sp)\n\t"

      /* Return to user mode */
      "sret\n\t"
      :
      : "r"(f)
      : "memory");

  __builtin_unreachable();
}

/* ==========================================================================
 * System Call Handling
 * ========================================================================== */

/* Syscall numbers (must match lib/syscall-nr.h) */
#define SYS_HALT 0
#define SYS_EXIT 1
#define SYS_EXEC 2
#define SYS_WAIT 3
#define SYS_CREATE 4
#define SYS_REMOVE 5
#define SYS_OPEN 6
#define SYS_FILESIZE 7
#define SYS_READ 8
#define SYS_WRITE 9
#define SYS_SEEK 10
#define SYS_TELL 11
#define SYS_CLOSE 12

/* Forward declaration */
extern void sbi_shutdown(void);

/*
 * syscall_handler - Handle a system call from user mode.
 *
 * Called from trap_handler when scause indicates ECALL from U-mode.
 * The syscall number is in a7, arguments in a0-a5, return value goes in a0.
 *
 * @f: Interrupt frame containing user registers
 */
void syscall_handler(struct intr_frame* f) {
  uint64_t syscall_num = f->a7;
  uint64_t arg0 = f->a0;
  uint64_t arg1 = f->a1;
  uint64_t arg2 = f->a2;

  (void)arg1;
  (void)arg2;

  console_puts("Syscall #");
  console_puthex(syscall_num);
  console_puts(" a0=");
  console_puthex(arg0);
  console_puts("\n");

  switch (syscall_num) {
    case SYS_HALT:
      console_puts("SYS_HALT: Shutting down\n");
      sbi_shutdown();
      break;

    case SYS_EXIT:
      console_puts("SYS_EXIT: Exit code ");
      console_puthex(arg0);
      console_puts("\n");
      /* For now, just halt. Full implementation would terminate thread. */
      sbi_shutdown();
      break;

    case SYS_WRITE:
      /* Minimal write for testing: arg0=fd, arg1=buffer, arg2=size */
      if (arg0 == 1) { /* stdout */
        const char* buf = (const char*)arg1;
        size_t size = arg2;
        for (size_t i = 0; i < size; i++) {
          console_putchar(buf[i]);
        }
        f->a0 = size; /* Return bytes written */
      } else {
        f->a0 = -1; /* Error */
      }
      break;

    default:
      console_puts("Unknown syscall\n");
      f->a0 = -1;
      break;
  }

  /* Advance past ecall instruction (4 bytes) */
  f->sepc += 4;
}

/* ==========================================================================
 * Process Stack Setup
 * ========================================================================== */

/*
 * process_setup_stack - Set up the user stack with arguments.
 *
 * Creates the initial user stack with:
 * - argv strings pushed to stack
 * - argv[] array pointing to strings
 * - argc
 *
 * @upt: User page table
 * @argc: Number of arguments
 * @argv: Argument strings
 * @sp: Output - initial stack pointer
 *
 * Returns true on success.
 */
bool process_setup_stack(struct user_page_table* upt, int argc, char** argv, uint64_t* sp) {
  /* Allocate initial stack page */
  void* stack_page = alloc_user_page();
  if (!stack_page)
    return false;

  uint64_t stack_top = USER_STACK_TOP;
  uint64_t stack_page_va = stack_top - PGSIZE;

  if (!upt_map_page(upt, stack_page_va, vtop(stack_page), true, false)) {
    free_user_page(stack_page);
    return false;
  }

  /* Start at top of stack and work down */
  uint64_t stack_ptr = stack_top;

  /* Push argument strings and save their addresses */
  uint64_t* argv_addrs = NULL;
  if (argc > 0) {
    argv_addrs = (uint64_t*)alloc_user_page();
    if (!argv_addrs) {
      return false;
    }

    for (int i = argc - 1; i >= 0; i--) {
      size_t len = strlen(argv[i]) + 1; /* Include null terminator */
      stack_ptr -= len;
      stack_ptr &= ~0xF; /* Align to 16 bytes */

      /* Copy string to stack */
      uint64_t page_offset = stack_ptr - stack_page_va;
      if (page_offset < PGSIZE) {
        memcpy((char*)stack_page + page_offset, argv[i], len);
      }
      argv_addrs[i] = stack_ptr;
    }

    /* Push argv array */
    stack_ptr -= (argc + 1) * sizeof(uint64_t); /* +1 for NULL terminator */
    stack_ptr &= ~0xF;

    uint64_t page_offset = stack_ptr - stack_page_va;
    if (page_offset < PGSIZE) {
      uint64_t* argv_array = (uint64_t*)((char*)stack_page + page_offset);
      for (int i = 0; i < argc; i++) {
        argv_array[i] = argv_addrs[i];
      }
      argv_array[argc] = 0; /* NULL terminator */
    }

    free_user_page(argv_addrs);
  }

  /* Align stack to 16 bytes as required by RISC-V ABI */
  stack_ptr &= ~0xF;

  *sp = stack_ptr;
  return true;
}
