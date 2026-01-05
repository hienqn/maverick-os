/* Test stdin stream (basic existence test). */

#include <stdio.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  /* Test that stdin exists and is configured */
  CHECK(stdin != NULL, "stdin is not NULL");
  CHECK(fileno(stdin) == 0, "stdin fd is 0");

  /* Note: We can't easily test interactive input in automated tests.
     This just verifies stdin is properly initialized. */

  msg("stdin tests passed");
}
