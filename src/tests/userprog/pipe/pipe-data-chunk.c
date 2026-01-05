/* Test that mismatched read/write sizes work correctly. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  char buf[16];
  int n1, n2, n3;

  CHECK(pipe(pipefd) == 0, "pipe()");

  /* Write in 4-byte chunks. */
  write(pipefd[1], "AAAA", 4);
  write(pipefd[1], "BBBB", 4);
  write(pipefd[1], "CCCC", 4);
  close(pipefd[1]);

  /* Read in 6-byte chunks. */
  n1 = read(pipefd[0], buf, 6);
  buf[n1] = '\0';
  CHECK(n1 == 6, "read 1: %d bytes = %s", n1, buf);

  n2 = read(pipefd[0], buf, 6);
  buf[n2] = '\0';
  CHECK(n2 == 6, "read 2: %d bytes = %s", n2, buf);

  n3 = read(pipefd[0], buf, 6);
  CHECK(n3 == 0, "read 3: %d (EOF)", n3);

  close(pipefd[0]);
}
