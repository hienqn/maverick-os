/*
 * Write-Ahead Logging (WAL) Implementation for Pintos Filesystem
 *
 * This module implements crash-consistent logging using the WAL technique.
 * All filesystem modifications are written to the log BEFORE being applied
 * to the actual data, enabling recovery after crashes.
 *
 * Recovery Model: Steal + UNDO/REDO (simplified ARIES)
 * - REDO: Replay committed transactions (data might not be on disk)
 * - UNDO: Rollback uncommitted transactions (data might be on disk)
 *
 * See wal.h for detailed documentation of the public API.
 */

#include "filesys/wal.h"
#include "filesys/cache.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Global WAL manager instance */
struct wal_manager wal;

/* Reference to the filesystem block device */
extern struct block* fs_device;

/* Flag to defer checkpoint (set when checkpoint needed but can't run safely) */
static bool checkpoint_pending = false;

/* Structure for tracking records to undo during recovery */
struct undo_entry {
  block_sector_t sector;
  uint16_t offset;
  uint16_t length;
  uint8_t old_data[WAL_MAX_DATA_SIZE];
  lsn_t lsn;
};

/* Forward declarations for internal helper functions */
static lsn_t wal_append_record(struct wal_record* record);
static bool wal_read_record(lsn_t lsn, struct wal_record* record);
static uint32_t wal_calculate_checksum(struct wal_record* record);

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

/* Initialize metadata sector (called during filesystem format) */
void wal_init_metadata(void) {
  struct wal_metadata meta;
  memset(&meta, 0, sizeof(meta));
  meta.magic = WAL_METADATA_MAGIC;
  meta.clean_shutdown = 1;
  meta.last_lsn = 0;
  meta.last_txn_id = 0;
  wal_write_metadata(&meta);
}

/* ============================================================
 * INITIALIZATION AND SHUTDOWN
 * ============================================================ */

void wal_init(bool format) {
  lock_init(&wal.wal_lock);

  if (format) {
    /* Fresh filesystem: initialize in-memory state */
    wal.next_lsn = 1;
    wal.flushed_lsn = 0;
    wal.next_txn_id = 1;

    wal.log_buffer = malloc(WAL_BUFFER_SIZE);
    if (wal.log_buffer == NULL) {
      PANIC("Failed to allocate WAL log buffer");
    }
    wal.buffer_size = WAL_BUFFER_SIZE;
    wal.buffer_used = 0;

    wal.stats_txn_begun = 0;
    wal.stats_txn_committed = 0;
    wal.stats_txn_aborted = 0;
    wal.stats_writes_logged = 0;

    list_init(&wal.active_txns);
    wal.checkpoint_lsn = 0;
    wal.checkpointing = false;
    return;
  }

  /* Existing filesystem: read metadata and check for crash */
  struct wal_metadata meta;
  wal_read_metadata(&meta);

  bool metadata_valid = (meta.magic == WAL_METADATA_MAGIC);

  if (!metadata_valid) {
    /* Corrupted metadata: reinitialize */
    wal_init_metadata();
    wal.next_lsn = 1;
    wal.flushed_lsn = 0;
    wal.next_txn_id = 1;
    meta.clean_shutdown = 0;
    wal_write_metadata(&meta);
  } else {
    bool clean_shutdown = (meta.clean_shutdown == 1);

    if (!clean_shutdown) {
      /* Crash detected: run recovery */
      wal_recover();
    } else {
      /* Clean shutdown: restore state from metadata */
      if (meta.last_lsn > 0) {
        wal.next_lsn = meta.last_lsn + 1;
        wal.flushed_lsn = meta.last_lsn;
      } else {
        wal.next_lsn = 1;
        wal.flushed_lsn = 0;
      }
      wal.next_txn_id = meta.last_txn_id + 1;
    }

    /* Mark as dirty for this session */
    meta.clean_shutdown = 0;
    wal_write_metadata(&meta);
  }

  /* Allocate log buffer */
  wal.log_buffer = malloc(WAL_BUFFER_SIZE);
  if (wal.log_buffer == NULL) {
    PANIC("Failed to allocate WAL log buffer");
  }
  wal.buffer_size = WAL_BUFFER_SIZE;
  wal.buffer_used = 0;

  /* Initialize statistics and transaction tracking */
  wal.stats_txn_begun = 0;
  wal.stats_txn_committed = 0;
  wal.stats_txn_aborted = 0;
  wal.stats_writes_logged = 0;

  list_init(&wal.active_txns);
  wal.checkpoint_lsn = 0;
  wal.checkpointing = false;
}

