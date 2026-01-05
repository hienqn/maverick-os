/* Test line buffering behavior. */

#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  char buf[128];

  CHECK(create("testfile.txt", 0), "create testfile.txt");

  /* stdout is line-buffered, so newline should trigger flush.
     We test this by writing to a file with line buffering. */

  fp = fopen("testfile.txt", "w");
  CHECK(fp != NULL, "fopen succeeds");

  /* setvbuf to line-buffered mode */
  setvbuf(fp, NULL, _IOLBF, 0);

  /* Write partial line - should stay in buffer */
  fputs("partial", fp);

  /* Write newline - should flush */
  fputc('\n', fp);

  /* Check if data is visible (line buffer should have flushed on \n) */
  FILE* fp2 = fopen("testfile.txt", "r");
  fgets(buf, sizeof buf, fp2);
  CHECK(strcmp(buf, "partial\n") == 0, "line buffer flushes on newline");
  fclose(fp2);

  /* Write another line */
  fputs("another line\n", fp);
  fclose(fp);

  /* Verify both lines */
  fp = fopen("testfile.txt", "r");
  fgets(buf, sizeof buf, fp);
  CHECK(strcmp(buf, "partial\n") == 0, "first line preserved");
  fgets(buf, sizeof buf, fp);
  CHECK(strcmp(buf, "another line\n") == 0, "second line correct");
  fclose(fp);

  msg("linebuf tests passed");
}
