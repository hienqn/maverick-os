/* Test partial reads followed by EOF. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  char buf[4];
  int n1, n2, n3, n4;

  CHECK(pipe(pipefd) == 0, "pipe()");

  write(pipefd[1], "ABCDEFGH", 8);
  close(pipefd[1]);

  /* Read in small chunks. */
  n1 = read(pipefd[0], buf, 3);
  CHECK(n1 == 3, "read 1: %d bytes", n1);

  n2 = read(pipefd[0], buf, 3);
  CHECK(n2 == 3, "read 2: %d bytes", n2);

  n3 = read(pipefd[0], buf, 3);
  CHECK(n3 == 2, "read 3: %d bytes (remaining)", n3);

  n4 = read(pipefd[0], buf, 3);
  CHECK(n4 == 0, "read 4: %d (EOF)", n4);

  close(pipefd[0]);
}
