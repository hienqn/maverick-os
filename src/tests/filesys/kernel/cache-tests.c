/*
 * Buffer Cache Kernel-Level Tests
 * ================================
 *
 * This file contains kernel-level unit tests for the buffer cache.
 * These tests run directly in kernel space and can call cache functions
 * directly, unlike user-level tests that go through syscalls.
 *
 * HOW THIS TEST INFRASTRUCTURE WORKS:
 * -----------------------------------
 *
 * 1. TEST REGISTRATION (tests/filesys/kernel/tests.c):
 *    Each test function is registered in a table with a name:
 *      {"cache-single", test_cache_single}
 *    The run_filesys_kernel_test() function looks up tests by name.
 *
 * 2. INVOKING TESTS (threads/init.c):
 *    The "rfkt" command (Run Filesys Kernel Test) is registered in
 *    the actions table. When you run:
 *      pintos -- rfkt cache-single
 *    It calls run_filesys_kernel_task() which calls run_filesys_kernel_test().
 *
 * 3. TEST OUTPUT:
 *    Use msg() to print test messages (from <test-lib.h>).
 *    Output appears as "(test-name) your message".
 *    Use cache_print_stats() to show hit/miss/eviction counts.
 *
 * HOW TO ADD A NEW TEST:
 * ----------------------
 *
 * 1. Write your test function here:
 *      void test_cache_my_test(void) {
 *        cache_reset_stats();
 *        // ... test code using cache_read(), cache_write(), etc. ...
 *        cache_print_stats();
 *      }
 *
 * 2. Declare it in tests.h:
 *      extern test_func test_cache_my_test;
 *
 * 3. Register it in tests.c:
 *      {"cache-my-test", test_cache_my_test},
 *
 * 4. Rebuild: cd src/filesys && make clean && make
 *
 * 5. Run: pintos -- -q -f rfkt cache-my-test
 *
 * TESTING TIPS:
 * -------------
 *
 * - Use cache_reset_stats() at the start of each test for clean measurements.
 * - Use high sector numbers (50+) to avoid conflicts with filesystem metadata.
 * - For concurrency tests, use semaphores to synchronize thread start/finish.
 * - The msg() function auto-prefixes output with the test name.
 */

#include "tests/filesys/kernel/tests.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "devices/block.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include <test-lib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Test: Basic read/write through cache. */
void test_cache_single(void) {
  char write_buf[BLOCK_SECTOR_SIZE];
  char read_buf[BLOCK_SECTOR_SIZE];
  block_sector_t test_sector = 50;  /* Pick a sector unlikely to be in use */
  
  /* Initialize test data. */
  memset(write_buf, 'A', BLOCK_SECTOR_SIZE);
  memset(read_buf, 0, BLOCK_SECTOR_SIZE);
  
  /* Reset stats for accurate measurement. */
  cache_reset_stats();
  
  /* Write to cache. */
  cache_write(test_sector, write_buf, 0, BLOCK_SECTOR_SIZE);
  msg("Wrote %d bytes to sector %d", BLOCK_SECTOR_SIZE, test_sector);
  
  /* Read back from cache. */
  cache_read(test_sector, read_buf);
  msg("Read %d bytes from sector %d", BLOCK_SECTOR_SIZE, test_sector);
  
  /* Verify data. */
  if (memcmp(write_buf, read_buf, BLOCK_SECTOR_SIZE) == 0) {
    msg("Data verification: PASSED");
  } else {
    msg("Data verification: FAILED");
  }
  
  cache_print_stats();
}

/* Test: Cache hit behavior - reading same sector multiple times. */
void test_cache_hit(void) {
  char buf[BLOCK_SECTOR_SIZE];
  block_sector_t test_sector = 51;
  
  cache_reset_stats();
  
  /* First read - should be a miss. */
  memset(buf, 'B', BLOCK_SECTOR_SIZE);
  cache_write(test_sector, buf, 0, BLOCK_SECTOR_SIZE);
  msg("Initial write complete");
  
  /* Read multiple times - should all be hits. */
  for (int i = 0; i < 10; i++) {
    cache_read(test_sector, buf);
  }
  msg("Read sector 10 times");
  
  cache_print_stats();
  msg("Expected: 10 hits, 0 misses for reads (1 miss for initial write)");
}

