#include "filesys/wal.h"
#include "filesys/cache.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stddef.h>
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
    wal.checkpointing = false;

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
  wal.checkpointing = false;
}

void wal_shutdown(void) {
  /*
   * Cleanly shut down the WAL (Write-Ahead Logging) subsystem.
   *
   * HIGH-LEVEL OVERVIEW:
   * This function ensures all WAL state is safely persisted to disk before
   * the filesystem unmounts. The clean shutdown marker tells wal_init() on
   * the next boot that the system shut down gracefully (no crash recovery needed).
   *
   * SHUTDOWN FLOW (CRITICAL ORDER):
   * 1. Wait for or abort any active transactions (future: implement transaction cleanup)
   * 2. Flush all pending log records to disk (ensures all logged operations are durable)
   * 3. Write clean shutdown marker to metadata (MUST be last - marks successful shutdown)
   * 4. Free allocated resources (log buffer, etc.)
   *
   * WHY THIS ORDER MATTERS:
   * - Log records must be flushed BEFORE writing the clean marker
   *   (otherwise marker says "clean" but log is incomplete)
   * - Clean marker must be written AFTER all flushes complete
   *   (this is the atomic point that indicates successful shutdown)
   * - Resources freed last (no longer needed after shutdown marker written)
   *
   * CRASH SAFETY:
   * If system crashes during shutdown, the clean_shutdown flag remains 0 (dirty),
   * so next boot will run recovery to restore consistency.
   */

  /* TODO: Wait for or abort any active transactions */
  /* For now, we assume all transactions have completed before shutdown.
   * Future enhancement: iterate through wal.active_txns and either:
   *   - Wait for them to complete (graceful shutdown)
   *   - Abort them (force shutdown)
   */

  /* STEP 1: Flush all pending log records to disk */
  /* This ensures all logged operations are durable before marking shutdown as clean */
  if (wal.next_lsn > 0) {
    wal_flush(wal.next_lsn - 1); /* Flush up to last assigned LSN */
  }

  /* STEP 2: Write clean shutdown marker - CRITICAL: This must happen last! */
  /* This is the atomic point that indicates successful shutdown. If we crash
   * before this, clean_shutdown remains 0 and recovery will run on next boot. */
  struct wal_metadata meta;
  wal_read_metadata(&meta);
  meta.magic = WAL_METADATA_MAGIC;
  meta.clean_shutdown = 1; /* Mark as clean shutdown */
  /* Store last LSN (0 if no records written yet) */
  meta.last_lsn = (wal.next_lsn > 0) ? wal.next_lsn - 1 : 0;
  /* Store last transaction ID (0 if no transactions yet) */
  meta.last_txn_id = (wal.next_txn_id > 0) ? wal.next_txn_id - 1 : 0;
  wal_write_metadata(&meta);

  /* STEP 3: Free allocated resources */
  /* Safe to free now - shutdown marker is written, system is clean */
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
  // 1. Allocate a new wal_txn structure
  struct wal_txn *txn = malloc(sizeof(struct wal_txn));
  if (txn == NULL) return NULL;

  // 2. Assign unique transaction ID (under lock)
  lock_acquire(&wal.wal_lock);
  txn->txn_id = wal.next_txn_id++;
  wal.stats_txn_begun++;
  lock_release(&wal.wal_lock);

  // 3. Set initial state
  txn->state = TXN_ACTIVE;

  // 4. Write BEGIN record to log
  struct wal_record rec;
  memset(&rec, 0, sizeof(rec));
  rec.type = WAL_BEGIN;
  rec.txn_id = txn->txn_id;
  lsn_t begin_lsn = wal_append_record(&rec);

  // 5. Store first LSN (needed for abort - to know where to start reading)
  txn->first_lsn = begin_lsn;
  // Initialize last_lsn to the same value (will be updated as more records are added)
  txn->last_lsn = begin_lsn;

  // 6. Add to active transaction list
  lock_acquire(&wal.wal_lock);
  list_push_back(&wal.active_txns, &txn->elem);
  lock_release(&wal.wal_lock);

  return txn;
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
* Log a write operation BEFORE modifying the actual data.
* 
* For writes larger than WAL_MAX_DATA_SIZE (232 bytes), we split into
* multiple log records. All records share the same txn_id, so during
* recovery they're processed together.
*/

if (txn == NULL || txn->state != TXN_ACTIVE) {
return false;
}

const uint8_t *old_bytes = (const uint8_t *)old_data;
const uint8_t *new_bytes = (const uint8_t *)new_data;

uint16_t bytes_logged = 0;

while (bytes_logged < length) {
/* Calculate how much to log in this record */
uint16_t chunk_size = length - bytes_logged;
if (chunk_size > WAL_MAX_DATA_SIZE) {
chunk_size = WAL_MAX_DATA_SIZE;
}

/* Create the log record */
struct wal_record rec;
memset(&rec, 0, sizeof(rec));
rec.type = WAL_WRITE;
rec.txn_id = txn->txn_id;
rec.sector = sector;
rec.offset = offset + bytes_logged;
rec.length = chunk_size;

/* Copy old and new data into the record */
memcpy(rec.old_data, old_bytes + bytes_logged, chunk_size);
memcpy(rec.new_data, new_bytes + bytes_logged, chunk_size);

/* Append to log */
lsn_t lsn = wal_append_record(&rec);

/* Update transaction's last LSN */
txn->last_lsn = lsn;

bytes_logged += chunk_size;
}

/* Update statistics */
lock_acquire(&wal.wal_lock);
wal.stats_writes_logged++;
lock_release(&wal.wal_lock);

return true;
}

