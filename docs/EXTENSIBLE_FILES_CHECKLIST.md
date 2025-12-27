# Extensible Files Implementation Checklist

This document provides a comprehensive checklist for implementing **Extensible Files** in Pintos. The goal is to modify the file system to support file growth beyond the initial allocation, using an indexed inode structure with direct, indirect, and doubly-indirect pointers.

---

## Overview

**Current State:** Pintos allocates each file as a single contiguous set of blocks (extent-based), making it vulnerable to external fragmentation and unable to extend files.

**Target State:** An indexed inode structure that:
- Eliminates external fragmentation
- Supports file extension up to 8 MiB
- Provides fast random access
- Gracefully handles out-of-memory and out-of-disk conditions

---

## 1. On-Disk Inode Structure Redesign

### Tasks

- [ ] **Replace single-extent design**
  - Current `struct inode_disk` uses a single `block_sector_t start` field
  - Replace with an indexed structure containing block pointers

- [ ] **Design indexed inode layout** with:
  - Direct block pointers (~12 is typical for Unix FFS)
  - Indirect block pointer (1)
  - Doubly-indirect block pointer (1)

- [ ] **Verify size constraint**
  - `struct inode_disk` must remain exactly `BLOCK_SECTOR_SIZE` (512 bytes)
  - Use the existing assertion: `ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);`

- [ ] **Calculate pointer counts** for 8 MiB support:
  - Each pointer is 4 bytes → max ~128 pointers per indirect block
  - 8 MiB = 2^23 bytes / 512 = 16,384 sectors needed
  - Example layout:
    - 12 direct blocks = 12 sectors (6 KB)
    - 1 indirect block = 128 sectors (64 KB)
    - 1 doubly-indirect block = 128 × 128 = 16,384 sectors (8 MB)

- [ ] **Handle unallocated blocks**
  - Use a sentinel value (e.g., `0` or `(block_sector_t) -1`) for blocks not yet allocated
  - Document which sentinel you choose

### Example Structure

```c
#define DIRECT_BLOCKS 12
#define INDIRECT_BLOCKS 1
#define DOUBLY_INDIRECT_BLOCKS 1

struct inode_disk {
  block_sector_t direct[DIRECT_BLOCKS];       /* Direct block pointers */
  block_sector_t indirect;                     /* Indirect block pointer */
  block_sector_t doubly_indirect;              /* Doubly-indirect block pointer */
  off_t length;                                /* File size in bytes */
  unsigned magic;                              /* Magic number */
  uint32_t unused[...];                        /* Pad to BLOCK_SECTOR_SIZE */
};
```

---

## 2. Sector Allocation Changes

### Tasks

- [ ] **Modify `free_map_allocate`**
  - Current implementation allocates *consecutive* sectors
  - You need non-contiguous allocation for indexed inodes

- [ ] **Create single-sector allocation helper**
  ```c
  bool free_map_allocate_one(block_sector_t *sectorp);
  ```

- [ ] **Update `byte_to_sector()`**
  - Current implementation assumes contiguous storage
  - Must traverse direct → indirect → doubly-indirect blocks

- [ ] **Update `inode_close()`**
  - Must free all allocated blocks including:
    - Direct data blocks
    - Indirect block (metadata) + its data blocks
    - Doubly-indirect block (metadata) + all indirect blocks + all data blocks

### Key Files

- `filesys/free-map.c` - Sector allocation
- `filesys/inode.c` - Block lookup and deallocation

---

## 3. File Extension Logic

### Tasks

- [ ] **Modify `inode_write_at()`** to:
  - Detect writes beyond current EOF
  - Allocate new data blocks on-demand
  - Allocate indirect/doubly-indirect metadata blocks as needed
  - Update the file length in the inode

- [ ] **Handle gaps (sparse files)**
  - Writing beyond EOF should either:
    - **Option A:** Zero-fill gap blocks (simpler, wastes disk space)
    - **Option B:** Leave blocks unallocated until explicitly written (sparse - more complex)
  - Document which approach you choose

- [ ] **Zero-fill partial sectors**
  - When extending, new bytes in the last sector must be zeroed

