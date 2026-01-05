# Address Space (Page Cache) Design Document

## Overview

This document describes the Linux-style `address_space` abstraction for PintOS. The `address_space` is a per-file data structure that manages cached pages, enabling:

- **Shared file pages**: Multiple processes mapping the same file share physical memory
- **Future page cache unification**: Foundation for replacing the buffer cache with a unified page cache

### Relationship to Linux

| Linux Concept | PintOS Equivalent | Purpose |
|---------------|-------------------|---------|
| `struct address_space` | `struct address_space` | Per-inode page manager |
| `struct page` | `struct page` | Cached page descriptor |
| `find_get_page()` | `find_get_page()` | Lookup page, increment ref |
| `add_to_page_cache()` | `add_to_page_cache()` | Insert page into cache |
| `put_page()` | `put_page()` | Release page reference |
| `filemap_write_and_wait()` | `filemap_write_and_wait()` | Writeback dirty pages |

---

## Architecture

### Current State (with this implementation)

```
                    ┌─────────────────────────────────────────┐
                    │              USER PROCESSES             │
                    └────────────────────┬────────────────────┘
                                         │
              ┌──────────────────────────┼──────────────────────────┐
              │                          │                          │
              ▼                          ▼                          ▼
     ┌────────────────┐        ┌────────────────┐         ┌────────────────┐
     │  Process A     │        │  Process B     │         │  Process C     │
     │  mmap(file F)  │        │  mmap(file F)  │         │  read(file F)  │
     └───────┬────────┘        └───────┬────────┘         └───────┬────────┘
             │                         │                          │
             │    ┌────────────────────┘                          │
             │    │                                               │
             ▼    ▼                                               │
     ┌─────────────────────────────────────────┐                  │
     │           ADDRESS_SPACE (file F)        │                  │
     │  ┌─────────────────────────────────┐    │                  │
     │  │  pages: hash table              │    │                  │
     │  │  ┌────────┐ ┌────────┐          │    │                  │
     │  │  │ page 0 │ │ page 1 │ ...      │    │                  │
     │  │  │ ref=2  │ │ ref=2  │          │    │                  │
     │  │  └────────┘ └────────┘          │    │                  │
     │  └─────────────────────────────────┘    │                  │
     └────────────────────┬────────────────────┘                  │
                          │                                       │
                          ▼                                       ▼
                    ┌─────────────────────────────────────────────────┐
                    │                  BUFFER CACHE                   │
                    │              (512-byte sectors)                 │
                    └────────────────────┬────────────────────────────┘
                                         │
                                         ▼
                                      [ DISK ]
```

**Key points:**
- Process A and B share the same physical pages via `address_space`
- Process C uses the traditional read() path via buffer cache
- Both paths eventually go through buffer cache to disk

### Future State (unified page cache)

```
     ┌────────────────┐        ┌────────────────┐         ┌────────────────┐
     │  Process A     │        │  Process B     │         │  Process C     │
     │  mmap(file F)  │        │  mmap(file F)  │         │  read(file F)  │
     └───────┬────────┘        └───────┬────────┘         └───────┬────────┘
             │                         │                          │
             └─────────────────────────┼──────────────────────────┘
                                       │
                                       ▼
                    ┌─────────────────────────────────────────┐
                    │           ADDRESS_SPACE (file F)        │
                    │          (unified page cache)           │
                    │     Serves BOTH mmap AND read/write     │
                    └────────────────────┬────────────────────┘
                                         │
                                         ▼
                                      [ DISK ]
```

---

## Data Structures

### Global Address Space Table

```c
/* Global table of all address_space structures, keyed by inode_sector */
static struct hash address_space_table;
static struct lock address_space_table_lock;
```

### struct address_space

Per-inode structure managing all cached pages for a file:

```c
struct address_space {
  block_sector_t inode_sector;  /* Inode sector (file identity) */
  struct hash pages;            /* Hash of struct page, keyed by offset */
  struct lock lock;             /* Protects this address_space */
  struct hash_elem elem;        /* In global address_space_table */
  int page_count;               /* Number of cached pages */
};
```

### struct page

Descriptor for a cached page (simplified Linux `struct page`):

```c
struct page {
  off_t offset;                 /* Page-aligned offset within file */
  void* kpage;                  /* Kernel virtual address of frame */
  atomic_t ref_count;           /* Number of users (mappings) */
  unsigned flags;               /* Page state flags */
  struct address_space* mapping;/* Back-pointer to address_space */
  struct hash_elem hash_elem;   /* In address_space->pages */
};
```

