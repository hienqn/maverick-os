/* Test fread() for binary input. */

#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  char buf[64];
  const char* data = "Hello, World!";
  size_t len = strlen(data);

  /* Create file with known content */
  CHECK(create("testfile.txt", 0), "create testfile.txt");
  int fd = open("testfile.txt");
  write(fd, data, len);
  close(fd);

  /* Test fread */
  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "fopen succeeds");

  memset(buf, 0, sizeof buf);
  size_t n = fread(buf, 1, len, fp);
  CHECK(n == len, "fread returns correct count");
  CHECK(strcmp(buf, data) == 0, "fread data matches");

  /* Test fread at EOF */
  n = fread(buf, 1, 10, fp);
  CHECK(n == 0, "fread at EOF returns 0");
  CHECK(feof(fp), "feof returns true after EOF");

  fclose(fp);

  /* Test fread with size > 1 */
  fp = fopen("testfile.txt", "r");
  memset(buf, 0, sizeof buf);
  n = fread(buf, 4, 3, fp); /* Read 3 items of 4 bytes each */
  CHECK(n == 3, "fread with size=4 returns 3 items");
  fclose(fp);

  msg("fread tests passed");
}
