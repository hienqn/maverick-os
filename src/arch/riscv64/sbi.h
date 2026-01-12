/* arch/riscv64/sbi.h - RISC-V Supervisor Binary Interface definitions.
 *
 * SBI provides a standard interface between the supervisor-mode OS and
 * the machine-mode firmware (e.g., OpenSBI). The kernel uses SBI calls
 * for console I/O, timer management, IPI, and system control.
 *
 * Reference: RISC-V SBI Specification v1.0
 */

#ifndef ARCH_RISCV64_SBI_H
#define ARCH_RISCV64_SBI_H

#include <stdint.h>

/* ==========================================================================
 * SBI Return Value Structure
 * ========================================================================== */

struct sbiret {
  long error; /* SBI error code */
  long value; /* Return value on success */
};

/* SBI Error Codes */
#define SBI_SUCCESS 0
#define SBI_ERR_FAILED -1
#define SBI_ERR_NOT_SUPPORTED -2
#define SBI_ERR_INVALID_PARAM -3
#define SBI_ERR_DENIED -4
#define SBI_ERR_INVALID_ADDRESS -5
#define SBI_ERR_ALREADY_AVAILABLE -6
#define SBI_ERR_ALREADY_STARTED -7
#define SBI_ERR_ALREADY_STOPPED -8

/* ==========================================================================
 * SBI Extension IDs
 * ========================================================================== */

/* Legacy Extensions (v0.1) - deprecated but widely supported */
#define SBI_EXT_LEGACY_SET_TIMER 0x0
#define SBI_EXT_LEGACY_CONSOLE_PUTCHAR 0x1
#define SBI_EXT_LEGACY_CONSOLE_GETCHAR 0x2
#define SBI_EXT_LEGACY_CLEAR_IPI 0x3
#define SBI_EXT_LEGACY_SEND_IPI 0x4
#define SBI_EXT_LEGACY_REMOTE_FENCE_I 0x5
#define SBI_EXT_LEGACY_REMOTE_SFENCE_VMA 0x6
#define SBI_EXT_LEGACY_REMOTE_SFENCE_VMA_ASID 0x7
#define SBI_EXT_LEGACY_SHUTDOWN 0x8

/* Base Extension (required) */
#define SBI_EXT_BASE 0x10
#define SBI_EXT_BASE_GET_SPEC_VERSION 0
#define SBI_EXT_BASE_GET_IMP_ID 1
#define SBI_EXT_BASE_GET_IMP_VERSION 2
#define SBI_EXT_BASE_PROBE_EXT 3
#define SBI_EXT_BASE_GET_MVENDORID 4
#define SBI_EXT_BASE_GET_MARCHID 5
#define SBI_EXT_BASE_GET_MIMPID 6

/* Timer Extension */
#define SBI_EXT_TIME 0x54494D45 /* "TIME" */
#define SBI_EXT_TIME_SET_TIMER 0

/* IPI Extension */
#define SBI_EXT_IPI 0x735049 /* "sPI" */
#define SBI_EXT_IPI_SEND_IPI 0

/* RFENCE Extension */
#define SBI_EXT_RFENCE 0x52464E43 /* "RFNC" */
#define SBI_EXT_RFENCE_I 0
#define SBI_EXT_RFENCE_SFENCE_VMA 1
#define SBI_EXT_RFENCE_SFENCE_VMA_ASID 2

/* HSM (Hart State Management) Extension */
#define SBI_EXT_HSM 0x48534D /* "HSM" */
#define SBI_EXT_HSM_HART_START 0
#define SBI_EXT_HSM_HART_STOP 1
#define SBI_EXT_HSM_HART_STATUS 2
#define SBI_EXT_HSM_HART_SUSPEND 3

/* System Reset Extension */
#define SBI_EXT_SRST 0x53525354 /* "SRST" */
#define SBI_EXT_SRST_RESET 0

/* Reset types for SRST */
#define SBI_SRST_TYPE_SHUTDOWN 0
#define SBI_SRST_TYPE_COLD_REBOOT 1
#define SBI_SRST_TYPE_WARM_REBOOT 2

/* Reset reasons for SRST */
#define SBI_SRST_REASON_NONE 0
#define SBI_SRST_REASON_SYSFAIL 1

/* Debug Console Extension */
#define SBI_EXT_DBCN 0x4442434E /* "DBCN" */
#define SBI_EXT_DBCN_WRITE 0
#define SBI_EXT_DBCN_READ 1
#define SBI_EXT_DBCN_WRITE_BYTE 2

/* ==========================================================================
 * SBI Function Declarations
 * ========================================================================== */

/* Raw SBI call - implemented in assembly or inline */
struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0, unsigned long arg1,
                        unsigned long arg2, unsigned long arg3, unsigned long arg4,
                        unsigned long arg5);

/* Base extension functions */
long sbi_get_spec_version(void);
long sbi_get_impl_id(void);
long sbi_get_impl_version(void);
long sbi_probe_extension(long extid);

/* Console I/O (legacy, works on all OpenSBI versions) */
void sbi_console_putchar(int ch);
int sbi_console_getchar(void);

/* Timer */
void sbi_set_timer(uint64_t stime_value);

/* System control */
void sbi_shutdown(void);
void sbi_reboot(void);

/* IPI (Inter-Processor Interrupt) */
void sbi_send_ipi(unsigned long hart_mask, unsigned long hart_mask_base);

/* Remote fence operations */
void sbi_remote_fence_i(unsigned long hart_mask, unsigned long hart_mask_base);
void sbi_remote_sfence_vma(unsigned long hart_mask, unsigned long hart_mask_base,
                           unsigned long start_addr, unsigned long size);

#endif /* ARCH_RISCV64_SBI_H */