- [ ] **Update inode on disk**
  - After successful extension, write the updated `inode_disk` to disk

### Pseudocode for Extension

```c
off_t inode_write_at(...) {
  // Check if write extends past EOF
  off_t end_pos = offset + size;
  if (end_pos > inode_length(inode)) {
    // Extend the file
    if (!inode_extend(inode, end_pos)) {
      // Handle failure - partial write or error
    }
  }
  // Proceed with normal write...
}
```

---

## 4. Failure Handling & Rollback (CRITICAL!)

### Tasks

- [ ] **Track allocated blocks during extension**
  - Keep a list of newly allocated sectors during extension

- [ ] **Rollback on allocation failure:**
  - Free all newly allocated blocks
  - Restore original file length
  - Return an error without corrupting the file

- [ ] **Avoid metadata inconsistency**
  - Don't update `inode_disk.length` until all blocks are successfully allocated

- [ ] **Handle malloc failures**
  - Check return values when allocating indirect block buffers

- [ ] **Atomic length update**
  - Only commit the new length after successful writes

### Rollback Strategy

```c
bool inode_extend(struct inode *inode, off_t new_length) {
  struct list allocated_sectors;
  list_init(&allocated_sectors);
  
  // Try to allocate all needed sectors
  while (need_more_sectors) {
    block_sector_t sector;
    if (!free_map_allocate_one(&sector)) {
      // Rollback: free all sectors in allocated_sectors
      rollback_allocations(&allocated_sectors);
      return false;
    }
    // Track this allocation
    list_push_back(&allocated_sectors, ...);
  }
  
  // Success: commit the new length
  inode->data.length = new_length;
  // Write inode to disk
  return true;
}
```

---

## 5. Synchronization

### Tasks

- [ ] **Lock the inode during extension**
  - Prevent concurrent writers from corrupting block pointers

- [ ] **Consider reader-writer locks**
  - Allow concurrent reads while serializing writes

- [ ] **Coordinate with buffer cache**
  - Ensure cache entries are flushed/invalidated appropriately

- [ ] **Protect the open inodes list**
  - Currently not synchronized in `inode.c`

### Recommended Approach

Add a lock to `struct inode`:

```c
struct inode {
  struct list_elem elem;
  block_sector_t sector;
  int open_cnt;
  bool removed;
  int deny_write_cnt;
  struct lock inode_lock;     /* Lock for inode operations */
  struct inode_disk data;
};
```

---

## 6. Inode Read/Write Operations

### Tasks

- [ ] **Update `inode_read_at()`**
  - Multi-level indirection lookup
  - Reading from partially allocated files
  - Returning 0 bytes for reads past EOF

- [ ] **Update `inode_write_at()`**
  - Multi-level block lookup
  - Extending file when writing past EOF
  - Returning partial writes on disk exhaustion

- [ ] **Update `inode_length()`**
  - May need to reload from disk if cached copy is stale

### Block Lookup Helper

```c
/* Returns the sector for the given byte offset, or -1 if not allocated */
static block_sector_t byte_to_sector(const struct inode *inode, off_t pos) {
  ASSERT(inode != NULL);
  if (pos >= inode->data.length)
    return -1;
  
  size_t sector_idx = pos / BLOCK_SECTOR_SIZE;
  
  if (sector_idx < DIRECT_BLOCKS) {
    // Direct block
    return inode->data.direct[sector_idx];
  }
  
  sector_idx -= DIRECT_BLOCKS;
  if (sector_idx < PTRS_PER_BLOCK) {
    // Indirect block
    return lookup_indirect(inode->data.indirect, sector_idx);
  }
  
  sector_idx -= PTRS_PER_BLOCK;
  // Doubly-indirect block
  return lookup_doubly_indirect(inode->data.doubly_indirect, sector_idx);
}
```

---

## 7. `inumber()` System Call

### Tasks

- [ ] **Add `SYS_INUMBER` handler** to `syscall.c`
  - Already defined in `lib/syscall-nr.h`

- [ ] **Validate the file descriptor**
  - Check bounds and NULL

