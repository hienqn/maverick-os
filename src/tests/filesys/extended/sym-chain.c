/* Tests chains of symbolic links (symlink to symlink). */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

static char buf[512];

void test_main(void) {
  const char* data = "Data through symlink chain";
  size_t data_len = strlen(data);
  int fd;
  int bytes_read;

  /* Create and write to a regular file. */
  CHECK(create("target-file", 512), "create \"target-file\"");
  CHECK((fd = open("target-file")) > 1, "open \"target-file\"");
  CHECK(write(fd, data, data_len) == (int)data_len, "write \"target-file\"");
  close(fd);
  msg("close \"target-file\"");

  /* Create a chain of symbolic links: link3 -> link2 -> link1 -> target-file */
  CHECK(symlink("target-file", "link1"), "symlink \"target-file\" -> \"link1\"");
  CHECK(symlink("link1", "link2"), "symlink \"link1\" -> \"link2\"");
  CHECK(symlink("link2", "link3"), "symlink \"link2\" -> \"link3\"");

  /* Read through the chain. */
  CHECK((fd = open("link3")) > 1, "open \"link3\"");
  bytes_read = read(fd, buf, sizeof buf);
  CHECK(bytes_read == (int)data_len, "read \"link3\"");
  buf[bytes_read] = '\0';
  CHECK(strcmp(buf, data) == 0, "verify contents through chain");
  close(fd);
  msg("close \"link3\"");

  /* Verify readlink returns immediate target, not final target. */
  bytes_read = readlink("link3", buf, sizeof buf);
  CHECK(bytes_read == (int)strlen("link2"), "readlink \"link3\" length correct");
  buf[bytes_read] = '\0';
  CHECK(strcmp(buf, "link2") == 0, "readlink \"link3\" returns \"link2\"");
}
