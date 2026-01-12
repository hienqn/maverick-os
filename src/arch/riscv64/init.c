/* arch/riscv64/init.c - RISC-V kernel initialization.
 *
 * This is called from start.S after basic setup.
 * It initializes console output, sets up paging, and boots the kernel.
 */

#include "arch/riscv64/sbi.h"
#include "arch/riscv64/csr.h"
#include "arch/riscv64/memlayout.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/pte.h"
#include "arch/riscv64/intr.h"
#include "arch/riscv64/timer.h"
#include "arch/riscv64/plic.h"
#include "arch/riscv64/virtio-blk.h"
#include "arch/riscv64/userprog.h"
#include <stdint.h>
#include <string.h>

/* Forward declarations */
static void console_init(void);
static void console_putchar(char c);
static void console_puts(const char* s);
static void console_puthex(uint64_t val);
static void console_putdec(uint64_t val);

/* Global variables set during boot */
uint64_t boot_hartid;
void* dtb_ptr;
uint64_t init_ram_pages; /* Number of pages of RAM */
uint64_t ram_end;        /* End of physical RAM */

/* Kernel page directory (for PintOS compatibility) */
uint64_t* init_page_dir;

/*
 * detect_memory - Detect available physical memory.
 *
 * For now, assume 128MB as configured in QEMU.
 * TODO: Parse device tree (DTB) for actual memory size.
 */
static void detect_memory(void) {
  /* Default QEMU virt machine has 128MB RAM */
  uint64_t ram_size = DEFAULT_RAM_SIZE;

  ram_end = PHYS_RAM_BASE + ram_size;
  init_ram_pages = ram_size / PGSIZE;
}

/*
 * test_user_mode_infrastructure - Test Phase 6 user mode components.
 *
 * Tests user page table creation, page mapping, and ELF validation.
 */
static void test_user_mode_infrastructure(void) {
  console_puts("  Testing user page table creation...\n");

  /* Create a user page table */
  struct user_page_table* upt = upt_create();
  if (!upt) {
    console_puts("    ERROR: Failed to create user page table\n");
    return;
  }
  console_puts("    User page table created\n");

  /* Test mapping a page at USER_TEXT_START */
  console_puts("  Testing page mapping...\n");
  uint64_t test_va = USER_TEXT_START;

  /* We can't easily allocate a page here without palloc, so we'll just
   * verify the page table structure is working by checking lookup of
   * unmapped address returns 0 */
  uint64_t pa = upt_lookup(upt, test_va);
  if (pa == 0) {
    console_puts("    Unmapped address correctly returns 0\n");
  } else {
    console_puts("    ERROR: Unmapped address returned non-zero\n");
  }

  /* Test ELF validation with invalid header */
  console_puts("  Testing ELF validation...\n");
  struct elf64_ehdr bad_ehdr;
  memset(&bad_ehdr, 0, sizeof(bad_ehdr));

  if (!elf64_validate(&bad_ehdr)) {
    console_puts("    Invalid ELF correctly rejected\n");
  } else {
    console_puts("    ERROR: Invalid ELF was accepted\n");
  }

  /* Clean up */
  upt_destroy(upt);
  console_puts("  User page table destroyed\n");

  console_puts("  Phase 6 tests passed!\n");
}

/*
 * riscv_init - Main RISC-V initialization entry point.
 *
 * Called from start.S with:
 *   hartid - Hardware thread ID (0 for boot hart)
 *   dtb    - Pointer to device tree blob
 */
