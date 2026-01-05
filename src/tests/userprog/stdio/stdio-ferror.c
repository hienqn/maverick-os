/* Test ferror() and error handling. */

#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;

  CHECK(create("testfile.txt", 0), "create testfile.txt");

  /* Open for read - no error initially */
  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "fopen succeeds");
  CHECK(!ferror(fp), "ferror is false initially");

  /* Normal read should not set error */
  fgetc(fp);
  CHECK(!ferror(fp), "ferror is false after read");

  fclose(fp);

  /* Open for write - no error */
  fp = fopen("testfile.txt", "w");
  CHECK(fp != NULL, "fopen for write succeeds");
  CHECK(!ferror(fp), "ferror is false for write stream");

  fputc('A', fp);
  CHECK(!ferror(fp), "ferror is false after write");

  fclose(fp);

  msg("ferror tests passed");
}
