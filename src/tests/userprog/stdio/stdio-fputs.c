/* Test fputs() for string output. */

#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  char buf[64];
  int result;

  CHECK(create("testfile.txt", 0), "create testfile.txt");

  /* Test basic fputs */
  fp = fopen("testfile.txt", "w");
  CHECK(fp != NULL, "fopen succeeds");

  result = fputs("Hello", fp);
  CHECK(result >= 0, "fputs returns non-negative");

  result = fputs(" World", fp);
  CHECK(result >= 0, "fputs second string succeeds");

  fclose(fp);

  /* Verify */
  fp = fopen("testfile.txt", "r");
  fgets(buf, sizeof buf, fp);
  CHECK(strcmp(buf, "Hello World") == 0, "file contains 'Hello World'");
  fclose(fp);

  /* Test fputs with newlines */
  fp = fopen("testfile.txt", "w");
  fputs("Line1\n", fp);
  fputs("Line2\n", fp);
  fclose(fp);

  fp = fopen("testfile.txt", "r");
  fgets(buf, sizeof buf, fp);
  CHECK(strcmp(buf, "Line1\n") == 0, "first line correct");
  fgets(buf, sizeof buf, fp);
  CHECK(strcmp(buf, "Line2\n") == 0, "second line correct");
  fclose(fp);

  msg("fputs tests passed");
}
