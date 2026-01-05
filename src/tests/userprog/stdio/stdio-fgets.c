/* Test fgets() for line input. */

#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  char buf[64];
  char* result;

  CHECK(create("testfile.txt", 0), "create testfile.txt");
  int fd = open("testfile.txt");
  write(fd, "Line1\nLine2\nLine3", 17);
  close(fd);

  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "fopen succeeds");

  /* Read first line */
  result = fgets(buf, sizeof buf, fp);
  CHECK(result == buf, "fgets returns buffer pointer");
  CHECK(strcmp(buf, "Line1\n") == 0, "first line is 'Line1\\n'");

  /* Read second line */
  result = fgets(buf, sizeof buf, fp);
  CHECK(result != NULL, "fgets second line succeeds");
  CHECK(strcmp(buf, "Line2\n") == 0, "second line is 'Line2\\n'");

  /* Read third line (no trailing newline) */
  result = fgets(buf, sizeof buf, fp);
  CHECK(result != NULL, "fgets third line succeeds");
  CHECK(strcmp(buf, "Line3") == 0, "third line is 'Line3'");

  /* Read at EOF */
  result = fgets(buf, sizeof buf, fp);
  CHECK(result == NULL, "fgets at EOF returns NULL");

  fclose(fp);

  /* Test with small buffer */
  fp = fopen("testfile.txt", "r");
  result = fgets(buf, 4, fp); /* Read at most 3 chars + NUL */
  CHECK(result != NULL, "fgets with small buffer succeeds");
  CHECK(strcmp(buf, "Lin") == 0, "small buffer gets 'Lin'");
  fclose(fp);

  msg("fgets tests passed");
}
