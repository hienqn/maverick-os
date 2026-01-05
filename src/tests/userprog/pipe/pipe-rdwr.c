/* Test basic write and read operations on a pipe. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int pipefd[2];
  char write_buf[] = "Hello, Pipe!";
  char read_buf[32];
  int bytes_written, bytes_read;

  CHECK(pipe(pipefd) == 0, "pipe()");

  bytes_written = write(pipefd[1], write_buf, sizeof(write_buf));
  CHECK(bytes_written == (int)sizeof(write_buf), "write %d bytes", bytes_written);

  bytes_read = read(pipefd[0], read_buf, sizeof(read_buf));
  CHECK(bytes_read == (int)sizeof(write_buf), "read %d bytes", bytes_read);
  CHECK(memcmp(write_buf, read_buf, sizeof(write_buf)) == 0, "data matches");

  close(pipefd[0]);
  close(pipefd[1]);
}
