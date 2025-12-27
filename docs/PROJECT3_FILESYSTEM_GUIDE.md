# Project 3: Filesystem - Comprehensive Review Guide

This document provides a complete overview of the Pintos filesystem, the Project 3 requirements, and implementation guidance.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Current Filesystem Architecture](#2-current-filesystem-architecture)
3. [Task 1: Buffer Cache](#3-task-1-buffer-cache)
4. [Task 2: Extensible Files](#4-task-2-extensible-files)
5. [Task 3: Subdirectories](#5-task-3-subdirectories)
6. [Task 4: Synchronization](#6-task-4-synchronization)
7. [Key Data Structures Reference](#7-key-data-structures-reference)
8. [Important Function Call Paths](#8-important-function-call-paths)
9. [Common Pitfalls & Tips](#9-common-pitfalls--tips)
10. [Design Checklist](#10-design-checklist)
11. [Quick Cheat Sheet: Things to Remember](#11-quick-cheat-sheet-things-to-remember)

---

## 1. Project Overview

Project 3 requires expanding the Pintos filesystem with four major components:

| Task | Description | Key Challenge |
|------|-------------|---------------|
| **Buffer Cache** | Cache disk blocks in memory | Eviction policy, concurrency |
| **Extensible Files** | Files can grow dynamically | Indexed inode structure |
| **Subdirectories** | Hierarchical directory structure | Path parsing, `.` and `..` |
| **Synchronization** | Thread-safe filesystem | Fine-grained locking |

**Maximum File/Partition Size:** 8 MiB (2^23 bytes)

---

## 2. Current Filesystem Architecture

### 2.1 File Organization

```
src/filesys/
├── inode.c/h       # Inode management (on-disk + in-memory)
├── directory.c/h   # Directory operations
├── file.c/h        # File handle operations
├── filesys.c/h     # High-level filesystem interface
├── free-map.c/h    # Disk space allocation (bitmap)
├── fsutil.c/h      # Filesystem utilities
└── off_t.h         # Offset type definition

src/devices/
├── block.c/h       # Block device abstraction
└── ide.c/h         # IDE disk controller
```

### 2.2 Disk Layout

```
┌─────────────────────────────────────────────────────────┐
│ Sector 0: Free-map inode (FREE_MAP_SECTOR)              │
├─────────────────────────────────────────────────────────┤
│ Sector 1: Root directory inode (ROOT_DIR_SECTOR)        │
├─────────────────────────────────────────────────────────┤
│ Sectors 2+: Free-map data, root dir data, file data     │
└─────────────────────────────────────────────────────────┘
```

**Block Size:** 512 bytes (BLOCK_SECTOR_SIZE)

### 2.3 Current Limitations (What You'll Fix)

| Limitation | Location | Project Task |
|------------|----------|--------------|
| No block caching | `inode.c:191` - direct `block_read()` | Buffer Cache |
| Contiguous allocation | `inode_disk.start` field | Extensible Files |
| Files can't grow | `inode_create()` pre-allocates | Extensible Files |
| Flat directory (root only) | `filesys.c` always uses root | Subdirectories |
| No synchronization | Throughout | Synchronization |

### 2.4 How Files Are Currently Stored

```c
// inode.c:15-20 - On-disk inode structure
struct inode_disk {
  block_sector_t start;     // First data sector (CONTIGUOUS!)
  off_t length;             // File size in bytes
  unsigned magic;           // 0x494e4f44 for validation
  uint32_t unused[125];     // Padding to 512 bytes
};
```

**Current Allocation:** Files must be stored in contiguous sectors.
- `inode_create()` calls `free_map_allocate(cnt, &start)` for contiguous blocks
- `byte_to_sector()` calculates: `start + pos / BLOCK_SECTOR_SIZE`

This is what you'll replace with indexed allocation!

---

## 3. Task 1: Buffer Cache

### 3.1 Requirements

| Requirement | Details |
|-------------|---------|
| **Size Limit** | Maximum 64 disk sectors cached |
| **Free-map Exception** | Can be cached separately (doesn't count) |
| **Write Policy** | Write-back (NOT write-through) |
| **Eviction Algorithm** | LRU, Clock, NRU, or N-th chance (NOT FIFO/RANDOM) |

### 3.2 What Must Change

Currently, all disk I/O goes directly through `block_read()` and `block_write()`:

```c
// inode.c:191 - Current direct disk read
block_read(fs_device, sector_idx, bounce);

// inode.c:254 - Current direct disk write
block_write(fs_device, sector_idx, bounce);
```

You need to intercept these with a cache layer.

### 3.3 Buffer Cache Design

```
┌──────────────────────────────────────────────────────────┐
│                    BUFFER CACHE (64 entries)             │
├──────────────────────────────────────────────────────────┤
│  Entry 0: [sector_num | dirty | valid | ref | data[512]] │
│  Entry 1: [sector_num | dirty | valid | ref | data[512]] │
│  ...                                                     │
│  Entry 63: [sector_num | dirty | valid | ref | data[512]]│
└──────────────────────────────────────────────────────────┘
```

**Suggested cache entry structure:**

```c
struct cache_entry {
  block_sector_t sector;    // Which disk sector this caches
  bool valid;               // Is this entry in use?
  bool dirty;               // Has this been modified?
  bool accessed;            // For clock algorithm
  uint8_t data[BLOCK_SECTOR_SIZE];  // The actual data
  struct lock entry_lock;   // Per-entry lock for fine-grained sync
  int readers;              // Reader count for reader-writer lock
  struct condition no_readers;  // For exclusive access
};
```

### 3.4 Clock Algorithm Implementation

```
       ┌───┐
       │ 0 │◄── clock_hand
       ├───┤
       │ 1 │
       ├───┤
       │ 2 │
       ├───┤
       │...│
       ├───┤
       │63 │
       └───┘

On cache miss:
1. Check entry at clock_hand
2. If accessed=0: evict this entry (write back if dirty)
3. If accessed=1: set accessed=0, advance clock_hand, goto 1
4. Load new sector into evicted slot
```

### 3.5 Concurrency Rules

1. **No eviction while in use:** Block threads from evicting entries being accessed
2. **Single loader:** Only one thread loads a block; others wait
3. **No premature access:** Threads can't access blocks before fully loaded
4. **No blocking I/O with global lock:** If using global cache lock, release before I/O

### 3.6 Optional Features

| Feature | Description |
|---------|-------------|
| **Write-behind** | Periodically flush dirty blocks using `timer_sleep()` |
| **Read-ahead** | When reading block N, asynchronously prefetch block N+1 |

### 3.7 Functions to Modify

| File | Function | Change |
|------|----------|--------|
| `inode.c` | `inode_read_at()` | Use cache instead of `block_read()` |
| `inode.c` | `inode_write_at()` | Use cache instead of `block_write()` |
| `filesys.c` | `filesys_done()` | Flush all dirty cache entries |

---

## 4. Task 2: Extensible Files

### 4.1 Requirements

| Requirement | Details |
|-------------|---------|
| **Max File Size** | 8 MiB (2^23 bytes) |
| **Structure** | Indexed inode (like Unix FFS) |
| **NOT Allowed** | FAT-based design |
| **File Growth** | Files start at 0, grow on write |
| **Sparse Files** | Optional: defer allocation for zero-filled gaps |

### 4.2 Indexed Inode Structure

Replace the current contiguous allocation with multi-level indexing:

```
┌─────────────────────────────────────────────────────────────┐
│                     INODE (512 bytes)                       │
├─────────────────────────────────────────────────────────────┤
│ length (4 bytes)                                            │
│ magic (4 bytes)                                             │
│ direct[0..N-1]        → data blocks directly                │
│ indirect              → block of 128 pointers → data        │
│ doubly_indirect       → block of 128 ptrs → 128 ptrs → data │
└─────────────────────────────────────────────────────────────┘
```

### 4.3 Calculating Block Counts

```
Sector size: 512 bytes
Pointers per sector: 512 / 4 = 128 pointers

Target: 8 MiB = 8,388,608 bytes = 16,384 sectors

Example design (one possibility):
- 123 direct pointers      → 123 × 512 = 62,976 bytes
- 1 indirect pointer       → 128 × 512 = 65,536 bytes
- 1 doubly indirect        → 128 × 128 × 512 = 8,388,608 bytes

Total addressable: > 8 MiB ✓
```

### 4.4 New inode_disk Structure

```c
#define DIRECT_BLOCKS 123    // Adjust based on your design
#define INDIRECT_BLOCKS 128  // 512/4 pointers per block

struct inode_disk {
  off_t length;                          // File size in bytes
  unsigned magic;                        // Magic number
  block_sector_t direct[DIRECT_BLOCKS];  // Direct block pointers
  block_sector_t indirect;               // Single indirect block
  block_sector_t doubly_indirect;        // Doubly indirect block
};
// Must still be exactly 512 bytes!
```

### 4.5 byte_to_sector() Rewrite

The key function to rewrite:

```c
// Current (inode.c:40-46) - simple contiguous
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

// New version - must handle indexed structure
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  if (pos >= inode->data.length)
    return -1;

  size_t sector_idx = pos / BLOCK_SECTOR_SIZE;

  if (sector_idx < DIRECT_BLOCKS) {
    // Direct block
    return inode->data.direct[sector_idx];
  }
  sector_idx -= DIRECT_BLOCKS;

  if (sector_idx < INDIRECT_BLOCKS) {
    // Indirect block - read pointer table
    block_sector_t indirect_block[INDIRECT_BLOCKS];
    // Read indirect table from inode->data.indirect
    // Return indirect_block[sector_idx]
  }
  sector_idx -= INDIRECT_BLOCKS;

  // Doubly indirect - two levels of indirection
  // ...
}
```

### 4.6 File Growth Semantics

| Operation | Effect |
|-----------|--------|
| `seek(pos)` | Does NOT extend file |
| `write(data)` past EOF | Extends file, fills gap with zeros |
| `read()` past EOF | Returns 0 bytes |

### 4.7 Key Functions to Modify

| File | Function | Change |
|------|----------|--------|
| `inode.c` | `struct inode_disk` | Add indexed pointers |
| `inode.c` | `byte_to_sector()` | Navigate index structure |
| `inode.c` | `inode_create()` | Don't pre-allocate all blocks |
| `inode.c` | `inode_write_at()` | Allocate blocks on demand, extend file |
| `inode.c` | `inode_close()` | Free all allocated blocks recursively |
| `free-map.c` | `free_map_allocate()` | May need single-block allocation |

### 4.8 New System Call

```c
int inumber(int fd);  // Returns inode sector number for fd
```

---

## 5. Task 3: Subdirectories

### 5.1 Requirements

| Requirement | Details |
|-------------|---------|
| **Path Separator** | `/` (forward slash) |
| **Absolute Paths** | Start with `/`, e.g., `/home/user/file.txt` |
| **Relative Paths** | From current directory, e.g., `docs/readme.txt` |
| **Special Entries** | `.` (current dir), `..` (parent dir) |
| **Name Limit** | 14 chars per component, but full path can be longer |
| **CWD Inheritance** | Child inherits parent's CWD on exec |

### 5.2 Directory Entry Changes

Directories need to track whether entries are files or subdirectories:

```c
// Current (directory.c:16-20)
struct dir_entry {
  block_sector_t inode_sector;
  char name[NAME_MAX + 1];
  bool in_use;
};

// New version - add type flag
struct dir_entry {
  block_sector_t inode_sector;
  char name[NAME_MAX + 1];
  bool in_use;
  bool is_dir;              // NEW: true if this is a directory
};
```

### 5.3 inode Changes for Directories

```c
struct inode_disk {
  // ... existing fields ...
  bool is_dir;              // Is this inode a directory?
  block_sector_t parent;    // Parent directory sector (for ..)
};
```

### 5.4 Path Parsing Algorithm

```c
// Parse "/home/user/file.txt"
bool resolve_path(const char* path, struct dir** parent, char* filename) {
  struct dir* dir;

  // Start from root or current directory
  if (path[0] == '/')
    dir = dir_open_root();
  else
    dir = dir_open(inode_reopen(thread_current()->cwd));

  // Tokenize and traverse
  char* token, *save_ptr;
  char* path_copy = malloc(strlen(path) + 1);
  strlcpy(path_copy, path, strlen(path) + 1);

  for (token = strtok_r(path_copy, "/", &save_ptr);
       token != NULL;
       token = strtok_r(NULL, "/", &save_ptr)) {

    char* next = strtok_r(NULL, "/", &save_ptr);

    if (next == NULL) {
      // This is the final component (filename)
      strlcpy(filename, token, NAME_MAX + 1);
      *parent = dir;
      return true;
    }

    // Traverse into subdirectory
    struct inode* inode;
    if (!dir_lookup(dir, token, &inode)) {
      dir_close(dir);
      return false;  // Path component not found
    }

    dir_close(dir);
    dir = dir_open(inode);
  }

  return true;
}
```

### 5.5 New System Calls

| Syscall | Signature | Description |
|---------|-----------|-------------|
| `chdir` | `bool chdir(const char* dir)` | Change current directory |
| `mkdir` | `bool mkdir(const char* dir)` | Create new directory |
| `readdir` | `bool readdir(int fd, char* name)` | Read directory entry |
| `isdir` | `bool isdir(int fd)` | Check if fd is a directory |
| `inumber` | `int inumber(int fd)` | Get inode sector number |

### 5.6 Syscall Updates Required

| Syscall | Change Needed |
|---------|---------------|
| `open` | Handle directories, parse paths |
| `close` | Handle directory file descriptors |
| `create` | Parse paths to find parent directory |
| `remove` | Can delete empty directories (not root) |
| `exec` | Child inherits parent's CWD |

### 5.7 Per-Process CWD

```c
// In thread.h or process.h
struct thread {
  // ... existing fields ...
  struct dir* cwd;          // Current working directory
};

// Initialize in thread creation
void thread_create(...) {
  t->cwd = dir_open_root();  // Default to root
}

// In process_execute or exec syscall - child inherits CWD
child->cwd = dir_reopen(parent->cwd);
```

### 5.8 Directory Deletion Rules

1. Cannot delete root directory
2. Can only delete empty directories (just `.` and `..`)
3. Open directories can still be deleted (like files)
4. Operations on deleted directory return errors

### 5.9 . and .. Implementation

When creating a new directory:

```c
bool dir_create(block_sector_t sector, block_sector_t parent_sector) {
  // Create the inode
  inode_create(sector, initial_size);

  // Add . entry pointing to self
  struct dir* dir = dir_open(inode_open(sector));
  dir_add(dir, ".", sector, true);

  // Add .. entry pointing to parent
  dir_add(dir, "..", parent_sector, true);

  dir_close(dir);
}
```

---

## 6. Task 4: Synchronization

### 6.1 Requirements

| Requirement | Details |
|-------------|---------|
| **No Global FS Lock** | Must be removed (from Project 2) |
| **Global Cache Lock OK** | But no blocking I/O while holding it |
| **Independent Operations** | Must run concurrently |
| **Same Sector Operations** | May be serialized |

### 6.2 What Counts as Independent?

| Operation A | Operation B | Independent? |
|-------------|-------------|--------------|
| Read file1 | Read file2 | Yes (if different sectors) |
| Read file1 | Write file2 | Yes (if different sectors) |
| Read file1 | Write file1 | No (same file) |
| Read sector 5 | Read sector 5 | No (same sector) |

### 6.3 Synchronization Points

| Component | Lock Type | Protects |
|-----------|-----------|----------|
| Buffer cache | Per-entry lock | Cache entry access |
| Inode list | Global list lock | `open_inodes` list |
| Free-map | Free-map lock | Bitmap modifications |
| Directory | Per-directory lock | Directory modifications |
| File | Per-inode lock | File metadata |

### 6.4 Suggested Lock Structure

```c
// Per-inode lock for file operations
struct inode {
  struct lock inode_lock;   // Protects metadata
  // ...
};

// Buffer cache entry lock
struct cache_entry {
  struct lock entry_lock;   // Protects this cache entry
  struct condition readers_done;
  int active_readers;
  bool writer_waiting;
  // ...
};

// Directory lock
struct dir {
  struct lock dir_lock;     // Protects directory modifications
  // ...
};
```

### 6.5 Reader-Writer Pattern for Cache

```c
void cache_read(block_sector_t sector, void* buffer) {
  struct cache_entry* e = cache_lookup_or_load(sector);

  lock_acquire(&e->entry_lock);
  e->active_readers++;
  lock_release(&e->entry_lock);

  // Read data (concurrent reads OK)
  memcpy(buffer, e->data, BLOCK_SECTOR_SIZE);

  lock_acquire(&e->entry_lock);
  e->active_readers--;
  if (e->active_readers == 0)
    cond_signal(&e->readers_done, &e->entry_lock);
  lock_release(&e->entry_lock);
}

void cache_write(block_sector_t sector, const void* buffer) {
  struct cache_entry* e = cache_lookup_or_load(sector);

  lock_acquire(&e->entry_lock);
  while (e->active_readers > 0)
    cond_wait(&e->readers_done, &e->entry_lock);

  // Write data (exclusive access)
  memcpy(e->data, buffer, BLOCK_SECTOR_SIZE);
  e->dirty = true;

  lock_release(&e->entry_lock);
}
```

---

## 7. Key Data Structures Reference

### 7.1 Current Structures (What Exists)

#### On-Disk Inode (`inode.c:15-20`)
```c
struct inode_disk {
  block_sector_t start;     // First data sector
  off_t length;             // File size in bytes
  unsigned magic;           // 0x494e4f44
  uint32_t unused[125];     // Padding to 512 bytes
};
```

#### In-Memory Inode (`inode.c:27-34`)
```c
struct inode {
  struct list_elem elem;     // Element in inode list
  block_sector_t sector;     // Sector number on disk
  int open_cnt;              // Reference count
  bool removed;              // Marked for deletion?
  int deny_write_cnt;        // Write denial count
  struct inode_disk data;    // Cached inode content
};
```

#### File Handle (`file.c:7-12`)
```c
struct file {
  struct inode* inode;       // File's inode
  off_t pos;                 // Current position
  bool deny_write;           // Write denied?
  int ref_count;             // For fork() sharing
};
```

#### Directory (`directory.c:10-13`)
```c
struct dir {
  struct inode* inode;       // Directory inode
  off_t pos;                 // For readdir iteration
};
```

#### Directory Entry (`directory.c:16-20`)
```c
struct dir_entry {
  block_sector_t inode_sector;   // Inode sector
  char name[NAME_MAX + 1];       // Filename (max 14 chars)
  bool in_use;                   // Valid entry?
};
```

### 7.2 Key Constants

| Constant | Value | Location |
|----------|-------|----------|
| `BLOCK_SECTOR_SIZE` | 512 | `block.h:11` |
| `FREE_MAP_SECTOR` | 0 | `filesys.h:8` |
| `ROOT_DIR_SECTOR` | 1 | `filesys.h:9` |
| `NAME_MAX` | 14 | `directory.c:12` |
| `INODE_MAGIC` | 0x494e4f44 | `inode.c:12` |

---

## 8. Important Function Call Paths

### 8.1 File Creation

```
filesys_create(name, size)
  ├── dir_open_root()
  │     └── inode_open(ROOT_DIR_SECTOR)
  ├── free_map_allocate(1, &inode_sector)  // For inode
  ├── inode_create(inode_sector, size)
  │     ├── free_map_allocate(sectors, &data_start)
  │     ├── block_write(inode_sector, inode_disk)
  │     └── [zero out data sectors]
  ├── dir_add(dir, name, inode_sector)
  │     └── inode_write_at()  // Write dir entry
  └── dir_close()
```

### 8.2 File Read

```
file_read(file, buffer, size)
  └── inode_read_at(inode, buffer, size, pos)
        └── [loop through sectors]
              ├── byte_to_sector(inode, offset)
              ├── block_read(sector, bounce_buffer)
              └── memcpy(buffer, bounce_buffer, chunk)
```

### 8.3 File Write

```
file_write(file, buffer, size)
  └── inode_write_at(inode, buffer, size, pos)
        └── [check deny_write_cnt]
        └── [loop through sectors]
              ├── byte_to_sector(inode, offset)
              ├── block_read() if partial sector
              ├── memcpy(bounce_buffer, data)
              └── block_write(sector, bounce_buffer)
```

### 8.4 File Deletion

```
filesys_remove(name)
  ├── dir_open_root()
  └── dir_remove(dir, name)
        ├── lookup(dir, name)  // Find entry
        ├── inode_open(entry.inode_sector)
        ├── [mark entry in_use = false]
        ├── inode_remove(inode)  // Mark removed = true
        └── inode_close(inode)
              └── [if open_cnt == 0 && removed]
                    ├── free_map_release(data_sectors)
                    └── free_map_release(inode_sector)
```

---

## 9. Common Pitfalls & Tips

### 9.1 Buffer Cache Pitfalls

| Pitfall | Solution |
|---------|----------|
| Forgetting to flush on shutdown | Modify `filesys_done()` to flush all dirty entries |
| Race in cache lookup | Use lock before checking if sector is cached |
| Evicting in-use entry | Track reference counts per entry |
| Deadlock with global lock | Release cache lock before disk I/O |

### 9.2 Extensible Files Pitfalls

| Pitfall | Solution |
|---------|----------|
| `inode_disk` not 512 bytes | Use `ASSERT(sizeof(struct inode_disk) == BLOCK_SECTOR_SIZE)` |
| Forgetting to free indirect blocks | Recursively free on `inode_close()` |
| Growing file but not updating length | Update `inode->data.length` and write back |
| Not handling sparse files | Either allocate zeros or track unallocated blocks |

### 9.3 Subdirectory Pitfalls

| Pitfall | Solution |
|---------|----------|
| Not handling relative paths | Check if path starts with `/` |
| Forgetting `.` and `..` | Add during `mkdir`, skip in `readdir` |
| Deleting non-empty directory | Check entry count before removal |
| CWD not inherited by child | Copy parent's CWD in exec |
| Deleting open directory | Allow it, but fail subsequent operations |

### 9.4 Synchronization Pitfalls

| Pitfall | Solution |
|---------|----------|
| Holding lock during I/O | Design lock-free I/O paths |
| Not protecting `open_inodes` list | Add list lock |
| Race in `free_map_allocate` | Lock the free-map |
| Deadlock from lock ordering | Document and enforce order |

---

## 10. Design Checklist

### Before Writing Code

- [ ] Understand current `inode_read_at()` and `inode_write_at()`
- [ ] Understand `byte_to_sector()` calculation
- [ ] Understand `free_map_allocate()` and `free_map_release()`
- [ ] Understand `dir_add()` and `dir_lookup()`
- [ ] Design new `inode_disk` structure (with indexed pointers)
- [ ] Plan cache entry structure
- [ ] Plan synchronization strategy (what locks, where)

### Buffer Cache

- [ ] Cache structure with 64 entries
- [ ] Lookup function (find sector in cache)
- [ ] Load function (read sector from disk into cache)
- [ ] Eviction with clock algorithm
- [ ] Write-back dirty entries
- [ ] Flush all on `filesys_done()`
- [ ] Concurrency: no eviction during access

### Extensible Files

- [ ] New `inode_disk` with direct/indirect/doubly-indirect
- [ ] Verify `sizeof(inode_disk) == 512`
- [ ] New `byte_to_sector()` for indexed navigation
- [ ] Modify `inode_create()` - don't pre-allocate
- [ ] Modify `inode_write_at()` - grow file on write
- [ ] Modify `inode_close()` - free all blocks
- [ ] Implement `inumber()` syscall

### Subdirectories

- [ ] Modify `struct dir_entry` - add `is_dir`
- [ ] Modify `struct inode_disk` - add `is_dir`, `parent`
- [ ] Path parsing function
- [ ] Per-thread CWD
- [ ] `mkdir()` - create directory with `.` and `..`
- [ ] `chdir()` - change CWD
- [ ] `readdir()` - iterate entries (skip `.` and `..`)
- [ ] `isdir()` - check if fd is directory
- [ ] Update `open()`, `create()`, `remove()` for paths

### Synchronization

- [ ] Remove global filesystem lock
- [ ] Add per-inode locks
- [ ] Add cache entry locks
- [ ] Add free-map lock
- [ ] Add directory locks
- [ ] Verify no blocking I/O with locks held

---

## Quick Reference: File Locations

| Component | File | Key Lines |
|-----------|------|-----------|
| On-disk inode | `inode.c` | 15-20 |
| In-memory inode | `inode.c` | 27-34 |
| `byte_to_sector()` | `inode.c` | 40-46 |
| `inode_create()` | `inode.c` | 60-89 |
| `inode_read_at()` | `inode.c` | 165-208 |
| `inode_write_at()` | `inode.c` | 215-268 |
| Directory struct | `directory.c` | 10-13 |
| Dir entry struct | `directory.c` | 16-20 |
| `dir_lookup()` | `directory.c` | 95-107 |
| `dir_add()` | `directory.c` | 115-150 |
| `filesys_create()` | `filesys.c` | 39-49 |
| `free_map_allocate()` | `free-map.c` | 25-34 |
| Block operations | `block.c` | 97-113 |

---

## 11. Quick Cheat Sheet: Things to Remember

### 11.1 The Big Picture

```
Name → Directory → Inode → Sectors → Bytes
```

```
User:       "open hello.txt, read 100 bytes"
                    │
filesys.c:  name → inode sector (via directory)
                    │
file.c:     tracks position per open()
                    │
inode.c:    file offset → disk sector
                    │
block.c:    read/write 512-byte sectors
                    │
               Physical Disk
```

**Key insight:** Disk is chaos. OS conventions create order.

---

### 11.2 Critical One-Liners

| Concept | Remember |
|---------|----------|
| Layer separation | Each layer does ONE thing |
| inode_disk | On disk, exactly 512 bytes |
| inode | In memory, with bookkeeping |
| file | Per `open()` call (tracks position) |
| inode | Per file on disk (shared) |
| fork() | Shares file handles (same position!) |
| open() | Creates new file handles (separate positions) |
| Deletion trigger | `removed == true` AND `open_cnt == 0` |
| Crash safety | Write metadata to disk BEFORE using allocated space |

---

### 11.3 Two Opens vs fork()

| Scenario | `struct file` | Position shared? |
|----------|---------------|------------------|
| Two `open()` calls | Separate | **No** |
| `fork()` | Same (ref_count++) | **Yes!** |

```c
// fork() uses file_dup() - increments ref_count, returns SAME pointer
struct file* file_dup(struct file* file) {
  file->ref_count++;
  return file;  // Same struct!
}
```

---

### 11.4 Lazy Deletion

```
remove("file.txt")
    │
    ▼
inode->removed = true    ← Just a flag!
    │
    ▼
File still readable if someone has it open
    │
    ▼
Last close() → open_cnt drops to 0
    │
    ▼
NOW sectors are actually freed
```

---

### 11.5 EOF Behavior

| Operation | Current Pintos | Your Project 3 |
|-----------|----------------|----------------|
| seek past EOF | Allowed | Allowed |
| read past EOF | Returns 0 bytes | Returns 0 bytes |
| write past EOF | Fails (returns 0) | **Extends file, zeros gap** |

---

### 11.6 Bounce Buffer

**Why?** `block_read()` ALWAYS reads exactly 512 bytes.

| Read Type | Action |
|-----------|--------|
| Full aligned sector | Read directly to buffer |
| Partial / unaligned | Read to bounce, memcpy portion |

---

### 11.7 Why Inode Deduplication?

`inode_open()` searches `open_inodes` list first:

| Reason | Benefit |
|--------|---------|
| Avoid disk read | Performance (100,000x faster) |
| Share same struct | Consistency (everyone sees updates) |

---

### 11.8 Executable Protection

```c
file_deny_write(exe);     // When loading executable
  → inode->deny_write_cnt++

file_allow_write(exe);    // When process exits
  → inode->deny_write_cnt--
```

Counter-based: multiple processes can run same executable.

---

### 11.9 Directory Tombstones

When deleting: set `in_use = false` instead of shifting entries.

| Approach | Delete Speed | Lookup Speed |
|----------|--------------|--------------|
| Shift entries | Slow O(n) | Fast |
| Tombstone | **Fast O(1)** | Slightly slower |

Tombstone slots get reused when adding new files.

---

### 11.10 Crash Safety

**Always write free-map to disk immediately after allocation!**

```
BAD:  allocate sectors → use them → crash → sectors lost forever
GOOD: allocate sectors → write bitmap → use them → crash → bitmap is safe
```

---

### 11.11 Race Conditions to Prevent

| Race | Solution |
|------|----------|
| Read during write | Per-entry lock |
| Evict during access | Pin count / ref count |
| Double allocation | Lock free-map |
| Concurrent dir modify | Per-directory lock |

---

### 11.12 Magic Numbers

| Value | Meaning |
|-------|---------|
| 512 | Sector size (`BLOCK_SECTOR_SIZE`) |
| 0 | Free-map sector (`FREE_MAP_SECTOR`) |
| 1 | Root dir sector (`ROOT_DIR_SECTOR`) |
| 14 | Max filename (`NAME_MAX`) |
| 0x494e4f44 | Inode magic ("INOD" in ASCII) |
| 128 | Pointers per indirect block (512/4) |

---

### 11.13 The Layer Stack

```
┌─────────────────────────────────────────┐
│  filesys.c  — "What inode is this name?"│
├─────────────────────────────────────────┤
│  file.c     — "Where am I in this file?"│
├─────────────────────────────────────────┤
│  inode.c    — "Which sectors hold data?"│
├─────────────────────────────────────────┤
│  block.c    — "Read/write 512 bytes"    │
└─────────────────────────────────────────┘
```

**Each layer can change independently!**
- Change disk hardware? Only touch `block.c`
- Change file structure? Only touch `inode.c` (your Project 3!)
- Add subdirectories? Mostly `filesys.c` and `directory.c`

---

### 11.14 Project 3 Task Summary

| Task | Size Limit | Key Implementation |
|------|------------|-------------------|
| Buffer Cache | 64 sectors | Clock eviction, write-back, per-entry locks |
| Extensible Files | 8 MiB max | Direct + indirect + doubly-indirect pointers |
| Subdirectories | 14 char names | Path parsing, `.`/`..`, per-process CWD |
| Synchronization | — | Remove global lock, fine-grained locking |

---

*Good luck with Project 3! Remember: understand the existing code thoroughly before modifying it.*
