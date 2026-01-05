/* Test that multiple pipes can coexist independently. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipe1[2], pipe2[2];
  char c1, c2;

  CHECK(pipe(pipe1) == 0, "first pipe()");
  CHECK(pipe(pipe2) == 0, "second pipe()");

  /* All four fds should be unique. */
  CHECK(pipe1[0] != pipe1[1] && pipe1[0] != pipe2[0] && pipe1[0] != pipe2[1] &&
            pipe1[1] != pipe2[0] && pipe1[1] != pipe2[1] && pipe2[0] != pipe2[1],
        "all fds are unique");

  /* Write/read on each pipe independently. */
  write(pipe1[1], "A", 1);
  write(pipe2[1], "B", 1);

  read(pipe1[0], &c1, 1);
  read(pipe2[0], &c2, 1);

  CHECK(c1 == 'A', "pipe1 data correct");
  CHECK(c2 == 'B', "pipe2 data correct");

  close(pipe1[0]);
  close(pipe1[1]);
  close(pipe2[0]);
  close(pipe2[1]);
}
