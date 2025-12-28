/* Tests that circular symbolic links are detected and handled. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  /* Create a symbolic link cycle: link1 -> link2 -> link1 */
  CHECK(symlink("link2", "link1"), "symlink \"link2\" -> \"link1\"");
  CHECK(symlink("link1", "link2"), "symlink \"link1\" -> \"link2\"");

  /* Attempting to open should fail due to cycle. */
  CHECK(open("link1") == -1, "open \"link1\" (must fail - cycle)");
  CHECK(open("link2") == -1, "open \"link2\" (must fail - cycle)");

  /* Create a self-referential symlink. */
  CHECK(symlink("self", "self"), "symlink \"self\" -> \"self\"");
  CHECK(open("self") == -1, "open \"self\" (must fail - self-reference)");

  /* readlink should still work on cyclic links. */
  char buf[64];
  int len = readlink("link1", buf, sizeof buf);
  CHECK(len == 5, "readlink \"link1\" length correct");
  buf[len] = '\0';
  CHECK(buf[0] == 'l' && buf[1] == 'i', "readlink \"link1\" returns \"link2\"");
}
