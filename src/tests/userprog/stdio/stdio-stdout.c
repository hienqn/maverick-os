/* Test stdout stream and fprintf to stdout. */

#include <stdio.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  /* Test that stdout exists */
  CHECK(stdout != NULL, "stdout is not NULL");

  /* Test fprintf to stdout */
  int n = fprintf(stdout, "Hello from fprintf\n");
  CHECK(n == 19, "fprintf returns correct count");

  /* Test fputs to stdout */
  int r = fputs("fputs output\n", stdout);
  CHECK(r >= 0, "fputs to stdout succeeds");

  /* Test fputc to stdout */
  fputc('X', stdout);
  fputc('\n', stdout);

  msg("stdout tests passed");
}
