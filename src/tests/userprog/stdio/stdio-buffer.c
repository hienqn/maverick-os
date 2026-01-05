/* Test buffered I/O behavior. */

#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  char buf[128];

  CHECK(create("testfile.txt", 0), "create testfile.txt");

  /* Test that writes are buffered */
  fp = fopen("testfile.txt", "w");
  CHECK(fp != NULL, "fopen succeeds");

  /* Write some data */
  fputs("Buffered data", fp);

  /* Before fclose/fflush, file might be empty (data in buffer) */
  /* After fclose, data should be written */
  fclose(fp);

  /* Verify data was written after close */
  fp = fopen("testfile.txt", "r");
  fgets(buf, sizeof buf, fp);
  CHECK(strcmp(buf, "Buffered data") == 0, "data written after close");
  fclose(fp);

  /* Test explicit fflush */
  fp = fopen("testfile.txt", "w");
  fputs("Flushed data", fp);
  fflush(fp);

  /* Data should be visible now even before close */
  FILE* fp2 = fopen("testfile.txt", "r");
  fgets(buf, sizeof buf, fp2);
  CHECK(strcmp(buf, "Flushed data") == 0, "data visible after fflush");
  fclose(fp2);
  fclose(fp);

  msg("buffer tests passed");
}
