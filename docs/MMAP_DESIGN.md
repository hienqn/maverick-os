# Memory-Mapped Files (mmap) Design Document

## Overview

This document describes the design for memory-mapped file support in PintOS. Memory-mapped files allow processes to access file contents as if they were in memory, enabling zero-copy file I/O and simplified file manipulation.

### Interface (Partial POSIX)

```c
/* Map LENGTH bytes from file FD starting at OFFSET into memory at ADDR.
   Returns mapped address on success, MAP_FAILED ((void*)-1) on error. */
void* mmap(void* addr, size_t length, int fd, off_t offset);

/* Unmap LENGTH bytes starting at ADDR.
   Returns 0 on success, -1 on error. */
int munmap(void* addr, size_t length);
```

This interface is a partial POSIX implementation:
- Includes `length` and `offset` for partial file mappings
- Omits `prot` and `flags` (PintOS lacks fine-grained memory permissions)

---

## Architecture

### System Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           USER PROCESS                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   User Code                                                             │
│   ┌──────────────────────────────────────────────────────────────────┐  │
│   │  char* data = mmap(0x10000000, 4096, fd, 0);                     │  │
│   │  printf("%s", data);  /* Triggers page fault on first access */  │  │
│   │  munmap(data, 4096);  /* Writes back if dirty */                 │  │
│   └──────────────────────────────────────────────────────────────────┘  │
│                              │                                          │
│                              ▼                                          │
│   ┌──────────────────────────────────────────────────────────────────┐  │
│   │                    VIRTUAL ADDRESS SPACE                         │  │
│   │  ┌──────────┬──────────┬──────────┬──────────┬──────────┐        │  │
│   │  │   Code   │   Data   │  Heap    │  mmap    │  Stack   │        │  │
│   │  │ (0x0804) │          │    ↓     │  region  │    ↑     │        │  │
│   │  └──────────┴──────────┴──────────┴──────────┴──────────┘        │  │
│   │                                       │                          │  │
│   └───────────────────────────────────────┼──────────────────────────┘  │
│                                           │                             │
└───────────────────────────────────────────┼─────────────────────────────┘
                                            │
════════════════════════════════════════════╪═════════════════════════════
                      KERNEL                │
                                            ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         SYSCALL HANDLER                                 │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  SYS_MMAP:  mmap_create(addr, length, fd, offset)               │    │
│  │  SYS_MUNMAP: mmap_destroy(addr, length)                         │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                              │                                          │
│                              ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                     MMAP SUBSYSTEM (vm/mmap.c)                  │    │
│  │                                                                 │    │
│  │   mmap_region list ──► [region1] ──► [region2] ──► ...          │    │
│  │                                                                 │    │
│  │   Functions:                                                    │    │
│  │   • mmap_create()      - Validate, create SPT entries           │    │
│  │   • mmap_destroy()     - Writeback dirty pages, cleanup         │    │
│  │   • mmap_destroy_all() - Called on process exit                 │    │
│  │   • mmap_find_region() - Lookup by address                      │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                              │                                          │
│                              ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │              SUPPLEMENTAL PAGE TABLE (vm/page.c)                │    │
│  │                                                                 │    │
│  │   Hash Table: upage → spt_entry                                 │    │
│  │   ┌──────────────────────────────────────────────┐              │    │
│  │   │  spt_entry {                                 │              │    │
│  │   │    upage: 0x10000000                         │              │    │
│  │   │    status: PAGE_FILE                         │              │    │
│  │   │    file: <file*>                             │              │    │
│  │   │    file_offset: 0                            │              │    │
│  │   │    read_bytes: 4096                          │              │    │
│  │   │    zero_bytes: 0                             │              │    │
│  │   │    writable: true                            │              │    │
│  │   │  }                                           │              │    │
│  │   └──────────────────────────────────────────────┘              │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                              │                                          │
│                              ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                   PAGE FAULT HANDLER                            │    │
│  │                                                                 │    │
│  │   1. Lookup SPT entry for faulting address                      │    │
│  │   2. If PAGE_FILE: allocate frame, read from file               │    │
│  │   3. Install mapping in hardware page table                     │    │
│  │   4. Update SPT entry status to PAGE_FRAME                      │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                              │                                          │
│                              ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                     FRAME TABLE (vm/frame.c)                    │    │
│  │                                                                 │    │
│  │   Physical frames with clock eviction                           │    │
│  │   On eviction of mmap page: write back to file if dirty         │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Data Structures

