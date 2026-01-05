/* Test ungetc() for character pushback. */

#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  int c;

  CHECK(create("testfile.txt", 0), "create testfile.txt");
  int fd = open("testfile.txt");
  write(fd, "ABC", 3);
  close(fd);

  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "fopen succeeds");

  /* Read and pushback */
  c = fgetc(fp);
  CHECK(c == 'A', "first fgetc returns 'A'");

  c = ungetc('A', fp);
  CHECK(c == 'A', "ungetc returns pushed char");

  c = fgetc(fp);
  CHECK(c == 'A', "fgetc after ungetc returns 'A'");

  /* Push back a different character */
  c = ungetc('X', fp);
  CHECK(c == 'X', "ungetc 'X' succeeds");
  c = fgetc(fp);
  CHECK(c == 'X', "fgetc returns pushed 'X'");

  /* ungetc clears EOF */
  while (fgetc(fp) != EOF)
    ;
  CHECK(feof(fp), "EOF is set");
  c = ungetc('Z', fp);
  CHECK(c == 'Z', "ungetc after EOF succeeds");
  CHECK(!feof(fp), "ungetc clears EOF flag");
  c = fgetc(fp);
  CHECK(c == 'Z', "fgetc returns pushed 'Z'");

  fclose(fp);

  msg("ungetc tests passed");
}