- [ ] **Get the inode** via `file_get_inode()`
  - Declared in `filesys/file.h`

- [ ] **Return inode number** via `inode_get_inumber()`
  - Declared in `filesys/inode.h`

### Implementation

```c
if (args[0] == SYS_INUMBER) {
  validate_pointer_and_exit_if_false(f, &args[1]);
  int fd = args[1];
  
  if (fd < 2 || fd >= MAX_FILE_DESCRIPTOR) {
    f->eax = -1;
    return;
  }
  
  struct file *file = thread_current()->pcb->fd_table[fd];
  if (file == NULL) {
    f->eax = -1;
    return;
  }
  
  struct inode *inode = file_get_inode(file);
  f->eax = inode_get_inumber(inode);
}
```

---

## 8. Root Directory Extension

### Tasks

- [ ] **Allow root directory to grow**
  - Currently limited to initial size (16 entries)
  - Directory is just a file; extension logic should apply

- [ ] **Create files with size 0**
  - The basic filesys requires `initial_size` at creation
  - Modify to support starting with size 0 and growing

---

## 9. Edge Cases

### Must Handle

- [ ] **Seek past EOF then read** → Should return 0 bytes
- [ ] **Seek past EOF then write** → Should extend file and zero-fill gap
- [ ] **Write to maximum file size (8 MiB)** → Verify all 16,384 sectors can be allocated
- [ ] **Multiple concurrent extends** → Synchronization must handle this
- [ ] **Extend then close without writing** → File state should be consistent
- [ ] **Out of disk space mid-write** → Partial write should succeed up to available space
- [ ] **Create file with size 0** → Should work; writes extend it
- [ ] **Truncate/overwrite existing file** → Handle inode reuse correctly

---

## 10. Testing Considerations

### Test Cases to Verify

- [ ] Sequential writes that grow file
- [ ] Random writes past EOF
- [ ] Sparse file creation (write at large offset)
- [ ] Disk exhaustion scenarios
- [ ] Maximum file size (8 MiB)
- [ ] `inumber()` returns consistent values for same file
- [ ] `inumber()` returns different values for different files
- [ ] Directory growth beyond 16 entries
- [ ] Concurrent file extension
- [ ] Mixed read/write with extension

### Running Tests

```bash
cd src/filesys
make clean && make
cd build
# Run specific tests as they become available
```

---

## 11. Performance Considerations

### Best Practices

- [ ] **Avoid FAT-style linked lists** (as specified in requirements)
- [ ] **Minimize disk reads for lookups** - Cache indirect blocks when possible
- [ ] **Consider read-ahead for sequential access** (integrate with `cache_prefetch.c`)
- [ ] **Batch allocations when possible** - Reduce free-map writes

---

## Quick Reference: Key Functions to Modify

| File | Function | Change Needed |
|------|----------|---------------|
| `inode.c` | `struct inode_disk` | Redesign with indexed pointers |
| `inode.c` | `byte_to_sector()` | Multi-level lookup |
| `inode.c` | `inode_create()` | Support size-0 files, indexed allocation |
| `inode.c` | `inode_write_at()` | Extension logic + rollback |
| `inode.c` | `inode_read_at()` | Multi-level lookup |
| `inode.c` | `inode_close()` | Free all block levels |
| `free-map.c` | `free_map_allocate()` | Single-sector allocation variant |
| `syscall.c` | `syscall_handler()` | Add `SYS_INUMBER` case |

---

## Design Document Questions

When writing your design document, be prepared to answer:

1. What is your inode structure layout? How many direct/indirect/doubly-indirect pointers?
2. How do you handle file extension? What's the algorithm?
3. How do you handle disk exhaustion during extension?
4. How do you ensure consistency if extension fails partway?
5. What synchronization do you use for inode operations?
6. Do you support sparse files? Why or why not?
7. How does your design achieve O(1) random access?

---

## References

- Unix FFS (Fast File System) - Original indexed inode design
- `src/filesys/inode.c` - Current implementation
- `src/filesys/free-map.c` - Block allocation
- `src/lib/syscall-nr.h` - System call numbers

