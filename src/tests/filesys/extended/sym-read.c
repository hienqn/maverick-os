/* Tests reading a file through a symbolic link. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

static char buf[512];

void test_main(void) {
  const char* data = "Hello, symbolic link world!";
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

  /* Read the file through the symbolic link. */
  CHECK((fd = open("symlink")) > 1, "open \"symlink\"");
  bytes_read = read(fd, buf, sizeof buf);
  CHECK(bytes_read == (int)data_len, "read \"symlink\"");
  buf[bytes_read] = '\0';
  CHECK(strcmp(buf, data) == 0, "verify contents match");
  close(fd);
  msg("close \"symlink\"");
}