void wal_shutdown(void) {
  /* Flush all pending log records to disk */
  if (wal.next_lsn > 0) {
    wal_flush(wal.next_lsn - 1);
  }

  /* Write clean shutdown marker */
  struct wal_metadata meta;
  wal_read_metadata(&meta);
  meta.magic = WAL_METADATA_MAGIC;
  meta.clean_shutdown = 1;
  meta.last_lsn = (wal.next_lsn > 0) ? wal.next_lsn - 1 : 0;
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
  struct wal_txn* txn = malloc(sizeof(struct wal_txn));
  if (txn == NULL)
    return NULL;

  /* Assign unique transaction ID */
  lock_acquire(&wal.wal_lock);
  txn->txn_id = wal.next_txn_id++;
  wal.stats_txn_begun++;
  lock_release(&wal.wal_lock);

  txn->state = TXN_ACTIVE;

  /* Write BEGIN record to log */
  struct wal_record rec;
  memset(&rec, 0, sizeof(rec));
  rec.type = WAL_BEGIN;
  rec.txn_id = txn->txn_id;
  lsn_t begin_lsn = wal_append_record(&rec);

  txn->first_lsn = begin_lsn;
  txn->last_lsn = begin_lsn;

  /* Add to active transaction list */
  lock_acquire(&wal.wal_lock);
  list_push_back(&wal.active_txns, &txn->elem);
  lock_release(&wal.wal_lock);

  return txn;
}

bool wal_txn_commit(struct wal_txn* txn) {
  if (txn == NULL || txn->state != TXN_ACTIVE) {
    return false;
  }

  /* Write COMMIT record to log */
  struct wal_record rec;
  memset(&rec, 0, sizeof(rec));
  rec.type = WAL_COMMIT;
  rec.txn_id = txn->txn_id;
  lsn_t commit_lsn = wal_append_record(&rec);

  /* Flush log to disk - this is the durability point */
  wal_flush(commit_lsn);

  txn->state = TXN_COMMITTED;

  /* Remove from active transaction list */
  lock_acquire(&wal.wal_lock);
  list_remove(&txn->elem);
  wal.stats_txn_committed++;
  lock_release(&wal.wal_lock);

  free(txn);

  /* Note: Deferred checkpoint is handled at shutdown or explicit checkpoint call.
     We don't trigger it here to avoid stack overflow from recursive calls
     when cache_write -> wal_txn_commit -> checkpoint -> cache_flush. */

  return true;
}

