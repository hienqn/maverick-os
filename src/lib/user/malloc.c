/* User-space malloc implementation using mmap.
 *
 * This implements a hybrid allocator:
 * - Large allocations (>= MMAP_THRESHOLD): Direct mmap/munmap per allocation
 * - Small allocations (< MMAP_THRESHOLD): Arena-based freelist within mmap'd pages
 *
 * This mirrors how glibc's malloc works on Unix systems.
 */

#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include "../syscall-nr.h"

/* Page size - must match kernel's PGSIZE */
#define PGSIZE 4096

/* Threshold for using direct mmap vs arena allocation */
#define MMAP_THRESHOLD PGSIZE

/* Size of each arena (one page) */
#define ARENA_SIZE PGSIZE

/* Minimum block size (must hold free_block pointer) */
#define MIN_BLOCK_SIZE 16

/* Number of size classes: 16, 32, 64, 128, 256, 512, 1024, 2048 */
#define NUM_SIZE_CLASSES 8

/* Magic numbers for validation */
#define ARENA_MAGIC 0xA7E40001 /* "AREA" approximation in hex */
#define MMAP_MAGIC 0x44A90001  /* "MMAP" approximation in hex */

/* Size classes in bytes */
static const size_t size_classes[NUM_SIZE_CLASSES] = {16, 32, 64, 128, 256, 512, 1024, 2048};

/* Free block header (embedded in free blocks) */
struct free_block {
  struct free_block* next;
};

/* Arena header (at start of each arena page) */
struct arena {
  uint32_t magic;               /* ARENA_MAGIC for validation */
  int size_class;               /* Index into size_classes array */
  size_t block_size;            /* Size of each block in this arena */
  size_t blocks_per_arena;      /* Number of blocks in arena */
  size_t free_count;            /* Number of free blocks */
  struct free_block* free_list; /* Head of free list */
  struct arena* next;           /* Next arena in global list */
};

/* Header for large mmap allocations (stored before user data) */
struct mmap_header {
  uint32_t magic;    /* MMAP_MAGIC for validation */
  size_t total_size; /* Total mmap size including header */
};

/* Per-size-class descriptor */
struct size_class_desc {
  struct arena* arena_list; /* List of arenas for this class */
};

/* Global state */
static struct size_class_desc descriptors[NUM_SIZE_CLASSES];
static int malloc_initialized = 0;
static lock_t malloc_lock;

/* Round up to page boundary */
static size_t round_up_page(size_t size) { return (size + PGSIZE - 1) & ~(PGSIZE - 1); }

/* Initialize malloc state */
static void malloc_init(void) {
  if (malloc_initialized)
    return;

  for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
    descriptors[i].arena_list = NULL;
  }

  lock_init(&malloc_lock);
  malloc_initialized = 1;
}

/* Find size class index for a given size */
static int find_size_class(size_t size) {
  for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
    if (size <= size_classes[i])
      return i;
  }
  return -1; /* Too large for small allocation */
}

/* Allocate a new arena for a size class */
static struct arena* alloc_arena(int class_idx) {
  /* Get a page via anonymous mmap */
  void* page = mmap_anon(NULL, ARENA_SIZE);
  if (page == (void*)MAP_FAILED)
    return NULL;

  struct arena* arena = (struct arena*)page;
  arena->magic = ARENA_MAGIC;
  arena->size_class = class_idx;
  arena->block_size = size_classes[class_idx];
  arena->blocks_per_arena = (ARENA_SIZE - sizeof(struct arena)) / arena->block_size;
  arena->free_count = arena->blocks_per_arena;
  arena->next = NULL;

  /* Initialize free list with all blocks */
  arena->free_list = NULL;
  uint8_t* block_start = (uint8_t*)arena + sizeof(struct arena);

  for (size_t i = 0; i < arena->blocks_per_arena; i++) {
    struct free_block* block = (struct free_block*)(block_start + i * arena->block_size);
    block->next = arena->free_list;
    arena->free_list = block;
  }

  return arena;
}

