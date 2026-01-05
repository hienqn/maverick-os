/* Test that data arrives in FIFO order. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  int i;
  char c;

  CHECK(pipe(pipefd) == 0, "pipe()");

  /* Write sequence 0-9. */
  for (i = 0; i < 10; i++) {
    c = '0' + i;
    write(pipefd[1], &c, 1);
  }
  close(pipefd[1]);

  /* Read and verify order. */
  for (i = 0; i < 10; i++) {
    read(pipefd[0], &c, 1);
    CHECK(c == '0' + i, "byte %d is '%c'", i, c);
  }

  close(pipefd[0]);
  msg("FIFO order verified");
}