/* Test: Eviction - fill cache beyond capacity. */
void test_cache_evict(void) {
  char buf[BLOCK_SECTOR_SIZE];
  
  cache_reset_stats();
  
  /* Write to more sectors than cache can hold (64 + some extra). */
  msg("Writing to 70 different sectors...");
  for (int i = 0; i < 70; i++) {
    memset(buf, 'C' + (i % 26), BLOCK_SECTOR_SIZE);
    cache_write(100 + i, buf, 0, BLOCK_SECTOR_SIZE);
  }
  
  cache_print_stats();
  msg("Expected: at least 6 evictions (70 - 64 = 6)");
  
  /* Read back some early sectors - should cause more evictions. */
  msg("Reading back first 10 sectors...");
  for (int i = 0; i < 10; i++) {
    cache_read(100 + i, buf);
  }
  
  cache_print_stats();
}

/* Concurrent access test data. */
static struct semaphore concurrent_start;
static struct semaphore concurrent_done;
static block_sector_t shared_sector = 200;

static void concurrent_reader(void *aux UNUSED) {
  char buf[BLOCK_SECTOR_SIZE];
  
  sema_down(&concurrent_start);  /* Wait for signal to start. */
  
  for (int i = 0; i < 100; i++) {
    cache_read(shared_sector, buf);
  }
  
  sema_up(&concurrent_done);  /* Signal completion. */
}

/* Test: Concurrent access from multiple threads. */
void test_cache_concurrent(void) {
  char buf[BLOCK_SECTOR_SIZE];
  int num_threads = 4;
  
  cache_reset_stats();
  
  /* Initialize synchronization. */
  sema_init(&concurrent_start, 0);
  sema_init(&concurrent_done, 0);
  
  /* Initialize the shared sector. */
  memset(buf, 'X', BLOCK_SECTOR_SIZE);
  cache_write(shared_sector, buf, 0, BLOCK_SECTOR_SIZE);
  msg("Initialized shared sector");
  
  /* Create reader threads. */
  for (int i = 0; i < num_threads; i++) {
    char name[16];
    snprintf(name, sizeof name, "reader-%d", i);
    thread_create(name, PRI_DEFAULT, concurrent_reader, NULL);
  }
  msg("Created %d reader threads", num_threads);
  
  /* Start all threads simultaneously. */
  for (int i = 0; i < num_threads; i++) {
    sema_up(&concurrent_start);
  }
  
  /* Wait for all threads to complete. */
  for (int i = 0; i < num_threads; i++) {
    sema_down(&concurrent_done);
  }
  
  msg("All threads completed");
  cache_print_stats();
  msg("Expected: high hit rate, no crashes or data corruption");
}

/* Test: Partial writes within a sector. */
void test_cache_write(void) {
  char buf[BLOCK_SECTOR_SIZE];
  char pattern1[] = "HELLO";
  char pattern2[] = "WORLD";
  
  cache_reset_stats();
  
  /* Initialize sector with zeros. */
  memset(buf, 0, BLOCK_SECTOR_SIZE);
  cache_write(300, buf, 0, BLOCK_SECTOR_SIZE);
  
  /* Partial write at beginning. */
  cache_write(300, pattern1, 0, strlen(pattern1));
  msg("Wrote '%s' at offset 0", pattern1);
  
  /* Partial write in middle. */
  cache_write(300, pattern2, 100, strlen(pattern2));
  msg("Wrote '%s' at offset 100", pattern2);
  
  /* Read back and verify. */
  cache_read(300, buf);
  
  if (memcmp(buf, pattern1, strlen(pattern1)) == 0) {
    msg("Pattern1 at offset 0: PASSED");
  } else {
    msg("Pattern1 at offset 0: FAILED");
  }
  
  if (memcmp(buf + 100, pattern2, strlen(pattern2)) == 0) {
    msg("Pattern2 at offset 100: PASSED");
  } else {
    msg("Pattern2 at offset 100: FAILED");
  }
  
  /* Verify zeros in between are preserved. */
  bool zeros_ok = true;
  for (int i = strlen(pattern1); i < 100; i++) {
    if (buf[i] != 0) {
      zeros_ok = false;
      break;
    }
  }
  msg("Zeros between patterns: %s", zeros_ok ? "PASSED" : "FAILED");
  
  cache_print_stats();
}