void wal_txn_abort(struct wal_txn* txn) {
  if (txn == NULL || txn->state != TXN_ACTIVE) {
    return;
  }

  /* Collect all WRITE records for this transaction.
   * Use heap allocation to avoid stack overflow (Pintos has 4KB kernel stack).
   * MAX_UNDO_RECORDS * sizeof(lsn_t) = 64 * 8 = 512 bytes on heap. */
#define MAX_UNDO_RECORDS 64
  lsn_t* undo_lsns = malloc(MAX_UNDO_RECORDS * sizeof(lsn_t));
  if (undo_lsns == NULL) {
    /* Can't undo without memory - at least mark as aborted */
    txn->state = TXN_ABORTED;
    lock_acquire(&wal.wal_lock);
    list_remove(&txn->elem);
    wal.stats_txn_aborted++;
    lock_release(&wal.wal_lock);
    free(txn);
    return;
  }
  size_t undo_count = 0;

  lock_acquire(&wal.wal_lock);
  lsn_t end_lsn = wal.next_lsn;
  lock_release(&wal.wal_lock);

  /* Flush log buffer to disk so we can read the records back */
  wal_flush(end_lsn);

  /* Use heap-allocated record buffer to minimize stack usage */
  struct wal_record* rec = malloc(sizeof(struct wal_record));
  if (rec == NULL) {
    free(undo_lsns);
    txn->state = TXN_ABORTED;
    lock_acquire(&wal.wal_lock);
    list_remove(&txn->elem);
    wal.stats_txn_aborted++;
    lock_release(&wal.wal_lock);
    free(txn);
    return;
  }

  for (lsn_t lsn = txn->first_lsn; lsn < end_lsn && undo_count < MAX_UNDO_RECORDS; lsn++) {
    if (!wal_read_record(lsn, rec)) {
      continue;
    }

    if (rec->txn_id == txn->txn_id && rec->type == WAL_WRITE) {
      undo_lsns[undo_count++] = lsn;
    }
  }

  /* Process WRITE records in REVERSE order, restoring old_data.
   * We use cache_write() instead of block_write() so the cache stays
   * consistent with disk after the UNDO. */
  for (size_t i = undo_count; i > 0; i--) {
    if (!wal_read_record(undo_lsns[i - 1], rec)) {
      continue;
    }

    /* Write the old_data back through the cache */
    cache_write(rec->sector, rec->old_data, rec->offset, rec->length);
  }

  /* Flush cache to ensure UNDO is on disk */
  cache_flush();

  /* Write ABORT record to log */
  memset(rec, 0, sizeof(*rec));
  rec->type = WAL_ABORT;
  rec->txn_id = txn->txn_id;
  wal_append_record(rec);

  free(rec);
  free(undo_lsns);

  txn->state = TXN_ABORTED;

  /* Remove from active transaction list */
  lock_acquire(&wal.wal_lock);
  list_remove(&txn->elem);
  wal.stats_txn_aborted++;
  lock_release(&wal.wal_lock);

  free(txn);
}

/* ============================================================
 * LOGGING OPERATIONS
 * ============================================================ */

bool wal_log_write(struct wal_txn* txn, block_sector_t sector, const void* old_data,
                   const void* new_data, uint16_t offset, uint16_t length) {
  if (txn == NULL || txn->state != TXN_ACTIVE) {
    return false;
  }

  const uint8_t* old_bytes = (const uint8_t*)old_data;
  const uint8_t* new_bytes = (const uint8_t*)new_data;

  uint16_t bytes_logged = 0;

  /* Split large writes into multiple records */
  while (bytes_logged < length) {
    uint16_t chunk_size = length - bytes_logged;
    if (chunk_size > WAL_MAX_DATA_SIZE) {
      chunk_size = WAL_MAX_DATA_SIZE;
    }

    struct wal_record rec;
    memset(&rec, 0, sizeof(rec));
    rec.type = WAL_WRITE;
    rec.txn_id = txn->txn_id;
    rec.sector = sector;
    rec.offset = offset + bytes_logged;
    rec.length = chunk_size;

    memcpy(rec.old_data, old_bytes + bytes_logged, chunk_size);
    memcpy(rec.new_data, new_bytes + bytes_logged, chunk_size);

    lsn_t lsn = wal_append_record(&rec);
    txn->last_lsn = lsn;

    bytes_logged += chunk_size;
  }

  lock_acquire(&wal.wal_lock);
  wal.stats_writes_logged++;
  lock_release(&wal.wal_lock);

  return true;
}

void wal_flush(lsn_t up_to_lsn) {
  lock_acquire(&wal.wal_lock);

  if (up_to_lsn <= wal.flushed_lsn) {
    lock_release(&wal.wal_lock);
    return;
  }

  if (wal.buffer_used == 0) {
    lock_release(&wal.wal_lock);
    return;
  }

  /* Write each record to its corresponding disk sector */
  size_t num_records = wal.buffer_used / sizeof(struct wal_record);
  lsn_t max_lsn_written = wal.flushed_lsn;

  for (size_t i = 0; i < num_records; i++) {
    struct wal_record* rec = (struct wal_record*)(wal.log_buffer + i * sizeof(struct wal_record));
    block_sector_t sector = WAL_LOG_START_SECTOR + ((rec->lsn - 1) % WAL_LOG_SECTORS);

    /* Write directly to disk (bypass cache for durability) */
    block_write(fs_device, sector, rec);

    if (rec->lsn > max_lsn_written) {
      max_lsn_written = rec->lsn;
    }
  }

  wal.flushed_lsn = max_lsn_written;
  lock_release(&wal.wal_lock);
}

