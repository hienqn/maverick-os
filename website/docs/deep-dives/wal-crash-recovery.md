---
sidebar_position: 3
---

import AnimatedFlow from '@site/src/components/AnimatedFlow';
import CodeWalkthrough from '@site/src/components/CodeWalkthrough';

# Write-Ahead Logging Deep Dive

This page explains how Write-Ahead Logging (WAL) ensures file system consistency after crashes.

## The Problem: Crash Consistency

Creating a file requires multiple disk writes:

1. Allocate an inode sector (update free map)
2. Initialize the inode
3. Add entry to parent directory

If the system crashes mid-operation, these structures become inconsistent:

```
SCENARIO: Crash after step 2

Free Map: says sector 42 is used ✓
Inode 42: initialized with file data ✓
Directory: no entry points to inode 42 ✗

RESULT: Orphan inode (allocated but unreachable)
        Space leak!
```

## The Solution: Write-Ahead Logging

The core principle:

> **"Never modify the original data until the operation is safely logged."**

<AnimatedFlow
  title="WAL Write Protocol"
  states={[
    { id: 'begin', label: 'BEGIN', description: 'Start transaction' },
    { id: 'log_old', label: 'Log Old Value', description: 'Save what data looks like now' },
    { id: 'log_new', label: 'Log New Value', description: 'Save what we want to write' },
    { id: 'commit', label: 'COMMIT', description: 'Mark transaction complete' },
    { id: 'apply', label: 'Apply Changes', description: 'Write to actual locations' },
    { id: 'done', label: 'Done', description: 'Operation complete' },
  ]}
  transitions={[
    { from: 'begin', to: 'log_old', label: '' },
    { from: 'log_old', to: 'log_new', label: 'for each block' },
    { from: 'log_new', to: 'commit', label: 'all logged' },
    { from: 'commit', to: 'apply', label: '' },
    { from: 'apply', to: 'done', label: '' },
  ]}
/>

## Log Record Format

```c
/* Log header (at start of log partition) */
struct wal_header {
  uint32_t magic;         /* WAL_MAGIC for validation */
  uint32_t num_records;   /* Number of records in log */
  uint32_t next_txn_id;   /* Next transaction ID to assign */
};

/* Individual log record */
struct wal_record {
  uint32_t txn_id;        /* Transaction this belongs to */
  enum wal_type type;     /* BEGIN, DATA, or COMMIT */

  /* For DATA records only */
  block_sector_t sector;  /* Which sector this modifies */
  uint8_t old_data[512];  /* Before image (for UNDO) */
  uint8_t new_data[512];  /* After image (for REDO) */
};

enum wal_type {
  WAL_BEGIN,   /* Start of transaction */
  WAL_DATA,    /* Data modification record */
  WAL_COMMIT   /* Transaction committed */
};
```

## Writing with WAL

<CodeWalkthrough
  title="WAL-Protected Write"
  code={`void wal_write_sector(block_sector_t sector, const void *new_data) {
  struct wal_record record;

  /* 1. Read current sector contents */
  block_read(fs_device, sector, record.old_data);

  /* 2. Prepare log record */
  record.txn_id = current_transaction;
  record.type = WAL_DATA;
  record.sector = sector;
  memcpy(record.new_data, new_data, BLOCK_SECTOR_SIZE);

  /* 3. Append record to log (must reach disk!) */
  append_to_log(&record);
  log_sync();  /* Force to disk */

  /* 4. Now safe to write actual sector */
  block_write(fs_device, sector, new_data);
}`}
  steps={[
    { lines: [1, 2, 3, 4, 5], title: 'Read Old Value', description: 'First, read the current contents of the sector. This is the "before image" needed for UNDO if we crash.' },
    { lines: [7, 8, 9, 10, 11], title: 'Prepare Record', description: 'Create a log record with transaction ID, sector number, old data, and new data.' },
    { lines: [13, 14, 15], title: 'Write to Log', description: 'Append the record to the log and force it to disk. This is the critical step - the log must be durable before proceeding.' },
    { lines: [17, 18], title: 'Apply Change', description: 'Only now is it safe to modify the actual data sector. If we crash after this, the log has all info needed to recover.' },
  ]}
