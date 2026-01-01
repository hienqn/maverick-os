#include "filesys/wal.h"
#include "filesys/cache.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <string.h>
#include <stdio.h>

/*
 * Write-Ahead Logging Implementation
 *
 * YOUR TASK: Fill in the function bodies below.
 * Each function has hints and questions to guide your implementation.
 */

/* Global WAL manager instance */
struct wal_manager wal;

/* Reference to the filesystem block device */
extern struct block* fs_device;

/* ============================================================
 * METADATA HELPER FUNCTIONS
 * ============================================================ */

/* Read WAL metadata from disk */
static void wal_read_metadata(struct wal_metadata* meta) {
  block_read(fs_device, WAL_METADATA_SECTOR, meta);
}

/* Write WAL metadata to disk */
static void wal_write_metadata(struct wal_metadata* meta) {
  block_write(fs_device, WAL_METADATA_SECTOR, meta);
}

/* Initialize metadata sector (called on first format) */
void wal_init_metadata(void) {
  struct wal_metadata meta;
  memset(&meta, 0, sizeof(meta));
  meta.magic = WAL_METADATA_MAGIC;
  meta.clean_shutdown = 1; /* Start as clean (fresh filesystem) */
  meta.last_lsn = 0;
  meta.last_txn_id = 0;
  wal_write_metadata(&meta);
}

/* ============================================================
 * INITIALIZATION AND SHUTDOWN
 * ============================================================ */

void wal_init(bool format) {
  /*
   * Initialize the WAL (Write-Ahead Logging) subsystem.
   *
   * HIGH-LEVEL OVERVIEW:
   * This function initializes the WAL system, which provides crash consistency
   * by logging all filesystem modifications before applying them. The system
   * can recover from crashes by replaying (or undoing) logged operations.
   *
   * INITIALIZATION FLOW:
   * 1. Initialize synchronization (lock)
   * 2. If formatting: Set fresh state, allocate buffer (metadata written in do_format)
   * 3. If not formatting: Read metadata, check for crash, recover if needed
   * 4. Allocate log buffer for batching writes
   * 5. Initialize transaction tracking and statistics
   *
   * CRASH DETECTION:
   * - Reads metadata sector to check clean_shutdown flag
   * - If dirty (clean_shutdown=0): System crashed, run recovery
   * - If clean (clean_shutdown=1): Normal boot, restore state from metadata
   * - If invalid metadata: Treat as corrupted, initialize fresh
   */
  lock_init(&wal.wal_lock);

  if (format) {
    /* FORMATTING PATH: Fresh filesystem initialization */
    /* Set in-memory state for new filesystem */
    wal.next_lsn = 1;
    wal.flushed_lsn = 0;
    wal.next_txn_id = 1;

    /* Allocate log buffer for batching log writes */
    wal.log_buffer = malloc(WAL_BUFFER_SIZE);
    if (wal.log_buffer == NULL) {
      PANIC("Failed to allocate WAL log buffer");
    }
    wal.buffer_size = WAL_BUFFER_SIZE;
    wal.buffer_used = 0;

    /* Initialize statistics counters */
    wal.stats_txn_begun = 0;
    wal.stats_txn_committed = 0;
    wal.stats_txn_aborted = 0;
    wal.stats_writes_logged = 0;

    /* Initialize active transaction tracking */
    list_init(&wal.active_txns);

    /* Initialize checkpoint LSN (no checkpoint yet) */
    wal.checkpoint_lsn = 0;

    /* Note: wal_init_metadata() will be called in do_format() to write metadata to disk */
    return;
  }

  /* NORMAL BOOT PATH: Mount existing filesystem */
  struct wal_metadata meta;
  wal_read_metadata(&meta);

  /* Check if metadata is valid (magic number matches) */
  bool metadata_valid = (meta.magic == WAL_METADATA_MAGIC);

  if (!metadata_valid) {
    /* CORRUPTED METADATA: Initialize fresh metadata */
    /* This shouldn't happen on a normal filesystem, but handle gracefully */
    wal_init_metadata();
    wal.next_lsn = 1;
    wal.flushed_lsn = 0;
    wal.next_txn_id = 1;
    /* Mark as dirty (will be set to clean on shutdown) */
    meta.clean_shutdown = 0;
    wal_write_metadata(&meta);
  } else {
    /* VALID METADATA: Check shutdown state */
    bool clean_shutdown = (meta.clean_shutdown == 1);

    if (!clean_shutdown) {
      /* CRASH DETECTED: System didn't shut down cleanly */
      /* Run recovery to restore filesystem to consistent state */
      wal_recover(); /* Scans log, redoes committed transactions, undoes uncommitted */
      /* After recovery, next_lsn should be set to max_lsn + 1 */
    } else {
      /* CLEAN SHUTDOWN: Restore state from metadata */
      if (meta.last_lsn > 0) {
        /* Continue from last LSN (ensures LSN continuity) */
        wal.next_lsn = meta.last_lsn + 1;
        wal.flushed_lsn = meta.last_lsn;
      } else {
        /* Fresh filesystem or no previous LSN */
        wal.next_lsn = 1;
        wal.flushed_lsn = 0;
      }
      wal.next_txn_id = meta.last_txn_id + 1;
    }

    /* Mark as dirty for this session (will be set to clean on shutdown) */
    meta.clean_shutdown = 0;
    wal_write_metadata(&meta);
  }

  /* Allocate log buffer for batching log writes (performance optimization) */
  wal.log_buffer = malloc(WAL_BUFFER_SIZE);
  if (wal.log_buffer == NULL) {
    PANIC("Failed to allocate WAL log buffer");
  }
  wal.buffer_size = WAL_BUFFER_SIZE;
  wal.buffer_used = 0;

  /* Initialize statistics counters (for testing/monitoring) */
  wal.stats_txn_begun = 0;
  wal.stats_txn_committed = 0;
  wal.stats_txn_aborted = 0;
  wal.stats_writes_logged = 0;

  /* Initialize active transaction tracking (for recovery and shutdown) */
  list_init(&wal.active_txns);

  /* Initialize checkpoint LSN (no checkpoint yet) */
  wal.checkpoint_lsn = 0;
}

