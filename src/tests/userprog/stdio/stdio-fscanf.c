/* Test fscanf() for formatted input. */

#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  int i, j;
  char str[32];
  char c;
  int n;

  CHECK(create("testfile.txt", 0), "create testfile.txt");

  /* Create test data */
  int fd = open("testfile.txt");
  write(fd, "42 hello X\n100 200\n", 19);
  close(fd);

  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "fopen succeeds");

  /* Test %d */
  n = fscanf(fp, "%d", &i);
  CHECK(n == 1, "fscanf %%d returns 1");
  CHECK(i == 42, "parsed integer is 42");

  /* Test %s */
  n = fscanf(fp, "%s", str);
  CHECK(n == 1, "fscanf %%s returns 1");
  CHECK(strcmp(str, "hello") == 0, "parsed string is 'hello'");

  /* Test %c (with leading space to skip whitespace) */
  n = fscanf(fp, " %c", &c);
  CHECK(n == 1, "fscanf %%c returns 1");
  CHECK(c == 'X', "parsed char is 'X'");

  /* Test multiple values */
  n = fscanf(fp, "%d %d", &i, &j);
  CHECK(n == 2, "fscanf two ints returns 2");
  CHECK(i == 100 && j == 200, "parsed 100 and 200");

  /* Test EOF */
  n = fscanf(fp, "%d", &i);
  CHECK(n == EOF, "fscanf at EOF returns EOF");

  fclose(fp);

  msg("fscanf tests passed");
}
