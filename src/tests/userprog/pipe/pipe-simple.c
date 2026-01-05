/* Test that pipe() creates two valid file descriptors. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];

  CHECK(pipe(pipefd) == 0, "pipe() returns 0 on success");
  CHECK(pipefd[0] >= 2, "read fd is valid (>= 2)");
  CHECK(pipefd[1] >= 2, "write fd is valid (>= 2)");
  CHECK(pipefd[0] != pipefd[1], "read and write fds are different");

  close(pipefd[0]);
  close(pipefd[1]);
}
