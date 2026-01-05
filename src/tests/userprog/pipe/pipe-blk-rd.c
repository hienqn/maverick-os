/* Test that read blocks when pipe is empty until data arrives. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  pid_t pid;

  CHECK(pipe(pipefd) == 0, "pipe()");

  pid = fork();
  if (pid < 0) {
    fail("fork failed");
  } else if (pid == 0) {
    /* Child: wait a bit by doing busy work, then write. */
    volatile int i;
    close(pipefd[0]);
    for (i = 0; i < 1000000; i++)
      ; /* Busy wait to simulate delay. */
    write(pipefd[1], "X", 1);
    msg("child wrote data");
    close(pipefd[1]);
    exit(0);
  } else {
    /* Parent: read should block until child writes. */
    char c;
    close(pipefd[1]);
    msg("parent about to read (should block)");
    read(pipefd[0], &c, 1);
    msg("parent read: %c", c);
    close(pipefd[0]);
    wait(pid);
  }
}
