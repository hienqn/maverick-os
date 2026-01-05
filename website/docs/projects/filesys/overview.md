---
sidebar_position: 4
---

import AnimatedFlow from '@site/src/components/AnimatedFlow';
import InteractiveDiagram from '@site/src/components/InteractiveDiagram';
import CodeWalkthrough from '@site/src/components/CodeWalkthrough';

# Project 4: File System

In this project, you'll implement a buffer cache, indexed files for extensibility, subdirectories, and write-ahead logging for crash consistency.

## Learning Goals

- Understand how file systems organize data on disk
- Implement a buffer cache with eviction and prefetching
- Build indexed files that can grow dynamically
- Support hierarchical directories with path resolution
- Ensure crash consistency with write-ahead logging

## Tasks Overview

| Task | Difficulty | Key Concepts |
|------|------------|--------------|
| Buffer Cache | ★★☆ | Caching, clock eviction, write-behind |
| Indexed Files | ★★★ | Multi-level indexing, file growth |
| Subdirectories | ★★☆ | Path resolution, `.` and `..` |
| Write-Ahead Logging | ★★★ | Transactions, crash recovery |

## Key Files

| File | Purpose |
|------|---------|
| `filesys/cache.c` | Buffer cache with 64 entries |
| `filesys/inode.c` | Indexed inode implementation |
| `filesys/directory.c` | Directory operations and path resolution |
| `filesys/free-map.c` | Bitmap-based sector allocation |
| `filesys/wal.c` | Write-ahead logging for crash consistency |

## Getting Started

```bash
cd src/filesys
make

# Run a specific test
cd build
make tests/filesys/extended/grow-create.result

# Run all filesystem tests
make check
```

## File System Architecture

<InteractiveDiagram
  title="File System Layers"
  nodes={[
    { id: 'syscall', label: 'System Calls', x: 300, y: 30, description: 'open, read, write, close, mkdir' },
    { id: 'filesys', label: 'Filesystem API', x: 300, y: 110, description: 'filesys_open, filesys_create' },
    { id: 'dir', label: 'Directory Layer', x: 150, y: 190, description: 'Path resolution, dir entries' },
    { id: 'inode', label: 'Inode Layer', x: 300, y: 190, description: 'File metadata, block mapping' },
    { id: 'cache', label: 'Buffer Cache', x: 450, y: 190, description: '64-entry LRU cache' },
    { id: 'wal', label: 'WAL', x: 300, y: 270, description: 'Transaction logging' },
    { id: 'block', label: 'Block Device', x: 300, y: 350, description: 'IDE disk driver' },
  ]}
  edges={[
    { from: 'syscall', to: 'filesys', label: '' },
    { from: 'filesys', to: 'dir', label: '' },
    { from: 'filesys', to: 'inode', label: '' },
    { from: 'dir', to: 'inode', label: '' },
    { from: 'inode', to: 'cache', label: '' },
    { from: 'cache', to: 'wal', label: '' },
    { from: 'wal', to: 'block', label: '' },
  ]}
/>

## Task 1: Buffer Cache

### Why Cache?

Without caching, every file read/write goes to disk:
- Disk latency: ~10ms
- Memory latency: ~100ns
- **100,000x difference!**

### Cache Structure

```c
#define CACHE_SIZE 64

struct cache_entry {
  block_sector_t sector;  /* Which disk sector */
  enum cache_state state; /* INVALID, LOADING, VALID */
  bool dirty;             /* Modified since loaded? */
  bool accessed;          /* For clock eviction */
  uint8_t data[512];      /* Sector data */

  struct lock entry_lock;
  struct condition loading_done;
};
```

### Clock Eviction Algorithm

When the cache is full, evict using clock (second-chance):

```
Cache: [E0] → [E1] → [E2] → ... → [E63]
               ↑
           clock_hand

For each entry starting at clock_hand:
  1. If accessed: clear accessed, move on
  2. If not accessed: evict this entry
     - Write to disk if dirty
     - Return slot for new sector
```

### Write-Behind (Periodic Flushing)

A background thread flushes dirty entries every 30 seconds:

