/* Tests symbolic links to directories. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int fd;

  /* Create a directory with a file in it. */
  CHECK(mkdir("mydir"), "mkdir \"mydir\"");
  CHECK(create("mydir/file", 512), "create \"mydir/file\"");

  /* Create a symbolic link to the directory. */
  CHECK(symlink("mydir", "dirlink"), "symlink \"mydir\" -> \"dirlink\"");

  /* Access the file through the symlink. */
  CHECK((fd = open("dirlink/file")) > 1, "open \"dirlink/file\"");
  close(fd);
  msg("close \"dirlink/file\"");

  /* Change directory through the symlink. */
  CHECK(chdir("dirlink"), "chdir \"dirlink\"");

  /* Open the file relative to new directory. */
  CHECK((fd = open("file")) > 1, "open \"file\"");
  close(fd);
  msg("close \"file\"");
}
