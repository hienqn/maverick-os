/* Test that you can't write to read end or read from write end. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  char buf[16];
  int n1, n2;

  CHECK(pipe(pipefd) == 0, "pipe()");

  /* Try to write to read end. */
  n1 = write(pipefd[0], "test", 4);
  msg("write to read fd returned %d", n1);

  /* Try to read from write end. */
  n2 = read(pipefd[1], buf, sizeof(buf));
  msg("read from write fd returned %d", n2);

  close(pipefd[0]);
  close(pipefd[1]);
}
