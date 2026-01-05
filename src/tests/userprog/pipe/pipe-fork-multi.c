/* Test multiple children reading from the same pipe. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define NUM_CHILDREN 3

void test_main(void) {
  int pipefd[2];
  pid_t pids[NUM_CHILDREN];
  int i;

  CHECK(pipe(pipefd) == 0, "pipe()");

  for (i = 0; i < NUM_CHILDREN; i++) {
    pids[i] = fork();
    if (pids[i] < 0) {
      fail("fork %d failed", i);
    } else if (pids[i] == 0) {
      /* Child: close write end and read one byte. */
      close(pipefd[1]);
      char c;
      int n = read(pipefd[0], &c, 1);
      if (n == 1)
        msg("child %d read: %c", i, c);
      else
        msg("child %d read returned %d", i, n);
      close(pipefd[0]);
      exit(0);
    }
  }

  /* Parent: close read end, write one char per child. */
  close(pipefd[0]);
  for (i = 0; i < NUM_CHILDREN; i++) {
    char c = 'A' + i;
    write(pipefd[1], &c, 1);
  }
  close(pipefd[1]);

  /* Wait for all children. */
  for (i = 0; i < NUM_CHILDREN; i++)
    wait(pids[i]);

  msg("all children finished");
}