/* ============================================================
 * CHECKPOINTING
 * ============================================================ */

void wal_checkpoint(void) {
  lock_acquire(&wal.wal_lock);

  if (wal.checkpointing) {
    lock_release(&wal.wal_lock);
    return;
  }
  wal.checkpointing = true;

  lock_release(&wal.wal_lock);

  /* Flush all dirty data pages to disk */
  cache_flush();

  /* Flush all pending log records to disk */
  if (wal.next_lsn > 0) {
    wal_flush(wal.next_lsn - 1);
  }

  /* Write checkpoint record */
  struct wal_record checkpoint_record;
  memset(&checkpoint_record, 0, sizeof(checkpoint_record));
  checkpoint_record.type = WAL_CHECKPOINT;
  checkpoint_record.txn_id = 0;
  wal_append_record(&checkpoint_record);

  /* Flush the checkpoint record immediately */
  lock_acquire(&wal.wal_lock);
  lsn_t checkpoint_lsn = wal.next_lsn - 1;
  lock_release(&wal.wal_lock);
  wal_flush(checkpoint_lsn);

  lock_acquire(&wal.wal_lock);
  wal.checkpointing = false;
  lock_release(&wal.wal_lock);
}

/* ============================================================
 * RECOVERY
 * ============================================================ */

