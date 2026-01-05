/* Test write to pipe with closed read end. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  int n;

  CHECK(pipe(pipefd) == 0, "pipe()");

  close(pipefd[0]); /* Close read end. */
  msg("read end closed");

  /* Write should fail (EPIPE/SIGPIPE in POSIX, -1 or process death in Pintos). */
  n = write(pipefd[1], "test", 4);
  msg("write to broken pipe returned %d", n);

  close(pipefd[1]);
}