void wal_flush(lsn_t up_to_lsn) {
  /*
   * Flush log records to disk up to the specified LSN.
   *
   * This function ensures all log records up to (and including) up_to_lsn
   * are written to disk. It flushes the in-memory log buffer, which contains
   * records that haven't been written to disk yet.
   *
   * IMPLEMENTATION:
   * Since records are appended sequentially and the buffer contains the most
   * recent records, we simply flush the buffer. The buffer should contain
   * records up to (or close to) up_to_lsn if it hasn't been flushed recently.
   *
   * NOTE: We bypass the buffer cache and write directly to disk for durability.
   * Log records must be persistent - if the cache evicts a log sector before
   * it's persisted, we could lose critical recovery information.
   */

  lock_acquire(&wal.wal_lock);

  /* If already flushed beyond this LSN, nothing to do */
  if (up_to_lsn <= wal.flushed_lsn) {
    lock_release(&wal.wal_lock);
    return;
  }

  /* Flush the buffer contents to disk */
  if (wal.buffer_used == 0) {
    lock_release(&wal.wal_lock);
    return; /* Nothing to flush */
  }

  /* Calculate number of records in buffer */
  size_t num_records = wal.buffer_used / sizeof(struct wal_record);
  lsn_t max_lsn_written = wal.flushed_lsn;

  /* Write each record to its corresponding disk sector */
  for (size_t i = 0; i < num_records; i++) {
    /* Get pointer to record i in the buffer */
    struct wal_record* rec = (struct wal_record*)(wal.log_buffer + i * sizeof(struct wal_record));

    /* Calculate which disk sector this LSN maps to */
    block_sector_t sector = WAL_LOG_START_SECTOR + ((rec->lsn - 1) % WAL_LOG_SECTORS);

    /* Write directly to disk (bypass cache for durability) */
    block_write(fs_device, sector, rec);

    /* Track highest LSN written */
    if (rec->lsn > max_lsn_written) {
      max_lsn_written = rec->lsn;
    }
  }

  /* Update flushed_lsn to reflect what's now on disk */
  wal.flushed_lsn = max_lsn_written;

  lock_release(&wal.wal_lock);
}

