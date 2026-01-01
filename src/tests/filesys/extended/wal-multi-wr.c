/* Test: wal-multi-write
 *
 * PURPOSE: Verify multiple sequential writes to the same file work correctly.
 *
 * WHAT IT TESTS:
 * - Multiple writes to the same file are all logged
 * - Data from all writes is correctly preserved
 * - WAL handles sequential modifications properly
 *
 * This tests that WAL correctly tracks multiple modifications to the same file.
 */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

/* Set to 1 after integrating WAL into the filesystem */
#define WAL_INTEGRATED 1

#define CHUNK_SIZE 128
#define NUM_WRITES 4
static char write_buf[CHUNK_SIZE];
static char read_buf[CHUNK_SIZE * NUM_WRITES];

void test_main(void) {
  int fd;
  int i;

#if !WAL_INTEGRATED
  fail("WAL not integrated! Set WAL_INTEGRATED=1 after hooking WAL into cache.c");
  return;
#endif

  /* Create and open file */
  CHECK(create("multiwrite", CHUNK_SIZE * NUM_WRITES), "create \"multiwrite\"");
  CHECK((fd = open("multiwrite")) > 1, "open \"multiwrite\"");

  /* Perform multiple writes with different patterns */
  for (i = 0; i < NUM_WRITES; i++) {
    memset(write_buf, 'A' + i, CHUNK_SIZE);
    CHECK(write(fd, write_buf, CHUNK_SIZE) == CHUNK_SIZE, "write chunk %d (pattern '%c')", i,
          'A' + i);
  }
  msg("All %d writes completed", NUM_WRITES);

  /* Close and reopen to ensure persistence */
  close(fd);
  CHECK((fd = open("multiwrite")) > 1, "reopen \"multiwrite\"");

  /* Read entire file */
  CHECK(read(fd, read_buf, CHUNK_SIZE * NUM_WRITES) == CHUNK_SIZE * NUM_WRITES, "read all data");

  /* Verify each chunk has correct pattern */
  for (i = 0; i < NUM_WRITES; i++) {
    char expected = 'A' + i;
    int offset = i * CHUNK_SIZE;
    int j;
    bool correct = true;

    for (j = 0; j < CHUNK_SIZE; j++) {
      if (read_buf[offset + j] != expected) {
        correct = false;
        break;
      }
    }
    CHECK(correct, "verify chunk %d has pattern '%c'", i, expected);
  }
  msg("All chunks verified correctly");

  close(fd);
}
