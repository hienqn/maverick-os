/* Test fclose() and resource cleanup. */

#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;

  CHECK(create("testfile.txt", 0), "create testfile.txt");

  /* Test normal close */
  fp = fopen("testfile.txt", "w");
  CHECK(fp != NULL, "fopen succeeds");
  CHECK(fclose(fp) == 0, "fclose returns 0 on success");

  /* Test that close flushes data */
  fp = fopen("testfile.txt", "w");
  CHECK(fp != NULL, "fopen for flush test");
  CHECK(fputc('A', fp) == 'A', "fputc writes 'A'");
  CHECK(fclose(fp) == 0, "fclose flushes and closes");

  /* Verify data was written */
  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "reopen file");
  CHECK(fgetc(fp) == 'A', "data was flushed before close");
  fclose(fp);

  msg("fclose tests passed");
}