### Page Flags

```c
#define PG_locked   (1 << 0)    /* Page is locked for I/O */
#define PG_dirty    (1 << 1)    /* Page has been modified */
#define PG_uptodate (1 << 2)    /* Page contains valid data */
#define PG_writeback (1 << 3)   /* Page is being written back */

/* Flag manipulation (like Linux) */
static inline void SetPageDirty(struct page* p) { p->flags |= PG_dirty; }
static inline void ClearPageDirty(struct page* p) { p->flags &= ~PG_dirty; }
static inline bool PageDirty(struct page* p) { return p->flags & PG_dirty; }
/* ... similar for other flags ... */
```

---

## API Reference

### Initialization

```c
/* Initialize the global address_space table.
   Called once during vm_init(). */
void address_space_init(void);
```

### Page Lookup and Acquisition

```c
/* Find a page in the cache and increment its reference count.

   @param inode_sector  File identity (from inode_get_inumber())
   @param offset        Page-aligned offset within file

   @return Pointer to page if found (ref_count incremented), NULL otherwise.

   Like Linux find_get_page(). */
struct page* find_get_page(block_sector_t inode_sector, off_t offset);
```

### Page Insertion

```c
/* Add a page to the cache.

   If a page already exists at this (inode_sector, offset), returns the
   existing page with ref_count incremented (caller should free their kpage).

   @param inode_sector  File identity
   @param offset        Page-aligned offset within file
   @param kpage         Kernel virtual address of the page data

   @return The cached page (new or existing) with ref_count incremented.

   Like Linux add_to_page_cache_lru(). */
struct page* add_to_page_cache(block_sector_t inode_sector, off_t offset,
                                void* kpage);
```

### Page Release

```c
/* Release a reference to a page.

   Decrements ref_count. When ref_count reaches 0, the page becomes
   eligible for reclaim (but is not immediately freed).

   @param page  The page to release.

   Like Linux put_page(). */
void put_page(struct page* page);
```

### Writeback

```c
/* Write back all dirty pages for a file and wait for completion.

   @param inode_sector  File identity
   @param file          File handle for writing (needed for file_write_at)

   Like Linux filemap_write_and_wait(). */
void filemap_write_and_wait(block_sector_t inode_sector, struct file* file);

/* Mark a page as dirty.
   The page will be written back on filemap_write_and_wait() or eviction. */
void set_page_dirty(struct page* page);
```

### Cache Invalidation

```c
/* Remove all cached pages for a file.
   Called when a file is deleted or truncated.

   @param inode_sector  File identity */
void truncate_inode_pages(block_sector_t inode_sector);
```

---

## Lifecycle Diagrams

### Page Lookup (Cache Hit)

```
find_get_page(inode_sector, offset)
          │
          ▼
┌─────────────────────┐
│ Lock global table   │
│ Find address_space  │
└─────────────────────┘
          │
          ▼
┌─────────────────────┐
│ Lock address_space  │
│ Hash lookup: offset │
└─────────────────────┘
          │
     Found page?
          │
    ┌─────┴─────┐
    │ Yes       │ No
    ▼           ▼
┌─────────┐  return NULL
│ ref++   │
│ Unlock  │
└─────────┘
    │
    ▼
return page
```

### Page Insertion

```
add_to_page_cache(inode_sector, offset, kpage)
          │
          ▼
┌─────────────────────────┐
│ Lock global table       │
│ Find or create          │
│ address_space           │
└─────────────────────────┘
          │
          ▼
┌─────────────────────────┐
│ Lock address_space      │
│ Check if page exists    │
└─────────────────────────┘
          │
     Page exists?
          │
    ┌─────┴─────┐
    │ Yes       │ No
    ▼           ▼
┌─────────┐ ┌─────────────────┐
│ ref++   │ │ Create new page │
│ Unlock  │ │ page->kpage =   │
│ Caller  │ │   kpage         │
│ frees   │ │ ref_count = 1   │
│ kpage   │ │ Insert to hash  │
└─────────┘ └─────────────────┘
    │               │
    └───────┬───────┘
            ▼
      return page
```

### Page Writeback on munmap

