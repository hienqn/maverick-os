/* Test feof() and EOF detection. */

#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;

  CHECK(create("testfile.txt", 0), "create testfile.txt");
  int fd = open("testfile.txt");
  write(fd, "AB", 2);
  close(fd);

  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "fopen succeeds");

  /* Not EOF initially */
  CHECK(!feof(fp), "feof is false at start");

  /* Read all data */
  fgetc(fp);
  CHECK(!feof(fp), "feof is false after first read");
  fgetc(fp);
  CHECK(!feof(fp), "feof is false after second read");

  /* Try to read past end */
  int c = fgetc(fp);
  CHECK(c == EOF, "fgetc returns EOF");
  CHECK(feof(fp), "feof is true after EOF");

  /* clearerr clears EOF */
  clearerr(fp);
  CHECK(!feof(fp), "feof is false after clearerr");

  fclose(fp);

  msg("feof tests passed");
}