void wal_shutdown(void) {
  /*
   * TODO: Cleanly shut down the WAL subsystem
   *
   * Steps to consider:
   * 1. Wait for or abort any active transactions
   * 2. Flush all pending log records to disk
   * 3. Write a "clean shutdown" marker
   * 4. Free allocated resources
   *
   * QUESTION: What order should these steps happen in?
   */

  /* TODO: Wait for or abort any active transactions */

  /* Flush all pending log records to disk */
  if (wal.next_lsn > 0) {
    wal_flush(wal.next_lsn - 1); /* Flush up to last assigned LSN */
  }

  /* Write clean shutdown marker - CRITICAL: This must happen last! */
  struct wal_metadata meta;
  wal_read_metadata(&meta);
  meta.magic = WAL_METADATA_MAGIC;
  meta.clean_shutdown = 1; /* Mark as clean shutdown */
  /* Store last LSN (0 if no records written yet) */
  meta.last_lsn = (wal.next_lsn > 0) ? wal.next_lsn - 1 : 0;
  /* Store last transaction ID (0 if no transactions yet) */
  meta.last_txn_id = (wal.next_txn_id > 0) ? wal.next_txn_id - 1 : 0;
  wal_write_metadata(&meta);

  /* Free allocated resources */
  if (wal.log_buffer != NULL) {
    free(wal.log_buffer);
    wal.log_buffer = NULL;
    wal.buffer_size = 0;
    wal.buffer_used = 0;
  }
}

/* ============================================================
 * TRANSACTION MANAGEMENT
 * ============================================================ */

struct wal_txn* wal_txn_begin(void) {
  /*
   * TODO: Begin a new transaction
   *
   * Steps to consider:
   * 1. Allocate a new wal_txn structure
   * 2. Assign a unique transaction ID
   * 3. Set the initial state to TXN_ACTIVE
   * 4. Write a WAL_BEGIN record to the log
   * 5. Add to active transaction list (if you're tracking them)
   *
   * QUESTION: Does the BEGIN record need to be flushed immediately?
   * What are the tradeoffs?
   */

  return NULL; /* Replace with your implementation */
}

