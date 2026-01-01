# Write-Ahead Logging (WAL) Implementation Guide

## Table of Contents
1. [Introduction: Why Do We Need WAL?](#1-introduction-why-do-we-need-wal)
2. [The Problem: Crash Consistency](#2-the-problem-crash-consistency)
3. [The Solution: Write-Ahead Logging](#3-the-solution-write-ahead-logging)
4. [Key Concepts](#4-key-concepts)
5. [Our Implementation Architecture](#5-our-implementation-architecture)
6. [Data Structures](#6-data-structures)
7. [The Transaction Lifecycle](#7-the-transaction-lifecycle)
8. [Recovery: REDO and UNDO](#8-recovery-redo-and-undo)
9. [Checkpointing](#9-checkpointing)
10. [Important Engineering Lessons](#10-important-engineering-lessons)
11. [Common Pitfalls and How to Avoid Them](#11-common-pitfalls-and-how-to-avoid-them)
12. [Testing WAL Systems](#12-testing-wal-systems)
13. [Summary](#13-summary)

---

## 1. Introduction: Why Do We Need WAL?

Imagine you're transferring $100 from Account A to Account B. This requires two operations:
1. Subtract $100 from Account A
2. Add $100 to Account B

**What happens if the power goes out after step 1 but before step 2?**

Without protection, you've just lost $100 into thin air. The money left Account A but never arrived at Account B. This is the **crash consistency problem**, and Write-Ahead Logging (WAL) is one of the most elegant solutions.

### The Core Principle

> **"Write the log first, then the data."**

Before making ANY change to actual data, we first write a record of what we're about to do (and what the old value was) to a separate log. Only after that log record is safely on disk do we modify the actual data.

This simple principle enables:
- **Atomicity**: All changes happen together, or none happen at all
- **Durability**: Once we say "committed," the data survives any crash
- **Recoverability**: After a crash, we can reconstruct a consistent state

---

## 2. The Problem: Crash Consistency

### 2.1 Why Filesystems Are Vulnerable

Modern filesystems maintain complex data structures:
- **Inodes**: Metadata about files (size, permissions, block pointers)
- **Data blocks**: Actual file contents
- **Free maps**: Track which blocks are in use
- **Directories**: Map names to inodes

A single "simple" operation like creating a file requires updating multiple structures:
1. Allocate an inode
2. Initialize the inode
3. Add entry to parent directory
4. Update free map

If a crash occurs mid-operation, these structures become **inconsistent**. For example:
- An inode might be marked as used, but no directory points to it (orphan)
- A directory might point to an uninitialized inode (corruption)
- The free map might not match actual usage (space leak or double-allocation)

### 2.2 The Torn Write Problem

Even a single sector write isn't atomic at the hardware level. A 512-byte sector write could be interrupted, leaving the sector with a mix of old and new data. This is called a **torn write**.

### 2.3 The Ordering Problem

Modern systems have multiple layers of caching:
- CPU caches
- OS buffer cache
- Disk write cache

Writes can be **reordered** for performance. If you write A then B, the disk might actually write B then A. This reordering is invisible during normal operation but catastrophic during crashes.

---

## 3. The Solution: Write-Ahead Logging

### 3.1 The Basic Idea

```
Instead of:                    We do:
┌─────────────┐               ┌─────────────┐
│ Write Data  │               │ 1. Write to │
│ to Disk     │               │    LOG      │
└─────────────┘               ├─────────────┤
      │                       │ 2. Write to │
      ▼                       │    DATA     │
   (crash?)                   └─────────────┘
   INCONSISTENT!                    │
                                    ▼
                              (crash at any point?)
                              LOG allows RECOVERY
```

### 3.2 What Gets Logged

For each data modification, we log:
- **Transaction ID**: Which transaction made this change
- **Location**: Which sector/offset is being modified
- **Old Data (Before Image)**: What the data looked like before
- **New Data (After Image)**: What the data should look like after
- **Operation Type**: BEGIN, WRITE, COMMIT, ABORT, CHECKPOINT

### 3.3 The WAL Protocol

1. **BEGIN**: Start a transaction, get a unique ID
2. **LOG WRITES**: Before each data modification:
   - Read current data (old_data)
   - Write log record with old_data and new_data
   - Ensure log record is on disk
   - NOW modify the actual data
3. **COMMIT**: Write COMMIT record to log, flush to disk
4. **CLEANUP**: Transaction is now durable

The **commit point** is when the COMMIT record hits the disk. Before that moment, the transaction can be rolled back. After that moment, the transaction is guaranteed to survive any crash.

---

## 4. Key Concepts

### 4.1 ACID Properties

WAL helps achieve the ACID properties of transactions:

| Property | Meaning | How WAL Helps |
|----------|---------|---------------|
| **Atomicity** | All or nothing | UNDO rolls back partial transactions |
| **Consistency** | Valid state to valid state | Recovery restores consistent state |
| **Isolation** | Transactions don't interfere | Each transaction has unique ID |
| **Durability** | Committed = permanent | COMMIT forces log to disk |

### 4.2 Log Sequence Numbers (LSN)

Every log record gets a unique, monotonically increasing **Log Sequence Number (LSN)**. LSNs are crucial for:
- Ordering operations correctly during recovery
- Determining which records have been flushed to disk
- Implementing checkpoints

```c
typedef uint64_t lsn_t;  // 64 bits = plenty of headroom
```

### 4.3 The Steal/No-Force Policy

Our implementation uses the **Steal + No-Force** policy:

- **Steal**: We CAN write uncommitted data to disk (the buffer cache can evict dirty pages from active transactions)
- **No-Force**: We DON'T have to write all data to disk at commit time (only the log must be flushed)

This policy requires both REDO and UNDO during recovery:
- **REDO**: Because committed data might not be on disk (no-force)
- **UNDO**: Because uncommitted data might be on disk (steal)

### 4.4 Before and After Images

Each WRITE log record contains:
- **Before Image (old_data)**: Used for UNDO if transaction aborts
- **After Image (new_data)**: Used for REDO if transaction committed

```c
struct wal_record {
    // ... header fields ...
    uint8_t old_data[WAL_MAX_DATA_SIZE];  // Before image (for UNDO)
    uint8_t new_data[WAL_MAX_DATA_SIZE];  // After image (for REDO)
};
```

---

## 5. Our Implementation Architecture

### 5.1 Disk Layout

```
Sector 0:       Free map inode
Sector 1:       Root directory inode
Sectors 2-65:   WAL log (64 sectors, circular buffer)
Sector 66:      WAL metadata
Sectors 67+:    Filesystem data
```

**Critical Lesson Learned**: Test data MUST use sectors >= 67 to avoid overwriting the WAL log! Using sectors 50-52 for test data caused mysterious failures because those sectors ARE part of the log.

### 5.2 Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      Application Layer                       │
│                   (file operations, syscalls)                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     Transaction Layer                        │
│              wal_txn_begin() / commit() / abort()           │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                       Logging Layer                          │
│                 wal_log_write() / wal_flush()               │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      Buffer Cache                            │
│              cache_read() / cache_write()                    │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      Block Device                            │
│                block_read() / block_write()                  │
└─────────────────────────────────────────────────────────────┘
```

### 5.3 Key Files

| File | Purpose |
|------|---------|
| `wal.h` | Public API, data structures, constants |
| `wal.c` | Implementation of all WAL operations |
| `cache.c` | Buffer cache (integrates with WAL) |

---

## 6. Data Structures

### 6.1 Log Record (On-Disk Format)

```c
struct wal_record {
    /* Header - 20 bytes */
    lsn_t lsn;                  // Unique sequence number (8 bytes)
    txn_id_t txn_id;            // Which transaction (4 bytes)
    enum wal_record_type type;  // BEGIN, WRITE, COMMIT, ABORT, CHECKPOINT (4 bytes)
    uint32_t checksum;          // CRC32 for corruption detection (4 bytes)

    /* For WAL_WRITE records - 8 bytes */
    block_sector_t sector;      // Which disk sector (4 bytes)
    uint16_t offset;            // Offset within sector (0-511) (2 bytes)
    uint16_t length;            // Bytes modified (2 bytes)

    /* Data payload - 464 bytes */
    uint8_t old_data[232];      // Before image (WAL_MAX_DATA_SIZE)
    uint8_t new_data[232];      // After image (WAL_MAX_DATA_SIZE)

    uint8_t padding[20];        // Pad to exactly 512 bytes
};
```

**Why exactly 512 bytes?** Each log record fits in exactly one disk sector. This ensures atomic writes at the hardware level - a record is either fully written or not written at all.

### 6.2 Record Types

```c
enum wal_record_type {
    WAL_INVALID = 0,   // Empty/corrupted slot
    WAL_BEGIN,         // Transaction started
    WAL_COMMIT,        // Transaction committed (durability point!)
    WAL_ABORT,         // Transaction aborted
    WAL_WRITE,         // Data modification
    WAL_CHECKPOINT,    // Checkpoint marker
};
```

### 6.3 Transaction Handle

```c
struct wal_txn {
    txn_id_t txn_id;        // Unique identifier
    enum txn_state state;   // ACTIVE, COMMITTED, or ABORTED
    lsn_t first_lsn;        // LSN of BEGIN record
    lsn_t last_lsn;         // LSN of most recent record
    struct list_elem elem;  // For active transaction list
};
```

### 6.4 WAL Manager (In-Memory State)

```c
struct wal_manager {
    struct lock wal_lock;      // Protects all WAL state

    /* LSN management */
    lsn_t next_lsn;            // Next LSN to assign
    lsn_t flushed_lsn;         // All records <= this are on disk

    /* Transaction ID management */
    txn_id_t next_txn_id;      // Next transaction ID to assign

    /* Log buffer (in-memory, flushed to disk periodically) */
    uint8_t *log_buffer;       // Buffer for log records
    size_t buffer_size;        // Size of the buffer (8 sectors = 4KB)
    size_t buffer_used;        // Bytes currently in buffer

    /* Active transaction tracking */
    struct list active_txns;   // List of active transactions

    /* Checkpoint management */
    lsn_t checkpoint_lsn;      // LSN of last checkpoint (0 if none)
    bool checkpointing;        // Prevents recursive checkpoint calls

    /* Statistics (for testing/verification) */
    uint32_t stats_txn_begun;
    uint32_t stats_txn_committed;
    uint32_t stats_txn_aborted;
    uint32_t stats_writes_logged;
};
```

### 6.5 Statistics Structure

```c
struct wal_stats {
    uint32_t txn_begun;      // Number of transactions started
    uint32_t txn_committed;  // Number of transactions committed
    uint32_t txn_aborted;    // Number of transactions aborted
    uint32_t writes_logged;  // Number of write operations logged
};
```

Use `wal_get_stats()` to retrieve current statistics and `wal_reset_stats()` to clear them.

### 6.6 Metadata (Persistent Across Reboots)

```c
struct wal_metadata {
    uint32_t magic;            // 0xDEADBEEF = valid metadata (4 bytes)
    uint32_t clean_shutdown;   // 1 = clean, 0 = need recovery (4 bytes)
    lsn_t last_lsn;            // Last LSN before shutdown (8 bytes)
    txn_id_t last_txn_id;      // Last transaction ID (4 bytes)
    uint8_t padding[492];      // Pad to exactly 512 bytes
};
```

---

## 7. The Transaction Lifecycle

### 7.1 Beginning a Transaction

```c
struct wal_txn *wal_txn_begin(void) {
    struct wal_txn *txn = malloc(sizeof(struct wal_txn));

    lock_acquire(&wal.wal_lock);
    txn->txn_id = wal.next_txn_id++;  // Unique ID
    lock_release(&wal.wal_lock);

    txn->state = TXN_ACTIVE;

    // Write BEGIN record to log
    struct wal_record rec = {0};
    rec.type = WAL_BEGIN;
    rec.txn_id = txn->txn_id;
    lsn_t begin_lsn = wal_append_record(&rec);

    txn->first_lsn = begin_lsn;
    txn->last_lsn = begin_lsn;

    // Track this transaction
    list_push_back(&wal.active_txns, &txn->elem);

    return txn;
}
```

### 7.2 Logging a Write

```c
bool wal_log_write(struct wal_txn *txn, block_sector_t sector,
                   const void *old_data, const void *new_data,
                   uint16_t offset, uint16_t length) {

    // Split large writes into multiple records
    // (each record can hold max 232 bytes of data)
    while (bytes_logged < length) {
        uint16_t chunk = min(length - bytes_logged, WAL_MAX_DATA_SIZE);

        struct wal_record rec = {0};
        rec.type = WAL_WRITE;
        rec.txn_id = txn->txn_id;
        rec.sector = sector;
        rec.offset = offset + bytes_logged;
        rec.length = chunk;

        memcpy(rec.old_data, old_data + bytes_logged, chunk);
        memcpy(rec.new_data, new_data + bytes_logged, chunk);

        txn->last_lsn = wal_append_record(&rec);
        bytes_logged += chunk;
    }
    return true;
}
```

**Key Point**: The log record is written BEFORE the actual data is modified. This is the "write-ahead" in Write-Ahead Logging.

### 7.3 Committing a Transaction

```c
bool wal_txn_commit(struct wal_txn *txn) {
    // Write COMMIT record
    struct wal_record rec = {0};
    rec.type = WAL_COMMIT;
    rec.txn_id = txn->txn_id;
    lsn_t commit_lsn = wal_append_record(&rec);

    // THIS IS THE CRITICAL STEP: Force log to disk
    wal_flush(commit_lsn);

    // After this point, the transaction is DURABLE
    txn->state = TXN_COMMITTED;
    list_remove(&txn->elem);
    free(txn);

    return true;
}
```

**The Durability Guarantee**: Once `wal_flush()` returns, the COMMIT record is on disk. Even if the system crashes immediately after, recovery will see the COMMIT and ensure all changes are applied.

### 7.4 Aborting a Transaction (UNDO)

```c
void wal_txn_abort(struct wal_txn *txn) {
    // Use HEAP allocation to avoid stack overflow (4KB kernel stack)
    #define MAX_UNDO_RECORDS 64
    lsn_t *undo_lsns = malloc(MAX_UNDO_RECORDS * sizeof(lsn_t));
    size_t undo_count = 0;

    // Flush log buffer so we can read records from disk
    wal_flush(wal.next_lsn);

    // Collect all WRITE records for this transaction
    for (lsn_t lsn = txn->first_lsn; lsn < wal.next_lsn; lsn++) {
        struct wal_record rec;
        if (wal_read_record(lsn, &rec) &&
            rec.txn_id == txn->txn_id &&
            rec.type == WAL_WRITE) {
            undo_lsns[undo_count++] = lsn;
        }
    }

    // Apply UNDO in REVERSE order (newest first)
    for (size_t i = undo_count; i > 0; i--) {
        wal_read_record(undo_lsns[i - 1], &rec);
        // Restore the old_data through the cache
        cache_write(rec.sector, rec.old_data, rec.offset, rec.length);
    }

    // Flush cache to ensure UNDO is on disk
    cache_flush();

    // Write ABORT record
    rec.type = WAL_ABORT;
    rec.txn_id = txn->txn_id;
    wal_append_record(&rec);

    free(undo_lsns);
    txn->state = TXN_ABORTED;
    list_remove(&txn->elem);  // Remove from active list
    free(txn);
}
```

**Why Reverse Order?** If a transaction wrote to the same location multiple times (A → B → C), we need to restore A, not B. By processing in reverse LSN order, the last UNDO restores the original value.

### 7.5 Thread-Local Transaction Management

Each thread can have an associated "current transaction" stored in thread-local storage:

```c
void wal_set_current_txn(struct wal_txn *txn) {
    struct thread *t = thread_current();
    t->current_txn = txn;
}

struct wal_txn *wal_get_current_txn(void) {
    struct thread *t = thread_current();
    return t->current_txn;
}
```

This allows higher-level code to implicitly use the current transaction without passing it through every function call.

---

## 8. Recovery: REDO and UNDO

Recovery runs automatically when the system starts and detects a previous crash (unclean shutdown).

### 8.1 The Three Phases

```
┌─────────────────────────────────────────────────────────────┐
│                    PHASE 1: ANALYSIS                         │
│  Scan the entire log to categorize transactions:            │
│  - Which transactions committed? (have COMMIT record)       │
│  - Which transactions aborted? (have ABORT record)          │
│  - Which were in-flight? (BEGIN but no COMMIT/ABORT)        │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     PHASE 2: REDO                            │
│  For each COMMITTED transaction:                             │
│  - Replay all WRITE records (apply new_data)                │
│  - Ensures committed changes are on disk                    │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     PHASE 3: UNDO                            │
│  For each UNCOMMITTED transaction:                           │
│  - Reverse all WRITE records (apply old_data)               │
│  - Process in reverse LSN order                             │
│  - Removes effects of incomplete transactions               │
└─────────────────────────────────────────────────────────────┘
```

### 8.2 Recovery Implementation

```c
void wal_recover(void) {
    // Arrays to track transaction states
    txn_id_t committed_txns[MAX_TXNS];
    txn_id_t uncommitted_txns[MAX_TXNS];

    /* ============ PHASE 1: ANALYSIS ============ */
    for (each log sector) {
        read record;
        verify checksum;

        if (record.type == WAL_COMMIT)
            mark_committed(record.txn_id);
        else if (record.type == WAL_ABORT)
            mark_aborted(record.txn_id);
        else if (record.type == WAL_BEGIN)
            track_transaction(record.txn_id);
    }

    // Transactions with BEGIN but no COMMIT/ABORT are uncommitted
    categorize_transactions();

    /* ============ PHASE 2: REDO ============ */
    for (each log sector) {
        if (record.type == WAL_WRITE && is_committed(record.txn_id)) {
            // Apply new_data to sector
            block_read(record.sector, buffer);
            memcpy(buffer + record.offset, record.new_data, record.length);
            block_write(record.sector, buffer);
        }
    }

    /* ============ PHASE 3: UNDO ============ */
    // Collect uncommitted WRITE records
    for (each log sector) {
        if (record.type == WAL_WRITE && is_uncommitted(record.txn_id)) {
            add_to_undo_list(record);
        }
    }

    // Sort by LSN descending (newest first)
    sort_by_lsn_descending(undo_list);

    // Apply old_data to undo changes
    for (each record in undo_list) {
        block_read(record.sector, buffer);
        memcpy(buffer + record.offset, record.old_data, record.length);
        block_write(record.sector, buffer);
    }
}
```

### 8.3 Why REDO Even for Committed Transactions?

With our No-Force policy, committed data might NOT be on disk when we crash:

```
Timeline:
1. Transaction writes data to cache
2. Transaction commits (COMMIT record flushed to log)
3. System returns "success" to user
4. CRASH before cache writes data to disk!

After crash:
- Log shows: COMMIT for transaction
- Disk data: Still has OLD value
- Solution: REDO replays the write
```

### 8.4 Why UNDO for Uncommitted Transactions?

With our Steal policy, uncommitted data might BE on disk when we crash:

```
Timeline:
1. Transaction writes data to cache
2. Cache is full, evicts dirty page to disk (STEAL)
3. CRASH before commit!

After crash:
- Log shows: BEGIN, WRITE, but NO COMMIT
- Disk data: Has NEW value (from stolen page)
- Solution: UNDO restores old value
```

---

## 9. Checkpointing

### 9.1 The Problem with Unbounded Recovery

Without checkpoints, recovery must scan the ENTIRE log from the beginning of time. As the log grows, recovery time grows unboundedly.

### 9.2 What a Checkpoint Does

A checkpoint guarantees that all committed data up to that point is on disk:

```c
void wal_checkpoint(void) {
    // 1. Flush all dirty cache pages to disk
    cache_flush();

    // 2. Flush all pending log records
    wal_flush(wal.next_lsn - 1);

    // 3. Write CHECKPOINT record
    struct wal_record rec = {0};
    rec.type = WAL_CHECKPOINT;
    wal_append_record(&rec);

    // 4. Record checkpoint LSN
    wal.checkpoint_lsn = rec.lsn;
}
```

### 9.3 How Checkpoints Speed Up Recovery

After a checkpoint, recovery only needs to process records AFTER the checkpoint:

```
Log: [old records...] [CHECKPOINT] [new records...]
                           │
                           └── Recovery starts here
```

### 9.4 When to Checkpoint

We use **deferred checkpointing** when the log is 75% full:

```c
/* In wal_append_record: */
if ((log_used >= (WAL_LOG_SECTORS * 3 / 4)) && !wal.checkpointing) {
    checkpoint_pending = true;  // Mark for later
}
```

**Why Deferred?** Immediate checkpointing can cause stack overflow:
- `cache_write()` → `wal_txn_commit()` → `wal_checkpoint()` → `cache_flush()` → recursive calls

Instead, checkpoints are executed at safe points (like shutdown) or explicitly called. The `checkpointing` flag prevents recursive checkpoint attempts.

This prevents the log from filling up while ensuring reasonably bounded recovery time.

---

## 10. Important Engineering Lessons

### 10.1 Sector Conflicts and Memory Layout

**Lesson**: Know your disk layout and NEVER let data overlap with system structures.

In our implementation:
- Sectors 2-65: WAL log
- Sector 66: WAL metadata
- Sectors 67+: User data

We had a bug where test data used sectors 50-52, which ARE part of the log. This corrupted log records and caused mysterious recovery failures.

**Rule**: Always use `#define` constants for sector ranges and validate inputs.

### 10.2 Stack vs Heap Allocation

**Lesson**: Kernel stacks are tiny (4KB in Pintos). Large buffers MUST go on the heap.

```c
// BAD - 1024 bytes on stack, might overflow
uint8_t buffer[BLOCK_SECTOR_SIZE * 2];

// GOOD - allocated on heap
uint8_t *buffer = malloc(BLOCK_SECTOR_SIZE * 2);
```

We use heap allocation throughout the WAL code for buffers and record arrays.

### 10.3 Lock Ordering and Deadlock Prevention

**Lesson**: Always acquire locks in a consistent order.

```c
// Potential deadlock if cache_flush acquires cache lock,
// then calls wal_flush which tries to acquire wal_lock,
// while another thread holds wal_lock and waits for cache_lock

// Solution: Document and enforce lock ordering:
// 1. wal_lock
// 2. cache_lock
// 3. block device lock
```

### 10.4 Checksum Everything

**Lesson**: Disks lie. Data gets corrupted. Always verify.

```c
uint32_t wal_calculate_checksum(struct wal_record *record) {
    uint32_t crc = 0xFFFFFFFF;
    // CRC32 over entire record (excluding checksum field itself)
    for (each byte except checksum) {
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}
```

During recovery, any record with a bad checksum is SKIPPED. This prevents corrupted data from propagating.

### 10.5 The Durability Point

**Lesson**: The ONLY durability guarantee is when the COMMIT record hits the disk.

```c
bool wal_txn_commit(struct wal_txn *txn) {
    lsn_t commit_lsn = wal_append_record(&commit_record);

    wal_flush(commit_lsn);  // <-- THIS is the durability point

    // After wal_flush returns, the transaction WILL survive any crash
    return true;
}
```

Don't return "success" to the user until `wal_flush` completes.

### 10.6 Idempotent Operations

**Lesson**: REDO operations must be idempotent (applying them twice has the same effect as applying once).

Our REDO simply overwrites the sector data with `new_data`. Applying this multiple times produces the same result. This is essential because we don't track exactly which records have already been applied.

---

## 11. Common Pitfalls and How to Avoid Them

### 11.1 Forgetting to Flush

**Pitfall**: Writing to the log buffer but forgetting to flush before declaring success.

```c
// WRONG
wal_append_record(&commit_record);
return true;  // Log might only be in memory!

// RIGHT
lsn_t lsn = wal_append_record(&commit_record);
wal_flush(lsn);  // Force to disk
return true;
```

### 11.2 Wrong UNDO Order

**Pitfall**: Applying UNDO in forward order instead of reverse.

If a transaction wrote: A=1, A=2, A=3
- Forward UNDO: Restores to 1, then 2, then 3 (WRONG - ends at 3)
- Reverse UNDO: Restores to 3, then 2, then 1 (RIGHT - ends at original)

### 11.3 Not Handling Partial Records

**Pitfall**: Assuming all log records are complete.

A crash can occur mid-write, leaving a torn record. Always verify checksums and handle invalid records gracefully:

```c
if (stored_checksum != calculated_checksum) {
    continue;  // Skip this record, don't crash
}
```

### 11.4 Recovery During Recovery

**Pitfall**: Crashing during recovery and corrupting state.

Recovery must be **idempotent**. If we crash during recovery and restart, running recovery again must produce the same correct result.

### 11.5 Circular Buffer Wrap-Around

**Pitfall**: Forgetting that the log is circular.

```c
// LSN 1 goes to sector 2
// LSN 64 goes to sector 65
// LSN 65 goes to sector 2 (WRAPPED!)

block_sector_t sector = WAL_LOG_START_SECTOR + ((lsn - 1) % WAL_LOG_SECTORS);
```

When reading records by LSN, verify that the record's stored LSN matches what you expect (it might have been overwritten after wrap-around).

### 11.6 Active Transaction Tracking

**Pitfall**: Forgetting to remove completed transactions from the active list.

```c
// In wal_txn_commit:
list_remove(&txn->elem);  // MUST remove from active_txns
free(txn);

// In wal_txn_abort:
list_remove(&txn->elem);  // MUST remove from active_txns
free(txn);
```

Orphaned entries in the active list cause memory leaks and incorrect recovery behavior.

---

## 12. Testing WAL Systems

### 12.1 Unit Tests

Test individual components in isolation:

```c
// Test: Transaction IDs are unique
void test_wal_txn_begin(void) {
    struct wal_txn *txn1 = wal_txn_begin();
    struct wal_txn *txn2 = wal_txn_begin();
    ASSERT(txn1->txn_id != txn2->txn_id);
}

// Test: Abort restores original data
void test_wal_txn_abort(void) {
    write_original_data('O');
    txn = wal_txn_begin();
    wal_log_write(txn, sector, old='O', new='N');
    write_new_data('N');
    wal_txn_abort(txn);
    ASSERT(read_data() == 'O');  // Original restored
}
```

### 12.2 Integration Tests

Test multiple components working together:

```c
// Test: Mixed committed and aborted transactions
void test_wal_recover_mixed(void) {
    txn1 = begin(); write('1'); commit();   // Should survive
    txn2 = begin(); write('2'); abort();    // Should be undone
    txn3 = begin(); write('3'); commit();   // Should survive
    txn4 = begin(); write('4'); abort();    // Should be undone

    ASSERT(sector[0] == '1');
    ASSERT(sector[1] == 'O');  // Original
    ASSERT(sector[2] == '3');
    ASSERT(sector[3] == 'O');  // Original
}
```

### 12.3 Crash Recovery Tests

The hardest tests simulate actual crashes:

```
Phase 1 (with disk format):
1. Initialize sectors with 'O'
2. Create committed transaction (write 'C')
3. Create uncommitted transaction (write 'U')
4. Mark metadata as "dirty" (unclean shutdown)
5. Exit WITHOUT calling wal_shutdown()

Phase 2 (WITHOUT disk format - same disk):
1. System boots, sees dirty metadata
2. Recovery runs automatically
3. Verify: committed data = 'C'
4. Verify: uncommitted data = 'O' (rolled back)
```

### 12.4 Stress Tests

Verify the system handles load:

```c
void test_wal_stress(void) {
    for (int i = 0; i < 100; i++) {
        txn = wal_txn_begin();
        for (int j = 0; j < 3; j++) {
            wal_log_write(...);
        }
        if (i % 5 == 0) wal_txn_abort(txn);
        else wal_txn_commit(txn);

        if (i % 25 == 0) wal_checkpoint();
    }
    // Verify no crashes, memory leaks, or corruption
}
```

---

## 13. Summary

### What We Built

A complete Write-Ahead Logging system for the Pintos filesystem that provides:

1. **Crash Consistency**: The filesystem is always in a valid state after recovery
2. **Atomicity**: Transactions either fully complete or fully roll back
3. **Durability**: Committed data survives any crash

### Key Components

| Component | Purpose |
|-----------|---------|
| `wal_init()` | Initialize WAL subsystem at mount |
| `wal_init_metadata()` | Initialize metadata sector during format |
| `wal_shutdown()` | Shutdown WAL and mark clean |
| `wal_txn_begin()` | Start a new transaction |
| `wal_log_write()` | Log a data modification |
| `wal_txn_commit()` | Commit (durability point) |
| `wal_txn_abort()` | Abort and UNDO changes |
| `wal_flush()` | Force log to disk |
| `wal_checkpoint()` | Create recovery checkpoint |
| `wal_recover()` | Three-phase crash recovery |
| `wal_set_current_txn()` | Set thread-local transaction |
| `wal_get_current_txn()` | Get thread-local transaction |
| `wal_get_stats()` | Get WAL statistics |
| `wal_reset_stats()` | Reset statistics counters |

### The Golden Rules

1. **Write the log first, then the data**
2. **Flush the COMMIT record before returning success**
3. **Verify checksums on everything**
4. **UNDO in reverse LSN order**
5. **Keep recovery idempotent**
6. **Know your sector layout - avoid conflicts**

### Further Reading

- **ARIES**: The industry-standard WAL algorithm (Mohan et al., 1992)
- **SQLite WAL**: A simple, well-documented implementation
- **PostgreSQL WAL**: Production-grade implementation with streaming replication
- **LevelDB/RocksDB**: Log-structured storage with WAL

---

*This document was written for the Pintos Operating System course. The WAL implementation provides crash consistency for the extended filesystem project.*