/>

## Transaction API

```c
/* Start a new transaction */
txn_id_t wal_begin(void) {
  struct wal_record begin_record;
  begin_record.type = WAL_BEGIN;
  begin_record.txn_id = next_txn_id++;

  append_to_log(&begin_record);
  return begin_record.txn_id;
}

/* Commit a transaction */
void wal_commit(txn_id_t txn) {
  struct wal_record commit_record;
  commit_record.type = WAL_COMMIT;
  commit_record.txn_id = txn;

  append_to_log(&commit_record);
  log_sync();  /* CRITICAL: commit must reach disk */
}

/* Example: atomic file creation */
void create_file(const char *name) {
  txn_id_t txn = wal_begin();

  block_sector_t inode_sector = free_map_allocate(1);
  inode_create(inode_sector);  /* WAL-protected */
  dir_add(parent, name, inode_sector);  /* WAL-protected */

  wal_commit(txn);  /* All or nothing */
}
```

## Recovery Algorithm

After a crash, the recovery process:

1. **Find committed transactions**: Scan log for COMMIT records
2. **REDO committed**: Apply new_data for committed transactions
3. **UNDO uncommitted**: Apply old_data for uncommitted transactions

<AnimatedFlow
  title="Recovery Process"
  states={[
    { id: 'scan', label: 'Scan Log', description: 'Read all log records' },
    { id: 'classify', label: 'Classify Txns', description: 'Identify committed vs uncommitted' },
    { id: 'redo', label: 'REDO Phase', description: 'Replay committed transactions' },
    { id: 'undo', label: 'UNDO Phase', description: 'Reverse uncommitted transactions' },
    { id: 'clear', label: 'Clear Log', description: 'Reset log for new operations' },
    { id: 'ready', label: 'Ready', description: 'System consistent, resume operations' },
  ]}
  transitions={[
    { from: 'scan', to: 'classify', label: '' },
    { from: 'classify', to: 'redo', label: '' },
    { from: 'redo', to: 'undo', label: '' },
    { from: 'undo', to: 'clear', label: '' },
    { from: 'clear', to: 'ready', label: '' },
  ]}
/>

<CodeWalkthrough
  title="Recovery Implementation"
  code={`void wal_recover(void) {
  /* Build sets of transaction IDs */
  struct bitmap *committed = bitmap_create(MAX_TXN);
  struct bitmap *started = bitmap_create(MAX_TXN);

  /* Phase 1: Scan log to classify transactions */
  for (each record in log) {
    if (record.type == WAL_BEGIN)
      bitmap_set(started, record.txn_id, true);
    else if (record.type == WAL_COMMIT)
      bitmap_set(committed, record.txn_id, true);
  }

  /* Phase 2: REDO - replay committed transactions */
  for (each record in log, forward order) {
    if (record.type == WAL_DATA &&
        bitmap_test(committed, record.txn_id)) {
      block_write(fs_device, record.sector, record.new_data);
    }
  }

  /* Phase 3: UNDO - reverse uncommitted transactions */
  for (each record in log, reverse order) {
    if (record.type == WAL_DATA &&
        bitmap_test(started, record.txn_id) &&
        !bitmap_test(committed, record.txn_id)) {
      block_write(fs_device, record.sector, record.old_data);
    }
  }

  /* Phase 4: Clear the log */
  wal_clear();

  bitmap_destroy(committed);
  bitmap_destroy(started);
}`}
  steps={[
    { lines: [1, 2, 3, 4, 5], title: 'Setup', description: 'Create bitmaps to track which transactions started and which committed.' },
    { lines: [7, 8, 9, 10, 11, 12, 13], title: 'Scan Phase', description: 'Read through entire log, marking BEGIN and COMMIT records for each transaction.' },
    { lines: [15, 16, 17, 18, 19, 20, 21], title: 'REDO Phase', description: 'For committed transactions, apply the new_data to ensure changes are on disk. Even if already applied, this is idempotent.' },
    { lines: [23, 24, 25, 26, 27, 28, 29, 30], title: 'UNDO Phase', description: 'For transactions that started but never committed, restore the old_data to reverse partial changes.' },
    { lines: [32, 33, 34, 35, 36], title: 'Cleanup', description: 'Clear the log since all data is now consistent. Free tracking structures.' },
  ]}