### mmap_region (vm/mmap.h)

Tracks a single memory-mapped region:

```c
struct mmap_region {
  void* start_addr;       /* Page-aligned starting virtual address */
  size_t length;          /* Requested length in bytes */
  size_t page_count;      /* Number of pages: DIV_ROUND_UP(length, PGSIZE) */
  struct file* file;      /* Private file reference (via file_reopen) */
  off_t offset;           /* Starting offset in file */
  struct list_elem elem;  /* Element in process's mmap_list */
};
```

### PCB Extension (userprog/process.h)

Add to `struct process`:

```c
struct list mmap_list;    /* List of mmap_region structs */
```

### Integration with SPT

Each page in an mmap region gets an SPT entry:

```c
spt_entry {
  upage: start_addr + (i * PGSIZE)
  status: PAGE_FILE
  file: region->file
  file_offset: region->offset + (i * PGSIZE)
  read_bytes: min(PGSIZE, remaining_bytes)
  zero_bytes: PGSIZE - read_bytes
  writable: true
}
```

---

## Lifecycle Diagrams

### mmap Lifecycle

```
mmap(addr, length, fd, offset)
          │
          ▼
┌─────────────────────┐
│   VALIDATION        │
│   • addr aligned?   │──── No ────► return MAP_FAILED
│   • offset aligned? │
│   • fd valid file?  │
│   • length > 0?     │
│   • no overlaps?    │
└─────────────────────┘
          │ Yes
          ▼
┌─────────────────────┐
│   CREATE REGION     │
│   • file_reopen(fd) │──── Fail ──► return MAP_FAILED
│   • alloc region    │
│   • set fields      │
└─────────────────────┘
          │
          ▼
┌─────────────────────┐
│   CREATE SPT ENTRIES│
│   for each page:    │
│   spt_create_file() │
└─────────────────────┘
          │
          ▼
┌─────────────────────┐
│   ADD TO LIST       │
│   list_push_back()  │
└─────────────────────┘
          │
          ▼
     return addr
```

### Page Fault on mmap Page

```
Access to mmap address (e.g., 0x10001000)
          │
          ▼
┌─────────────────────┐
│   PAGE FAULT        │
│   exception.c       │
└─────────────────────┘
          │
          ▼
┌─────────────────────┐
│   SPT LOOKUP        │
│   spt_find(addr)    │──── Not found ──► Kill process
└─────────────────────┘
          │ Found spt_entry
          ▼
┌─────────────────────┐
│   CHECK STATUS      │
│   status == ?       │
└─────────────────────┘
          │ PAGE_FILE
          ▼
┌─────────────────────┐
│   ALLOCATE FRAME    │
│   frame_alloc()     │──── May evict ──► Write back if dirty
└─────────────────────┘
          │
          ▼
┌─────────────────────┐
│   LOAD FROM FILE    │
│   file_read_at()    │
│   Zero remaining    │
└─────────────────────┘
          │
          ▼
┌─────────────────────┐
│   INSTALL MAPPING   │
│   pagedir_set_page()│
└─────────────────────┘
          │
          ▼
┌─────────────────────┐
│   UPDATE SPT        │
│   status=PAGE_FRAME │
│   kpage=frame addr  │
└─────────────────────┘
          │
          ▼
     Return to user
```

### munmap Lifecycle