/* ============================================================
 * CHECKPOINTING
 * ============================================================ */

void wal_checkpoint(void) {
  /*
   * Create a checkpoint to apply committed changes to data pages.
   *
   * HIGH-LEVEL OVERVIEW:
   * In WAL, changes are logged BEFORE being applied to data pages. A checkpoint
   * ensures that all committed transactions have their changes applied to (flushed
   * to) the actual data pages on disk. This establishes a known-good state where
   * the data on disk matches all committed log records up to the checkpoint LSN.
   *
   * WHAT CHECKPOINTING DOES:
   * - Applies/flushes committed changes: All dirty data pages containing
   *   modifications from committed transactions are written to disk
   * - Marks a recovery point: After a checkpoint, recovery only needs to
   *   process log records after the checkpoint (not before)
   * - Reduces recovery time: Instead of scanning from LSN 1, recovery can
   *   start from checkpoint_lsn, dramatically reducing work
   *
   * CHECKPOINT PROCESS:
   * 1. Prevent recursive checkpoint calls using checkpointing flag
   * 2. Flush all dirty cache pages to disk (APPLIES committed changes to data)
   * 3. Flush all pending log records to disk (ensures log is up-to-date)
   * 4. Create and append a WAL_CHECKPOINT record (marks this point in log)
   * 5. Flush the checkpoint record to disk immediately
   * 6. Clear the checkpointing flag
   *
   * KEY INSIGHT:
   * After a checkpoint, all committed transactions up to checkpoint_lsn have
   * their data modifications on disk. If we crash, we only need to redo/undo
   * transactions that came after the checkpoint.
   *
   * CHECKPOINT TYPE:
   * This implements a "fuzzy" checkpoint - we allow active transactions to
   * continue during checkpointing. A "sharp" checkpoint would require all
   * transactions to complete first, which provides stronger guarantees but
   * can block normal operation.
   *
   * AUTOMATIC TRIGGERING:
   * Checkpoints are automatically triggered when the log reaches 75% capacity
   * (48 out of 64 sectors in use). This prevents log overflow and ensures
   * recovery remains efficient.
   */

  lock_acquire(&wal.wal_lock);

  /* Prevent recursive checkpoint calls */
  if (wal.checkpointing) {
    lock_release(&wal.wal_lock);
    return; /* Already checkpointing, skip */
  }
  wal.checkpointing = true;

  lock_release(&wal.wal_lock);

  /* STEP 1: Flush all dirty DATA pages to disk */
  /* This applies committed changes to the actual filesystem data sectors */
  /* Example: writes inode changes to inode sectors, file data to file sectors */
  cache_flush();

  /* STEP 2: Flush all pending LOG records to disk */
  /* This writes log records from memory buffer to log sectors (sectors 2-65) */
  /* Log records contain "before" and "after" images needed for recovery */
  if (wal.next_lsn > 0) {
    wal_flush(wal.next_lsn - 1);
  }

  /* STEP 3: Create and write checkpoint record */
  struct wal_record checkpoint_record;
  memset(&checkpoint_record, 0, sizeof(checkpoint_record));
  checkpoint_record.type = WAL_CHECKPOINT;
  checkpoint_record.txn_id = 0; /* Checkpoint is not part of a transaction */

  /* Append checkpoint record (this will update checkpoint_lsn automatically) */
  /* Note: wal_append_record() acquires its own lock, so we don't hold it here */
  wal_append_record(&checkpoint_record);

  /* STEP 4: Flush the checkpoint record to disk immediately */
  lock_acquire(&wal.wal_lock);
  lsn_t checkpoint_lsn = wal.next_lsn - 1;
  lock_release(&wal.wal_lock);
  wal_flush(checkpoint_lsn);
  lock_acquire(&wal.wal_lock);

  /* Clear checkpointing flag */
  wal.checkpointing = false;
  lock_release(&wal.wal_lock);
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

  lock_acquire(&wal.wal_lock);

  /* Assign the next LSN to this record */
  lsn_t assigned_lsn = wal.next_lsn;
  record->lsn = assigned_lsn;

  /* Calculate checksum (must be done after LSN is assigned, before copying to buffer) */
  record->checksum = 0; /* Clear checksum field for calculation */
  record->checksum = wal_calculate_checksum(record);

  /* Check if buffer has space, flush if needed - MUST be before copy to prevent overflow! */
  if (wal.buffer_used + sizeof(struct wal_record) > wal.buffer_size) {
    /* Flush buffer contents to disk (release lock, flush, re-acquire) */
    lsn_t current_next = wal.next_lsn;
    lock_release(&wal.wal_lock);
    wal_flush(current_next - 1);
    lock_acquire(&wal.wal_lock);
    wal.buffer_used = 0;
  }

  /* Now safe to copy - buffer has space guaranteed */
  memcpy(wal.log_buffer + wal.buffer_used, record, sizeof(struct wal_record));
  wal.buffer_used += sizeof(struct wal_record);

  /* If this is a checkpoint record, update checkpoint_lsn */
  if (record->type == WAL_CHECKPOINT) {
    wal.checkpoint_lsn = assigned_lsn;
  }

  /* Increment next_lsn for the next record */
  wal.next_lsn++;

  /* Check if log is 75% full and trigger checkpoint if needed */
  /* Calculate how many sectors are in use since last checkpoint */
  lsn_t log_used;
  if (wal.checkpoint_lsn == 0) {
    /* No checkpoint yet - all records since LSN 1 are "in use" */
    log_used = wal.next_lsn - 1;
  } else {
    /* Calculate sectors used since checkpoint (handles wraparound) */
    log_used = wal.next_lsn - wal.checkpoint_lsn;
  }

  /* Trigger checkpoint when log is 75% full (48 out of 64 sectors) */
  bool should_checkpoint = (log_used >= (WAL_LOG_SECTORS * 3 / 4)) && !wal.checkpointing;

  lock_release(&wal.wal_lock);

  /* Trigger checkpoint outside the lock to avoid deadlock */
  if (should_checkpoint) {
    wal_checkpoint();
  }

  /* Return the LSN that was actually assigned to this record */
  return assigned_lsn;
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

/* CRC32 lookup table for polynomial 0xEDB88320 (reversed CRC32) */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d};