/>

## Crash Scenarios

### Scenario 1: Crash Before Commit

```
Log:  [BEGIN txn=1] [DATA sector=42 old=X new=Y]
      ↑ crash here

Recovery:
- txn 1 started but not committed
- UNDO: write X back to sector 42
- Result: like operation never happened ✓
```

### Scenario 2: Crash After Commit, Before Apply

```
Log:  [BEGIN txn=1] [DATA sector=42 old=X new=Y] [COMMIT txn=1]
      ↑ crash here (Y not yet written to sector 42)

Recovery:
- txn 1 is committed
- REDO: write Y to sector 42
- Result: operation completed ✓
```

### Scenario 3: Crash After Apply

```
Log:  [BEGIN txn=1] [DATA sector=42 old=X new=Y] [COMMIT txn=1]
Disk: sector 42 contains Y
      ↑ crash here

Recovery:
- txn 1 is committed
- REDO: write Y to sector 42 (no-op, already there)
- Result: operation completed ✓
```

## Checkpointing

The log grows with each operation. Checkpointing truncates it:

```c
void wal_checkpoint(void) {
  /* 1. Ensure all pending writes are on disk */
  cache_flush();

  /* 2. Clear the log (all transactions are durable) */
  wal_clear();

  /* 3. Record checkpoint time (optional) */
  checkpoint_time = timer_ticks();
}
```

Checkpointing is safe because:
- All data is on disk (cache_flush)
- No in-progress transactions (we don't checkpoint mid-transaction)
- Recovery would REDO everything anyway

## Deferred Checkpointing

Instead of checkpointing immediately, defer to reduce I/O:

```c
/* Background thread */
void checkpoint_daemon(void) {
  while (true) {
    timer_sleep(CHECKPOINT_INTERVAL);

    if (log_size() > CHECKPOINT_THRESHOLD) {
      wal_checkpoint();
    }
  }
}
```

## Performance Considerations

### Write Amplification

Every data write becomes:
1. Read old data (1 read)
2. Write log record (1 write)
3. Write actual data (1 write)

This is **3x overhead** in the worst case. Mitigations:
- Batch multiple operations in one transaction
- Defer log sync until commit
- Use group commit (multiple transactions share one log sync)

### Log Placement

The log should be on the same disk as data for consistency, but:
- Sequential writes to log (fast)
- Random writes to data (slower)

Consider using a separate log partition at the start of the disk for faster sequential writes.

## Common Bugs

### Missing Log Sync

```c
/* BUG: Log record might not reach disk before data write */
append_to_log(&record);
/* log_sync(); -- MISSING! */
block_write(fs_device, sector, new_data);
```

If we crash after `block_write` but before log is flushed, we can't recover.

### Infinite Recursion

```c
/* BUG: WAL logging triggers more WAL logging */
void wal_write_sector(block_sector_t sector, const void *data) {
  append_to_log(...);  /* This writes to disk... */
                       /* ...which might trigger free map update... */
                       /* ...which calls wal_write_sector... */
}
```

Solution: Mark certain inodes (like free map) to skip WAL.

### UNDO Order

UNDO must happen in **reverse** order:

```c
/* Transaction: A then B */
Log: [DATA sector=A] [DATA sector=B]

/* UNDO must be: B then A */
/* Otherwise if A depends on B, we corrupt data */
```

## Summary

| Concept | Purpose |
|---------|---------|
| **WAL** | Ensure atomicity and durability |
| **Before Image** | Enables UNDO of partial transactions |
| **After Image** | Enables REDO of committed transactions |
| **COMMIT Record** | Marks transaction as complete |
| **Checkpoint** | Truncates log safely |

## Related Topics

- [Project 4: File System](/docs/projects/filesys/overview) - WAL implementation
- [Buffer Cache](/docs/projects/filesys/overview#task-1-buffer-cache) - Caching layer
