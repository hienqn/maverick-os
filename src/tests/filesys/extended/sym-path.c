/* Tests symbolic links as intermediate path components. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

static char buf[512];

void test_main(void) {
  const char* data = "Deep nested data";
  size_t data_len = strlen(data);
  int fd;
  int bytes_read;

  /* Create a directory structure: a/b/c with a file in c. */
  CHECK(mkdir("a"), "mkdir \"a\"");
  CHECK(mkdir("a/b"), "mkdir \"a/b\"");
  CHECK(mkdir("a/b/c"), "mkdir \"a/b/c\"");
  CHECK(create("a/b/c/file", 0), "create \"a/b/c/file\"");
  CHECK((fd = open("a/b/c/file")) > 1, "open \"a/b/c/file\"");
  CHECK(write(fd, data, data_len) == (int)data_len, "write \"a/b/c/file\"");
  close(fd);
  msg("close \"a/b/c/file\"");

  /* Create a symlink to an intermediate directory. */
  CHECK(symlink("a/b", "shortcut"), "symlink \"a/b\" -> \"shortcut\"");

  /* Access nested file through symlink. */
  CHECK((fd = open("shortcut/c/file")) > 1, "open \"shortcut/c/file\"");
  bytes_read = read(fd, buf, sizeof buf);
  CHECK(bytes_read == (int)data_len, "read \"shortcut/c/file\"");
  buf[bytes_read] = '\0';
  CHECK(strcmp(buf, data) == 0, "verify contents via symlink path");
  close(fd);
  msg("close \"shortcut/c/file\"");

  /* Create a symlink inside a directory to another location. */
  CHECK(symlink("c/file", "a/b/link-to-file"), "symlink \"c/file\" -> \"a/b/link-to-file\"");
  CHECK((fd = open("a/b/link-to-file")) > 1, "open \"a/b/link-to-file\"");
  bytes_read = read(fd, buf, sizeof buf);
  CHECK(bytes_read == (int)data_len, "read \"a/b/link-to-file\"");
  close(fd);
  msg("close \"a/b/link-to-file\"");
}