static uint32_t wal_calculate_checksum(struct wal_record* record) {
  /*
   * Calculate a CRC32 checksum for a log record to detect corruption during recovery.
   *
   * IMPLEMENTATION: CRC32 (polynomial 0xEDB88320, reversed)
   * - Standard CRC32 used by Ethernet, ZIP, PNG, etc.
   * - Calculates checksum over all bytes EXCEPT the checksum field itself
   * - Checksum field is at offset 16-19 (4 bytes)
   * - Struct is 512 bytes total
   *
   * CRC32 provides stronger corruption detection than XOR:
   * - Detects single-bit errors, burst errors, and many multi-bit errors
   * - Standard algorithm ensures compatibility with common tools
   */
  uint32_t crc = 0xFFFFFFFF; /* Initial value */
  uint8_t* bytes = (uint8_t*)record;
  size_t checksum_offset = offsetof(struct wal_record, checksum);
  size_t checksum_size = sizeof(uint32_t);

  /* Process all bytes before the checksum field */
  for (size_t i = 0; i < checksum_offset; i++) {
    uint8_t byte = bytes[i];
    crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF];
  }

  /* Process all bytes after the checksum field */
  for (size_t i = checksum_offset + checksum_size; i < sizeof(struct wal_record); i++) {
    uint8_t byte = bytes[i];
    crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF];
  }

  /* Return final CRC (inverted) */
  return ~crc;
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
