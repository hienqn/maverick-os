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
 */

#include "tests/filesys/kernel/tests.h"
#include <test-lib.h>
#include <debug.h>
#include <string.h>

/*
 * Test registry table.
 * Each entry maps a test name to its function pointer.
 * Format: {"test-name", test_function_name}
 */
static const struct test filesys_kernel_tests[] = {
    {"cache-single", test_cache_single},       /* Basic read/write */
    {"cache-hit", test_cache_hit},             /* Cache hit behavior */
    {"cache-evict", test_cache_evict},         /* Eviction when full */
    {"cache-concurrent", test_cache_concurrent}, /* Multi-threaded access */
    {"cache-write", test_cache_write},         /* Partial sector writes */
};

/*
 * Runs the filesys kernel test named NAME.
 * Called from run_filesys_kernel_task() in threads/init.c.
 * Panics if the test name is not found in the registry.
 */
void run_filesys_kernel_test(const char* name) {
  const struct test* t;
  size_t num_tests = sizeof filesys_kernel_tests / sizeof *filesys_kernel_tests;

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

