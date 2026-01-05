/* Test that child gets EOF when parent closes write end without writing. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  pid_t pid;
  char buf[16];

  CHECK(pipe(pipefd) == 0, "pipe()");

  pid = fork();
  if (pid < 0) {
    fail("fork failed");
  } else if (pid == 0) {
    /* Child: close write end, read until EOF. */
    close(pipefd[1]);
    int n = read(pipefd[0], buf, sizeof(buf));
    msg("child read returned %d (EOF expected: 0)", n);
    close(pipefd[0]);
    exit(n == 0 ? 0 : 1);
  } else {
    /* Parent: close both ends without writing. */
    close(pipefd[0]);
    close(pipefd[1]); /* This should cause child to get EOF. */
    int status = wait(pid);
    CHECK(status == 0, "child got EOF correctly");
  }
}
