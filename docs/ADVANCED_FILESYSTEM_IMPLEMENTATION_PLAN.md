# Advanced File System Implementation Plan

This document outlines potential enhancements to the Pintos file system based on advanced concepts from operating systems research. Each section includes theoretical background, implementation strategy, and references to foundational papers.

---

## Table of Contents

1. [Current Architecture Summary](#current-architecture-summary)
2. [Write-Ahead Logging (Journaling)](#1-write-ahead-logging-journaling)
3. [Metadata-Only Journaling](#2-metadata-only-journaling)
4. [Copy-on-Write Semantics](#3-copy-on-write-semantics)
5. [Log-Structured File System](#4-log-structured-file-system)
6. [RAID-like Redundancy](#5-raid-like-redundancy)
7. [Implementation Priority Matrix](#implementation-priority-matrix)
8. [References](#references)

---

## Current Architecture Summary

The Pintos file system currently implements:

| Component | Status | Location |
|-----------|--------|----------|
| Buffer Cache | Complete | `filesys/cache.c` (64 entries, clock eviction) |
| Read-Ahead Prefetching | Complete | `filesys/cache_prefetch.c` |
| Write-Back Caching | Complete | 30-second periodic flusher |
| Indexed Inodes | Complete | Direct + indirect + doubly-indirect blocks |
| Directory Hierarchy | Complete | Full path traversal with `.` and `..` |
| Transaction/Journaling | **Not Implemented** | — |
| Crash Recovery | **Not Implemented** | — |

**Current Layer Stack:**
```
┌─────────────────────────────┐
│   User Programs (syscalls)  │
├─────────────────────────────┤
│   filesys.c (operations)    │
├─────────────────────────────┤
│   inode.c / directory.c     │
├─────────────────────────────┤
│   cache.c (buffer cache)    │
├─────────────────────────────┤
│   block.c (device layer)    │
├─────────────────────────────┤
│   ide.c (hardware driver)   │
└─────────────────────────────┘
```

---

## 1. Write-Ahead Logging (Journaling)

### Concept Overview

Write-Ahead Logging (WAL) ensures atomicity of file system operations by recording intended changes to a log before applying them to the actual file system structures. If a crash occurs, the system can replay committed transactions or discard incomplete ones.

**Key Invariant:** No data page is written to disk until all log records describing changes to that page have been forced to the log.

### Theoretical Background

The ARIES (Algorithms for Recovery and Isolation Exploiting Semantics) algorithm, developed by C. Mohan et al. at IBM Research, formalized WAL concepts that are used in virtually all modern database and file systems. Key concepts include:

- **Log Sequence Numbers (LSN):** Unique, monotonically increasing identifiers for log records
- **Write-Ahead Protocol:** Log records must reach disk before corresponding data pages
- **Redo/Undo Logging:** Support for replaying committed and undoing uncommitted transactions
- **Checkpointing:** Periodic snapshots to limit recovery time

### Implementation Strategy for Pintos

#### Phase 1: Log Infrastructure (~300 lines)

**New Files:**
- `filesys/log.h` - Log record structures and API
- `filesys/log.c` - Log management implementation

**Data Structures:**

```c
/* Log record types */
enum log_record_type {
  LOG_BEGIN,        /* Transaction start */
  LOG_COMMIT,       /* Transaction committed */
  LOG_ABORT,        /* Transaction aborted */
  LOG_UPDATE,       /* Data modification */
  LOG_CHECKPOINT    /* Checkpoint marker */
};

/* Log record header (fits in one sector with payload) */
struct log_record {
  uint32_t lsn;                    /* Log sequence number */
  uint32_t prev_lsn;               /* Previous LSN in same transaction */
  uint32_t txn_id;                 /* Transaction identifier */
  enum log_record_type type;       /* Record type */
  block_sector_t sector;           /* Affected sector (for UPDATE) */
  uint16_t offset;                 /* Offset within sector */
  uint16_t length;                 /* Length of change */
  uint8_t old_data[256];           /* Before image (for undo) */
  uint8_t new_data[256];           /* After image (for redo) */
};

/* In-memory log state */
struct log_state {
  struct lock lock;                /* Protects log state */
  block_sector_t log_start;        /* First sector of log region */
  block_sector_t log_end;          /* Last sector of log region */
  uint32_t head;                   /* Next write position */
  uint32_t tail;                   /* Oldest active transaction */
  uint32_t current_lsn;            /* Next LSN to assign */
  uint32_t next_txn_id;            /* Next transaction ID */
};
```

**Reserved Disk Layout:**

```
┌────────────────────────────────────────────────────────────┐
│ Sector 0: Free-map inode                                   │
│ Sector 1: Root directory inode                             │
│ Sectors 2-257: Journal log (256 sectors = 128KB)           │
│ Sectors 258+: File system data                             │
└────────────────────────────────────────────────────────────┘
```

#### Phase 2: Transaction API (~200 lines)

```c
/* Transaction control API */
struct transaction *txn_begin(void);
void txn_commit(struct transaction *txn);
void txn_abort(struct transaction *txn);

/* Logged operations (replace direct cache writes) */
void txn_write(struct transaction *txn, block_sector_t sector,
               const void *data, size_t offset, size_t size);

/* Recovery */
void log_recover(void);  /* Called at mount time */
```

#### Phase 3: Integration with File Operations (~300 lines)

Wrap existing multi-step operations in transactions:

**Example: Atomic File Creation**

```c
bool filesys_create(const char *name, off_t initial_size, bool is_dir) {
  struct transaction *txn = txn_begin();

  /* Step 1: Allocate inode sector */
  block_sector_t inode_sector;
  if (!free_map_allocate_txn(txn, 1, &inode_sector)) {
    txn_abort(txn);
    return false;
  }

  /* Step 2: Initialize inode */
  if (!inode_create_txn(txn, inode_sector, initial_size, is_dir)) {
    txn_abort(txn);
    return false;
  }

  /* Step 3: Add directory entry */
  if (!dir_add_txn(txn, dir, name, inode_sector)) {
    txn_abort(txn);
    return false;
  }

  txn_commit(txn);  /* Atomic commit point */
  return true;
}
```

#### Phase 4: Crash Recovery (~200 lines)

```c
void log_recover(void) {
  /* Phase 1: Analysis - scan log to find active transactions */
  struct list active_txns;
  scan_log_for_active_transactions(&active_txns);

  /* Phase 2: Redo - replay all committed transactions */
  redo_committed_transactions();

  /* Phase 3: Undo - rollback uncommitted transactions */
  undo_uncommitted_transactions(&active_txns);

  /* Phase 4: Write checkpoint */
  write_checkpoint();
}
```

### Complexity Assessment

| Aspect | Estimate |
|--------|----------|
| New code | ~1000 lines |
| Files modified | 5-7 files |
| Difficulty | Medium-High |
| Risk | Medium (requires careful testing) |

---

## 2. Metadata-Only Journaling

### Concept Overview

A lighter-weight alternative to full data journaling that only logs changes to file system metadata (inodes, directories, free maps), not file contents. This provides structural consistency while avoiding the overhead of writing data twice.

This is the default mode for ext4 (`data=ordered`) and most production file systems.

### Trade-offs

| Mode | What's Logged | Performance | Data Safety |
|------|---------------|-------------|-------------|
| Full Journal | Metadata + Data | Slowest | Highest |
| Ordered | Metadata only (data written first) | Medium | Medium |
| Writeback | Metadata only (any order) | Fastest | Lowest |

### Implementation Strategy for Pintos

This is a simplified version of full journaling:

#### Changes from Full Journaling

1. **Only log these operations:**
   - Free-map allocations/deallocations
   - Inode creation/modification/deletion
   - Directory entry additions/removals

2. **Data writes go directly to cache** (not through log)

3. **Ordered mode implementation:**
   ```c
   void txn_commit(struct transaction *txn) {
     /* Step 1: Flush all dirty data blocks for files in this txn */
     flush_transaction_data_blocks(txn);

     /* Step 2: Write metadata log records */
     write_log_records(txn);

     /* Step 3: Write commit record */
     write_commit_record(txn);

     /* Step 4: Apply metadata changes to actual locations */
     apply_metadata_changes(txn);
   }
   ```

### Complexity Assessment

| Aspect | Estimate |
|--------|----------|
| New code | ~600 lines |
| Files modified | 4-5 files |
| Difficulty | Medium |
| Risk | Low-Medium |

---

## 3. Copy-on-Write Semantics

### Concept Overview

Instead of modifying data blocks in place, copy-on-write (COW) file systems:
1. Allocate a new block
2. Write the modified data to the new block
3. Update the pointer atomically
4. Free the old block (or keep it for snapshots)

This approach, pioneered by ZFS and Btrfs, provides:
- **Atomic updates:** Pointer update is a single sector write
- **Snapshots:** Old blocks can be preserved for point-in-time copies
- **Self-healing:** Combined with checksums, corrupted blocks can be detected

### Theoretical Background

ZFS introduced the concept of a "Merkle tree" of checksums where every block is verified, and copy-on-write ensures that writes never corrupt existing data.

### Implementation Strategy for Pintos

#### Phase 1: COW Block Writes (~250 lines)

Modify `inode_write_at()` to use COW:

```c
off_t inode_write_at(struct inode *inode, const void *buffer,
                     off_t size, off_t offset) {
  /* For each block being written: */
  for each block {
    block_sector_t old_sector = get_block_sector(inode, block_idx);

    /* Allocate new sector */
    block_sector_t new_sector;
    if (!free_map_allocate(1, &new_sector))
      return bytes_written;

    /* If partial block, read old data first */
    if (partial_write) {
      cache_read(old_sector, bounce_buffer);
      memcpy(bounce_buffer + offset, buffer, size);
    } else {
      memcpy(bounce_buffer, buffer, size);
    }

    /* Write to new location */
    cache_write(new_sector, bounce_buffer);

    /* Update inode pointer (atomic) */
    update_block_pointer(inode, block_idx, new_sector);

    /* Free old sector */
    if (old_sector != 0)
      free_map_release(old_sector, 1);
  }
}
```

#### Phase 2: Atomic Inode Updates (~200 lines)

The inode itself must be updated atomically:

```c
struct inode_disk {
  /* ... existing fields ... */
  uint32_t generation;        /* Incremented on each update */
  uint32_t checksum;          /* CRC32 of inode contents */
};

void inode_update_atomic(struct inode *inode) {
  /* Allocate new inode sector */
  block_sector_t new_sector;
  free_map_allocate(1, &new_sector);

  /* Write new inode with incremented generation */
  inode->data.generation++;
  inode->data.checksum = compute_checksum(&inode->data);
  cache_write(new_sector, &inode->data);

  /* Update parent pointer (directory entry or indirect block) */
  update_parent_pointer(inode, new_sector);

  /* Free old inode sector */
  free_map_release(inode->sector, 1);
  inode->sector = new_sector;
}
```

#### Phase 3: Snapshot Support (Optional, ~400 lines)

```c
/* Snapshot descriptor */
struct snapshot {
  uint32_t id;
  uint32_t timestamp;
  block_sector_t root_inode;   /* Root inode at snapshot time */
  struct snapshot *next;
};

/* Create snapshot - just save current root pointer */
struct snapshot *snapshot_create(void) {
  struct snapshot *snap = malloc(sizeof *snap);
  snap->root_inode = get_root_inode_sector();
  snap->timestamp = timer_ticks();
  /* Mark all current blocks as referenced by snapshot */
  increment_block_refcounts();
  return snap;
}
```

### Complexity Assessment

| Aspect | Estimate |
|--------|----------|
| New code | ~600-1000 lines |
| Files modified | 3-5 files |
| Difficulty | Medium-High |
| Risk | Medium (fragmentation concerns) |

---

## 4. Log-Structured File System

### Concept Overview

In a log-structured file system (LFS), the entire disk is treated as a circular log. All writes (data and metadata) are appended sequentially, optimizing for write performance. This approach is particularly effective for workloads with many small writes.

### Theoretical Background

The seminal paper by Rosenblum and Ousterhout (1992) demonstrated that LFS could achieve 10x better write performance than traditional Unix file systems for small files, while matching read performance through effective use of caching.

**Key insight:** With large buffer caches, most reads are served from memory, so disk access patterns are dominated by writes. Sequential writes are much faster than random writes.

### Architecture

```
Traditional FS:              Log-Structured FS:
┌─────┬─────┬─────┐          ┌─────────────────────────────────┐
│inode│inode│data │          │ Log: i1 d1 d2 i2 d3 i3 d4 ...   │
├─────┼─────┼─────┤          │      ──────────────────────────►│
│data │free │data │          │      (all writes sequential)    │
├─────┼─────┼─────┤          └─────────────────────────────────┘
│data │data │data │
└─────┴─────┴─────┘
```

### Implementation Strategy for Pintos

**WARNING:** This requires a fundamental redesign of the file system. It is not recommended as an incremental change but documented here for completeness.

#### Major Components

1. **Segment Manager:**
   - Disk divided into large segments (e.g., 512KB each)
   - Segments are the unit of cleaning

2. **Segment Summary Blocks:**
   - Each segment starts with a summary describing its contents
   - Maps logical blocks to physical locations

3. **Inode Map:**
   - Indirection table mapping inode numbers to current disk locations
   - Inodes move on every update (COW property)

4. **Segment Cleaner:**
   - Background thread that reclaims fragmented segments
   - Copies live blocks to new segment, frees old segment

#### Pseudocode for Log Write

```c
void lfs_write(struct inode *inode, const void *data, size_t size, off_t offset) {
  acquire_segment_lock();

  /* 1. Allocate space in current segment */
  block_sector_t data_sector = current_segment_alloc(1);

  /* 2. Write data block */
  segment_write(data_sector, data);

  /* 3. Update inode (also in log) */
  inode->data.direct[block_idx] = data_sector;
  block_sector_t inode_sector = current_segment_alloc(1);
  segment_write(inode_sector, &inode->data);

  /* 4. Update inode map */
  inode_map[inode->inum] = inode_sector;

  /* 5. Update segment summary */
  segment_summary_add(data_sector, BLOCK_DATA, inode->inum, block_idx);
  segment_summary_add(inode_sector, BLOCK_INODE, inode->inum, 0);

  /* 6. If segment full, start new segment */
  if (segment_full())
    start_new_segment();

  release_segment_lock();
}
```

### Complexity Assessment

| Aspect | Estimate |
|--------|----------|
| New code | ~2000-3000 lines |
| Files modified | Complete rewrite of inode.c, free-map.c |
| Difficulty | Very High |
| Risk | High (entire FS redesign) |

---

## 5. RAID-like Redundancy

### Concept Overview

RAID (Redundant Array of Independent Disks) provides data protection through redundancy across multiple disks. The original paper by Patterson, Gibson, and Katz defined several RAID levels:

| Level | Description | Overhead | Fault Tolerance |
|-------|-------------|----------|-----------------|
| RAID 0 | Striping only | 0% | None |
| RAID 1 | Mirroring | 100% | 1 disk |
| RAID 5 | Striping + distributed parity | 1 disk | 1 disk |
| RAID 6 | Striping + double parity | 2 disks | 2 disks |

### Implementation Strategy for Pintos

Since Pintos typically uses a single virtual disk, RAID implementation would be primarily educational. We can simulate multiple disks by partitioning a single disk.

#### Phase 1: Virtual Disk Abstraction (~300 lines)

```c
/* Virtual disk that spans multiple underlying disks */
struct raid_device {
  enum raid_level level;       /* RAID 0, 1, 5, or 6 */
  size_t num_disks;
  struct block *disks[MAX_RAID_DISKS];
  size_t stripe_size;          /* Blocks per stripe unit */
  size_t total_sectors;
};

/* RAID block operations */
void raid_read(struct raid_device *raid, block_sector_t sector, void *buffer);
void raid_write(struct raid_device *raid, block_sector_t sector, const void *buffer);
```

#### Phase 2: RAID 1 (Mirroring) (~200 lines)

```c
void raid1_write(struct raid_device *raid, block_sector_t sector,
                 const void *buffer) {
  /* Write to all mirrors */
  for (size_t i = 0; i < raid->num_disks; i++) {
    block_write(raid->disks[i], sector, buffer);
  }
}

void raid1_read(struct raid_device *raid, block_sector_t sector,
                void *buffer) {
  /* Read from first available disk */
  /* Could implement read balancing here */
  block_read(raid->disks[0], sector, buffer);
}
```

#### Phase 3: RAID 5 (Striping + Parity) (~400 lines)

```c
/* XOR parity calculation */
void compute_parity(void *parity, void **data_blocks, size_t num_blocks) {
  memset(parity, 0, BLOCK_SECTOR_SIZE);
  for (size_t i = 0; i < num_blocks; i++) {
    for (size_t j = 0; j < BLOCK_SECTOR_SIZE; j++) {
      ((uint8_t *)parity)[j] ^= ((uint8_t *)data_blocks[i])[j];
    }
  }
}

void raid5_write(struct raid_device *raid, block_sector_t sector,
                 const void *buffer) {
  /* Calculate stripe and parity disk */
  size_t stripe = sector / raid->stripe_size;
  size_t parity_disk = stripe % raid->num_disks;
  size_t data_disk = (sector % (raid->num_disks - 1));
  if (data_disk >= parity_disk) data_disk++;

  /* Read old data and parity */
  void *old_data = malloc(BLOCK_SECTOR_SIZE);
  void *old_parity = malloc(BLOCK_SECTOR_SIZE);
  block_read(raid->disks[data_disk], sector_in_disk, old_data);
  block_read(raid->disks[parity_disk], parity_sector, old_parity);

  /* Compute new parity: P' = P XOR old_data XOR new_data */
  void *new_parity = malloc(BLOCK_SECTOR_SIZE);
  for (size_t i = 0; i < BLOCK_SECTOR_SIZE; i++) {
    ((uint8_t *)new_parity)[i] = ((uint8_t *)old_parity)[i] ^
                                  ((uint8_t *)old_data)[i] ^
                                  ((uint8_t *)buffer)[i];
  }

  /* Write new data and parity */
  block_write(raid->disks[data_disk], sector_in_disk, buffer);
  block_write(raid->disks[parity_disk], parity_sector, new_parity);

  free(old_data);
  free(old_parity);
  free(new_parity);
}
```

### Complexity Assessment

| Aspect | Estimate |
|--------|----------|
| New code | ~600-1000 lines |
| Files modified | 2-3 files (new raid.c, modify block.c) |
| Difficulty | Medium |
| Risk | Low (isolated from rest of FS) |

---

## Implementation Priority Matrix

Based on educational value, complexity, and integration with existing code:

| Feature | Priority | Complexity | Educational Value | Prerequisites |
|---------|----------|------------|-------------------|---------------|
| Metadata-Only Journaling | **1 (High)** | Medium | High | None |
| Write-Ahead Logging | **2 (High)** | Medium-High | Very High | None |
| RAID 1 Mirroring | **3 (Medium)** | Low | Medium | None |
| Copy-on-Write | **4 (Medium)** | Medium-High | High | Journaling recommended |
| RAID 5 Parity | **5 (Low)** | Medium | Medium | RAID 1 |
| Log-Structured FS | **6 (Low)** | Very High | Very High | Complete redesign |

### Recommended Implementation Path

```
┌─────────────────────────────────────────────────────────────────┐
│ Step 1: Metadata-Only Journaling                                │
│   - Provides crash consistency for file system structure        │
│   - Foundation for more advanced features                       │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Step 2: Full Write-Ahead Logging (Optional)                     │
│   - Extends metadata journaling to include data                 │
│   - Complete ACID guarantees                                    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Step 3: Copy-on-Write (Optional)                                │
│   - Enables snapshots and atomic updates                        │
│   - Works well with journaling for hybrid approach              │
└─────────────────────────────────────────────────────────────────┘
```

---

## References

### Foundational Papers

1. **ARIES: Write-Ahead Logging**
   - Mohan, C., et al. "ARIES: A Transaction Recovery Method Supporting Fine-Granularity Locking and Partial Rollbacks Using Write-Ahead Logging." *ACM Transactions on Database Systems*, 1992.
   - [Stanford CS345D PDF](https://web.stanford.edu/class/cs345d-01/rl/aries.pdf)
   - [ACM Digital Library](https://dl.acm.org/doi/10.1145/128765.128770)
   - [IBM Research Publication](https://research.ibm.com/publications/aries-a-transaction-recovery-method-supporting-fine-granularity-locking-and-partial-rollbacks-using-write-ahead-logging)

2. **Log-Structured File Systems**
   - Rosenblum, M. and Ousterhout, J. "The Design and Implementation of a Log-Structured File System." *ACM Transactions on Computer Systems*, 1992.
   - [Stanford PDF](https://web.stanford.edu/~ouster/cgi-bin/papers/lfs.pdf)
   - [ACM Digital Library](https://dl.acm.org/doi/10.1145/146941.146943)
   - [Berkeley CS262 PDF](https://people.eecs.berkeley.edu/~brewer/cs262/LFS.pdf)

3. **RAID**
   - Patterson, D., Gibson, G., and Katz, R. "A Case for Redundant Arrays of Inexpensive Disks (RAID)." *SIGMOD Conference*, 1988.
   - [CMU PDF](https://www.cs.cmu.edu/~garth/RAIDpaper/Patterson88.pdf)
   - [UC Berkeley Tech Report](https://www2.eecs.berkeley.edu/Pubs/TechRpts/1987/CSD-87-391.pdf)
   - [Computer History Museum](https://www.computerhistory.org/storageengine/u-c-berkeley-paper-catalyses-interest-in-raid/)

4. **ZFS and Copy-on-Write**
   - Bonwick, J. and Ahrens, M. "The Zettabyte File System."
   - [Semantic Scholar](https://www.semanticscholar.org/paper/The-Zettabyte-File-System-Bonwick-Ahrens/27f81148ecbcd04dd97cebd717c8921e5f2a4373)
   - [COW File Systems Analysis (M.Sc. Thesis)](https://www.sakisk.me/files/copy-on-write-based-file-systems.pdf)

5. **Btrfs**
   - Rodeh, O., Bacik, J., and Mason, C. "BTRFS: The Linux B-Tree Filesystem." *ACM Transactions on Storage*, 2013.
   - [ACM Digital Library](https://dl.acm.org/doi/10.1145/2501620.2501623)

### Linux Kernel Documentation

6. **ext4 Journaling**
   - [Linux Kernel Journal Documentation](https://www.kernel.org/doc/html/latest/filesystems/ext4/journal.html)
   - [GitHub: ext4 journal.rst](https://github.com/torvalds/linux/blob/master/Documentation/filesystems/ext4/journal.rst)
   - [LWN: Fast Commits for ext4](https://lwn.net/Articles/842385/)

### Educational Resources

7. **OSTEP (Operating Systems: Three Easy Pieces)**
   - [Chapter 43: Log-structured File Systems](https://pages.cs.wisc.edu/~remzi/OSTEP/file-lfs.pdf)
   - Free online textbook with excellent coverage of file system concepts

8. **Berkeley CS262A Lecture Slides**
   - [ARIES Lecture Notes](https://people.eecs.berkeley.edu/~kubitron/courses/cs262a-F13/lectures/lec05-aries-rev.pdf)

---

## Appendix: Testing Strategies

### Crash Consistency Testing

To verify journaling implementations, use systematic crash testing:

```c
/* Crash injection points */
enum crash_point {
  CRASH_BEFORE_LOG_WRITE,
  CRASH_AFTER_LOG_WRITE,
  CRASH_BEFORE_COMMIT,
  CRASH_AFTER_COMMIT,
  CRASH_DURING_APPLY
};

/* Test harness */
void test_crash_recovery(enum crash_point cp) {
  /* 1. Start operation */
  filesys_create("test.txt", 100, false);

  /* 2. Inject crash at specified point */
  inject_crash(cp);

  /* 3. Simulate reboot and recovery */
  filesys_done();
  filesys_init(false);  /* Recovery happens here */

  /* 4. Verify consistency */
  assert(filesystem_is_consistent());

  /* 5. Verify expected state based on crash point */
  if (cp < CRASH_AFTER_COMMIT) {
    assert(!file_exists("test.txt"));  /* Should be rolled back */
  } else {
    assert(file_exists("test.txt"));   /* Should be committed */
  }
}
```

### Performance Benchmarks

Compare implementations using these workloads:

1. **Small File Workload:** Create/delete 1000 small files
2. **Large Sequential Write:** Write 1MB file sequentially
3. **Random Write:** Random 4KB writes across large file
4. **Metadata-Heavy:** Directory operations (mkdir, rmdir, rename)

---

*Document created: December 2024*
*Last updated: December 2024*
*Status: Planning phase - awaiting implementation decisions*
