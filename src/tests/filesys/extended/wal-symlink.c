/* Test: wal-symlink
 *
 * PURPOSE: Verify WAL correctly handles symbolic link operations.
 *
 * STRATEGY:
 * 1. Create a regular file with data
 * 2. Create a symlink to that file
 * 3. Read through the symlink and verify data
 * 4. Write through the symlink and verify changes
 * 5. Remove the symlink (file should remain)
 * 6. Create symlink to directory
 *
 * WHAT IT TESTS:
 * - Symlink creation is properly logged
 * - Reading/writing through symlinks works with WAL
 * - Symlink removal doesn't affect target
 * - Directory symlinks work correctly
 *
 * EXPECTED BEHAVIOR:
 * - Data read through symlink matches original
 * - Writes through symlink modify original file
 * - Removing symlink leaves target intact
 *
 * KEY INSIGHT: Symlinks add an extra level of indirection.
 * WAL must correctly log all operations through symlinks.
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

  msg("=== Phase 1: Create target file ===");

  /* Create original file with known content */
  memset(buf, 'T', FILE_SIZE); /* T = Target */
  CHECK(create("target.txt", FILE_SIZE), "create \"target.txt\"");
  CHECK((fd = open("target.txt")) > 1, "open \"target.txt\"");
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "write to \"target.txt\"");
  close(fd);
  msg("Created target.txt with 'T' pattern");

  msg("=== Phase 2: Create and use symlink ===");

  /* Create symlink to target */
  CHECK(symlink("target.txt", "link.txt"), "create symlink \"link.txt\" -> \"target.txt\"");
  msg("Created symlink link.txt -> target.txt");

  /* Read through symlink */
  CHECK((fd = open("link.txt")) > 1, "open \"link.txt\" (symlink)");
  memset(buf, 0, FILE_SIZE);
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read through symlink");
  CHECK(buf[0] == 'T' && buf[FILE_SIZE - 1] == 'T', "verify data through symlink is 'T'");
  close(fd);
  msg("Read through symlink: correct data 'T'");

  msg("=== Phase 3: Write through symlink ===");

  /* Write through symlink */
  memset(buf, 'W', FILE_SIZE); /* W = Written through symlink */
  CHECK((fd = open("link.txt")) > 1, "open \"link.txt\" for writing");
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "write through symlink");
  close(fd);
  msg("Wrote 'W' pattern through symlink");

  /* Verify original file was modified */
  CHECK((fd = open("target.txt")) > 1, "open \"target.txt\" directly");
  memset(buf, 0, FILE_SIZE);
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read from \"target.txt\"");
  CHECK(buf[0] == 'W' && buf[FILE_SIZE - 1] == 'W', "verify target has 'W' (written through link)");
  close(fd);
  msg("Target file now contains 'W': write through symlink worked");

  msg("=== Phase 4: Remove symlink, keep target ===");

  CHECK(remove("link.txt"), "remove symlink \"link.txt\"");
  msg("Removed symlink");

  /* Symlink should be gone */
  fd = open("link.txt");
  if (fd != -1) {
    close(fd);
    fail("symlink should be removed");
  }
  msg("Verified symlink is gone");

  /* Target should still exist with correct data */
  CHECK((fd = open("target.txt")) > 1, "open \"target.txt\" after symlink removal");
  memset(buf, 0, FILE_SIZE);
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read from \"target.txt\"");
  CHECK(buf[0] == 'W' && buf[FILE_SIZE - 1] == 'W', "verify target still has 'W'");
  close(fd);
  msg("Target file still exists with 'W' data");

  msg("=== Phase 5: Directory symlink ===");

  /* Create a directory */
  CHECK(mkdir("mydir"), "create directory \"mydir\"");

  /* Create a file in the directory */
  memset(buf, 'D', FILE_SIZE); /* D = Directory file */
  CHECK(create("mydir/file.txt", FILE_SIZE), "create \"mydir/file.txt\"");
  CHECK((fd = open("mydir/file.txt")) > 1, "open \"mydir/file.txt\"");
  CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "write to \"mydir/file.txt\"");
  close(fd);
  msg("Created mydir/file.txt with 'D' pattern");

  /* Create symlink to directory */
  CHECK(symlink("mydir", "dirlink"), "create symlink \"dirlink\" -> \"mydir\"");
  msg("Created symlink dirlink -> mydir");

  /* Access file through directory symlink */
  CHECK((fd = open("dirlink/file.txt")) > 1, "open \"dirlink/file.txt\" (through dir symlink)");
  memset(buf, 0, FILE_SIZE);
  CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read through directory symlink");
  CHECK(buf[0] == 'D' && buf[FILE_SIZE - 1] == 'D', "verify data through dir symlink is 'D'");
  close(fd);
  msg("Read through directory symlink: correct data 'D'");

  msg("=== Cleanup ===");

  /* Clean up */
  CHECK(remove("dirlink"), "remove dir symlink");
  CHECK(remove("mydir/file.txt"), "remove \"mydir/file.txt\"");
  CHECK(remove("mydir"), "remove directory \"mydir\"");
  CHECK(remove("target.txt"), "remove \"target.txt\"");
  msg("Cleanup complete");

  msg("Symlink with WAL test: PASSED");
}
