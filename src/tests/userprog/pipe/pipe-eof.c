/* Test that read returns 0 (EOF) when all write ends are closed. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  char buf[16];
  int n1, n2;

  CHECK(pipe(pipefd) == 0, "pipe()");

  /* Write some data. */
  write(pipefd[1], "test", 4);

  /* Close write end. */
  close(pipefd[1]);
  msg("write end closed");

  /* Read the data. */
  n1 = read(pipefd[0], buf, 4);
  CHECK(n1 == 4, "first read got %d bytes", n1);

  /* Read again - should get EOF. */
  n2 = read(pipefd[0], buf, sizeof(buf));
  CHECK(n2 == 0, "second read returned %d (EOF)", n2);

  close(pipefd[0]);
}
