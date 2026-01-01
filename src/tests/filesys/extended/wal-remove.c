/* Test: wal-remove
 *
 * PURPOSE: Verify WAL correctly handles file removal operations.
 *
 * STRATEGY:
 * 1. Create multiple files with data
 * 2. Remove some files
 * 3. Verify removed files are gone
 * 4. Verify remaining files still have correct data
 * 5. Create new files in place of removed ones
 *
 * WHAT IT TESTS:
 * - File removal is properly logged to WAL
 * - Directory entries are correctly updated
 * - Free map updates are handled correctly
 * - Removed files cannot be accessed
 * - New files can reuse freed space
 *
 * EXPECTED BEHAVIOR:
 * - Removed files return errors on open
 * - Remaining files have correct content
 * - New files can be created successfully
 *
 * KEY INSIGHT: File removal involves multiple metadata updates
 * (directory entry, inode, free map). WAL must ensure atomicity.
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

#if !WAL_INTEGRATED
  fail("WAL not integrated! Set WAL_INTEGRATED=1 after hooking WAL into cache.c");
  return;
#endif

  msg("=== Phase 1: Create files ===");

  /* Create file A with pattern 'A' */
  memset(buf, 'A', FILE_SIZE);
  CHECK(create("file_a", FILE_SIZE), "create \"file_a\"");
  CHECK((fd = open("file_a")) > 1, "open \"file_a\"");
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "write to \"file_a\"");
  close(fd);
  msg("Created file_a with 'A' pattern");

  /* Create file B with pattern 'B' */
  memset(buf, 'B', FILE_SIZE);
  CHECK(create("file_b", FILE_SIZE), "create \"file_b\"");
  CHECK((fd = open("file_b")) > 1, "open \"file_b\"");
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "write to \"file_b\"");
  close(fd);
  msg("Created file_b with 'B' pattern");

  /* Create file C with pattern 'C' */
  memset(buf, 'C', FILE_SIZE);
  CHECK(create("file_c", FILE_SIZE), "create \"file_c\"");
  CHECK((fd = open("file_c")) > 1, "open \"file_c\"");
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "write to \"file_c\"");
  close(fd);
  msg("Created file_c with 'C' pattern");

  msg("=== Phase 2: Remove file_b ===");

  CHECK(remove("file_b"), "remove \"file_b\"");
  msg("Removed file_b");

  /* Verify file_b is gone */
  fd = open("file_b");
  if (fd != -1) {
    close(fd);
    fail("file_b should not be openable after removal");
  }
  msg("Verified file_b cannot be opened");

  msg("=== Phase 3: Verify remaining files ===");

  /* Verify file_a still has 'A' */
  CHECK((fd = open("file_a")) > 1, "open \"file_a\" after removal of file_b");
  memset(buf, 0, FILE_SIZE);
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read from \"file_a\"");
  CHECK(buf[0] == 'A' && buf[FILE_SIZE - 1] == 'A', "verify \"file_a\" contains 'A'");
  close(fd);
  msg("file_a verified: still contains 'A'");

  /* Verify file_c still has 'C' */
  CHECK((fd = open("file_c")) > 1, "open \"file_c\" after removal of file_b");
  memset(buf, 0, FILE_SIZE);
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read from \"file_c\"");
  CHECK(buf[0] == 'C' && buf[FILE_SIZE - 1] == 'C', "verify \"file_c\" contains 'C'");
  close(fd);
  msg("file_c verified: still contains 'C'");

  msg("=== Phase 4: Reuse freed space ===");

  /* Create new file_b with pattern 'X' */
  memset(buf, 'X', FILE_SIZE);
  CHECK(create("file_b", FILE_SIZE), "recreate \"file_b\"");
  CHECK((fd = open("file_b")) > 1, "open new \"file_b\"");
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "write to new \"file_b\"");
  close(fd);
  msg("Created new file_b with 'X' pattern");

  /* Verify new file_b has 'X' */
  CHECK((fd = open("file_b")) > 1, "reopen new \"file_b\"");
  memset(buf, 0, FILE_SIZE);
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read from new \"file_b\"");
  CHECK(buf[0] == 'X' && buf[FILE_SIZE - 1] == 'X', "verify new \"file_b\" contains 'X'");
  close(fd);
  msg("New file_b verified: contains 'X'");

  msg("=== Phase 5: Remove remaining files ===");

  CHECK(remove("file_a"), "remove \"file_a\"");
  CHECK(remove("file_b"), "remove new \"file_b\"");
  CHECK(remove("file_c"), "remove \"file_c\"");
  msg("All files removed");

  /* Verify all files are gone */
  if (open("file_a") != -1)
    fail("file_a should be gone");
  if (open("file_b") != -1)
    fail("file_b should be gone");
  if (open("file_c") != -1)
    fail("file_c should be gone");
  msg("Verified all files removed successfully");

  msg("File removal with WAL test: PASSED");
}