```
mmap_destroy(addr, length)
          │
          ▼
┌─────────────────────────┐
│ For each page in region │
└─────────────────────────┘
          │
          ▼
┌─────────────────────────┐
│ If page is private copy │
│ (COW happened):         │
│   Write to file if dirty│
│   Free the private frame│
└─────────────────────────┘
          │
          ▼
┌─────────────────────────┐
│ If page is shared:      │
│   put_page() to decr ref│
│   (ref=0 pages written  │
│    back on eviction)    │
└─────────────────────────┘
          │
          ▼
┌─────────────────────────┐
│ Remove SPT entries      │
│ Close file reference    │
│ Free mmap_region        │
└─────────────────────────┘
```

---

## Integration with mmap

### mmap Creation Flow

```c
void* mmap_create(int fd, void* addr, size_t length, off_t offset) {
  // 1. Validate parameters
  // 2. Get file from fd, call file_reopen()
  // 3. Get inode_sector from file

  block_sector_t inode_sector = inode_get_inumber(file_get_inode(file));

  // 4. Create mmap_region
  // 5. For each page in the mapping:
  for (size_t i = 0; i < page_count; i++) {
    void* upage = addr + i * PGSIZE;
    off_t page_offset = offset + i * PGSIZE;

    // Create SPT entry with PAGE_MMAP_SHARED status
    spt_create_mmap_page(spt, upage, inode_sector, page_offset, writable);
  }

  // 6. Add region to process's mmap_list
  return addr;
}
```

### Page Fault on mmap'd Page

```c
bool handle_mmap_fault(struct spt_entry* entry, bool write) {
  // 1. Try to find page in cache
  struct page* cached = find_get_page(entry->inode_sector,
                                       entry->file_page_offset);

  if (cached != NULL) {
    // Cache hit: share the existing page
    if (write && entry->writable) {
      // COW: make private copy
      void* private_kpage = frame_alloc(...);
      memcpy(private_kpage, cached->kpage, PGSIZE);
      put_page(cached);  // Release shared reference

      entry->status = PAGE_FRAME;
      entry->kpage = private_kpage;
      pagedir_set_page(pd, entry->upage, private_kpage, true);
    } else {
      // Read-only access: use shared page
      entry->kpage = cached->kpage;
      pagedir_set_page(pd, entry->upage, cached->kpage, false);
    }
    return true;
  }

  // Cache miss: load from file
  void* kpage = frame_alloc(...);

  // Read file data into frame
  file_read_at(file, kpage, PGSIZE, entry->file_page_offset);

  // Add to cache for future sharing
  struct page* page = add_to_page_cache(entry->inode_sector,
                                         entry->file_page_offset,
                                         kpage);

  // Install mapping (read-only unless this is a write fault)
  bool page_writable = write && entry->writable;
  if (page_writable) {
    // Private copy for write
    // ... COW logic ...
  } else {
    entry->kpage = page->kpage;
    pagedir_set_page(pd, entry->upage, page->kpage, false);
  }

  return true;
}
```

---

## Synchronization

### Lock Hierarchy

1. `address_space_table_lock` (global)
2. `address_space->lock` (per-file)
3. `frame_lock` (global, from vm/frame.c)

Always acquire in this order to prevent deadlock.

### Reference Counting

- `ref_count` uses atomic operations or is protected by `address_space->lock`
- A page with `ref_count > 0` cannot be evicted
- When `ref_count` reaches 0, page is eligible for reclaim but not immediately freed

---

## Future: Unified Page Cache

To unify mmap and read/write through a single page cache:

### Step 1: Modify inode_read_at()

```c
off_t inode_read_at(struct inode* inode, void* buffer,
                    off_t size, off_t offset) {
  block_sector_t inode_sector = inode_get_inumber(inode);

  while (bytes_read < size) {
    off_t page_offset = offset & ~PGMASK;  // Round down to page

    // Try page cache first
    struct page* page = find_get_page(inode_sector, page_offset);
    if (page != NULL) {
      // Cache hit: copy from cached page
      memcpy(buffer + bytes_read,
             page->kpage + (offset - page_offset),
             chunk_size);
      put_page(page);
    } else {
      // Cache miss: fall back to buffer cache
      // (or load into page cache and then copy)
    }

    bytes_read += chunk_size;
    offset += chunk_size;
  }
  return bytes_read;
}
```

### Step 2: Modify inode_write_at()

Similar pattern: check page cache, update if present, mark dirty.

### Step 3: Remove buffer cache for data

Keep buffer cache only for metadata (inode blocks, directory entries, free map).

---

## Files

| File | Purpose |
|------|---------|
| `src/vm/address-space.h` | Data structures and declarations |
| `src/vm/address-space.c` | Implementation |

## Related Documents

- `docs/MMAP_DESIGN.md` - mmap syscall design (uses address_space)
