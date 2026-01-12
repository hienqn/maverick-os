/* arch/riscv64/sbi.c - RISC-V SBI call implementations.
 *
 * This file implements the SBI wrappers that the kernel uses to
 * communicate with the firmware (OpenSBI).
 */

#include "arch/riscv64/sbi.h"

/* ==========================================================================
 * Raw SBI ecall
 *
 * The ecall instruction traps to machine mode. OpenSBI handles the call
 * and returns results in a0 (error) and a1 (value).
 *
 * Arguments are passed in a0-a5, extension ID in a7, function ID in a6.
 * ========================================================================== */

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0, unsigned long arg1,
                        unsigned long arg2, unsigned long arg3, unsigned long arg4,
                        unsigned long arg5) {
  struct sbiret ret;

  register unsigned long a0 asm("a0") = arg0;
  register unsigned long a1 asm("a1") = arg1;
  register unsigned long a2 asm("a2") = arg2;
  register unsigned long a3 asm("a3") = arg3;
  register unsigned long a4 asm("a4") = arg4;
  register unsigned long a5 asm("a5") = arg5;
  register unsigned long a6 asm("a6") = fid;
  register unsigned long a7 asm("a7") = ext;

  asm volatile("ecall"
               : "+r"(a0), "+r"(a1)
               : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
               : "memory");

  ret.error = a0;
  ret.value = a1;
  return ret;
}

/* ==========================================================================
 * Base Extension (0x10)
 * ========================================================================== */

long sbi_get_spec_version(void) {
  struct sbiret ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_SPEC_VERSION, 0, 0, 0, 0, 0, 0);
  return ret.error ? -1 : ret.value;
}

long sbi_get_impl_id(void) {
  struct sbiret ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_ID, 0, 0, 0, 0, 0, 0);
  return ret.error ? -1 : ret.value;
}

long sbi_get_impl_version(void) {
  struct sbiret ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_VERSION, 0, 0, 0, 0, 0, 0);
  return ret.error ? -1 : ret.value;
}

long sbi_probe_extension(long extid) {
  struct sbiret ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_PROBE_EXT, extid, 0, 0, 0, 0, 0);
  return ret.error ? 0 : ret.value;
}

/* ==========================================================================
 * Console I/O (Legacy Extension)
 *
 * These use the legacy SBI v0.1 interface which is deprecated but
 * universally supported. For production, use DBCN extension if available.
 * ========================================================================== */

void sbi_console_putchar(int ch) {
  /* Legacy putchar: extension 0x1, no function ID needed */
  sbi_ecall(SBI_EXT_LEGACY_CONSOLE_PUTCHAR, 0, ch, 0, 0, 0, 0, 0);
}

int sbi_console_getchar(void) {
  /* Legacy getchar: extension 0x2, returns char or -1 */
  struct sbiret ret = sbi_ecall(SBI_EXT_LEGACY_CONSOLE_GETCHAR, 0, 0, 0, 0, 0, 0, 0);
  return (int)ret.error; /* Legacy uses error field for return value */
}

/* ==========================================================================
 * Timer Extension
 *
 * Sets the timer to fire at the specified time (in terms of mtime).
 * Uses TIME extension if available, falls back to legacy.
 * ========================================================================== */

void sbi_set_timer(uint64_t stime_value) {
  /* Try TIME extension first, fall back to legacy */
  if (sbi_probe_extension(SBI_EXT_TIME)) {
    sbi_ecall(SBI_EXT_TIME, SBI_EXT_TIME_SET_TIMER, stime_value, 0, 0, 0, 0, 0);
  } else {
    /* Legacy set_timer: extension 0x0 */
    sbi_ecall(SBI_EXT_LEGACY_SET_TIMER, 0, stime_value, 0, 0, 0, 0, 0);
  }
}

/* ==========================================================================
 * System Control
 * ========================================================================== */

void sbi_shutdown(void) {
  /* Try SRST extension first, fall back to legacy */
  if (sbi_probe_extension(SBI_EXT_SRST)) {
    sbi_ecall(SBI_EXT_SRST, SBI_EXT_SRST_RESET, SBI_SRST_TYPE_SHUTDOWN, SBI_SRST_REASON_NONE, 0, 0,
              0, 0);
  } else {
    /* Legacy shutdown: extension 0x8 */
    sbi_ecall(SBI_EXT_LEGACY_SHUTDOWN, 0, 0, 0, 0, 0, 0, 0);
  }
  /* Should not return */
  while (1) {
    asm volatile("wfi");
  }
}

void sbi_reboot(void) {
  if (sbi_probe_extension(SBI_EXT_SRST)) {
    sbi_ecall(SBI_EXT_SRST, SBI_EXT_SRST_RESET, SBI_SRST_TYPE_COLD_REBOOT, SBI_SRST_REASON_NONE, 0,
              0, 0, 0);
  }
  /* If SRST not available, try shutdown as fallback */
  sbi_shutdown();
}

/* ==========================================================================
 * IPI (Inter-Processor Interrupt)
 * ========================================================================== */

void sbi_send_ipi(unsigned long hart_mask, unsigned long hart_mask_base) {
  if (sbi_probe_extension(SBI_EXT_IPI)) {
    sbi_ecall(SBI_EXT_IPI, SBI_EXT_IPI_SEND_IPI, hart_mask, hart_mask_base, 0, 0, 0, 0);
  } else {
    /* Legacy send_ipi: extension 0x4, takes pointer to hart mask */
    sbi_ecall(SBI_EXT_LEGACY_SEND_IPI, 0, (unsigned long)&hart_mask, 0, 0, 0, 0, 0);
  }
}

/* ==========================================================================
 * Remote Fence Operations
 * ========================================================================== */

void sbi_remote_fence_i(unsigned long hart_mask, unsigned long hart_mask_base) {
  if (sbi_probe_extension(SBI_EXT_RFENCE)) {
    sbi_ecall(SBI_EXT_RFENCE, SBI_EXT_RFENCE_I, hart_mask, hart_mask_base, 0, 0, 0, 0);
  } else {
    /* Legacy remote_fence_i: extension 0x5 */
    sbi_ecall(SBI_EXT_LEGACY_REMOTE_FENCE_I, 0, (unsigned long)&hart_mask, 0, 0, 0, 0, 0);
  }
}

void sbi_remote_sfence_vma(unsigned long hart_mask, unsigned long hart_mask_base,
                           unsigned long start_addr, unsigned long size) {
  if (sbi_probe_extension(SBI_EXT_RFENCE)) {
    sbi_ecall(SBI_EXT_RFENCE, SBI_EXT_RFENCE_SFENCE_VMA, hart_mask, hart_mask_base, start_addr,
              size, 0, 0);
  } else {
    /* Legacy remote_sfence_vma: extension 0x6 */
    sbi_ecall(SBI_EXT_LEGACY_REMOTE_SFENCE_VMA, 0, (unsigned long)&hart_mask, start_addr, size, 0,
              0, 0);
  }
}
