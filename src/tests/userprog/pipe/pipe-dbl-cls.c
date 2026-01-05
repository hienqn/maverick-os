/* Test that closing same fd twice is handled gracefully. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];

  CHECK(pipe(pipefd) == 0, "pipe()");

  close(pipefd[0]);
  msg("first close of read end");

  close(pipefd[0]); /* Second close of same fd. */
  msg("second close of read end (should be no-op or error)");

  close(pipefd[1]);
  msg("closed write end");
}