```
munmap(addr, length)
          │
          ▼
┌─────────────────────┐
│   FIND REGION       │
│   mmap_find_region()│──── Not found ──► return -1
└─────────────────────┘
          │ Found
          ▼
┌─────────────────────┐
│   FOR EACH PAGE     │◄─────────────────┐
│   in region         │                  │
└─────────────────────┘                  │
          │                              │
          ▼                              │
┌─────────────────────┐                  │
│   GET SPT ENTRY     │                  │
│   spt_find(upage)   │                  │
└─────────────────────┘                  │
          │                              │
          ▼                              │
┌─────────────────────┐                  │
│   IF PAGE_FRAME:    │                  │
│   • Check dirty bit │                  │
│   • If dirty:       │                  │
│     file_write_at() │                  │
│   • frame_free()    │                  │
└─────────────────────┘                  │
          │                              │
          ▼                              │
┌─────────────────────┐                  │
│   REMOVE SPT ENTRY  │                  │
│   spt_remove()      │                  │
└─────────────────────┘                  │
          │                              │
          ▼                              │
┌─────────────────────┐                  │
│   More pages?       │──── Yes ─────────┘
└─────────────────────┘
          │ No
          ▼
┌─────────────────────┐
│   CLEANUP REGION    │
│   • file_close()    │
│   • list_remove()   │
│   • free(region)    │
└─────────────────────┘
          │
          ▼
     return 0
```

---

## API Reference

### Kernel Functions (vm/mmap.h)

| Function | Description |
|----------|-------------|
| `void* mmap_create(void* addr, size_t length, int fd, off_t offset)` | Create mapping. Returns address or MAP_FAILED. |
| `int mmap_destroy(void* addr, size_t length)` | Remove mapping, writeback dirty pages. Returns 0/-1. |
| `void mmap_destroy_all(void)` | Remove all mappings for current process. Called on exit. |
| `struct mmap_region* mmap_find_region(void* addr)` | Find region containing address. |
| `bool mmap_range_available(void* addr, size_t length)` | Check if address range is free. |
| `bool mmap_writeback_page(struct mmap_region* r, size_t idx)` | Write single page back to file. |

### Validation Rules for mmap_create

| Check | Error |
|-------|-------|
| `addr == NULL` | MAP_FAILED |
| `pg_ofs(addr) != 0` (not page-aligned) | MAP_FAILED |
| `pg_ofs(offset) != 0` (not page-aligned) | MAP_FAILED |
| `length == 0` | MAP_FAILED |
| `fd == 0 || fd == 1` (stdin/stdout) | MAP_FAILED |
| `fd` not a valid open file | MAP_FAILED |
| File length is 0 | MAP_FAILED |
| Range overlaps existing mapping (SPT check) | MAP_FAILED |
| Range overlaps stack region | MAP_FAILED |

---

## Integration Points

### 1. Process Initialization (process.c: pcb_init)

```c
list_init(&pcb->mmap_list);
```

### 2. Process Exit (process.c: process_exit)

```c
/* BEFORE spt_destroy() - needs SPT to find loaded pages */
mmap_destroy_all();
```

### 3. Syscall Handler (syscall.c)

```c
case SYS_MMAP: {
  void* addr = (void*)args[1];
  size_t length = (size_t)args[2];
  int fd = (int)args[3];
  off_t offset = (off_t)args[4];
  f->eax = (uint32_t)mmap_create(addr, length, fd, offset);
  break;
}

case SYS_MUNMAP: {
  void* addr = (void*)args[1];
  size_t length = (size_t)args[2];
  f->eax = mmap_destroy(addr, length);
  break;
}
```

### 4. Fork Behavior

Mappings are **NOT** inherited by child processes. The child's `mmap_list` starts empty (initialized by `pcb_init`). This is simpler than implementing shared mappings with reference counting.

---

## Design Rationale

### Why address-based munmap instead of mapid?

POSIX uses `munmap(addr, length)` not a mapping ID. This:
- Matches real-world systems
- Allows partial unmapping (advanced feature)
- Eliminates need for mapid allocation/tracking

### Why file_reopen()?

When user calls `mmap(fd, ...)`, we call `file_reopen()` to get an independent file reference. This:
- Allows user to `close(fd)` without breaking the mapping
- Each mapping has its own file position (not shared with fd)
- Properly cleans up when mapping is removed

### Why lazy loading?

Pages are not loaded immediately on `mmap()`. Instead:
- SPT entries are created with `status = PAGE_FILE`
- Page fault triggers actual file read
- Benefits:
  - Fast mmap() call
  - Only used pages consume memory
  - Large files don't require huge upfront allocation

