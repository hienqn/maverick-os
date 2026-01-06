/* Test simple malloc/free operations using mmap-based allocator. */

#include <syscall.h>
#include <stdlib.h>
#include <stdint.h>
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

  /* === EDGE CASES === */

  /* Test malloc(0) - should return NULL or unique pointer */
  msg("testing malloc(0)...");
  void* zero_alloc = malloc(0);
  /* Either NULL or valid pointer is acceptable per C standard */
  msg("malloc(0) returned %p", zero_alloc);
  free(zero_alloc); /* Should be safe even if NULL */
  msg("malloc(0) handled correctly");

  /* Test free(NULL) - should be a no-op */
  msg("testing free(NULL)...");
  free(NULL);
  msg("free(NULL) succeeded");

  /* Test realloc(NULL, n) - should behave like malloc(n) */
  msg("testing realloc(NULL, 50)...");
  char* realloc_null = realloc(NULL, 50);
  if (realloc_null == NULL)
    fail("realloc(NULL, 50) returned NULL");
  memset(realloc_null, 'D', 50);
  msg("realloc(NULL, n) works like malloc");
  free(realloc_null);

  /* Test realloc to smaller size */
  msg("testing realloc shrink...");
  char* shrink = malloc(200);
  if (shrink == NULL)
    fail("malloc(200) for shrink test failed");
  memset(shrink, 'E', 200);
  shrink = realloc(shrink, 50);
  if (shrink == NULL)
    fail("realloc shrink returned NULL");
  /* Verify original data in remaining portion */
  for (int i = 0; i < 50; i++) {
    if (shrink[i] != 'E')
      fail("realloc shrink corrupted data at byte %d", i);
  }
  msg("realloc shrink preserved data");
  free(shrink);

  /* Test realloc(ptr, 0) - should behave like free */
  msg("testing realloc(ptr, 0)...");
  char* realloc_zero = malloc(100);
  if (realloc_zero == NULL)
    fail("malloc for realloc(ptr, 0) test failed");
  void* result = realloc(realloc_zero, 0);
  /* Result may be NULL or valid pointer - either is acceptable */
  msg("realloc(ptr, 0) returned %p", result);
  /* If non-NULL was returned, free it */
  if (result != NULL)
    free(result);
  msg("realloc(ptr, 0) handled correctly");

  /* Test alignment - pointers should be aligned */
  msg("testing alignment...");
  for (int i = 0; i < 10; i++) {
    void* p = malloc(1 + i); /* Various small sizes */
    if (p == NULL)
      fail("alignment test alloc %d failed", i);
    /* Check 8-byte alignment (common minimum) */
    if (((uintptr_t)p & 7) != 0)
      fail("allocation not 8-byte aligned: %p", p);
    free(p);
  }
  msg("all allocations properly aligned");

  /* Test page-boundary allocation */
  msg("testing page-size allocation (4096 bytes)...");
  char* page_alloc = malloc(4096);
  if (page_alloc == NULL)
    fail("malloc(4096) returned NULL");
  memset(page_alloc, 'F', 4096);
  if (page_alloc[0] != 'F' || page_alloc[4095] != 'F')
    fail("page-size allocation write failed");
  msg("page-size allocation succeeded");
  free(page_alloc);

  /* Test memory reuse after free */
  msg("testing memory reuse...");
  void* first = malloc(64);
  if (first == NULL)
    fail("first alloc for reuse test failed");
  free(first);
  void* second = malloc(64);
  if (second == NULL)
    fail("second alloc for reuse test failed");
  /* They might be the same address if free list works */
  msg("reuse test: first=%p, second=%p", first, second);
  free(second);
  msg("memory reuse test completed");

  msg("end");
}
