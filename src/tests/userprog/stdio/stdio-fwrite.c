/* Test fwrite() for binary output. */

#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  char buf[64];
  const char* data = "Test data for fwrite";
  size_t len = strlen(data);

  CHECK(create("testfile.txt", 0), "create testfile.txt");

  /* Test basic fwrite */
  fp = fopen("testfile.txt", "w");
  CHECK(fp != NULL, "fopen for write");
  size_t n = fwrite(data, 1, len, fp);
  CHECK(n == len, "fwrite returns correct count");
  fclose(fp);

  /* Verify written data */
  fp = fopen("testfile.txt", "r");
  memset(buf, 0, sizeof buf);
  fread(buf, 1, len, fp);
  CHECK(strcmp(buf, data) == 0, "written data matches");
  fclose(fp);

  /* Test fwrite with size > 1 */
  fp = fopen("testfile.txt", "w");
  int nums[] = {1, 2, 3, 4, 5};
  n = fwrite(nums, sizeof(int), 5, fp);
  CHECK(n == 5, "fwrite 5 ints succeeds");
  fclose(fp);

  /* Verify int array */
  fp = fopen("testfile.txt", "r");
  int read_nums[5];
  n = fread(read_nums, sizeof(int), 5, fp);
  CHECK(n == 5, "fread 5 ints succeeds");
  CHECK(read_nums[0] == 1 && read_nums[4] == 5, "int data matches");
  fclose(fp);

  msg("fwrite tests passed");
}
