/* Test that small data transfers maintain integrity. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  char write_data[] = "The quick brown fox";
  char read_data[32];
  int written, total_read, n;

  CHECK(pipe(pipefd) == 0, "pipe()");

  written = write(pipefd[1], write_data, strlen(write_data) + 1);
  CHECK(written == (int)strlen(write_data) + 1, "wrote %d bytes", written);

  close(pipefd[1]);

  total_read = 0;
  while ((n = read(pipefd[0], read_data + total_read, sizeof(read_data) - total_read)) > 0) {
    total_read += n;
  }

  CHECK(total_read == written, "read %d bytes", total_read);
  CHECK(strcmp(write_data, read_data) == 0, "data integrity verified");

  close(pipefd[0]);
}
