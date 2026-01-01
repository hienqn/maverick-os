/* Test: wal-basic
 *
 * PURPOSE: Verify basic filesystem operations work correctly with WAL enabled.
 *
 * WHAT IT TESTS:
 * - File creation with WAL logging
 * - File write operations are properly logged and persisted
 * - File read returns correct data after WAL-logged writes
 * - File close and reopen preserves data
 *
 * This is a sanity check that WAL doesn't break normal filesystem operations.
 *
 * NOTE: This test verifies filesystem operations work. To verify WAL is
 * actually being used, run the kernel tests (rfkt wal-*) which directly
 * test the WAL API.
 */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

/* Define WAL_INTEGRATED as 1 after you integrate WAL into the filesystem.
 * This will enable the integration check that fails if WAL isn't being used. */
#define WAL_INTEGRATED 0

#define FILE_SIZE 512
static char write_buf[FILE_SIZE];
static char read_buf[FILE_SIZE];

void test_main(void) {
  int fd;

#if !WAL_INTEGRATED
  fail("WAL not integrated! Set WAL_INTEGRATED=1 after hooking WAL into cache.c");
  return; /* Not reached - fail() exits */
#endif

  /* Initialize write buffer with known pattern */
  memset(write_buf, 'A', FILE_SIZE);

  /* Create a file */
  CHECK(create("testfile", FILE_SIZE), "create \"testfile\"");
  msg("File created successfully");

  /* Open the file */
  CHECK((fd = open("testfile")) > 1, "open \"testfile\"");
  msg("File opened, fd = %d", fd);

  /* Write data to the file */
  CHECK(write(fd, write_buf, FILE_SIZE) == FILE_SIZE, "write %d bytes to \"testfile\"", FILE_SIZE);
  msg("Write completed");

  /* Seek back to beginning */
  seek(fd, 0);

  /* Read and verify data */
  CHECK(read(fd, read_buf, FILE_SIZE) == FILE_SIZE, "read %d bytes from \"testfile\"", FILE_SIZE);

  /* Verify data integrity */
  CHECK(memcmp(write_buf, read_buf, FILE_SIZE) == 0, "verify data integrity");
  msg("Data verification passed");

  /* Close the file */
  close(fd);
  msg("File closed");

  /* Reopen and verify data persists */
  CHECK((fd = open("testfile")) > 1, "reopen \"testfile\"");

  memset(read_buf, 0, FILE_SIZE);
  CHECK(read(fd, read_buf, FILE_SIZE) == FILE_SIZE, "read after reopen");

  CHECK(memcmp(write_buf, read_buf, FILE_SIZE) == 0, "verify data persists after reopen");
  msg("Data persistence verified");

  close(fd);
}