/* Test: Partial reads using cache_read_at. */
void test_cache_read_at(void) {
  char write_buf[BLOCK_SECTOR_SIZE];
  char read_buf[128];
  block_sector_t test_sector = 400;
  
  cache_reset_stats();
  
  /* Initialize sector with known pattern: 0, 1, 2, ..., 255, 0, 1, ... */
  for (int i = 0; i < BLOCK_SECTOR_SIZE; i++) {
    write_buf[i] = (char)(i % 256);
  }
  cache_write(test_sector, write_buf, 0, BLOCK_SECTOR_SIZE);
  msg("Initialized sector with sequential pattern");
  
  /* Read first 10 bytes. */
  memset(read_buf, 0xFF, sizeof(read_buf));
  cache_read_at(test_sector, read_buf, 0, 10);
  bool ok = true;
  for (int i = 0; i < 10; i++) {
    if (read_buf[i] != (char)i) {
      ok = false;
      break;
    }
  }
  msg("Read bytes 0-9: %s", ok ? "PASSED" : "FAILED");
  
  /* Read middle portion (bytes 100-149). */
  memset(read_buf, 0xFF, sizeof(read_buf));
  cache_read_at(test_sector, read_buf, 100, 50);
  ok = true;
  for (int i = 0; i < 50; i++) {
    if (read_buf[i] != (char)(100 + i)) {
      ok = false;
      break;
    }
  }
  msg("Read bytes 100-149: %s", ok ? "PASSED" : "FAILED");
  
  /* Read last 20 bytes of sector. */
  memset(read_buf, 0xFF, sizeof(read_buf));
  cache_read_at(test_sector, read_buf, BLOCK_SECTOR_SIZE - 20, 20);
  ok = true;
  for (int i = 0; i < 20; i++) {
    int expected = (BLOCK_SECTOR_SIZE - 20 + i) % 256;
    if (read_buf[i] != (char)expected) {
      ok = false;
      break;
    }
  }
  msg("Read last 20 bytes: %s", ok ? "PASSED" : "FAILED");
  
  cache_print_stats();
}

/* Test: Verify dirty data survives eviction and is written back. */
void test_cache_dirty(void) {
  char write_buf[BLOCK_SECTOR_SIZE];
  char read_buf[BLOCK_SECTOR_SIZE];
  block_sector_t target_sector = 500;
  
  cache_reset_stats();
  
  /* Write distinctive pattern to target sector. */
  memset(write_buf, 'D', BLOCK_SECTOR_SIZE);
  write_buf[0] = 'S';  /* Start marker */
  write_buf[BLOCK_SECTOR_SIZE - 1] = 'E';  /* End marker */
  cache_write(target_sector, write_buf, 0, BLOCK_SECTOR_SIZE);
  msg("Wrote distinctive pattern to sector %d", target_sector);
  
  /* Fill cache with other sectors to evict our target. */
  msg("Filling cache with 70 other sectors to force eviction...");
  for (int i = 0; i < 70; i++) {
    char tmp[BLOCK_SECTOR_SIZE];
    memset(tmp, 'X', BLOCK_SECTOR_SIZE);
    cache_write(600 + i, tmp, 0, BLOCK_SECTOR_SIZE);
  }
  
  cache_print_stats();
  msg("Target sector should have been evicted and written to disk");
  
  /* Read target sector back - should come from disk. */
  cache_read(target_sector, read_buf);
  
  bool ok = (read_buf[0] == 'S') && 
            (read_buf[BLOCK_SECTOR_SIZE - 1] == 'E') &&
            (read_buf[1] == 'D') && (read_buf[2] == 'D');
  msg("Data integrity after eviction: %s", ok ? "PASSED" : "FAILED");
  
  cache_print_stats();
}

/* Test: Explicit cache flush. */
void test_cache_flush_fn(void) {
  char write_buf[BLOCK_SECTOR_SIZE];
  char read_buf[BLOCK_SECTOR_SIZE];
  block_sector_t test_sector = 700;
  
  cache_reset_stats();
  
  /* Write data to cache. */
  memset(write_buf, 'F', BLOCK_SECTOR_SIZE);
  write_buf[0] = '@';
  cache_write(test_sector, write_buf, 0, BLOCK_SECTOR_SIZE);
  msg("Wrote data to sector %d", test_sector);
  
  /* Explicitly flush cache. */
  cache_flush();
  msg("Called cache_flush()");
  
  cache_print_stats();
  msg("Writebacks should be >= 1 (our dirty sector was flushed)");
  
  /* Read back and verify. */
  cache_read(test_sector, read_buf);
  bool ok = (read_buf[0] == '@') && (read_buf[1] == 'F');
  msg("Data after flush: %s", ok ? "PASSED" : "FAILED");
}

