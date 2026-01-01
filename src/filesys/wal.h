#ifndef FILESYS_WAL_H
#define FILESYS_WAL_H

#include <list.h>
#include <stdbool.h>
#include <stdint.h>
#include "devices/block.h"
#include "threads/synch.h"

/*
 * Write-Ahead Logging (WAL) for Pintos Filesystem
 *
 * WAL ensures crash consistency by writing all modifications to a log
 * BEFORE applying them to the actual data. This allows recovery after
 * a crash by replaying (REDO) committed transactions and rolling back
 * (UNDO) uncommitted transactions.
 *
 * KEY PRINCIPLE: "Write the log first, then the data."
 *
 * DISK LAYOUT:
 *   Sector 0:      Free map inode
 *   Sector 1:      Root directory inode
 *   Sectors 2-65:  WAL log (64 sectors, circular)
 *   Sector 66:     WAL metadata
 *   Sectors 67+:   Filesystem data
 *
 * RECOVERY MODEL: Steal + UNDO/REDO
 *   - Steal: Buffer cache can evict dirty pages from uncommitted transactions
 *   - REDO: Replay committed transactions (data might not be on disk)
 *   - UNDO: Rollback uncommitted transactions (data might be on disk)
 */

/* ============================================================
 * CONFIGURATION CONSTANTS
 * ============================================================ */

#define WAL_LOG_SECTORS 64     /* Number of sectors reserved for the log */
#define WAL_LOG_START_SECTOR 2 /* First log sector on disk */
#define WAL_METADATA_SECTOR                                                                        \
  (WAL_LOG_START_SECTOR + WAL_LOG_SECTORS)      /* Metadata sector (sector 66) */
#define WAL_BUFFER_SIZE (8 * BLOCK_SECTOR_SIZE) /* In-memory buffer: 8 sectors = 4KB */
#define WAL_MAX_DATA_SIZE 232                   /* Max bytes of data per log record */
#define WAL_METADATA_MAGIC 0xDEADBEEF           /* Magic number for valid metadata */

/* ============================================================
 * TYPE DEFINITIONS
 * ============================================================ */

typedef uint32_t txn_id_t; /* Transaction identifier */
typedef uint64_t lsn_t;    /* Log Sequence Number */

/* ============================================================
 * LOG RECORD TYPES
 * ============================================================ */

enum wal_record_type {
  WAL_INVALID = 0, /* Empty/invalid record */
  WAL_BEGIN,       /* Transaction start marker */
  WAL_COMMIT,      /* Transaction committed successfully */
  WAL_ABORT,       /* Transaction aborted */
  WAL_WRITE,       /* Data write operation (contains old/new data) */
  WAL_CHECKPOINT,  /* Checkpoint marker */
};

/* ============================================================
 * TRANSACTION STATE
 * ============================================================ */

enum txn_state {
  TXN_ACTIVE,    /* Transaction in progress */
  TXN_COMMITTED, /* Transaction committed successfully */
  TXN_ABORTED,   /* Transaction aborted/rolled back */
};

/* Transaction handle returned by wal_txn_begin() */
struct wal_txn {
  txn_id_t txn_id;       /* Unique transaction identifier */
  enum txn_state state;  /* Current transaction state */
  struct list_elem elem; /* Element in active transactions list */
  lsn_t first_lsn;       /* LSN of BEGIN record (for rollback) */
  lsn_t last_lsn;        /* LSN of most recent record */
};

/* ============================================================
 * LOG RECORD STRUCTURE (ON-DISK FORMAT)
 *
 * One log record = one 512-byte sector.
 * Large writes (> 232 bytes) are split into multiple records
 * sharing the same txn_id.
 * ============================================================ */

struct wal_record {
  /* Header - 24 bytes */
  lsn_t lsn;                 /* Log sequence number (unique, increasing) */
  txn_id_t txn_id;           /* Which transaction wrote this record */
  enum wal_record_type type; /* Type of log record */
  uint32_t checksum;         /* CRC32 for corruption detection */

  /* For WAL_WRITE records - 8 bytes */
  block_sector_t sector; /* Which sector was modified */
  uint16_t offset;       /* Offset within the sector (0-511) */
  uint16_t length;       /* Length of the data (max WAL_MAX_DATA_SIZE) */

  /* Data payload - 480 bytes */
  uint8_t old_data[WAL_MAX_DATA_SIZE]; /* Before image (for UNDO) */
  uint8_t new_data[WAL_MAX_DATA_SIZE]; /* After image (for REDO) */

  uint8_t padding[20]; /* Pad to exactly 512 bytes */
};

_Static_assert(sizeof(struct wal_record) == BLOCK_SECTOR_SIZE,
               "wal_record must be exactly 512 bytes");

