/* Tests basic symlink creation and readlink. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  char buf[64];
  int len;

  /* Create a regular file first. */
  CHECK(create("target-file", 512), "create \"target-file\"");

  /* Create a symbolic link to the file. */
  CHECK(symlink("target-file", "symlink"), "symlink \"target-file\" -> \"symlink\"");

  /* Verify we can read the link target. */
  len = readlink("symlink", buf, sizeof buf);
  CHECK(len == (int)strlen("target-file"), "readlink \"symlink\" length correct");
  buf[len] = '\0';
  CHECK(strcmp(buf, "target-file") == 0, "readlink \"symlink\" returns \"target-file\"");
}
