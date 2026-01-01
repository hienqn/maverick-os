#ifndef FILESYS_WAL_H
#define FILESYS_WAL_H

#include <stdbool.h>
#include <stdint.h>
#include "devices/block.h"
#include "threads/synch.h"
#include <list.h>

/*
 * Write-Ahead Logging (WAL) for Pintos Filesystem
 *
 * WAL ensures crash consistency by writing all modifications to a log
 * BEFORE applying them to the actual data. This allows recovery after
 * a crash by replaying (or undoing) logged operations.
 *
 * KEY PRINCIPLE: "Write the log first, then the data."
 */

/* ============================================================
 * CONFIGURATION CONSTANTS
 * ============================================================
 *
 * QUESTIONS TO THINK ABOUT:
 * - How many log sectors do you need? What's the tradeoff between
 *   log size and available data space?
 * - What happens when the log becomes full?
 * - How does log size affect checkpoint frequency?
 */

#define WAL_LOG_SECTORS 64     /* Number of sectors reserved for the log */
#define WAL_LOG_START_SECTOR 2 /* Where does the log begin on disk? */
#define WAL_METADATA_SECTOR                                                                        \
  (WAL_LOG_START_SECTOR + WAL_LOG_SECTORS) /* Metadata sector (sector 66) */

/* Log buffer size - holds multiple log records before flushing to disk */
#define WAL_BUFFER_SIZE (8 * BLOCK_SECTOR_SIZE) /* 8 sectors = 4KB buffer */

/* ============================================================
 * LOG RECORD TYPES
 * ============================================================
 *
 * QUESTIONS TO THINK ABOUT:
 * - What operations need to be logged for crash recovery?
 * - Do you need both UNDO and REDO information, or just one?
 * - How do you mark transaction boundaries?
 */

enum wal_record_type {
  WAL_INVALID = 0, /* Empty/invalid record */
  WAL_BEGIN,       /* Transaction start marker */
  WAL_COMMIT,      /* Transaction committed successfully */
  WAL_ABORT,       /* Transaction aborted */
  WAL_WRITE,       /* Data write operation */
  WAL_CHECKPOINT,  /* Checkpoint marker */

  /* TODO: Add more record types if needed for your design */
};

/* ============================================================
 * TRANSACTION STRUCTURE
 * ============================================================
 *
 * QUESTIONS TO THINK ABOUT:
 * - What state does a transaction need to track?
 * - How do you handle nested transactions (or do you)?
 * - What's the lifecycle of a transaction object?
 */

typedef uint32_t txn_id_t;
typedef uint64_t lsn_t; /* Log Sequence Number */

enum txn_state {
  TXN_ACTIVE,
  TXN_COMMITTED,
  TXN_ABORTED,
};

struct wal_txn {
  txn_id_t txn_id;       /* Unique transaction identifier */
  enum txn_state state;  /* Current transaction state */
  struct list_elem elem; /* Element in active transactions list */

  lsn_t first_lsn;       /* LSN of BEGIN record (for rollback - scan from here) */
  lsn_t last_lsn;        /* LSN of most recent record (for efficient abort) */
};

/* ============================================================
 * LOG RECORD STRUCTURE (ON-DISK FORMAT)
 * ============================================================
 *
 * Design: One log record = one 512-byte sector (simple!)
 * Large writes are split into multiple records sharing the same txn_id.
 */

#define WAL_MAX_DATA_SIZE 232 /* Max bytes of data per record */

struct wal_record {
  /* Header - 24 bytes */
  lsn_t lsn;                 /* Log sequence number (unique, increasing) */
  txn_id_t txn_id;           /* Which transaction wrote this record */
  enum wal_record_type type; /* Type of log record */
  uint32_t checksum;         /* For detecting corruption */

  /* For WAL_WRITE records - 8 bytes */
  block_sector_t sector; /* Which sector was modified */
  uint16_t offset;       /* Offset within the sector (0-511) */
  uint16_t length;       /* Length of the data (max WAL_MAX_DATA_SIZE) */

  /* Data payload - 480 bytes
   * We store BOTH old and new data for UNDO/REDO capability.
   * Max 232 bytes each → 464 bytes total, plus padding.
   *
   * For writes larger than 232 bytes, split into multiple records.
   * All records share the same txn_id for atomicity.
   */
  uint8_t old_data[WAL_MAX_DATA_SIZE]; /* Before image (for UNDO) */
  uint8_t new_data[WAL_MAX_DATA_SIZE]; /* After image (for REDO) */

  uint8_t padding[20]; /* Pad to exactly 512 bytes */
};

/* Verify struct fits in one sector at compile time */
_Static_assert(sizeof(struct wal_record) == BLOCK_SECTOR_SIZE,
               "wal_record must be exactly 512 bytes");

/* ============================================================
 * WAL METADATA STRUCTURE (ON-DISK FORMAT)
 * ============================================================
 *
 * Stored in a dedicated sector to track clean shutdown state
 * and persist LSN information across reboots.
 */

#define WAL_METADATA_MAGIC 0xDEADBEEF /* Magic number to identify valid metadata */

struct wal_metadata {
  uint32_t magic;          /* Magic number (WAL_METADATA_MAGIC) */
  uint32_t clean_shutdown; /* 1 = clean shutdown, 0 = dirty/crashed */
  lsn_t last_lsn;          /* Last LSN written (for recovery) */
  txn_id_t last_txn_id;    /* Last transaction ID (optional) */
  uint8_t padding[492];    /* Pad to exactly 512 bytes (4+4+8+4+492=512) */
};

/* Verify struct fits in one sector at compile time */
_Static_assert(sizeof(struct wal_metadata) == BLOCK_SECTOR_SIZE,
               "wal_metadata must be exactly 512 bytes");

