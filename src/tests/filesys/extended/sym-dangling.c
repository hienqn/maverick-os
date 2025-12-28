/* Tests dangling symbolic links (symlinks to non-existent targets). */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

static char buf[512];

void test_main(void) {
  const char* data = "New file data";
  size_t data_len = strlen(data);
  int fd;
  int bytes_read;

  /* Create a symlink to a non-existent file. */
  CHECK(symlink("nonexistent", "dangling"), "symlink \"nonexistent\" -> \"dangling\"");

  /* Verify readlink works on dangling symlink. */
  bytes_read = readlink("dangling", buf, sizeof buf);
  CHECK(bytes_read == (int)strlen("nonexistent"), "readlink \"dangling\" length correct");
  buf[bytes_read] = '\0';
  CHECK(strcmp(buf, "nonexistent") == 0, "readlink \"dangling\" returns \"nonexistent\"");

  /* Opening dangling symlink should fail. */
  CHECK(open("dangling") == -1, "open \"dangling\" (must fail)");

  /* Create the target file - symlink should now work. */
  CHECK(create("nonexistent", 512), "create \"nonexistent\"");
  CHECK((fd = open("nonexistent")) > 1, "open \"nonexistent\"");
  CHECK(write(fd, data, data_len) == (int)data_len, "write \"nonexistent\"");
  close(fd);
  msg("close \"nonexistent\"");

  /* Now opening through symlink should succeed. */
  CHECK((fd = open("dangling")) > 1, "open \"dangling\" (now works)");
  bytes_read = read(fd, buf, sizeof buf);
  CHECK(bytes_read == (int)data_len, "read \"dangling\"");
  buf[bytes_read] = '\0';
  CHECK(strcmp(buf, data) == 0, "verify contents through symlink");
  close(fd);
  msg("close \"dangling\"");

  /* Remove target - symlink becomes dangling again. */
  CHECK(remove("nonexistent"), "remove \"nonexistent\"");
  CHECK(open("dangling") == -1, "open \"dangling\" (dangling again)");
}
