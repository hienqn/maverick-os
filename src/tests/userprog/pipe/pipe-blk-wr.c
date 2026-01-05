/* Test pipe buffer capacity and large data transfer.
   This test verifies that data can be written and read in large chunks. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define CHUNK_SIZE 1024
#define NUM_CHUNKS 8

void test_main(void) {
  int pipefd[2];
  pid_t pid;
  static char buf[CHUNK_SIZE];
  int i;

  CHECK(pipe(pipefd) == 0, "pipe()");
  memset(buf, 'A', sizeof(buf));

  pid = fork();
  if (pid < 0) {
    fail("fork failed");
  } else if (pid == 0) {
    /* Child: write multiple chunks to fill buffer. */
    int total = 0;
    close(pipefd[0]);
    for (i = 0; i < NUM_CHUNKS; i++) {
      int n = write(pipefd[1], buf, CHUNK_SIZE);
      if (n > 0)
        total += n;
    }
    msg("child wrote %d bytes total", total);
    close(pipefd[1]);
    exit(total);
  } else {
    /* Parent: read all data. */
    int total = 0;
    int n;
    close(pipefd[1]);
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
      total += n;
    }
    msg("parent read %d bytes total", total);
    close(pipefd[0]);
    int status = wait(pid);
    CHECK(total == status, "bytes match: wrote %d, read %d", status, total);
  }
}
