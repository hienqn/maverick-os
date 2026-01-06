/* Test simple malloc/free operations using mmap-based allocator. */

#include <syscall.h>
#include <stdlib.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  msg("begin");

  /* Test small allocation (arena-based) */
  msg("allocating 100 bytes...");
  char* small = malloc(100);
  if (small == NULL)
    fail("malloc(100) returned NULL");
  msg("small allocation at %p", small);

  /* Write to it to verify the memory is usable */
  memset(small, 'A', 100);
  if (small[0] != 'A' || small[99] != 'A')
    fail("memory write failed");
  msg("small allocation write succeeded");

  /* Test large allocation (direct mmap) */
  msg("allocating 8192 bytes...");
  char* large = malloc(8192);
  if (large == NULL)
    fail("malloc(8192) returned NULL");
  msg("large allocation at %p", large);

  /* Write to verify */
  memset(large, 'B', 8192);
  if (large[0] != 'B' || large[8191] != 'B')
    fail("large memory write failed");
  msg("large allocation write succeeded");

  /* Free allocations */
  msg("freeing small allocation...");
  free(small);
  msg("freeing large allocation...");
  free(large);

  /* Test calloc */
  msg("testing calloc(10, 20)...");
  char* zeroed = calloc(10, 20);
  if (zeroed == NULL)
    fail("calloc returned NULL");
  for (int i = 0; i < 200; i++) {
    if (zeroed[i] != 0) {
      fail("calloc memory not zeroed at byte %d", i);
    }
  }
  msg("calloc returned zeroed memory");
  free(zeroed);

  /* Test realloc */
  msg("testing realloc...");
  char* r = malloc(50);
  if (r == NULL)
    fail("initial malloc for realloc test failed");
  memset(r, 'C', 50);

  r = realloc(r, 100);
  if (r == NULL)
    fail("realloc returned NULL");
  /* Check original data preserved */
  for (int i = 0; i < 50; i++) {
    if (r[i] != 'C') {
      fail("realloc did not preserve data at byte %d", i);
    }
  }
  msg("realloc preserved original data");
  free(r);

  /* Multiple small allocations to test arena */
  msg("testing multiple small allocations...");
  void* ptrs[20];
  for (int i = 0; i < 20; i++) {
    ptrs[i] = malloc(64);
    if (ptrs[i] == NULL)
      fail("allocation %d failed", i);
    memset(ptrs[i], i, 64);
  }
  msg("allocated 20 blocks of 64 bytes");

  for (int i = 0; i < 20; i++) {
    free(ptrs[i]);
  }
  msg("freed all 20 blocks");

  msg("end");
}
