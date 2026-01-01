/* Test: wal-multi-file
 *
 * PURPOSE: Verify WAL correctly handles operations on multiple files.
 *
 * WHAT IT TESTS:
 * - Creating and writing to multiple files
 * - Each file's data remains isolated and correct
 * - WAL correctly tracks separate file modifications
 *
 * This tests that WAL doesn't mix up data between different files.
 */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

/* Set to 1 after integrating WAL into the filesystem */
#define WAL_INTEGRATED 0

#define FILE_SIZE 256
#define NUM_FILES 3
static char buf[FILE_SIZE];

static const char* filenames[NUM_FILES] = {"file_a", "file_b", "file_c"};
static const char patterns[NUM_FILES] = {'X', 'Y', 'Z'};

void test_main(void) {
  int fd;
  int i, j;

#if !WAL_INTEGRATED
  fail("WAL not integrated! Set WAL_INTEGRATED=1 after hooking WAL into cache.c");
  return;
#endif

  /* Create and write to multiple files */
  for (i = 0; i < NUM_FILES; i++) {
    memset(buf, patterns[i], FILE_SIZE);

    CHECK(create(filenames[i], FILE_SIZE), "create \"%s\"", filenames[i]);
    CHECK((fd = open(filenames[i])) > 1, "open \"%s\"", filenames[i]);
    CHECK(write(fd, buf, FILE_SIZE) == FILE_SIZE, "write '%c' pattern to \"%s\"", patterns[i],
          filenames[i]);
    close(fd);
  }
  msg("Created and wrote to %d files", NUM_FILES);

  /* Verify each file has correct, isolated data */
  for (i = 0; i < NUM_FILES; i++) {
    bool correct = true;

    CHECK((fd = open(filenames[i])) > 1, "reopen \"%s\"", filenames[i]);
    CHECK(read(fd, buf, FILE_SIZE) == FILE_SIZE, "read from \"%s\"", filenames[i]);

    for (j = 0; j < FILE_SIZE; j++) {
      if (buf[j] != patterns[i]) {
        correct = false;
        break;
      }
    }
    CHECK(correct, "verify \"%s\" contains only '%c'", filenames[i], patterns[i]);
    close(fd);
  }
  msg("All files verified - data isolation confirmed");
}
