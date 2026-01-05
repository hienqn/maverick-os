/* Test child-to-parent communication via pipe (reverse direction). */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  pid_t pid;
  char msg_in[32];

  CHECK(pipe(pipefd) == 0, "pipe()");

  pid = fork();
  if (pid < 0) {
    fail("fork failed");
  } else if (pid == 0) {
    /* Child: close read end, write to pipe. */
    close(pipefd[0]);
    char msg_out[] = "Hello from child";
    write(pipefd[1], msg_out, sizeof(msg_out));
    close(pipefd[1]);
    exit(0);
  } else {
    /* Parent: close write end, wait for child, read from pipe. */
    close(pipefd[1]);
    wait(pid);
    read(pipefd[0], msg_in, sizeof(msg_in));
    msg("parent received: %s", msg_in);
    close(pipefd[0]);
  }
}
