/* arch/riscv64/devices.h - Device function prototypes for RISC-V.
 *
 * Provides stubs for device functions required by common code.
 * Most functions are implemented via SBI on RISC-V.
 */

#ifndef ARCH_RISCV64_DEVICES_H
#define ARCH_RISCV64_DEVICES_H

#include <stdint.h>

/* Serial/console output */
void serial_putc(uint8_t c);
void serial_flush(void);

/* VGA output (stub for compatibility) */
void vga_putc(uint8_t c);

/* System shutdown */
void shutdown(void);
void shutdown_power_off(void);
void shutdown_reboot(void);

#endif /* ARCH_RISCV64_DEVICES_H */
