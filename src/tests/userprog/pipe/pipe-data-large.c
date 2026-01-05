/* Test large data transfers (multiple buffer sizes). */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define DATA_SIZE 8192 /* 8KB */

static char write_data[DATA_SIZE];
static char read_data[DATA_SIZE];

void test_main(void) {
  int pipefd[2];
  pid_t pid;
  int i;

  /* Initialize with pattern. */
  for (i = 0; i < DATA_SIZE; i++)
    write_data[i] = (char)(i % 256);

  CHECK(pipe(pipefd) == 0, "pipe()");

  pid = fork();
  if (pid < 0) {
    fail("fork failed");
  } else if (pid == 0) {
    /* Child: write all data. */
    int total = 0;
    int n;
    close(pipefd[0]);
    while (total < DATA_SIZE) {
      n = write(pipefd[1], write_data + total, DATA_SIZE - total);
      if (n <= 0)
        break;
      total += n;
    }
    close(pipefd[1]);
    exit(total);
  } else {
    /* Parent: read all data. */
    int total = 0;
    int n;
    close(pipefd[1]);
    while ((n = read(pipefd[0], read_data + total, DATA_SIZE - total)) > 0) {
      total += n;
    }
    close(pipefd[0]);

    int status = wait(pid);
    CHECK(status == DATA_SIZE, "child wrote %d bytes", status);
    CHECK(total == DATA_SIZE, "parent read %d bytes", total);
    CHECK(memcmp(write_data, read_data, DATA_SIZE) == 0, "data integrity verified");
  }
}
