/*
 * Filesys Kernel Test Declarations
 * =================================
 *
 * This header declares all kernel-level filesystem tests.
 *
 * ARCHITECTURE:
 * -------------
 *
 *   User runs: pintos -- rfkt cache-single
 *                              │
 *                              ▼
 *   threads/init.c:  run_filesys_kernel_task()
 *                              │
 *                              ▼
 *   tests.c:         run_filesys_kernel_test("cache-single")
 *                              │
 *                              ▼
 *   cache-tests.c:   test_cache_single()
 *
 * TO ADD A NEW TEST:
 * ------------------
 * 1. Add declaration here:  extern test_func test_my_new_test;
 * 2. Implement in cache-tests.c (or new .c file)
 * 3. Register in tests.c: {"my-new-test", test_my_new_test}
 */

#ifndef TESTS_FILESYS_KERNEL_TESTS_H
#define TESTS_FILESYS_KERNEL_TESTS_H

#include <test-lib.h>

/* Run a filesys kernel test by name.
   Called from threads/init.c via the "rfkt" action. */
void run_filesys_kernel_test(const char* name);

/*
 * Cache test function declarations.
 * Each test is implemented in cache-tests.c.
 */
extern test_func test_cache_single;      /* Basic read/write through cache */
extern test_func test_cache_hit;         /* Repeated reads = cache hits */
extern test_func test_cache_evict;       /* Fill cache, trigger eviction */
extern test_func test_cache_concurrent;  /* Multi-threaded cache access */
extern test_func test_cache_write;       /* Partial sector writes */
extern test_func test_cache_read_at;     /* Partial sector reads */
extern test_func test_cache_dirty;       /* Dirty writeback on eviction */
extern test_func test_cache_flush_fn;    /* Explicit cache flush */
extern test_func test_cache_overwrite;   /* Overwriting same sector */
extern test_func test_cache_mixed_rw;    /* Mixed concurrent read/write */
extern test_func test_cache_stress;      /* Stress test with many ops */

/* Prefetch tests */
extern test_func test_cache_prefetch_basic;      /* Basic prefetch works */
extern test_func test_cache_prefetch_seq;        /* Sequential read prefetch */
extern test_func test_cache_prefetch_nodup;      /* No duplicate prefetch */
extern test_func test_cache_prefetch_overflow;   /* Queue overflow handling */
extern test_func test_cache_prefetch_concurrent; /* Prefetch with concurrent readers */

#endif /* tests/filesys/kernel/tests.h */

