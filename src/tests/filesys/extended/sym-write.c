/* Tests writing to a file through a symbolic link. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

static char buf[512];

void test_main(void) {
  const char* data = "Data written through symlink";
  size_t data_len = strlen(data);
  int fd;
  int bytes_read;

  /* Create an empty regular file. */
  CHECK(create("target-file", 0), "create \"target-file\"");

  /* Create a symbolic link to the file. */
  CHECK(symlink("target-file", "symlink"), "symlink \"target-file\" -> \"symlink\"");

  /* Write to the file through the symbolic link. */
  CHECK((fd = open("symlink")) > 1, "open \"symlink\"");
  CHECK(write(fd, data, data_len) == (int)data_len, "write \"symlink\"");
  close(fd);
  msg("close \"symlink\"");

  /* Verify by reading the original file directly. */
  CHECK((fd = open("target-file")) > 1, "open \"target-file\"");
  bytes_read = read(fd, buf, sizeof buf);
  CHECK(bytes_read == (int)data_len, "read \"target-file\"");
  buf[bytes_read] = '\0';
  CHECK(strcmp(buf, data) == 0, "verify contents in \"target-file\"");
  close(fd);
  msg("close \"target-file\"");
}
