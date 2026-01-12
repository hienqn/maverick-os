/* arch/riscv64/init.c - RISC-V early initialization.
 *
 * This is called from start.S after basic setup.
 * It initializes console output and prints the boot message.
 */

#include "arch/riscv64/sbi.h"
#include "arch/riscv64/csr.h"
#include "arch/riscv64/memlayout.h"
#include <stdint.h>

/* Forward declarations */
static void console_init(void);
static void console_puts(const char* s);
static void console_puthex(uint64_t val);

/* Global variables set during boot */
uint64_t boot_hartid;
void* dtb_ptr;
uint64_t init_ram_pages; /* For compatibility with threads/init.c */

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

  long impl_id = sbi_get_impl_id();
  console_puts("  SBI impl ID: ");
  console_puthex(impl_id);
  console_puts("\n");

  /* Print CSR values for debugging */
  uint64_t sstatus = csr_read(sstatus);
  console_puts("  sstatus: ");
  console_puthex(sstatus);
  console_puts("\n");

  console_puts("\n");
  console_puts("RISC-V Phase 1 boot complete!\n");
  console_puts("Halting...\n");

  /* For Phase 1, just halt here */
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

static void console_init(void) { /* Nothing to do for SBI console */ }

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

/*
 * Dummy functions required by the rest of PintOS.
 * These will be properly implemented in later phases.
 */

void intr_disable(void) { csr_clear(sstatus, SSTATUS_SIE); }

void intr_enable(void) { csr_set(sstatus, SSTATUS_SIE); }

/* Minimal debug_panic for assertion failures */
void debug_panic(const char* file, int line, const char* function, const char* message, ...) {
  console_puts("\n*** PANIC ***\n");
  console_puts("File: ");
  console_puts(file);
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
