/* Test that closing pipe file descriptors works correctly. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];

  CHECK(pipe(pipefd) == 0, "pipe()");

  close(pipefd[0]);
  msg("closed read end");

  close(pipefd[1]);
  msg("closed write end");
}