### Why writeback on munmap/exit?

Modified (dirty) pages must be written back to the file:
- `pagedir_is_dirty()` checks if page was written
- Only dirty pages are written (optimization)
- Ensures file reflects all modifications

---

## Testing

Tests are in `src/tests/vm/`:

| Test | Description |
|------|-------------|
| `mmap-read` | Map file, read contents |
| `mmap-write` | Map file, modify, verify with read() |
| `mmap-unmap` | Verify unmapped memory is inaccessible |
| `mmap-exit` | Dirty pages written on process exit |
| `mmap-twice` | Multiple concurrent mappings |
| `mmap-overlap` | Overlapping mappings rejected |
| `mmap-bad-fd` | Invalid fd rejected |
| `mmap-null` | NULL address rejected |
| `mmap-misalign` | Misaligned address rejected |

---

## Files Modified/Created

| File | Action |
|------|--------|
| `src/vm/mmap.h` | **Create** - Data structures and declarations |
| `src/vm/mmap.c` | **Create** - Implementation |
| `src/lib/user/syscall.h` | **Modify** - Update function signatures |
| `src/lib/user/syscall.c` | **Modify** - Update wrappers |
| `src/userprog/process.h` | **Modify** - Add mmap_list to PCB |
| `src/userprog/process.c` | **Modify** - Init in pcb_init, cleanup in exit |
| `src/userprog/syscall.c` | **Modify** - Add syscall handlers |
| `src/Makefile.build` | **Modify** - Add vm/mmap.c |

---

## Future: Address Space Integration

This initial mmap implementation uses **per-process frames** (no sharing between processes). A future enhancement will add Linux-style `address_space` for shared file pages.

See `docs/ADDRESS_SPACE_DESIGN.md` for the full design.

### Current vs Future Architecture

```
CURRENT (Phase 1 - implement this first):
┌──────────────┐     ┌──────────────┐
│  Process A   │     │  Process B   │
│  mmap(fileF) │     │  mmap(fileF) │
└──────┬───────┘     └──────┬───────┘
       │                    │
       ▼                    ▼
┌──────────────┐     ┌──────────────┐
│  Frame A     │     │  Frame B     │   ← Separate frames (no sharing)
│  (copy of F) │     │  (copy of F) │
└──────────────┘     └──────────────┘

FUTURE (Phase 2 - address_space):
┌──────────────┐     ┌──────────────┐
│  Process A   │     │  Process B   │
│  mmap(fileF) │     │  mmap(fileF) │
└──────┬───────┘     └──────┬───────┘
       │                    │
       └────────┬───────────┘
                ▼
       ┌────────────────┐
       │ address_space  │
       │   (file F)     │
       │ ┌────────────┐ │
       │ │ Shared     │ │   ← Single frame, shared read-only
       │ │ Frame      │ │      COW on write
       │ └────────────┘ │
       └────────────────┘
```

### Preparing for Future Integration

When implementing mmap now, structure your code to make future `address_space` integration easy:

1. **Store `inode_sector` in `mmap_region`** (even if unused now):
   ```c
   struct mmap_region {
     ...
     block_sector_t inode_sector;  /* For future address_space lookup */
   };
   ```

2. **Use `PAGE_FILE` status for now** - later we'll add `PAGE_MMAP_SHARED`

3. **Keep page loading logic modular** - the `spt_load_page()` path for `PAGE_FILE` will later check `address_space` first

4. **Track writable flag per-region** - needed for future COW:
   ```c
   struct mmap_region {
     ...
     bool writable;  /* For future COW support */
   };
   ```

### Integration Points (for later)

When adding `address_space`, you'll modify:

| Location | Change |
|----------|--------|
| `spt_load_page()` for PAGE_FILE | Check `find_get_page()` before loading from disk |
| `mmap_create()` | Use `PAGE_MMAP_SHARED` instead of `PAGE_FILE` |
| `vm_handle_fault()` | Handle COW for shared pages |
| `munmap` | Call `put_page()` instead of freeing frame directly |
