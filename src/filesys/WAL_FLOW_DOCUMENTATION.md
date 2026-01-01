# WAL Initialization and Shutdown Flow Documentation

This document describes the complete flow of WAL initialization and shutdown for different scenarios.

## Overview

The WAL (Write-Ahead Log) system uses a metadata sector (sector 66) to track:
- Whether the system shut down cleanly (`clean_shutdown` flag)
- The last LSN (Log Sequence Number) written
- The last transaction ID used

## Key Functions

- `wal_init(bool format)`: Called during filesystem mount
- `wal_shutdown()`: Called during filesystem unmount
- `wal_init_metadata()`: Initializes the metadata sector (called during format)
- `do_format()`: Formats the filesystem (only called when `format=true`)

---

## Scenario 1: Fresh Filesystem (First Time Format)

**Trigger**: Boot with `-f` flag (format=true)

### Flow:

1. **`filesys_init(true)` called**
   - `cache_init()`
   - **`wal_init(true)` called**
     - Initialize `wal_lock`
     - Set in-memory state:
       - `wal.next_lsn = 1`
       - `wal.flushed_lsn = 0`
       - `wal.next_txn_id = 1`
     - **Return early** (no metadata read/write)
   - `inode_init()`
   - `free_map_init()`
   - **`do_format()` called**
     - `free_map_create()` (wipes free map)
     - **`wal_init_metadata()` called**
       - Write metadata sector with:
         - `magic = WAL_METADATA_MAGIC`
         - `clean_shutdown = 1` (fresh filesystem starts clean)
         - `last_lsn = 0`
         - `last_txn_id = 0`
     - Create root directory
     - `free_map_close()`

2. **System runs normally**
   - Transactions create log records
   - LSNs increment: 1, 2, 3, ...

3. **`filesys_done()` called (clean shutdown)**
   - `cache_shutdown()`
   - **`wal_shutdown()` called**
     - Flush all pending log records to disk
     - Read current metadata
     - Update metadata:
       - `clean_shutdown = 1` (mark as clean)
       - `last_lsn = wal.next_lsn - 1` (last assigned LSN)
       - `last_txn_id = wal.next_txn_id - 1` (last assigned transaction ID)
     - Write metadata to disk
   - `free_map_close()`

**Result**: Metadata sector contains clean shutdown marker with last LSN/TXN ID.

---

## Scenario 2: Normal Boot (Clean Shutdown)

**Trigger**: Boot without `-f` flag (format=false), previous shutdown was clean

### Flow:

1. **`filesys_init(false)` called**
   - `cache_init()`
   - **`wal_init(false)` called**
     - Initialize `wal_lock`
     - Read metadata from sector 66
     - **Metadata is valid** (`magic == WAL_METADATA_MAGIC`)
     - **`clean_shutdown == 1`** (clean shutdown detected)
     - Restore state from metadata:
       - If `meta.last_lsn > 0`:
         - `wal.next_lsn = meta.last_lsn + 1`
         - `wal.flushed_lsn = meta.last_lsn`
       - Else (fresh filesystem):
         - `wal.next_lsn = 1`
         - `wal.flushed_lsn = 0`
       - `wal.next_txn_id = meta.last_txn_id + 1`
     - Mark as dirty: `meta.clean_shutdown = 0`
     - Write metadata (mark as dirty for this session)
   - `inode_init()`
   - `free_map_init()`
   - `do_format()` **NOT called** (format=false)
   - `free_map_open()`

2. **System runs normally**
   - Continues from where it left off
   - New LSNs continue from `next_lsn`

3. **`filesys_done()` called (clean shutdown)**
   - Same as Scenario 1, step 3

**Result**: System continues seamlessly from previous session.

---

## Scenario 3: Crash Recovery (Dirty Shutdown)

**Trigger**: Boot without `-f` flag (format=false), previous shutdown was NOT clean (crash)

### Flow:

1. **`filesys_init(false)` called**
   - `cache_init()`
   - **`wal_init(false)` called**
     - Initialize `wal_lock`
     - Read metadata from sector 66
     - **Metadata is valid** (`magic == WAL_METADATA_MAGIC`)
     - **`clean_shutdown == 0`** (dirty shutdown detected - crash!)
     - **`wal_recover()` called**
       - Scans log sectors (2-65) to find maximum LSN
       - Determines which transactions were active at crash
       - **REDO phase**: Replays all committed transactions
       - **UNDO phase**: Rolls back all uncommitted transactions
       - Sets `wal.next_lsn = max_lsn + 1`
     - Mark as dirty: `meta.clean_shutdown = 0`
     - Write metadata (still dirty, will be clean after this session)
   - `inode_init()`
   - `free_map_init()`
   - `do_format()` **NOT called** (format=false)
   - `free_map_open()`

2. **System runs normally**
   - Continues from recovered state
   - New LSNs continue from `next_lsn` (after max found in log)

3. **`filesys_done()` called (clean shutdown)**
   - Same as Scenario 1, step 3