/* Test: Overwriting same sector multiple times. */
void test_cache_overwrite(void) {
  char buf[BLOCK_SECTOR_SIZE];
  block_sector_t test_sector = 800;
  
  cache_reset_stats();
  
  /* Write different patterns to same sector. */
  for (int round = 0; round < 10; round++) {
    memset(buf, 'A' + round, BLOCK_SECTOR_SIZE);
    cache_write(test_sector, buf, 0, BLOCK_SECTOR_SIZE);
  }
  msg("Wrote 10 different patterns to same sector");
  
  /* Read back - should see last pattern ('J'). */
  cache_read(test_sector, buf);
  
  bool ok = true;
  for (int i = 0; i < BLOCK_SECTOR_SIZE; i++) {
    if (buf[i] != 'J') {
      ok = false;
      break;
    }
  }
  msg("Final read shows last pattern ('J'): %s", ok ? "PASSED" : "FAILED");
  
  cache_print_stats();
  msg("Expected: Only 1 miss (first write), rest are hits");
}

/* Mixed read/write concurrency test data. */
static struct semaphore mixed_start;
static struct semaphore mixed_done;
static block_sector_t mixed_sectors[8] = {900, 901, 902, 903, 904, 905, 906, 907};
static volatile int mixed_errors = 0;

static void mixed_worker(void *aux) {
  int id = (int)(intptr_t)aux;
  char buf[BLOCK_SECTOR_SIZE];
  
  sema_down(&mixed_start);
  
  for (int i = 0; i < 50; i++) {
    int sector_idx = (id + i) % 8;
    block_sector_t sector = mixed_sectors[sector_idx];
    
    if (i % 2 == 0) {
      /* Write operation. */
      memset(buf, 'A' + id, BLOCK_SECTOR_SIZE);
      cache_write(sector, buf, 0, BLOCK_SECTOR_SIZE);
    } else {
      /* Read operation. */
      cache_read(sector, buf);
      /* Just verify it's a valid character pattern (A-H). */
      if (buf[0] < 'A' || buf[0] > 'H') {
        mixed_errors++;
      }
    }
  }
  
  sema_up(&mixed_done);
}

/* Test: Mixed concurrent reads and writes. */
void test_cache_mixed_rw(void) {
  char buf[BLOCK_SECTOR_SIZE];
  int num_threads = 4;
  
  cache_reset_stats();
  mixed_errors = 0;
  
  sema_init(&mixed_start, 0);
  sema_init(&mixed_done, 0);
  
  /* Initialize sectors with valid data. */
  for (int i = 0; i < 8; i++) {
    memset(buf, 'A', BLOCK_SECTOR_SIZE);
    cache_write(mixed_sectors[i], buf, 0, BLOCK_SECTOR_SIZE);
  }
  msg("Initialized %d shared sectors", 8);
  
  /* Create worker threads. */
  for (int i = 0; i < num_threads; i++) {
    char name[16];
    snprintf(name, sizeof name, "mixed-%d", i);
    thread_create(name, PRI_DEFAULT, mixed_worker, (void *)(intptr_t)i);
  }
  msg("Created %d worker threads", num_threads);
  
  /* Start all threads. */
  for (int i = 0; i < num_threads; i++) {
    sema_up(&mixed_start);
  }
  
  /* Wait for completion. */
  for (int i = 0; i < num_threads; i++) {
    sema_down(&mixed_done);
  }
  
  msg("All threads completed, errors: %d", mixed_errors);
  msg("Mixed read/write test: %s", mixed_errors == 0 ? "PASSED" : "FAILED");
  cache_print_stats();
}

/* Test: Stress test with many rapid operations. */
void test_cache_stress(void) {
  char buf[BLOCK_SECTOR_SIZE];
  int num_operations = 500;
  int num_sectors = 100;  /* More than cache size (64) */
  
  cache_reset_stats();
  msg("Starting stress test: %d operations across %d sectors", 
      num_operations, num_sectors);
  
  /* Perform random-ish operations. */
  for (int i = 0; i < num_operations; i++) {
    int sector = 1000 + (i * 7) % num_sectors;  /* Pseudo-random sector selection */
    
    if (i % 3 == 0) {
      /* Full sector write. */
      memset(buf, 'S' + (i % 26), BLOCK_SECTOR_SIZE);
      cache_write(sector, buf, 0, BLOCK_SECTOR_SIZE);
    } else if (i % 3 == 1) {
      /* Partial write. */
      int offset = (i * 13) % 400;
      int size = 50 + (i % 50);
      memset(buf, 'P', size);
      cache_write(sector, buf, offset, size);
    } else {
      /* Read. */
      cache_read(sector, buf);
    }
  }
  
  msg("Stress test complete");
  cache_print_stats();
  
  /* Flush to ensure all dirty data is written. */
  cache_flush();
  msg("Final flush complete");
  cache_print_stats();
}

