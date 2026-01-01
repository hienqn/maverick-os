/* Test: wal-grow
 *
 * PURPOSE: Verify WAL correctly handles file growth operations.
 *
 * WHAT IT TESTS:
 * - Growing a file by appending data
 * - WAL logs block allocation operations
 * - Data integrity across multiple blocks
 * - File size reporting after growth
 *
 * File growth involves both data writes and metadata updates (inode),
 * so this tests WAL's handling of compound operations.
 */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

/* Set to 1 after integrating WAL into the filesystem */
#define WAL_INTEGRATED 0

#define CHUNK_SIZE 512 /* One block */
#define NUM_CHUNKS 8   /* Grow to 4KB */
static char write_buf[CHUNK_SIZE];
static char read_buf[CHUNK_SIZE];

void test_main(void) {
  int fd;
  int i, j;
  int fsize;

#if !WAL_INTEGRATED
  fail("WAL not integrated! Set WAL_INTEGRATED=1 after hooking WAL into cache.c");
  return;
#endif

  /* Create empty file */
  CHECK(create("growfile", 0), "create \"growfile\" with size 0");
  CHECK((fd = open("growfile")) > 1, "open \"growfile\"");

  /* Grow file by appending chunks */
  for (i = 0; i < NUM_CHUNKS; i++) {
    memset(write_buf, '0' + i, CHUNK_SIZE);
    CHECK(write(fd, write_buf, CHUNK_SIZE) == CHUNK_SIZE, "append chunk %d (pattern '%c')", i,
          '0' + i);
  }
  msg("File grown to %d bytes (%d chunks)", CHUNK_SIZE * NUM_CHUNKS, NUM_CHUNKS);

  /* Verify file size */
  fsize = filesize(fd);
  CHECK(fsize == CHUNK_SIZE * NUM_CHUNKS, "verify filesize is %d", CHUNK_SIZE * NUM_CHUNKS);

  close(fd);

  /* Reopen and verify all data */
  CHECK((fd = open("growfile")) > 1, "reopen \"growfile\"");

  for (i = 0; i < NUM_CHUNKS; i++) {
    bool correct = true;
    char expected = '0' + i;

    CHECK(read(fd, read_buf, CHUNK_SIZE) == CHUNK_SIZE, "read chunk %d", i);

    for (j = 0; j < CHUNK_SIZE; j++) {
      if (read_buf[j] != expected) {
        correct = false;
        break;
      }
    }
    CHECK(correct, "verify chunk %d has pattern '%c'", i, expected);
  }
  msg("All %d chunks verified after file growth", NUM_CHUNKS);

  close(fd);
}