```c
void cache_flusher(void *aux UNUSED) {
  while (true) {
    timer_sleep(30 * TIMER_FREQ);  /* 30 seconds */
    cache_flush();  /* Write all dirty entries */
  }
}
```

### Read-Ahead Prefetching

When reading sector N, prefetch N+1 in background:

```c
void cache_read(block_sector_t sector, void *buffer) {
  /* Read requested sector */
  struct cache_entry *e = cache_get(sector);
  memcpy(buffer, e->data, 512);

  /* Prefetch next sector asynchronously */
  prefetch_queue_add(sector + 1);
}
```

## Task 2: Indexed Files (Extensible)

### The Problem

Original PintOS uses contiguous allocation:
- File data stored in consecutive sectors
- Cannot grow files after creation
- External fragmentation

### Multi-Level Indexing

```
┌─────────────────────────────────────────────────────────┐
│  inode_disk (512 bytes on disk)                         │
├─────────────────────────────────────────────────────────┤
│  direct[12]     →  12 sectors directly (6KB)            │
│  indirect       →  128 more sectors (64KB)              │
│  doubly_indirect → 128 × 128 = 16384 sectors (~8MB)     │
│  length         →  File size in bytes                   │
│  is_dir         →  Directory flag                       │
├─────────────────────────────────────────────────────────┤
│  Maximum file size: 12 + 128 + 16384 = 16524 sectors    │
│                   = ~8MB                                │
└─────────────────────────────────────────────────────────┘
```

### Block Lookup

```c
/* Get the disk sector for byte position POS in file */
block_sector_t byte_to_sector(struct inode *inode, off_t pos) {
  size_t block_idx = pos / BLOCK_SECTOR_SIZE;

  if (block_idx < 12) {
    /* Direct block */
    return inode->direct[block_idx];
  }
  block_idx -= 12;

  if (block_idx < 128) {
    /* Single indirect */
    block_sector_t *indirect = load_indirect(inode->indirect);
    return indirect[block_idx];
  }
  block_idx -= 128;

  /* Doubly indirect */
  size_t i = block_idx / 128;
  size_t j = block_idx % 128;
  block_sector_t *dbl = load_indirect(inode->doubly_indirect);
  block_sector_t *ind = load_indirect(dbl[i]);
  return ind[j];
}
```

### File Growth

When writing past EOF, allocate new blocks:

```c
off_t inode_write_at(struct inode *inode, const void *buf,
                     off_t size, off_t offset) {
  off_t end_pos = offset + size;

  /* Extend file if needed */
  if (end_pos > inode_length(inode)) {
    if (!inode_extend(inode, end_pos))
      return 0;  /* Disk full */
  }

  /* Write data to allocated blocks */
  /* ... */
}
```

## Task 3: Subdirectories

### Directory Structure

Each directory entry:
```c
struct dir_entry {
  block_sector_t inode_sector;  /* Inode sector, or 0 if unused */
  char name[NAME_MAX + 1];      /* File name (null-terminated) */
  bool in_use;                  /* Is this entry in use? */
};
```

### Path Resolution

```c
/* Resolve "/foo/bar/baz.txt" to its inode */
struct inode *resolve_path(const char *path) {
  struct dir *dir;

  /* Start at root or cwd */
  if (path[0] == '/')
    dir = dir_open_root();
  else
    dir = dir_reopen(thread_current()->pcb->cwd);

  /* Walk path components */
  char *token, *save_ptr;
  for (token = strtok_r(path, "/", &save_ptr);
       token != NULL;
       token = strtok_r(NULL, "/", &save_ptr)) {

    struct inode *next_inode;
    if (!dir_lookup(dir, token, &next_inode)) {
      dir_close(dir);
      return NULL;  /* Not found */
    }

    dir_close(dir);
    if (!inode_is_dir(next_inode))
      return next_inode;  /* Found the file */

    dir = dir_open(next_inode);
  }

  return dir_get_inode(dir);
}
```

### Special Entries

Every directory contains:
- `.` - Self reference
- `..` - Parent directory