/* ============================================================
 * WAL METADATA STRUCTURE (ON-DISK FORMAT)
 *
 * Stored in sector 66 to track clean shutdown state and
 * persist LSN/txn_id information across reboots.
 * ============================================================ */

struct wal_metadata {
  uint32_t magic;          /* Magic number (WAL_METADATA_MAGIC) */
  uint32_t clean_shutdown; /* 1 = clean shutdown, 0 = dirty/crashed */
  lsn_t last_lsn;          /* Last LSN written */
  txn_id_t last_txn_id;    /* Last transaction ID */
  uint8_t padding[492];    /* Pad to exactly 512 bytes */
};

_Static_assert(sizeof(struct wal_metadata) == BLOCK_SECTOR_SIZE,
               "wal_metadata must be exactly 512 bytes");

/* ============================================================
 * WAL MANAGER STATE (IN-MEMORY)
 * ============================================================ */

struct wal_manager {
  struct lock wal_lock; /* Protects all WAL state */

  /* LSN management */
  lsn_t next_lsn;    /* Next LSN to assign */
  lsn_t flushed_lsn; /* All records up to here are on disk */

  /* Transaction ID management */
  txn_id_t next_txn_id; /* Next transaction ID to assign */

  /* Log buffer (in-memory, flushed to disk periodically) */
  uint8_t* log_buffer; /* Buffer for log records */
  size_t buffer_size;  /* Size of the buffer */
  size_t buffer_used;  /* Bytes currently in buffer */

  /* Active transaction tracking */
  struct list active_txns; /* List of active transactions */

  /* Checkpoint management */
  lsn_t checkpoint_lsn; /* LSN of last checkpoint (0 if none) */
  bool checkpointing;   /* Prevents recursive checkpoint calls */

  /* Statistics (for testing/verification) */
  uint32_t stats_txn_begun;
  uint32_t stats_txn_committed;
  uint32_t stats_txn_aborted;
  uint32_t stats_writes_logged;
};

/* Statistics structure for testing */
struct wal_stats {
  uint32_t txn_begun;
  uint32_t txn_committed;
  uint32_t txn_aborted;
  uint32_t writes_logged;
};

/* Global WAL manager instance */
extern struct wal_manager wal;

/* ============================================================
 * PUBLIC API
 * ============================================================ */

/* --- Initialization and Shutdown --- */

/* Initialize the WAL subsystem. Called at filesystem mount.
 * If format is true, initializes for a fresh filesystem. */
void wal_init(bool format);

/* Initialize WAL metadata sector. Called during filesystem format. */
void wal_init_metadata(void);

/* Shutdown the WAL subsystem. Called at filesystem unmount.
 * Flushes all pending records and writes clean shutdown marker. */
void wal_shutdown(void);

/* --- Transaction Management --- */

/* Begin a new transaction. Returns a transaction handle.
 * Writes a BEGIN record to the log. */
struct wal_txn* wal_txn_begin(void);

/* Commit a transaction. Writes COMMIT record and flushes log.
 * Returns true on success. Transaction is freed after commit. */
bool wal_txn_commit(struct wal_txn* txn);

/* Abort a transaction. Undoes all changes by restoring old_data.
 * Writes ABORT record. Transaction is freed after abort. */
void wal_txn_abort(struct wal_txn* txn);

/* --- Logging Operations --- */

/* Log a write operation BEFORE modifying the actual data.
 * Splits large writes (> WAL_MAX_DATA_SIZE) into multiple records.
 * Returns true on success. */
bool wal_log_write(struct wal_txn* txn, block_sector_t sector, const void* old_data,
                   const void* new_data, uint16_t offset, uint16_t length);

/* Force all log records up to the given LSN to disk. */
void wal_flush(lsn_t up_to_lsn);

/* --- Checkpointing --- */

/* Create a checkpoint. Flushes all dirty data to disk and
 * writes a CHECKPOINT record. Reduces recovery time. */
void wal_checkpoint(void);

/* --- Recovery --- */

/* Recover the filesystem after a crash.
 * Phase 1: Analysis - identify committed/uncommitted transactions
 * Phase 2: REDO - replay committed transactions
 * Phase 3: UNDO - rollback uncommitted transactions */
void wal_recover(void);

/* --- Thread-Local Transaction Management --- */

/* Set the current transaction for the running thread.
 * Use NULL to clear the current transaction. */
void wal_set_current_txn(struct wal_txn* txn);

/* Get the current transaction for the running thread.
 * Returns NULL if no transaction is active. */
struct wal_txn* wal_get_current_txn(void);

/* --- Statistics --- */

/* Get WAL statistics for testing/verification. */
void wal_get_stats(struct wal_stats* stats);

/* Reset WAL statistics to zero. */
void wal_reset_stats(void);

#endif /* filesys/wal.h */
