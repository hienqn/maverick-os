/* Test fopen() and basic file stream creation. */

#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  FILE* fp;

  /* Create a test file first */
  CHECK(create("testfile.txt", 0), "create testfile.txt");

  /* Test opening for write */
  fp = fopen("testfile.txt", "w");
  CHECK(fp != NULL, "fopen(\"testfile.txt\", \"w\") succeeds");
  CHECK(fclose(fp) == 0, "fclose succeeds");

  /* Test opening for read */
  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "fopen(\"testfile.txt\", \"r\") succeeds");
  CHECK(fclose(fp) == 0, "fclose succeeds");

  /* Test opening non-existent file for read */
  fp = fopen("nonexistent.txt", "r");
  CHECK(fp == NULL, "fopen nonexistent file returns NULL");

  /* Test fileno */
  fp = fopen("testfile.txt", "r");
  CHECK(fp != NULL, "fopen for fileno test");
  CHECK(fileno(fp) >= 2, "fileno returns valid fd");
  fclose(fp);

  msg("fopen tests passed");
}
