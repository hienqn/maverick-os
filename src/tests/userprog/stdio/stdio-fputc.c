/* Test fputc() and putc() for character output. */

#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  int c;

  CHECK(create("testfile.txt", 0), "create testfile.txt");

  /* Test fputc */
  fp = fopen("testfile.txt", "w");
  CHECK(fp != NULL, "fopen succeeds");

  c = fputc('X', fp);
  CHECK(c == 'X', "fputc returns written char");
  c = fputc('Y', fp);
  CHECK(c == 'Y', "fputc returns 'Y'");
  c = fputc('Z', fp);
  CHECK(c == 'Z', "fputc returns 'Z'");

  fclose(fp);

  /* Verify */
  fp = fopen("testfile.txt", "r");
  CHECK(fgetc(fp) == 'X', "first char is 'X'");
  CHECK(fgetc(fp) == 'Y', "second char is 'Y'");
  CHECK(fgetc(fp) == 'Z', "third char is 'Z'");
  fclose(fp);

  /* Test putc */
  fp = fopen("testfile.txt", "w");
  c = putc('!', fp);
  CHECK(c == '!', "putc returns written char");
  fclose(fp);

  msg("fputc tests passed");
}
