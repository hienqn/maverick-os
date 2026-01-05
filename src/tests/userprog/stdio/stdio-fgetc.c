/* Test fgetc() and getc() for character input. */

#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  int c;

  /* Create file with known content */
  CHECK(create("testfile.txt", 0), "create testfile.txt");
  int fd = open("testfile.txt");
  write(fd, "ABC", 3);
  close(fd);

  /* Test fgetc */
  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "fopen succeeds");

  c = fgetc(fp);
  CHECK(c == 'A', "fgetc returns 'A'");
  c = fgetc(fp);
  CHECK(c == 'B', "fgetc returns 'B'");
  c = fgetc(fp);
  CHECK(c == 'C', "fgetc returns 'C'");
  c = fgetc(fp);
  CHECK(c == EOF, "fgetc returns EOF at end");

  fclose(fp);

  /* Test getc (should be equivalent) */
  fp = fopen("testfile.txt", "r");
  c = getc(fp);
  CHECK(c == 'A', "getc returns 'A'");
  fclose(fp);

  msg("fgetc tests passed");
}