bool wal_txn_commit(struct wal_txn* txn) {
  /*
   * TODO: Commit a transaction
   *
   * Steps to consider:
   * 1. Write a WAL_COMMIT record to the log
   * 2. CRITICAL: Flush the log to disk (why is this critical?)
   * 3. Update transaction state to TXN_COMMITTED
   * 4. Remove from active transaction list
   * 5. Free the transaction structure
   *
   * THE KEY INSIGHT:
   * A transaction is only considered committed when its COMMIT
   * record is safely on disk. This is the "durability" guarantee.
   *
   * QUESTION: What happens if the system crashes between writing
   * the COMMIT record and flushing it to disk?
   */

  return false; /* Replace with your implementation */
}

void wal_txn_abort(struct wal_txn* txn) {
  /*
   * TODO: Abort a transaction, undoing all its changes
   *
   * Steps to consider:
   * 1. Read log records for this transaction (in reverse order!)
   * 2. For each WAL_WRITE record, restore the old_data
   * 3. Write a WAL_ABORT record to the log
   * 4. Update transaction state to TXN_ABORTED
   * 5. Free the transaction structure
   *
   * QUESTION: Why do you need to process records in reverse order?
   *
   * HINT: You need to track the first LSN for this transaction
   * so you know where to start reading.
   */
}

/* ============================================================
 * LOGGING OPERATIONS
 * ============================================================ */

bool wal_log_write(struct wal_txn* txn, block_sector_t sector, const void* old_data,
                   const void* new_data, uint16_t offset, uint16_t length) {
  /*
   * TODO: Log a write operation
   *
   * This is called BEFORE the actual data is written to the cache/disk.
   *
   * Steps to consider:
   * 1. Create a WAL_WRITE record
   * 2. Fill in: txn_id, sector, offset, length, old_data, new_data
   * 3. Calculate and store checksum
   * 4. Append the record to the log buffer
   * 5. Return the LSN of this record (caller might need it)
   *
   * QUESTIONS:
   * - Should you flush the log record immediately or buffer it?
   * - What if the log buffer is full?
   * - What if old_data or new_data is larger than your record can hold?
   */

  return false; /* Replace with your implementation */
}

void wal_flush(lsn_t up_to_lsn) {
  /*
   * TODO: Flush log records to disk up to the specified LSN
   *
   * Steps to consider:
   * 1. If up_to_lsn <= flushed_lsn, nothing to do
   * 2. Write buffered log records to disk
   * 3. Use block_write() or your cache layer
   * 4. Update flushed_lsn
   *
   * QUESTION: Should you use the buffer cache for log writes,
   * or bypass it and write directly to disk? Why?
   *
   * HINT: Consider what happens if the cache evicts a log sector
   * before it's persisted...
   */
}

/* ============================================================
 * CHECKPOINTING
 * ============================================================ */

void wal_checkpoint(void) {
  /*
   * TODO: Create a checkpoint
   *
   * A checkpoint establishes a known-good state that limits how far
   * back recovery needs to go.
   *
   * Steps to consider:
   * 1. Flush all dirty data pages to disk (not just log!)
   * 2. Write a WAL_CHECKPOINT record with info about active transactions
   * 3. Flush the log
   * 4. Record the checkpoint LSN
   *
   * QUESTIONS:
   * - What's a "fuzzy" vs "sharp" checkpoint?
   * - Can you take a checkpoint while transactions are active?
   * - How does checkpoint frequency affect:
   *   a) Normal operation performance?
   *   b) Recovery time after a crash?
   *
   * ADVANCED: You might want to reclaim log space after a checkpoint.
   * How do you know which log records are no longer needed?
   */
}

/* ============================================================
 * RECOVERY
 * ============================================================ */