void wal_recover(void) {
  /*
   * Three-phase recovery (simplified ARIES):
   * 1. ANALYSIS: Scan log, categorize transactions
   * 2. REDO: Replay committed transactions
   * 3. UNDO: Rollback uncommitted transactions
   */

#define MAX_TXNS 256
  txn_id_t committed_txns[MAX_TXNS];
  size_t committed_count = 0;
  txn_id_t uncommitted_txns[MAX_TXNS];
  size_t uncommitted_count = 0;

  txn_id_t seen_txns[MAX_TXNS];
  bool txn_committed[MAX_TXNS];
  bool txn_aborted[MAX_TXNS];
  size_t seen_count = 0;

  lsn_t max_lsn = 0;

  /* Phase 1: ANALYSIS - Scan log to categorize transactions */
  for (block_sector_t s = 0; s < WAL_LOG_SECTORS; s++) {
    struct wal_record rec;
    block_sector_t sector = WAL_LOG_START_SECTOR + s;
    block_read(fs_device, sector, &rec);

    /* Verify checksum */
    uint32_t stored = rec.checksum;
    rec.checksum = 0;
    uint32_t calculated = wal_calculate_checksum(&rec);
    rec.checksum = stored;

    if (stored != calculated || rec.lsn == 0) {
      continue;
    }

    if (rec.lsn > max_lsn) {
      max_lsn = rec.lsn;
    }

    /* Find or add transaction to tracking */
    size_t txn_idx = MAX_TXNS;
    for (size_t i = 0; i < seen_count; i++) {
      if (seen_txns[i] == rec.txn_id) {
        txn_idx = i;
        break;
      }
    }
    if (txn_idx == MAX_TXNS && seen_count < MAX_TXNS && rec.txn_id != 0) {
      txn_idx = seen_count++;
      seen_txns[txn_idx] = rec.txn_id;
      txn_committed[txn_idx] = false;
      txn_aborted[txn_idx] = false;
    }

    if (txn_idx < MAX_TXNS) {
      if (rec.type == WAL_COMMIT) {
        txn_committed[txn_idx] = true;
      } else if (rec.type == WAL_ABORT) {
        txn_aborted[txn_idx] = true;
      }
    }
  }

  /* Categorize transactions */
  for (size_t i = 0; i < seen_count; i++) {
    if (txn_committed[i]) {
      committed_txns[committed_count++] = seen_txns[i];
    } else if (!txn_aborted[i]) {
      uncommitted_txns[uncommitted_count++] = seen_txns[i];
    }
  }

  /* DEBUG: Print transaction categorization */
  printf("WAL RECOVERY: Found %u transactions\n", (unsigned)seen_count);
  printf("  Committed: %u, Uncommitted: %u\n", (unsigned)committed_count,
         (unsigned)uncommitted_count);
  for (size_t i = 0; i < committed_count; i++) {
    printf("    Committed txn_id=%u\n", committed_txns[i]);
  }
  for (size_t i = 0; i < uncommitted_count; i++) {
    printf("    Uncommitted txn_id=%u\n", uncommitted_txns[i]);
  }

  /* Phase 2: REDO - Replay committed transactions */
  for (block_sector_t s = 0; s < WAL_LOG_SECTORS; s++) {
    struct wal_record rec;
    block_sector_t sector = WAL_LOG_START_SECTOR + s;
    block_read(fs_device, sector, &rec);

    uint32_t stored = rec.checksum;
    rec.checksum = 0;
    uint32_t calculated = wal_calculate_checksum(&rec);
    rec.checksum = stored;
    if (stored != calculated)
      continue;

    if (rec.type != WAL_WRITE)
      continue;

    bool is_committed = false;
    for (size_t i = 0; i < committed_count; i++) {
      if (committed_txns[i] == rec.txn_id) {
        is_committed = true;
        break;
      }
    }

    if (is_committed) {
      uint8_t sector_data[BLOCK_SECTOR_SIZE];
      block_read(fs_device, rec.sector, sector_data);
      memcpy(sector_data + rec.offset, rec.new_data, rec.length);
      block_write(fs_device, rec.sector, sector_data);
    }
  }

  /* Phase 3: UNDO - Rollback uncommitted transactions (in reverse LSN order) */
#define MAX_UNDO 512
  struct undo_entry undo_records[MAX_UNDO];
  size_t undo_count = 0;

  for (block_sector_t s = 0; s < WAL_LOG_SECTORS; s++) {
    struct wal_record rec;
    block_sector_t sector = WAL_LOG_START_SECTOR + s;
    block_read(fs_device, sector, &rec);

    uint32_t stored = rec.checksum;
    rec.checksum = 0;
    uint32_t calculated = wal_calculate_checksum(&rec);
    rec.checksum = stored;
    if (stored != calculated)
      continue;

    if (rec.type != WAL_WRITE)
      continue;

    bool is_uncommitted = false;
    for (size_t i = 0; i < uncommitted_count; i++) {
      if (uncommitted_txns[i] == rec.txn_id) {
        is_uncommitted = true;
        break;
      }
    }

    if (is_uncommitted && undo_count < MAX_UNDO) {
      undo_records[undo_count].sector = rec.sector;
      undo_records[undo_count].offset = rec.offset;
      undo_records[undo_count].length = rec.length;
      memcpy(undo_records[undo_count].old_data, rec.old_data, rec.length);
      undo_records[undo_count].lsn = rec.lsn;
      undo_count++;
    }
  }

  /* Sort by LSN descending for proper undo order */
  for (size_t i = 0; i < undo_count; i++) {
    for (size_t j = i + 1; j < undo_count; j++) {
      if (undo_records[j].lsn > undo_records[i].lsn) {
        struct undo_entry tmp = undo_records[i];
        undo_records[i] = undo_records[j];
        undo_records[j] = tmp;
      }
    }
  }

  /* DEBUG: Print UNDO records */
  printf("WAL RECOVERY: UNDO phase - %u records to undo\n", (unsigned)undo_count);

  /* Apply UNDO in reverse LSN order */
  for (size_t i = 0; i < undo_count; i++) {
    uint8_t sector_data[BLOCK_SECTOR_SIZE];
    block_read(fs_device, undo_records[i].sector, sector_data);
    printf("  UNDO: sector %u, offset %u, length %u, old_data[0]='%c'\n", undo_records[i].sector,
           undo_records[i].offset, undo_records[i].length, undo_records[i].old_data[0]);
    printf("    Before: sector_data[0]='%c'\n", sector_data[0]);
    memcpy(sector_data + undo_records[i].offset, undo_records[i].old_data, undo_records[i].length);
    printf("    After:  sector_data[0]='%c'\n", sector_data[0]);
    block_write(fs_device, undo_records[i].sector, sector_data);
  }

  /* Update WAL state for continued operation */
  wal.next_lsn = max_lsn + 1;
  wal.flushed_lsn = max_lsn;
  wal.next_txn_id = 1;
  for (size_t i = 0; i < seen_count; i++) {
    if (seen_txns[i] >= wal.next_txn_id) {
      wal.next_txn_id = seen_txns[i] + 1;
    }
  }
}

