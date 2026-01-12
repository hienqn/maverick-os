/* arch/riscv64/devices.c - Device compatibility stubs for RISC-V.
 *
 * RISC-V uses SBI for console output and doesn't have VGA.
 * This file provides compatibility stubs for the kernel interfaces
 * that expect serial/VGA functions.
 */

#include "arch/riscv64/devices.h"
#include "arch/riscv64/sbi.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * serial_putc - Output a character via serial (SBI console).
 *
 * On RISC-V, we use SBI's debug console extension.
 */
void serial_putc(uint8_t c) { sbi_console_putchar(c); }

/*
 * serial_flush - Flush serial output buffer.
 *
 * SBI console is unbuffered, so this is a no-op.
 */
void serial_flush(void) { /* No-op: SBI console writes are synchronous */
}

/*
 * vga_putc - Output a character to VGA display.
 *
 * RISC-V virt machine has no VGA, so this is a no-op.
 */
void vga_putc(uint8_t c __attribute__((unused))) { /* No VGA on RISC-V virt machine */
}

/*
 * shutdown - Shut down the machine.
 *
 * Uses SBI system reset extension.
 */
void shutdown(void) {
  sbi_shutdown();
  /* If SBI shutdown fails, halt */
  while (1) {
    asm volatile("wfi");
  }
}

/*
 * shutdown_power_off - Power off the machine.
 *
 * Alias for shutdown() for compatibility.
 */
void shutdown_power_off(void) { shutdown(); }

/*
 * shutdown_reboot - Reboot the machine.
 *
 * Not yet implemented - just shuts down.
 */
void shutdown_reboot(void) {
  /* TODO: Use SBI SRST extension for warm reboot */
  shutdown();
}

/*
 * input_getc - Get a character from keyboard/serial input.
 *
 * Uses SBI console getchar. Returns the character, or blocks until available.
 * Returns -1 if no input is available (non-blocking on RISC-V SBI).
 */
uint8_t input_getc(void) {
  int c;
  /* SBI getchar returns -1 if no character available.
     Loop until we get a valid character. */
  do {
    c = sbi_console_getchar();
  } while (c < 0);
  return (uint8_t)c;
}
