/* arch/riscv64/userprog.h - RISC-V user program support.
 *
 * Provides user mode support including:
 * - User page table management
 * - ELF64 loading
 * - User/kernel mode transitions
 * - Syscall dispatch
 */

#ifndef ARCH_RISCV64_USERPROG_H
#define ARCH_RISCV64_USERPROG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "arch/riscv64/intr.h"

/* User virtual address space layout for Sv39.
 * 
 * Sv39 provides 39-bit virtual addresses (512GB).
 * We split the address space:
 *   0x0000000000000000 - 0x0000003FFFFFFFFF : User space (256GB)
 *   0xFFFFFFC000000000 - 0xFFFFFFFFFFFFFFFF : Kernel space (256GB)
 *
 * User space layout:
 *   0x0000000000400000 : Program text start (4MB)
 *   0x0000000010000000 : Heap start (256MB)
 *   0x0000003FBFFFF000 : User stack top (grows down)
 *   0x0000003FC0000000 : User stack guard page
 */

/* Include memlayout.h for USER_STACK_TOP and other layout definitions */
#include "arch/riscv64/memlayout.h"

#define USER_TEXT_START 0x0000000000400000UL /* 4MB - program start */
#define USER_HEAP_START 0x0000000010000000UL /* 256MB - heap start */
/* USER_STACK_TOP is defined in memlayout.h */
#define USER_STACK_SIZE (8 * 1024 * 1024) /* 8MB max stack */
#define USER_STACK_BOTTOM (USER_STACK_TOP - USER_STACK_SIZE)

/* Page table entry count */
#define PT_ENTRIES 512

/* User page table (3-level Sv39) */
struct user_page_table {
  uint64_t* root; /* L2 page table (root) - physical address */
  uint64_t asid;  /* Address Space ID for TLB tagging */
};

/* Process control block extensions for RISC-V */
struct riscv_pcb {
  struct user_page_table pt; /* User page table */
  uint64_t entry_point;      /* Program entry point */
  uint64_t user_sp;          /* User stack pointer */
  uint64_t brk;              /* Program break (heap end) */
};

/* ELF64 header structures */
#define ELF_MAGIC 0x464C457F /* "\x7fELF" */

/* ELF64 file header */
struct elf64_ehdr {
  uint8_t e_ident[16];  /* ELF identification */
  uint16_t e_type;      /* Object file type */
  uint16_t e_machine;   /* Machine type */
  uint32_t e_version;   /* Object file version */
  uint64_t e_entry;     /* Entry point address */
  uint64_t e_phoff;     /* Program header offset */
  uint64_t e_shoff;     /* Section header offset */
  uint32_t e_flags;     /* Processor-specific flags */
  uint16_t e_ehsize;    /* ELF header size */
  uint16_t e_phentsize; /* Program header entry size */
  uint16_t e_phnum;     /* Number of program headers */
  uint16_t e_shentsize; /* Section header entry size */
  uint16_t e_shnum;     /* Number of section headers */
  uint16_t e_shstrndx;  /* Section name string table index */
} __attribute__((packed));

/* ELF64 program header */
struct elf64_phdr {
  uint32_t p_type;   /* Segment type */
  uint32_t p_flags;  /* Segment flags */
  uint64_t p_offset; /* Offset in file */
  uint64_t p_vaddr;  /* Virtual address */
  uint64_t p_paddr;  /* Physical address (unused) */
  uint64_t p_filesz; /* Size in file */
  uint64_t p_memsz;  /* Size in memory */
  uint64_t p_align;  /* Alignment */
} __attribute__((packed));

/* ELF identification indices */
#define EI_MAG0 0    /* Magic number byte 0 */
#define EI_MAG1 1    /* Magic number byte 1 */
#define EI_MAG2 2    /* Magic number byte 2 */
#define EI_MAG3 3    /* Magic number byte 3 */
#define EI_CLASS 4   /* File class */
#define EI_DATA 5    /* Data encoding */
#define EI_VERSION 6 /* File version */

/* ELF classes */
#define ELFCLASS64 2 /* 64-bit objects */

/* ELF data encodings */
#define ELFDATA2LSB 1 /* Little-endian */

/* ELF machine types */
#define EM_RISCV 243 /* RISC-V */

/* ELF types */
#define ET_EXEC 2 /* Executable file */
#define ET_DYN 3  /* Shared object file */

/* Program header types */
#define PT_NULL 0    /* Unused */
#define PT_LOAD 1    /* Loadable segment */
#define PT_DYNAMIC 2 /* Dynamic linking info */
#define PT_INTERP 3  /* Interpreter path */
#define PT_NOTE 4    /* Auxiliary info */
#define PT_PHDR 6    /* Program header table */

/* Program header flags */
#define PF_X 0x1 /* Executable */
#define PF_W 0x2 /* Writable */
#define PF_R 0x4 /* Readable */

/* Function prototypes */

/* Page table management */
struct user_page_table* upt_create(void);
void upt_destroy(struct user_page_table* pt);
bool upt_map_page(struct user_page_table* pt, uint64_t va, uint64_t pa, bool writable,
                  bool executable);
bool upt_unmap_page(struct user_page_table* pt, uint64_t va);
uint64_t upt_lookup(struct user_page_table* pt, uint64_t va);
void upt_activate(struct user_page_table* pt);

/* ELF loading */
bool elf64_validate(const struct elf64_ehdr* ehdr);
bool elf64_load(const void* elf_data, size_t elf_size, struct user_page_table* pt, uint64_t* entry);

/* User mode transitions */
void user_entry(struct intr_frame* f) __attribute__((noreturn));
/* syscall_handler is now shared - declared in userprog/syscall.h */

/* Process support */
bool process_setup_stack(struct user_page_table* pt, int argc, char** argv, uint64_t* sp);

#endif /* ARCH_RISCV64_USERPROG_H */
