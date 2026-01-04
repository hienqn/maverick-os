#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

/* Large buffer to span multiple pages and test COW properly. */
#define BUFFER_SIZE 8192
static char buffer[BUFFER_SIZE];

void test_main(void) {
  /* Initialize buffer with known pattern before fork. */
  memset(buffer, 'A', BUFFER_SIZE);

  msg("Parent initialized buffer with 'A's");

  pid_t pid = fork();
  if (pid < 0) {
    fail("fork returned %d", pid);
  } else if (pid == 0) {
    /* Child process */

    /* Verify child sees parent's data (COW sharing). */
    if (buffer[0] != 'A' || buffer[BUFFER_SIZE - 1] != 'A')
      fail("Child doesn't see parent's initial data");
    msg("Child verified initial buffer is 'A's");

    /* Write to buffer - should trigger COW copy. */
    memset(buffer, 'B', BUFFER_SIZE);
    msg("Child wrote 'B's to buffer");

    /* Verify child sees its own data. */
    if (buffer[0] != 'B' || buffer[BUFFER_SIZE - 1] != 'B')
      fail("Child doesn't see its own writes");
    msg("Child verified buffer is 'B's");

  } else {
    /* Parent process */

    /* Write to buffer before child does - triggers COW. */
    memset(buffer, 'C', BUFFER_SIZE);
    msg("Parent wrote 'C's to buffer");

    /* Wait for child to complete. */
    int status = wait(pid);
    if (status != 0)
      fail("Child exited with status %d", status);

    /* Verify parent still sees its own data (child's writes didn't affect us). */
    if (buffer[0] != 'C' || buffer[BUFFER_SIZE - 1] != 'C')
      fail("Parent's buffer was corrupted by child");
    msg("Parent verified buffer is still 'C's");
  }
}
