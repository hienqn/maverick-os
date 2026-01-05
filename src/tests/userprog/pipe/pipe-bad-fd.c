/* Test that operations on closed/invalid pipe fds fail gracefully. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  char buf[16];
  int n;

  CHECK(pipe(pipefd) == 0, "pipe()");

  /* Close both ends. */
  close(pipefd[0]);
  close(pipefd[1]);

  /* Read/write on closed fds should fail. */
  n = read(pipefd[0], buf, sizeof(buf));
  msg("read on closed fd returned %d", n);

  n = write(pipefd[1], "test", 4);
  msg("write on closed fd returned %d", n);
}
