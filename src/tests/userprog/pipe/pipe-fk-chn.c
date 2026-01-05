/* Test pipeline of processes (like shell pipes: A | B | C). */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipe1[2], pipe2[2];
  pid_t pid1, pid2;
  char result[8];
  int i;

  CHECK(pipe(pipe1) == 0, "pipe1()");

  pid1 = fork();
  if (pid1 < 0) {
    fail("fork 1 failed");
  } else if (pid1 == 0) {
    /* Process A: write "DATA" to pipe1. */
    close(pipe1[0]);
    write(pipe1[1], "DATA", 4);
    close(pipe1[1]);
    exit(0);
  }

  CHECK(pipe(pipe2) == 0, "pipe2()");

  pid2 = fork();
  if (pid2 < 0) {
    fail("fork 2 failed");
  } else if (pid2 == 0) {
    /* Process B: read from pipe1, transform to lowercase, write to pipe2. */
    close(pipe1[1]);
    close(pipe2[0]);
    char buf[8];
    read(pipe1[0], buf, 4);
    /* Transform by converting to lowercase. */
    for (i = 0; i < 4; i++)
      buf[i] = buf[i] + ('a' - 'A');
    write(pipe2[1], buf, 4);
    close(pipe1[0]);
    close(pipe2[1]);
    exit(0);
  }

  /* Process C (main): close unused ends and read from pipe2. */
  close(pipe1[0]);
  close(pipe1[1]);
  close(pipe2[1]);

  wait(pid1);
  wait(pid2);

  read(pipe2[0], result, 4);
  result[4] = '\0';
  msg("pipeline result: %s", result);
  CHECK(strcmp(result, "data") == 0, "pipeline transformation correct");
  close(pipe2[0]);
}