```c
bool dir_create(block_sector_t sector, block_sector_t parent) {
  /* Create inode */
  inode_create_dir(sector, 2 * sizeof(struct dir_entry));

  /* Add . and .. entries */
  struct dir *dir = dir_open(inode_open(sector));
  dir_add(dir, ".", sector);
  dir_add(dir, "..", parent);
  dir_close(dir);
  return true;
}
```

## Task 4: Write-Ahead Logging (WAL)

### The Crash Consistency Problem

Creating a file requires multiple writes:
1. Allocate inode sector (free map)
2. Initialize inode
3. Add directory entry

If crash after step 2, the inode is allocated but unreachable (orphan).

### WAL Solution

<AnimatedFlow
  title="WAL Transaction Lifecycle"
  states={[
    { id: 'begin', label: 'BEGIN', description: 'Start transaction, assign ID' },
    { id: 'log', label: 'LOG', description: 'Write old/new values to log' },
    { id: 'commit', label: 'COMMIT', description: 'Write commit record to log' },
    { id: 'apply', label: 'APPLY', description: 'Write actual data to disk' },
    { id: 'complete', label: 'COMPLETE', description: 'Transaction finished' },
  ]}
  transitions={[
    { from: 'begin', to: 'log', label: 'for each block' },
    { from: 'log', to: 'commit', label: 'all logged' },
    { from: 'commit', to: 'apply', label: 'committed' },
    { from: 'apply', to: 'complete', label: 'all applied' },
  ]}
/>

### Log Record Format

```c
struct wal_record {
  uint32_t txn_id;              /* Transaction ID */
  enum { WAL_BEGIN, WAL_DATA, WAL_COMMIT } type;
  block_sector_t sector;        /* For DATA records */
  uint8_t old_data[512];        /* Before image */
  uint8_t new_data[512];        /* After image */
};
```

### Recovery Algorithm

On startup after crash:

```c
void wal_recover(void) {
  /* Phase 1: REDO committed transactions */
  for (each transaction in log) {
    if (has_commit_record(txn)) {
      for (each DATA record in txn) {
        write_sector(record->sector, record->new_data);
      }
    }
  }

  /* Phase 2: UNDO uncommitted transactions */
  for (each transaction in log) {
    if (!has_commit_record(txn)) {
      for (each DATA record in txn, reverse order) {
        write_sector(record->sector, record->old_data);
      }
    }
  }

  /* Clear log */
  wal_clear();
}
```

### Checkpoint

Periodically truncate the log:

```c
void wal_checkpoint(void) {
  /* Ensure all data is on disk */
  cache_flush();

  /* Clear the log since all transactions are durable */
  wal_clear();
}
```

## Testing

### Run All Filesystem Tests

```bash
cd src/filesys
make check
```

### Key Test Categories

```bash
# Buffer cache
make tests/filesys/extended/syn-read.result
make tests/filesys/extended/syn-write.result

# File growth
make tests/filesys/extended/grow-create.result
make tests/filesys/extended/grow-seq-sm.result
make tests/filesys/extended/grow-file-size.result

# Subdirectories
make tests/filesys/extended/dir-mkdir.result
make tests/filesys/extended/dir-rm-tree.result

# Crash consistency (WAL)
make tests/filesys/extended/wal-basic.result
make tests/filesys/extended/wal-multi-file.result
```

## Common Issues

### Cache Deadlock

**Problem**: Thread A holds cache entry for sector X, waits for Y. Thread B holds Y, waits for X.

**Solution**: Always acquire cache entries in sector order, or use try-lock with retry.

### Indirect Block Allocation Failure

**Problem**: File growth fails partway through, leaving inconsistent state.

**Solution**: Use WAL to make file growth atomic. If allocation fails, abort entire transaction.

### Directory Not Empty Check

**Problem**: `rmdir` on non-empty directory corrupts filesystem.

**Solution**: Check that directory only contains `.` and `..` before removal.

### Path Resolution Stack Overflow

**Problem**: Deep directory trees cause kernel stack overflow.

**Solution**: Use iterative path resolution with loop, not recursion.

## Next Steps

After completing this project:

- [System Calls Concept](/docs/concepts/system-calls) - File system syscall interface
- [WAL Crash Recovery Deep Dive](/docs/deep-dives/wal-crash-recovery) - Detailed recovery algorithm
