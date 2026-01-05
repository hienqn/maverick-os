/* Test sscanf() for formatted input from strings. */

#include <stdio.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int i, j;
  char str[32];
  unsigned u;
  int n;

  /* Test basic integer */
  n = sscanf("42", "%d", &i);
  CHECK(n == 1, "sscanf %%d returns 1");
  CHECK(i == 42, "parsed 42");

  /* Test negative integer */
  n = sscanf("-123", "%d", &i);
  CHECK(n == 1, "sscanf negative returns 1");
  CHECK(i == -123, "parsed -123");

  /* Test hex */
  n = sscanf("ff", "%x", &u);
  CHECK(n == 1, "sscanf %%x returns 1");
  CHECK(u == 255, "parsed 0xff as 255");

  /* Test string */
  n = sscanf("hello world", "%s", str);
  CHECK(n == 1, "sscanf %%s returns 1");
  CHECK(strcmp(str, "hello") == 0, "parsed 'hello'");

  /* Test multiple values */
  n = sscanf("10 20 30", "%d %d %d", &i, &j, &u);
  CHECK(n == 3, "sscanf three ints returns 3");
  CHECK(i == 10 && j == 20 && u == 30, "parsed 10, 20, 30");

  /* Test with literals */
  n = sscanf("x=5", "x=%d", &i);
  CHECK(n == 1, "sscanf with literal returns 1");
  CHECK(i == 5, "parsed x=5");

  /* Test no match */
  n = sscanf("abc", "%d", &i);
  CHECK(n == 0, "sscanf no match returns 0");

  msg("sscanf tests passed");
}
