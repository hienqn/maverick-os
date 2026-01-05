/* Test ftell() for position reporting. */

#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  long pos;

  CHECK(create("testfile.txt", 0), "create testfile.txt");
  int fd = open("testfile.txt");
  write(fd, "0123456789", 10);
  close(fd);

  /* Test ftell during read */
  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "fopen succeeds");

  pos = ftell(fp);
  CHECK(pos == 0, "ftell at start is 0");

  fgetc(fp);
  pos = ftell(fp);
  CHECK(pos == 1, "ftell after 1 read is 1");

  fgetc(fp);
  fgetc(fp);
  fgetc(fp);
  pos = ftell(fp);
  CHECK(pos == 4, "ftell after 4 reads is 4");

  fseek(fp, 0, SEEK_END);
  pos = ftell(fp);
  CHECK(pos == 10, "ftell at end is 10");

  fclose(fp);

  /* Test ftell during write */
  fp = fopen("testfile.txt", "w");
  pos = ftell(fp);
  CHECK(pos == 0, "ftell at write start is 0");

  fputc('A', fp);
  fputc('B', fp);
  fputc('C', fp);
  fflush(fp); /* Ensure buffer is written for accurate ftell */
  pos = ftell(fp);
  CHECK(pos == 3, "ftell after 3 writes is 3");

  fclose(fp);

  msg("ftell tests passed");
}