/* ============================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================ */

/* Append a log record to the in-memory buffer.
 * Returns the assigned LSN. Flushes buffer if full. */
static lsn_t wal_append_record(struct wal_record* record) {
  lock_acquire(&wal.wal_lock);

  lsn_t assigned_lsn = wal.next_lsn;
  record->lsn = assigned_lsn;

  /* Calculate checksum after LSN is assigned */
  record->checksum = 0;
  record->checksum = wal_calculate_checksum(record);

  /* Flush buffer if full (must check before copy) */
  if (wal.buffer_used + sizeof(struct wal_record) > wal.buffer_size) {
    lsn_t current_next = wal.next_lsn;
    lock_release(&wal.wal_lock);
    wal_flush(current_next - 1);
    lock_acquire(&wal.wal_lock);
    wal.buffer_used = 0;
  }

  /* Copy record to buffer */
  memcpy(wal.log_buffer + wal.buffer_used, record, sizeof(struct wal_record));
  wal.buffer_used += sizeof(struct wal_record);

  if (record->type == WAL_CHECKPOINT) {
    wal.checkpoint_lsn = assigned_lsn;
  }

  wal.next_lsn++;

  /* Trigger checkpoint when log is 75% full */
  lsn_t log_used;
  if (wal.checkpoint_lsn == 0) {
    log_used = wal.next_lsn - 1;
  } else {
    log_used = wal.next_lsn - wal.checkpoint_lsn;
  }

  /* Mark checkpoint as pending if log is 75% full (don't trigger here to avoid recursion) */
  if ((log_used >= (WAL_LOG_SECTORS * 3 / 4)) && !wal.checkpointing) {
    checkpoint_pending = true;
  }

  lock_release(&wal.wal_lock);

  return assigned_lsn;
}

/* Read a log record from disk given its LSN.
 * Returns true if record is valid, false if corrupted or overwritten. */
static bool wal_read_record(lsn_t lsn, struct wal_record* record) {
  if (lsn == 0 || record == NULL) {
    return false;
  }

  block_sector_t sector = WAL_LOG_START_SECTOR + ((lsn - 1) % WAL_LOG_SECTORS);
  block_read(fs_device, sector, record);

  /* Verify LSN matches (log may have wrapped) */
  if (record->lsn != lsn) {
    return false;
  }

  /* Verify checksum */
  uint32_t stored_checksum = record->checksum;
  record->checksum = 0;
  uint32_t calculated_checksum = wal_calculate_checksum(record);
  record->checksum = stored_checksum;

  return (stored_checksum == calculated_checksum);
}

/* CRC32 lookup table for polynomial 0xEDB88320 */
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

/* Calculate CRC32 checksum for a log record (excludes checksum field) */
static uint32_t wal_calculate_checksum(struct wal_record* record) {
  uint32_t crc = 0xFFFFFFFF;
  uint8_t* bytes = (uint8_t*)record;
  size_t checksum_offset = offsetof(struct wal_record, checksum);
  size_t checksum_size = sizeof(uint32_t);

  /* Process bytes before checksum field */
  for (size_t i = 0; i < checksum_offset; i++) {
    crc = (crc >> 8) ^ crc32_table[(crc ^ bytes[i]) & 0xFF];
  }

  /* Process bytes after checksum field */
  for (size_t i = checksum_offset + checksum_size; i < sizeof(struct wal_record); i++) {
    crc = (crc >> 8) ^ crc32_table[(crc ^ bytes[i]) & 0xFF];
  }

  return ~crc;
}

/* ============================================================
 * THREAD-LOCAL TRANSACTION MANAGEMENT
 * ============================================================ */

void wal_set_current_txn(struct wal_txn* txn) {
  struct thread* t = thread_current();
  t->current_txn = txn;
}

struct wal_txn* wal_get_current_txn(void) {
  struct thread* t = thread_current();
  return t->current_txn;
}

/* ============================================================
 * STATISTICS
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
