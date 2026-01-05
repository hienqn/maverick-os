/* Test fprintf() for formatted output. */

#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;
  char buf[128];
  int n;

  CHECK(create("testfile.txt", 0), "create testfile.txt");

  /* Test basic formats */
  fp = fopen("testfile.txt", "w");
  CHECK(fp != NULL, "fopen succeeds");

  n = fprintf(fp, "Number: %d\n", 42);
  CHECK(n == 11, "fprintf returns char count");

  fprintf(fp, "String: %s\n", "hello");
  fprintf(fp, "Hex: 0x%x\n", 255);
  fprintf(fp, "Char: %c\n", 'X');

  fclose(fp);

  /* Verify output */
  fp = fopen("testfile.txt", "r");
  fgets(buf, sizeof buf, fp);
  CHECK(strcmp(buf, "Number: 42\n") == 0, "integer format correct");

  fgets(buf, sizeof buf, fp);
  CHECK(strcmp(buf, "String: hello\n") == 0, "string format correct");

  fgets(buf, sizeof buf, fp);
  CHECK(strcmp(buf, "Hex: 0xff\n") == 0, "hex format correct");

  fgets(buf, sizeof buf, fp);
  CHECK(strcmp(buf, "Char: X\n") == 0, "char format correct");

  fclose(fp);

  /* Test width and padding */
  fp = fopen("testfile.txt", "w");
  fprintf(fp, "[%5d]", 42);
  fprintf(fp, "[%-5d]", 42);
  fprintf(fp, "[%05d]", 42);
  fclose(fp);

  fp = fopen("testfile.txt", "r");
  fread(buf, 1, 21, fp);
  buf[21] = '\0';
  CHECK(strcmp(buf, "[   42][42   ][00042]") == 0, "width/padding correct");
  fclose(fp);

  msg("fprintf tests passed");
}
