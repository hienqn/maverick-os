/* Test that pipe with invalid pointer fails. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int* bad_ptr = (int*)0xDEADBEEF;

  msg("calling pipe with bad pointer");

  /* This should fail or kill the process. */
  int result = pipe(bad_ptr);

  /* If we get here, pipe returned an error. */
  msg("pipe with bad pointer returned %d", result);
}