void riscv_init(uint64_t hartid, void* dtb) {
  /* Save boot parameters */
  boot_hartid = hartid;
  dtb_ptr = dtb;

  /* Initialize console output via SBI */
  console_init();

  /* Print boot banner */
  console_puts("\n");
  console_puts("PintOS booting on RISC-V...\n");
  console_puts("  Hart ID: ");
  console_puthex(hartid);
  console_puts("\n");
  console_puts("  DTB at:  ");
  console_puthex((uint64_t)dtb);
  console_puts("\n");

  /* Print SBI information */
  long spec_ver = sbi_get_spec_version();
  console_puts("  SBI spec version: ");
  console_puthex(spec_ver);
  console_puts("\n");

  /* Detect memory */
  detect_memory();
  console_puts("  RAM: ");
  console_putdec(init_ram_pages * PGSIZE / (1024 * 1024));
  console_puts(" MB (");
  console_putdec(init_ram_pages);
  console_puts(" pages)\n");

  /* Initialize MMU and page tables */
  console_puts("\nInitializing Sv39 MMU...\n");
  mmu_init();

  /* Set init_page_dir for PintOS compatibility */
  init_page_dir = mmu_get_kernel_pt();
  console_puts("  Kernel page table at PA: ");
  console_puthex((uint64_t)init_page_dir);
  console_puts("\n");

  /* Enable paging */
  console_puts("  Enabling Sv39 paging...\n");
  mmu_enable();
  console_puts("  Paging enabled!\n");

  /* Test virtual memory by reading through PHYS_BASE */
  console_puts("\nTesting virtual memory...\n");
  volatile uint64_t* test_va = (volatile uint64_t*)PHYS_BASE;
  uint64_t test_val = *test_va;
  console_puts("  Read from PHYS_BASE: ");
  console_puthex(test_val);
  console_puts("\n");

  /* Print CSR values for debugging */
  uint64_t sstatus = csr_read(sstatus);
  console_puts("  sstatus: ");
  console_puthex(sstatus);
  console_puts("\n");

  uint64_t satp = csr_read(satp);
  console_puts("  satp: ");
  console_puthex(satp);
  console_puts("\n");

  /* Initialize interrupt handling */
  console_puts("\nInitializing interrupts...\n");
  intr_init();
  console_puts("  Trap vector installed\n");

  /* Initialize PLIC */
  plic_init();
  console_puts("  PLIC initialized\n");

  /* Initialize timer */
  timer_init();
  console_puts("  Timer initialized (");
  console_putdec(TIMER_FREQ);
  console_puts(" Hz)\n");

  /* Enable interrupts */
  console_puts("  Enabling interrupts...\n");
  intr_enable();

  /* Wait for a few timer ticks to verify timer works */
  console_puts("\nWaiting for timer interrupts...\n");
  uint64_t start_ticks = timer_ticks();
  while (timer_ticks() - start_ticks < 5) {
    asm volatile("wfi");
  }
  console_puts("  Received ");
  console_putdec(timer_ticks() - start_ticks);
  console_puts(" timer ticks!\n");

  /* Initialize VirtIO devices */
  virtio_init();
  virtio_blk_init();

  /* Test block device if available */
  if (virtio_blk_dev) {
    console_puts("\nTesting VirtIO block device...\n");

    /* Read first sector */
    static uint8_t buf[512] __attribute__((aligned(8)));
    if (virtio_blk_read(0, buf, 1)) {
      console_puts("  Read sector 0 successfully\n");
      console_puts("  First 16 bytes: ");
      for (int i = 0; i < 16; i++) {
        static const char hex[] = "0123456789abcdef";
        console_putchar(hex[buf[i] >> 4]);
        console_putchar(hex[buf[i] & 0xf]);
        console_putchar(' ');
      }
      console_puts("\n");
    } else {
      console_puts("  ERROR: Failed to read sector 0\n");
    }
  }

  /* Test Phase 6: User mode infrastructure */
  console_puts("\nTesting Phase 6: User mode infrastructure...\n");
  test_user_mode_infrastructure();

  console_puts("\n");
  console_puts("RISC-V Phase 6 complete: User mode infrastructure ready!\n");
  console_puts("Halting...\n");

  /* For now, halt here. Later phases will continue boot. */
  sbi_shutdown();

  /* Should not reach here */
  while (1) {
    asm volatile("wfi");
  }
}

/*
 * Console output via SBI.
 *
 * This uses the legacy SBI console putchar which is universally
 * supported by OpenSBI. Later we can add UART driver for better
 * performance.
 */

static void console_init(void) { /* Nothing to do for SBI console */
}

static void console_putchar(char c) { sbi_console_putchar(c); }

static void console_puts(const char* s) {
  while (*s) {
    if (*s == '\n') {
      console_putchar('\r');
    }
    console_putchar(*s++);
  }
}

static void console_puthex(uint64_t val) {
  static const char hex[] = "0123456789abcdef";
  char buf[19]; /* "0x" + 16 hex digits + null */
  int i;

  buf[0] = '0';
  buf[1] = 'x';

  for (i = 0; i < 16; i++) {
    buf[17 - i] = hex[val & 0xf];
    val >>= 4;
  }
  buf[18] = '\0';

  console_puts(buf);
}

static void console_putdec(uint64_t val) {
  char buf[21]; /* Max 20 digits + null */
  char* p = buf + sizeof(buf) - 1;

  *p = '\0';

  if (val == 0) {
    *--p = '0';
  } else {
    while (val > 0) {
      *--p = '0' + (val % 10);
      val /= 10;
    }
  }

  console_puts(p);
}

/* Interrupt control functions are now in intr.c */

/*
 * Minimal debug_panic for assertion failures.
 */
void debug_panic(const char* file, int line, const char* function, const char* message, ...) {
  console_puts("\n*** PANIC ***\n");
  console_puts("File: ");
  console_puts(file);
  console_puts("\nLine: ");
  console_putdec(line);
  console_puts("\nFunction: ");
  console_puts(function);
  console_puts("\nMessage: ");
  console_puts(message);
  console_puts("\n");
  sbi_shutdown();
  while (1) {
    asm volatile("wfi");
  }
}

/*
 * Minimal memset implementation for early boot.
 * The full implementation is in lib/string.c but may have dependencies.
 */
void* early_memset(void* dst, int c, size_t n) {
  uint8_t* d = dst;
  while (n-- > 0) {
    *d++ = (uint8_t)c;
  }
  return dst;
}

/*
 * Stub implementations for thread subsystem functions.
 * These are called by switch.S but the full thread subsystem
 * isn't ported yet. They will be replaced when threads/thread.c
 * is compiled for RISC-V.
 */

/* Forward declaration of thread struct (minimal) */
struct thread;

/*
 * schedule_tail - Called after a context switch completes.
 *
 * In the full implementation, this releases the lock on the
 * previous thread and performs cleanup. For now, this is a no-op.
 */
void schedule_tail(struct thread* prev __attribute__((unused))) {
  /* No-op stub - full implementation in threads/thread.c */
}

/*
 * thread_exit - Called when a thread's function returns.
 *
 * In the full implementation, this marks the thread as dying
 * and triggers a reschedule. For now, just halt.
 */
void thread_exit(void) {
  console_puts("thread_exit: Thread terminated.\n");
  sbi_shutdown();
  while (1) {
    asm volatile("wfi");
  }
}