/* ============================================================
 * WAL MANAGER STATE (IN-MEMORY)
 * ============================================================
 *
 * QUESTIONS TO THINK ABOUT:
 * - What global state does the WAL system need?
 * - How do you synchronize access to the log?
 * - How do you track active transactions?
 */

struct wal_manager {
  struct lock wal_lock; /* Protects WAL state */

  lsn_t next_lsn;    /* Next LSN to assign */
  lsn_t flushed_lsn; /* All records up to here are on disk */

  txn_id_t next_txn_id; /* Next transaction ID to assign */

  /* Log buffer - holds records before writing to disk */
  uint8_t* log_buffer; /* In-memory log buffer */
  size_t buffer_size;  /* Size of the buffer */
  size_t buffer_used;  /* Bytes currently in buffer */

  /* Statistics - for verifying WAL is integrated */
  uint32_t stats_txn_begun;     /* Number of transactions started */
  uint32_t stats_txn_committed; /* Number of transactions committed */
  uint32_t stats_txn_aborted;   /* Number of transactions aborted */
  uint32_t stats_writes_logged; /* Number of write operations logged */

  /* Active transaction tracking */
  struct list active_txns; /* List of currently active transactions */

  /* Checkpoint information */
  lsn_t checkpoint_lsn; /* LSN of last checkpoint (0 if none) */
  bool checkpointing;   /* Flag to prevent recursive checkpoint calls */
};

/* WAL statistics structure (for testing/verification) */
struct wal_stats {
  uint32_t txn_begun;
  uint32_t txn_committed;
  uint32_t txn_aborted;
  uint32_t writes_logged;
};

/* Global WAL manager instance */
extern struct wal_manager wal;

/* ============================================================
 * WAL PUBLIC API
 * ============================================================
 *
 * These are the functions you need to implement.
 * Think about what each function should do before implementing.
 */

/* --- Initialization and Shutdown --- */

/* Initialize the WAL subsystem. Called once at filesystem mount.
 *
 * If format is true, we're formatting a fresh filesystem, so skip recovery
 * and initialize metadata fresh.
 *
 * QUESTIONS:
 * - What needs to be initialized?
 * - Should this trigger recovery if there was a crash?
 */
void wal_init(bool format);

/* Initialize WAL metadata sector. Called during filesystem format.
 * This sets up the metadata sector for a fresh filesystem.
 */
void wal_init_metadata(void);

/* Shutdown the WAL subsystem. Called at filesystem unmount.
 *
 * QUESTIONS:
 * - What needs to happen before shutdown is complete?
 * - What if there are active transactions?
 */
void wal_shutdown(void);

/* --- Transaction Management --- */

/* Begin a new transaction. Returns a transaction handle.
 *
 * QUESTIONS:
 * - What state needs to be set up for a new transaction?
 * - Should this write a BEGIN record to the log immediately?
 */
struct wal_txn* wal_txn_begin(void);

/* Commit a transaction. All changes become durable.
 *
 * QUESTIONS:
 * - What does "commit" mean in terms of the log?
 * - When is it safe to return from this function?
 * - What's the relationship between commit and log flush?
 */
bool wal_txn_commit(struct wal_txn* txn);

/* Abort a transaction. All changes are rolled back.
 *
 * QUESTIONS:
 * - How do you undo the changes made by this transaction?
 * - Do you need the old data images for this?
 */
void wal_txn_abort(struct wal_txn* txn);

/* --- Logging Operations --- */

/* Log a write operation BEFORE modifying the actual data.
 *
 * If length > WAL_MAX_DATA_SIZE, this function must split the write
 * into multiple log records. All records share the same txn_id.
 *
 * Example: Writing 512 bytes to sector 50 creates 3 records:
 *   Record 1: sector=50, offset=0,   length=232, data[0..231]
 *   Record 2: sector=50, offset=232, length=232, data[232..463]
 *   Record 3: sector=50, offset=464, length=48,  data[464..511]
 *
 * During recovery, all records with same txn_id are processed together.
 */
bool wal_log_write(struct wal_txn* txn, block_sector_t sector, const void* old_data,
                   const void* new_data, uint16_t offset, uint16_t length);

/* Force all log records up to the given LSN to disk.
 *
 * QUESTIONS:
 * - Why might you need to force a flush?
 * - What's the relationship between this and commit?
 */
void wal_flush(lsn_t up_to_lsn);

/* --- Checkpointing --- */

/* Create a checkpoint. Reduces recovery time.
 *
 * QUESTIONS:
 * - What is a checkpoint and why is it needed?
 * - What needs to be written/flushed during a checkpoint?
 * - How does a checkpoint affect recovery?
 */
void wal_checkpoint(void);

/* --- Recovery --- */

/* Recover the filesystem after a crash.
 *
 * QUESTIONS:
 * - How do you know if recovery is needed?
 * - What's the difference between UNDO and REDO recovery?
 * - In what order do you process log records during recovery?
 */
void wal_recover(void);

/* --- Statistics (for testing/verification) --- */

/* Get WAL statistics. Used by tests to verify WAL is integrated.
 * If stats show zero transactions after filesystem operations,
 * it means WAL is not properly integrated yet. */
void wal_get_stats(struct wal_stats* stats);

/* Reset WAL statistics to zero. Call before a test to get clean counts. */
void wal_reset_stats(void);

/* ============================================================
 * INTERNAL/HELPER FUNCTIONS (for your implementation)
 * ============================================================
 */

/* Append a record to the log buffer */
static lsn_t wal_append_record(struct wal_record* record);

/* Read a log record from disk */
static bool wal_read_record(lsn_t lsn, struct wal_record* record);

/* Calculate checksum for a log record */
static uint32_t wal_calculate_checksum(struct wal_record* record);

#endif /* filesys/wal.h */
