/* Tests that removing a symlink does not remove the target file. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

static char buf[512];

void test_main(void) {
  const char* data = "Target file data";
  size_t data_len = strlen(data);
  int fd;
  int bytes_read;

  /* Create and write to a regular file. */
  CHECK(create("target-file", 0), "create \"target-file\"");
  CHECK((fd = open("target-file")) > 1, "open \"target-file\"");
  CHECK(write(fd, data, data_len) == (int)data_len, "write \"target-file\"");
  close(fd);
  msg("close \"target-file\"");

  /* Create a symbolic link to the file. */
  CHECK(symlink("target-file", "symlink"), "symlink \"target-file\" -> \"symlink\"");

  /* Remove the symbolic link. */
  CHECK(remove("symlink"), "remove \"symlink\"");

  /* Verify the symlink is gone. */
  CHECK(open("symlink") == -1, "open \"symlink\" (must fail)");

  /* Verify the target file still exists and has correct contents. */
  CHECK((fd = open("target-file")) > 1, "open \"target-file\" (still exists)");
  bytes_read = read(fd, buf, sizeof buf);
  CHECK(bytes_read == (int)data_len, "read \"target-file\"");
  buf[bytes_read] = '\0';
  CHECK(strcmp(buf, data) == 0, "verify contents intact");
  close(fd);
  msg("close \"target-file\"");
}