/* Allocate from small allocation path */
static void* malloc_small(size_t size) {
  int class_idx = find_size_class(size);
  if (class_idx < 0)
    return NULL;

  struct size_class_desc* desc = &descriptors[class_idx];

  /* Find arena with free blocks */
  struct arena* arena = desc->arena_list;
  while (arena != NULL && arena->free_count == 0) {
    arena = arena->next;
  }

  /* No free blocks - allocate new arena */
  if (arena == NULL) {
    arena = alloc_arena(class_idx);
    if (arena == NULL)
      return NULL;
    arena->next = desc->arena_list;
    desc->arena_list = arena;
  }

  /* Pop block from arena's free list */
  struct free_block* block = arena->free_list;
  arena->free_list = block->next;
  arena->free_count--;

  return (void*)block;
}

/* Allocate via direct mmap for large allocations */
static void* malloc_large(size_t size) {
  /* Add header size and round up to page boundary */
  size_t total_size = round_up_page(size + sizeof(struct mmap_header));

  void* ptr = mmap_anon(NULL, total_size);
  if (ptr == (void*)MAP_FAILED)
    return NULL;

  struct mmap_header* header = (struct mmap_header*)ptr;
  header->magic = MMAP_MAGIC;
  header->total_size = total_size;

  /* Return pointer after header */
  return (void*)(header + 1);
}

/* Get arena from block pointer (arena is at page-aligned address) */
static struct arena* block_to_arena(void* ptr) {
  return (struct arena*)((uintptr_t)ptr & ~(PGSIZE - 1));
}

/* Check if pointer is a large mmap allocation */
static int is_mmap_allocation(void* ptr) {
  struct mmap_header* header = (struct mmap_header*)ptr - 1;
  /* Check if header is page-aligned (mmap returns page-aligned) */
  if (((uintptr_t)header & (PGSIZE - 1)) != 0)
    return 0;
  return header->magic == MMAP_MAGIC;
}

/* Main malloc function */
void* malloc(size_t size) {
  if (size == 0)
    return NULL;

  if (!malloc_initialized)
    malloc_init();

  void* result;

  lock_acquire(&malloc_lock);

  if (size >= MMAP_THRESHOLD) {
    result = malloc_large(size);
  } else {
    result = malloc_small(size);
  }

  lock_release(&malloc_lock);

  return result;
}

/* Main free function */
void free(void* ptr) {
  if (ptr == NULL)
    return;

  if (!malloc_initialized)
    return; /* Can't free if never initialized */

  lock_acquire(&malloc_lock);

  if (is_mmap_allocation(ptr)) {
    /* Large allocation - munmap directly */
    struct mmap_header* header = (struct mmap_header*)ptr - 1;
    munmap((mapid_t)header);
  } else {
    /* Small allocation - return to arena free list */
    struct arena* arena = block_to_arena(ptr);
    if (arena->magic != ARENA_MAGIC) {
      /* Invalid pointer - corruption or double-free, just ignore */
      lock_release(&malloc_lock);
      return;
    }

    struct free_block* block = (struct free_block*)ptr;
    block->next = arena->free_list;
    arena->free_list = block;
    arena->free_count++;

    /* If arena is completely free, we could munmap it.
       For simplicity, we keep it allocated for future use. */
  }

  lock_release(&malloc_lock);
}

/* Allocate zeroed memory */
void* calloc(size_t nmemb, size_t size) {
  /* Check for overflow */
  size_t total = nmemb * size;
  if (nmemb != 0 && total / nmemb != size)
    return NULL;

  void* ptr = malloc(total);
  if (ptr != NULL)
    memset(ptr, 0, total);

  return ptr;
}

/* Resize allocation */
void* realloc(void* ptr, size_t size) {
  if (ptr == NULL)
    return malloc(size);

  if (size == 0) {
    free(ptr);
    return NULL;
  }

  /* Get current allocation size */
  size_t old_size;
  if (is_mmap_allocation(ptr)) {
    struct mmap_header* header = (struct mmap_header*)ptr - 1;
    old_size = header->total_size - sizeof(struct mmap_header);
  } else {
    struct arena* arena = block_to_arena(ptr);
    old_size = arena->block_size;
  }

  /* If new size fits in current allocation, return same pointer */
  if (size <= old_size) {
    return ptr;
  }

  /* Allocate new, copy, free old */
  void* new_ptr = malloc(size);
  if (new_ptr != NULL) {
    memcpy(new_ptr, ptr, old_size < size ? old_size : size);
    free(ptr);
  }

  return new_ptr;
}
