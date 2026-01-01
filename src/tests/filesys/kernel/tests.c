/*
 * Filesys Kernel Test Registry
 * ============================
 *
 * This file maintains the registry of all kernel-level filesystem tests.
 * It maps test names (strings) to test functions.
 *
 * ADDING A NEW TEST:
 * ------------------
 * 1. Implement your test in cache-tests.c (or create a new .c file)
 * 2. Declare it in tests.h:  extern test_func test_your_test;
 * 3. Add an entry below:     {"your-test", test_your_test},
 *
 * RUNNING TESTS:
 * --------------
 * From src/filesys/build:
 *   pintos -v -k --qemu --filesys-size=2 -- -q -f rfkt <test-name>
 *
 * Example:
 *   pintos -v -k --qemu --filesys-size=2 -- -q -f rfkt cache-single
 *
 * NOTE: For WAL recovery tests, DO NOT use the -f flag, as it formats
 * the filesystem and prevents testing recovery scenarios. Recovery tests
 * need to persist state across boots to verify crash recovery works.
 *
 * Recovery test example (no -f flag):
 *   pintos -v -k --qemu --filesys-size=2 -- -q rfkt wal-recover-commit
 */

#include "tests/filesys/kernel/tests.h"
#include <test-lib.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>

/*
 * Test registry table.
 * Each entry maps a test name to its function pointer.
 * Format: {"test-name", test_function_name}
 */
static const struct test filesys_kernel_tests[] = {
    {"cache-single", test_cache_single},         /* Basic read/write */
    {"cache-hit", test_cache_hit},               /* Cache hit behavior */
    {"cache-evict", test_cache_evict},           /* Eviction when full */
    {"cache-concurrent", test_cache_concurrent}, /* Multi-threaded access */
    {"cache-write", test_cache_write},           /* Partial sector writes */
    {"cache-read-at", test_cache_read_at},       /* Partial sector reads */
    {"cache-dirty", test_cache_dirty},           /* Dirty writeback */
    {"cache-flush", test_cache_flush_fn},        /* Explicit flush */
    {"cache-overwrite", test_cache_overwrite},   /* Overwrite same sector */
    {"cache-mixed-rw", test_cache_mixed_rw},     /* Mixed concurrent R/W */
    {"cache-stress", test_cache_stress},         /* Stress test */
    /* Prefetch tests */
    {"cache-prefetch-basic", test_cache_prefetch_basic},           /* Basic prefetch */
    {"cache-prefetch-seq", test_cache_prefetch_seq},               /* Sequential prefetch */
    {"cache-prefetch-nodup", test_cache_prefetch_nodup},           /* No duplicate prefetch */
    {"cache-prefetch-overflow", test_cache_prefetch_overflow},     /* Queue overflow */
    {"cache-prefetch-concurrent", test_cache_prefetch_concurrent}, /* Concurrent prefetch */

    /* ============================================================
     * WAL (Write-Ahead Logging) Tests
     * ============================================================ */

    /* Initialization tests */
    {"wal-init", test_wal_init},         /* WAL initialization */
    {"wal-shutdown", test_wal_shutdown}, /* Clean shutdown */

    /* Transaction lifecycle tests */
    {"wal-txn-begin", test_wal_txn_begin},       /* Transaction creation */
    {"wal-txn-commit", test_wal_txn_commit},     /* Commit & durability */
    {"wal-txn-abort", test_wal_txn_abort},       /* Abort & UNDO */
    {"wal-txn-multiple", test_wal_txn_multiple}, /* Multiple concurrent txns */

    /* Logging tests */
    {"wal-log-write", test_wal_log_write}, /* Basic write logging */
    {"wal-log-split", test_wal_log_split}, /* Large writes split */
    {"wal-log-flush", test_wal_log_flush}, /* Log buffer flush */
    {"wal-log-full", test_wal_log_full},   /* Log full behavior */

    /* Recovery tests */
    {"wal-recover-commit", test_wal_recover_commit}, /* REDO committed */
    {"wal-recover-abort", test_wal_recover_abort},   /* UNDO uncommitted */
    {"wal-recover-order", test_wal_recover_order},   /* Correct ordering */

    /* Checkpoint tests */
    {"wal-checkpoint", test_wal_checkpoint}, /* Checkpoint creation */

    /* Integration/stress tests */
    {"wal-stress", test_wal_stress}, /* Stress test */
};

/*
 * Runs the filesys kernel test named NAME.
 * Called from run_filesys_kernel_task() in threads/init.c.
 * 
 * Special names:
 *   "all"    - Run all tests
 *   "list"   - List all available test names
 *
 * Panics if the test name is not found in the registry.
 */
void run_filesys_kernel_test(const char* name) {
  const struct test* t;
  size_t num_tests = sizeof filesys_kernel_tests / sizeof *filesys_kernel_tests;

  /* Special case: "all" runs every test. */
  if (!strcmp(name, "all")) {
    printf("Running all %d filesys kernel tests...\n\n", (int)num_tests);

    for (t = filesys_kernel_tests; t < filesys_kernel_tests + num_tests; t++) {
      test_name = t->name;
      printf("=== %s ===\n", t->name);
      msg("begin");
      t->function();
      msg("end");
      printf("\n");
    }

    printf("=== ALL %d TESTS COMPLETE ===\n", (int)num_tests);
    return;
  }

  /* Special case: "list" shows available tests. */
  if (!strcmp(name, "list")) {
    printf("Available filesys kernel tests:\n");
    for (t = filesys_kernel_tests; t < filesys_kernel_tests + num_tests; t++) {
      printf("  %s\n", t->name);
    }
    printf("\nUse 'rfkt all' to run all tests.\n");
    return;
  }

  /* Normal case: run specific test by name. */
  for (t = filesys_kernel_tests; t < filesys_kernel_tests + num_tests; t++) {
    if (!strcmp(name, t->name)) {
      test_name = name;
      msg("begin");
      t->function();
      msg("end");
      return;
    }
  }
  PANIC("no filesys kernel test named \"%s\"", name);
}
