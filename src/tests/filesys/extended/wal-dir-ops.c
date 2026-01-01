/* Test: wal-dir-ops
 *
 * PURPOSE: Verify WAL correctly handles directory operations.
 *
 * WHAT IT TESTS:
 * - Directory creation is logged
 * - File creation within directories is logged
 * - Directory structure persists correctly
 * - Nested directory operations work with WAL
 *
 * This tests that WAL handles metadata (directory) operations correctly.
 */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

/* Set to 1 after integrating WAL into the filesystem */
#define WAL_INTEGRATED 0

#define FILE_SIZE 128
static char buf[FILE_SIZE];

void test_main(void) {
  int fd;

#if !WAL_INTEGRATED
  fail("WAL not integrated! Set WAL_INTEGRATED=1 after hooking WAL into cache.c");
  return;
#endif

  /* Create directory structure */
  CHECK(mkdir("dir1"), "mkdir \"dir1\"");
  CHECK(mkdir("dir1/subdir"), "mkdir \"dir1/subdir\"");
  msg("Directory structure created");

  /* Create files in different directories */
  memset(buf, '1', FILE_SIZE);
  CHECK(create("dir1/file1.txt", FILE_SIZE), "create \"dir1/file1.txt\"");
  CHECK((fd = open("dir1/file1.txt")) > 1, "open \"dir1/file1.txt\"");
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "write to \"dir1/file1.txt\"");
  close(fd);

  memset(buf, '2', FILE_SIZE);
  CHECK(create("dir1/subdir/file2.txt", FILE_SIZE), "create \"dir1/subdir/file2.txt\"");
  CHECK((fd = open("dir1/subdir/file2.txt")) > 1, "open \"dir1/subdir/file2.txt\"");
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "write to \"dir1/subdir/file2.txt\"");
  close(fd);
  msg("Files created in directory structure");

  /* Verify files can be opened via paths */
  CHECK((fd = open("dir1/file1.txt")) > 1, "reopen \"dir1/file1.txt\"");
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read \"dir1/file1.txt\"");
  CHECK(buf[0] == '1', "verify \"dir1/file1.txt\" content");
  close(fd);

  CHECK((fd = open("dir1/subdir/file2.txt")) > 1, "reopen \"dir1/subdir/file2.txt\"");
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read \"dir1/subdir/file2.txt\"");
  CHECK(buf[0] == '2', "verify \"dir1/subdir/file2.txt\" content");
  close(fd);

  msg("Directory operations verified successfully");
}
