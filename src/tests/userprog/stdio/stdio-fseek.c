/* Test fseek() for stream positioning. */

#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  int c;

  CHECK(create("testfile.txt", 0), "create testfile.txt");
  int fd = open("testfile.txt");
  write(fd, "ABCDEFGHIJ", 10);
  close(fd);

  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "fopen succeeds");

  /* Test SEEK_SET */
  CHECK(fseek(fp, 5, SEEK_SET) == 0, "fseek SEEK_SET returns 0");
  c = fgetc(fp);
  CHECK(c == 'F', "after seek to 5, read 'F'");

  /* Test SEEK_SET to beginning */
  CHECK(fseek(fp, 0, SEEK_SET) == 0, "fseek to 0 succeeds");
  c = fgetc(fp);
  CHECK(c == 'A', "after seek to 0, read 'A'");

  /* Test SEEK_CUR (forward) */
  CHECK(fseek(fp, 3, SEEK_CUR) == 0, "fseek SEEK_CUR +3 succeeds");
  c = fgetc(fp);
  CHECK(c == 'E', "after skip 3, read 'E'");

  /* Test SEEK_END */
  CHECK(fseek(fp, -2, SEEK_END) == 0, "fseek SEEK_END -2 succeeds");
  c = fgetc(fp);
  CHECK(c == 'I', "2 from end is 'I'");

  fclose(fp);

  /* Test rewind */
  fp = fopen("testfile.txt", "r");
  fgetc(fp);
  fgetc(fp);
  fgetc(fp);
  rewind(fp);
  c = fgetc(fp);
  CHECK(c == 'A', "rewind goes to beginning");
  fclose(fp);

  msg("fseek tests passed");
}
