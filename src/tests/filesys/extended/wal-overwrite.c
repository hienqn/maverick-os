/* Test: wal-overwrite
 *
 * PURPOSE: Verify WAL correctly handles overwriting data in place.
 *
 * WHAT IT TESTS:
 * - Writing to a file, then overwriting the same location
 * - Final data reflects the last write (correct ordering)
 * - WAL tracks before/after images correctly for overwrites
 *
 * This is critical for testing that WAL applies changes in correct order.
 */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

/* Set to 1 after integrating WAL into the filesystem */
#define WAL_INTEGRATED 1

#define FILE_SIZE 256
static char buf[FILE_SIZE];

void test_main(void) {
  int fd;
  int i;

#if !WAL_INTEGRATED
  fail("WAL not integrated! Set WAL_INTEGRATED=1 after hooking WAL into cache.c");
  return;
#endif

  /* Create file with initial content */
  CHECK(create("overwrite", FILE_SIZE), "create \"overwrite\"");
  CHECK((fd = open("overwrite")) > 1, "open \"overwrite\"");

  /* Write initial pattern 'A' */
  memset(buf, 'A', FILE_SIZE);
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "write initial 'A' pattern");

  /* Seek back and verify */
  seek(fd, 0);
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read after first write");
  CHECK(buf[0] == 'A' && buf[FILE_SIZE - 1] == 'A', "verify 'A' pattern");
  msg("Initial write verified");

  /* Overwrite with 'B' */
  seek(fd, 0);
  memset(buf, 'B', FILE_SIZE);
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "overwrite with 'B' pattern");

  /* Verify overwrite */
  seek(fd, 0);
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read after overwrite");
  CHECK(buf[0] == 'B' && buf[FILE_SIZE - 1] == 'B', "verify 'B' pattern");
  msg("First overwrite verified");

  /* Overwrite again with 'C' */
  seek(fd, 0);
  memset(buf, 'C', FILE_SIZE);
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "overwrite with 'C' pattern");

  close(fd);

  /* Reopen and verify final state */
  CHECK((fd = open("overwrite")) > 1, "reopen \"overwrite\"");
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read after reopen");

  /* Verify final content is 'C' (last write) */
  for (i = 0; i < FILE_SIZE; i++) {
    if (buf[i] != 'C') {
      fail("byte %d is '%c', expected 'C'", i, buf[i]);
    }
  }
  msg("Final content is 'C' - overwrite ordering correct");

  close(fd);
}