void wal_recover(void) {
  /*
   * TODO: Recover the filesystem after a crash
   *
   * This is where WAL pays off! You can restore consistency
   * even after an unexpected shutdown.
   *
   * ARIES-STYLE RECOVERY (classic approach):
   *
   * Phase 1: ANALYSIS
   * - Scan the log from the last checkpoint
   * - Determine which transactions were active at crash time
   * - Build a list of dirty pages that might need redo
   *
   * Phase 2: REDO
   * - Replay all logged operations from checkpoint forward
   * - This brings the database to the state it was at crash time
   * - Redo even committed transactions (they might not have made it to disk)
   *
   * Phase 3: UNDO
   * - Roll back all transactions that were active at crash time
   * - These transactions never committed, so their changes must be undone
   *
   * QUESTIONS:
   * - Why do you redo committed transactions?
   * - Why do you undo uncommitted transactions?
   * - What's the difference between "logical" and "physical" redo?
   *
   * SIMPLER APPROACH (if ARIES feels complex):
   * - Just scan the log and redo all committed transactions
   * - This works but might be slower
   */
}

/* ============================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================ */

static lsn_t wal_append_record(struct wal_record* record) {
  /*
   * TODO: Append a record to the in-memory log buffer
   *
   * Steps:
   * 1. Assign the next LSN to the record
   * 2. Copy the record to the log buffer
   * 3. Update buffer_used
   * 4. If buffer is full, flush to disk
   * 5. Return the assigned LSN
   *
   * QUESTION: What synchronization is needed here?
   */

  return 0; /* Replace with your implementation */
}

static void wal_flush_buffer(void) {
  /*
   * TODO: Write the log buffer contents to disk
   *
   * Steps:
   * 1. Calculate which log sectors need to be written
   * 2. Write each sector using block_write()
   * 3. Update flushed_lsn
   * 4. Clear or reset the buffer
   *
   * IMPORTANT: Consider atomicity!
   * What if the system crashes in the middle of this function?
   *
   * HINT: The WAL itself needs to be written atomically.
   * Some systems use a "group commit" approach where multiple
   * transactions share a single disk write.
   */
}

static bool wal_read_record(lsn_t lsn, struct wal_record* record) {
  /*
   * TODO: Read a log record from disk given its LSN
   *
   * Steps:
   * 1. Calculate which sector contains this LSN
   * 2. Read the sector from disk
   * 3. Extract the record from the sector
   * 4. Verify the checksum
   * 5. Return true if successful, false if corrupted
   *
   * QUESTION: How do you map an LSN to a disk location?
   * (This depends on your log format design)
   */

  return false; /* Replace with your implementation */
}

static uint32_t wal_calculate_checksum(struct wal_record* record) {
  /*
   * TODO: Calculate a checksum for a log record
   *
   * This is used to detect corruption during recovery.
   *
   * Simple approach: XOR all bytes together
   * Better approach: CRC32
   *
   * HINT: Don't include the checksum field itself in the calculation!
   */

  return 0; /* Replace with your implementation */
}

/* ============================================================
 * STATISTICS (for testing/verification)
 * ============================================================ */

void wal_get_stats(struct wal_stats* stats) {
  lock_acquire(&wal.wal_lock);
  stats->txn_begun = wal.stats_txn_begun;
  stats->txn_committed = wal.stats_txn_committed;
  stats->txn_aborted = wal.stats_txn_aborted;
  stats->writes_logged = wal.stats_writes_logged;
  lock_release(&wal.wal_lock);
}

void wal_reset_stats(void) {
  lock_acquire(&wal.wal_lock);
  wal.stats_txn_begun = 0;
  wal.stats_txn_committed = 0;
  wal.stats_txn_aborted = 0;
  wal.stats_writes_logged = 0;
  lock_release(&wal.wal_lock);
}

/* ============================================================
 * INTEGRATION POINTS
 * ============================================================
 *
 * To use WAL, you'll need to modify existing code:
 *
 * 1. In cache.c (cache_write):
 *    - Before writing data, call wal_log_write()
 *    - After commit, the data write can proceed
 *
 * 2. In inode.c (inode_write_at, inode_extend):
 *    - Wrap multi-block operations in transactions
 *    - This ensures atomic file extension
 *
 * 3. In filesys.c (filesys_create, filesys_remove):
 *    - Use transactions for directory modifications
 *    - Ensures metadata consistency
 *
 * 4. In free-map.c:
 *    - Log allocation/deallocation operations
 *    - Critical for preventing block leaks after crash
 *
 * QUESTION: Which operations should be grouped into a single transaction?
 * For example, should creating a file and adding it to a directory
 * be one transaction or two?
 */