**Result**: Filesystem recovered to consistent state, continues normally.

---

## Scenario 4: Reformatting Existing Filesystem

**Trigger**: Boot with `-f` flag (format=true) on existing filesystem

### Flow:

1. **`filesys_init(true)` called**
   - `cache_init()`
   - **`wal_init(true)` called**
     - Initialize `wal_lock`
     - Set in-memory state:
       - `wal.next_lsn = 1`
       - `wal.flushed_lsn = 0`
       - `wal.next_txn_id = 1`
     - **Return early** (no metadata read, no recovery)
     - **Old metadata/log is ignored** (we're wiping everything)
   - `inode_init()`
   - `free_map_init()`
   - **`do_format()` called**
     - `free_map_create()` (wipes free map - destroys old filesystem)
     - **`wal_init_metadata()` called**
       - **Overwrites** old metadata with fresh values:
         - `magic = WAL_METADATA_MAGIC`
         - `clean_shutdown = 1`
         - `last_lsn = 0`
         - `last_txn_id = 0`
     - Create root directory
     - `free_map_close()`

2. **System runs normally**
   - Fresh start, LSNs start from 1

3. **`filesys_done()` called (clean shutdown)**
   - Same as Scenario 1, step 3

**Result**: Old filesystem completely wiped, fresh start.

---

## Scenario 5: Corrupted/Missing Metadata (Edge Case)

**Trigger**: Boot without `-f` flag (format=false), but metadata is invalid/missing

### Flow:

1. **`filesys_init(false)` called**
   - `cache_init()`
   - **`wal_init(false)` called**
     - Initialize `wal_lock`
     - Read metadata from sector 66
     - **Metadata is invalid** (`magic != WAL_METADATA_MAGIC`)
     - Treat as corrupted/missing metadata
     - **`wal_init_metadata()` called**
       - Initialize fresh metadata
     - Set in-memory state:
       - `wal.next_lsn = 1`
       - `wal.flushed_lsn = 0`
       - `wal.next_txn_id = 1`
     - Mark as dirty: `meta.clean_shutdown = 0`
     - Write metadata
   - `inode_init()`
   - `free_map_init()`
   - `do_format()` **NOT called** (format=false)
   - `free_map_open()`

2. **System runs normally**
   - Starts fresh (metadata was corrupted, so we can't trust old state)

3. **`filesys_done()` called (clean shutdown)**
   - Same as Scenario 1, step 3

**Result**: Corrupted metadata replaced, system starts fresh.

---

## Key Design Decisions

1. **Format flag passed to `wal_init()`**: Prevents unnecessary recovery when formatting
2. **Metadata initialized in `do_format()`**: Ensures metadata is written during format
3. **Early return in `wal_init(true)`**: Skips metadata read when formatting (old data irrelevant)
4. **Clean shutdown marker**: Distinguishes crash from clean shutdown
5. **Last LSN stored in metadata**: Allows LSN continuity across reboots

---

## State Transitions

### Metadata `clean_shutdown` Flag:

```
Fresh filesystem → clean_shutdown = 1 (in do_format)
Normal operation → clean_shutdown = 0 (in wal_init, marks session as active)
Clean shutdown → clean_shutdown = 1 (in wal_shutdown)
Crash → clean_shutdown = 0 (remains dirty, detected on next boot)
```

### LSN Continuity:

```
Format: next_lsn = 1
Clean shutdown: next_lsn = last_lsn + 1 (from metadata)
Crash recovery: next_lsn = max_lsn + 1 (from log scan)
```

---

## Notes

- The metadata sector (66) is **never** wiped during normal operation
- Only `do_format()` wipes and reinitializes metadata
- Recovery only runs when `format=false` AND `clean_shutdown=0`
- LSNs are **monotonically increasing** across all scenarios (never reset to 0 after first use)

---

## Testing Considerations

### Running WAL Tests

**For most WAL tests** (initialization, transactions, logging):
```bash
pintos -v -k --qemu --filesys-size=2 -- -q -f rfkt <test-name>
```
- Use `-f` flag to format filesystem (fresh start for each test)
- Example: `pintos ... -f rfkt wal-init`

**For WAL recovery tests** (crash recovery scenarios):
```bash
pintos -v -k --qemu --filesys-size=2 -- -q rfkt <test-name>
```
- **DO NOT use `-f` flag** - filesystem must persist to test recovery
- Recovery tests simulate: do work → crash → boot → recover
- If `-f` is used, the filesystem is wiped and recovery can't be tested
- Example: `pintos ... rfkt wal-recover-commit` (no `-f`)

### Why Recovery Tests Need No `-f` Flag

Recovery tests need to verify:
1. **First boot**: Do operations, commit/abort transactions
2. **Simulate crash**: Don't call `wal_shutdown()` cleanly
3. **Second boot**: Boot WITHOUT `-f` → should detect crash → run recovery → verify data

If `-f` is used, step 3 wipes the filesystem, making recovery testing impossible.

