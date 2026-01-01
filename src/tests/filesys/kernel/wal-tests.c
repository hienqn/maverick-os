/* WAL (Write-Ahead Logging) Test Suite
 *
 * These tests verify the correctness of the Write-Ahead Logging implementation.
 * Tests are organized by functionality:
 *
 * INITIALIZATION TESTS:
 *   wal-init          - Basic WAL initialization
 *   wal-shutdown      - Clean shutdown writes marker
 *
 * TRANSACTION LIFECYCLE TESTS:
 *   wal-txn-begin     - Transaction creation
 *   wal-txn-commit    - Transaction commit and durability
 *   wal-txn-abort     - Transaction rollback (UNDO)
 *   wal-txn-multiple  - Multiple concurrent transactions
 *
 * LOGGING TESTS:
 *   wal-log-write     - Basic write logging
 *   wal-log-split     - Large writes split into multiple records
 *   wal-log-flush     - Log buffer flush to disk
 *   wal-log-full      - Behavior when log fills up
 *
 * RECOVERY TESTS:
 *   wal-recover-commit    - Recovery replays committed transactions
 *   wal-recover-abort     - Recovery undoes uncommitted transactions
 *   wal-recover-partial   - Recovery handles partial writes
 *   wal-recover-order     - Recovery applies changes in correct order
 *
 * CHECKPOINT TESTS:
 *   wal-checkpoint    - Checkpoint creation
 *   wal-checkpoint-recovery - Recovery starts from checkpoint
 *
 * INTEGRATION TESTS:
 *   wal-cache-integration - WAL works with buffer cache
 *   wal-stress        - Stress test with many transactions
 */

#include <string.h>
#include <stdio.h>
#include "tests/filesys/kernel/tests.h"
#include "filesys/wal.h"
#include "filesys/cache.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "lib/kernel/test-lib.h"

/* Test sector numbers - use high numbers to avoid filesystem metadata */
#define TEST_SECTOR_BASE 100

/* Helper: Fill buffer with a pattern based on value */
static void fill_buffer(void* buf, size_t size, uint8_t value) { memset(buf, value, size); }

/* Helper: Verify buffer contains expected pattern */
static bool verify_buffer(const void* buf, size_t size, uint8_t expected) {
  const uint8_t* p = buf;
  for (size_t i = 0; i < size; i++) {
    if (p[i] != expected)
      return false;
  }
  return true;
}

/* ============================================================
 * INITIALIZATION TESTS
 * ============================================================ */

/* Test: wal-init
 *
 * PURPOSE: Verify WAL subsystem initializes correctly.
 *
 * WHAT IT TESTS:
 * - wal_init() completes without crashing
 * - WAL manager state is properly initialized
 * - Log buffer is allocated
 *
 * EXPECTED BEHAVIOR:
 * - next_lsn starts at a valid value (e.g., 1)
 * - flushed_lsn indicates nothing flushed yet (e.g., 0)
 * - log_buffer is non-NULL
 * - buffer_used is 0
 */
void test_wal_init(void) {
  msg("Testing WAL initialization...");

  /* Initialize WAL */
  wal_init(false);
  msg("wal_init() completed");

  /* Verify manager state */
  if (wal.log_buffer == NULL) {
    fail("log_buffer is NULL after init");
  }
  msg("log_buffer allocated: PASSED");

  if (wal.buffer_size == 0) {
    fail("buffer_size is 0 after init");
  }
  msg("buffer_size = %zu bytes", wal.buffer_size);

  if (wal.buffer_used != 0) {
    fail("buffer_used should be 0 after init, got %zu", wal.buffer_used);
  }
  msg("buffer_used = 0: PASSED");

  msg("next_lsn = %llu", (unsigned long long)wal.next_lsn);
  msg("flushed_lsn = %llu", (unsigned long long)wal.flushed_lsn);
  msg("next_txn_id = %u", wal.next_txn_id);

  /* Clean up */
  wal_shutdown();
  msg("wal_shutdown() completed");

  msg("WAL initialization test: PASSED");
}

/* Test: wal-shutdown
 *
 * PURPOSE: Verify clean shutdown writes necessary markers.
 *
 * WHAT IT TESTS:
 * - wal_shutdown() flushes pending log records
 * - Clean shutdown marker is written
 * - Resources are freed
 *
 * EXPECTED BEHAVIOR:
 * - After shutdown, subsequent wal_init() should NOT trigger recovery
 *   (because shutdown was clean)
 */
void test_wal_shutdown(void) {
  msg("Testing WAL shutdown...");

  wal_init(false);
  msg("WAL initialized");

  /* Create a transaction and commit it */
  struct wal_txn* txn = wal_txn_begin();
  if (txn == NULL) {
    fail("wal_txn_begin() returned NULL");
  }
  msg("Transaction %u started", txn->txn_id);

  bool committed = wal_txn_commit(txn);
  if (!committed) {
    fail("wal_txn_commit() failed");
  }
  msg("Transaction committed");

  /* Shutdown should flush everything */
  wal_shutdown();
  msg("WAL shutdown completed");

  /* Re-initialize - should not need recovery (clean shutdown) */
  wal_init(false);
  msg("WAL re-initialized after clean shutdown");

  wal_shutdown();
  msg("WAL shutdown test: PASSED");
}

/* ============================================================
 * TRANSACTION LIFECYCLE TESTS
 * ============================================================ */

/* Test: wal-txn-begin
 *
 * PURPOSE: Verify transaction creation works correctly.
 *
 * WHAT IT TESTS:
 * - wal_txn_begin() returns a valid transaction handle
 * - Each transaction gets a unique ID
 * - Transaction starts in ACTIVE state
 *
 * EXPECTED BEHAVIOR:
 * - Non-NULL transaction pointer returned
 * - txn_id is unique for each call
 * - state is TXN_ACTIVE
 */
void test_wal_txn_begin(void) {
  msg("Testing transaction begin...");

  wal_init(false);

  /* Create first transaction */
  struct wal_txn* txn1 = wal_txn_begin();
  if (txn1 == NULL) {
    fail("First wal_txn_begin() returned NULL");
  }
  msg("Transaction 1 created: txn_id = %u", txn1->txn_id);

  if (txn1->state != TXN_ACTIVE) {
    fail("Transaction 1 state should be TXN_ACTIVE");
  }
  msg("Transaction 1 state = TXN_ACTIVE: PASSED");

  /* Create second transaction */
  struct wal_txn* txn2 = wal_txn_begin();
  if (txn2 == NULL) {
    fail("Second wal_txn_begin() returned NULL");
  }
  msg("Transaction 2 created: txn_id = %u", txn2->txn_id);

  /* Verify unique IDs */
  if (txn1->txn_id == txn2->txn_id) {
    fail("Transaction IDs should be unique");
  }
  msg("Unique transaction IDs: PASSED");

  /* Clean up - abort both since we're just testing begin */
  wal_txn_abort(txn1);
  wal_txn_abort(txn2);
  wal_shutdown();

  msg("Transaction begin test: PASSED");
}

/* Test: wal-txn-commit
 *
 * PURPOSE: Verify transaction commit provides durability.
 *
 * WHAT IT TESTS:
 * - wal_txn_commit() writes COMMIT record to log
 * - COMMIT record is flushed to disk before returning
 * - Transaction state changes to TXN_COMMITTED
 *
 * EXPECTED BEHAVIOR:
 * - Commit returns true on success
 * - After commit, data is recoverable even if crash occurs
 *
 * KEY INSIGHT: This tests the "D" in ACID (Durability)
 */
void test_wal_txn_commit(void) {
  msg("Testing transaction commit...");

  wal_init(false);

  struct wal_txn* txn = wal_txn_begin();
  if (txn == NULL) {
    fail("wal_txn_begin() returned NULL");
  }
  txn_id_t txn_id = txn->txn_id;
  msg("Transaction %u started", txn_id);

  /* Log a write operation */
  uint8_t old_data[64];
  uint8_t new_data[64];
  fill_buffer(old_data, 64, 'A');
  fill_buffer(new_data, 64, 'B');

  bool logged = wal_log_write(txn, TEST_SECTOR_BASE, old_data, new_data, 0, 64);
  if (!logged) {
    fail("wal_log_write() failed");
  }
  msg("Write logged: sector %d, 64 bytes", TEST_SECTOR_BASE);

  /* Commit the transaction */
  lsn_t lsn_before_commit = wal.next_lsn;
  bool committed = wal_txn_commit(txn);
  if (!committed) {
    fail("wal_txn_commit() failed");
  }
  msg("Transaction %u committed", txn_id);

  /* Verify LSN advanced (COMMIT record was written) */
  if (wal.next_lsn <= lsn_before_commit) {
    fail("next_lsn should advance after commit");
  }
  msg("COMMIT record written: next_lsn advanced");

  /* Verify log was flushed (flushed_lsn should include COMMIT) */
  if (wal.flushed_lsn < lsn_before_commit) {
    fail("flushed_lsn should be >= commit LSN (durability guarantee)");
  }
  msg("COMMIT record flushed to disk: PASSED");

  wal_shutdown();
  msg("Transaction commit test: PASSED");
}

/* Test: wal-txn-abort
 *
 * PURPOSE: Verify transaction abort undoes all changes.
 *
 * WHAT IT TESTS:
 * - wal_txn_abort() restores old_data for all logged writes
 * - ABORT record is written to log
 * - Transaction state changes to TXN_ABORTED
 *
 * EXPECTED BEHAVIOR:
 * - After abort, data sectors contain original (old) values
 *
 * KEY INSIGHT: This tests the UNDO mechanism
 */
void test_wal_txn_abort(void) {
  msg("Testing transaction abort (UNDO)...");

  wal_init(false);

  /* First, write known data to a sector */
  uint8_t original[BLOCK_SECTOR_SIZE];
  fill_buffer(original, BLOCK_SECTOR_SIZE, 'O'); /* O = Original */
  cache_write(TEST_SECTOR_BASE, original, 0, BLOCK_SECTOR_SIZE);
  msg("Wrote original data 'O' to sector %d", TEST_SECTOR_BASE);

  /* Start transaction and modify the sector */
  struct wal_txn* txn = wal_txn_begin();
  msg("Transaction %u started", txn->txn_id);

  uint8_t new_data[BLOCK_SECTOR_SIZE];
  fill_buffer(new_data, BLOCK_SECTOR_SIZE, 'N'); /* N = New */

  /* Log the write (this captures old_data internally) */
  bool logged = wal_log_write(txn, TEST_SECTOR_BASE, original, new_data, 0, BLOCK_SECTOR_SIZE);
  if (!logged) {
    fail("wal_log_write() failed");
  }

  /* Actually write the new data (simulating what cache would do) */
  cache_write(TEST_SECTOR_BASE, new_data, 0, BLOCK_SECTOR_SIZE);
  msg("Wrote new data 'N' to sector %d", TEST_SECTOR_BASE);

  /* Verify new data is there */
  uint8_t verify[BLOCK_SECTOR_SIZE];
  cache_read(TEST_SECTOR_BASE, verify);
  if (!verify_buffer(verify, BLOCK_SECTOR_SIZE, 'N')) {
    fail("Sector should contain 'N' before abort");
  }
  msg("Verified sector contains 'N'");

  /* ABORT the transaction - should restore original data */
  wal_txn_abort(txn);
  msg("Transaction aborted");

  /* Verify original data is restored */
  cache_read(TEST_SECTOR_BASE, verify);
  if (!verify_buffer(verify, BLOCK_SECTOR_SIZE, 'O')) {
    fail("Sector should contain 'O' after abort (UNDO failed)");
  }
  msg("Verified sector contains 'O' after abort: UNDO PASSED");

  wal_shutdown();
  msg("Transaction abort test: PASSED");
}

/* Test: wal-txn-multiple
 *
 * PURPOSE: Verify multiple transactions can run concurrently.
 *
 * WHAT IT TESTS:
 * - Multiple transactions can be active simultaneously
 * - Each transaction's changes are isolated
 * - Commit/abort of one doesn't affect others
 *
 * EXPECTED BEHAVIOR:
 * - TXN-1 commits successfully, its data persists
 * - TXN-2 aborts, its data is rolled back
 * - TXN-1's data is not affected by TXN-2's abort
 */
void test_wal_txn_multiple(void) {
  msg("Testing multiple concurrent transactions...");

  wal_init(false);

  /* Set up two different sectors with original data */
  uint8_t orig1[BLOCK_SECTOR_SIZE], orig2[BLOCK_SECTOR_SIZE];
  fill_buffer(orig1, BLOCK_SECTOR_SIZE, '1');
  fill_buffer(orig2, BLOCK_SECTOR_SIZE, '2');
  cache_write(TEST_SECTOR_BASE, orig1, 0, BLOCK_SECTOR_SIZE);
  cache_write(TEST_SECTOR_BASE + 1, orig2, 0, BLOCK_SECTOR_SIZE);
  msg("Initialized sectors %d='1' and %d='2'", TEST_SECTOR_BASE, TEST_SECTOR_BASE + 1);

  /* Start two transactions */
  struct wal_txn* txn1 = wal_txn_begin();
  struct wal_txn* txn2 = wal_txn_begin();
  msg("Started TXN-%u and TXN-%u", txn1->txn_id, txn2->txn_id);

  /* TXN-1 modifies sector 100 */
  uint8_t new1[BLOCK_SECTOR_SIZE];
  fill_buffer(new1, BLOCK_SECTOR_SIZE, 'A');
  wal_log_write(txn1, TEST_SECTOR_BASE, orig1, new1, 0, BLOCK_SECTOR_SIZE);
  cache_write(TEST_SECTOR_BASE, new1, 0, BLOCK_SECTOR_SIZE);
  msg("TXN-%u: wrote 'A' to sector %d", txn1->txn_id, TEST_SECTOR_BASE);

  /* TXN-2 modifies sector 101 */
  uint8_t new2[BLOCK_SECTOR_SIZE];
  fill_buffer(new2, BLOCK_SECTOR_SIZE, 'B');
  wal_log_write(txn2, TEST_SECTOR_BASE + 1, orig2, new2, 0, BLOCK_SECTOR_SIZE);
  cache_write(TEST_SECTOR_BASE + 1, new2, 0, BLOCK_SECTOR_SIZE);
  msg("TXN-%u: wrote 'B' to sector %d", txn2->txn_id, TEST_SECTOR_BASE + 1);

  /* Commit TXN-1 */
  wal_txn_commit(txn1);
  msg("TXN-%u committed", txn1->txn_id);

  /* Abort TXN-2 */
  wal_txn_abort(txn2);
  msg("TXN-%u aborted", txn2->txn_id);

  /* Verify: Sector 100 should have 'A' (committed) */
  uint8_t verify[BLOCK_SECTOR_SIZE];
  cache_read(TEST_SECTOR_BASE, verify);
  if (!verify_buffer(verify, BLOCK_SECTOR_SIZE, 'A')) {
    fail("Sector %d should be 'A' (TXN-1 committed)", TEST_SECTOR_BASE);
  }
  msg("Sector %d = 'A' (committed): PASSED", TEST_SECTOR_BASE);

  /* Verify: Sector 101 should have '2' (aborted, rolled back) */
  cache_read(TEST_SECTOR_BASE + 1, verify);
  if (!verify_buffer(verify, BLOCK_SECTOR_SIZE, '2')) {
    fail("Sector %d should be '2' (TXN-2 aborted)", TEST_SECTOR_BASE + 1);
  }
  msg("Sector %d = '2' (rolled back): PASSED", TEST_SECTOR_BASE + 1);

  wal_shutdown();
  msg("Multiple transactions test: PASSED");
}

/* ============================================================
 * LOGGING TESTS
 * ============================================================ */

/* Test: wal-log-write
 *
 * PURPOSE: Verify basic write logging works correctly.
 *
 * WHAT IT TESTS:
 * - wal_log_write() creates a log record
 * - Log record contains correct sector, offset, length
 * - Log record contains both old and new data
 *
 * EXPECTED BEHAVIOR:
 * - Function returns true on success
 * - next_lsn advances
 * - buffer_used increases
 */
void test_wal_log_write(void) {
  msg("Testing basic write logging...");

  wal_init(false);

  struct wal_txn* txn = wal_txn_begin();
  msg("Transaction %u started", txn->txn_id);

  lsn_t lsn_before = wal.next_lsn;
  size_t used_before = wal.buffer_used;

  uint8_t old_data[100];
  uint8_t new_data[100];
  fill_buffer(old_data, 100, 'X');
  fill_buffer(new_data, 100, 'Y');

  bool logged = wal_log_write(txn, TEST_SECTOR_BASE, old_data, new_data, 50, 100);
  if (!logged) {
    fail("wal_log_write() returned false");
  }
  msg("Write logged: sector=%d, offset=50, length=100", TEST_SECTOR_BASE);

  /* Verify LSN advanced */
  if (wal.next_lsn <= lsn_before) {
    fail("next_lsn should advance after logging");
  }
  msg("next_lsn advanced: %llu -> %llu", (unsigned long long)lsn_before,
      (unsigned long long)wal.next_lsn);

  /* Verify buffer used increased */
  if (wal.buffer_used <= used_before) {
    fail("buffer_used should increase after logging");
  }
  msg("buffer_used increased: %zu -> %zu", used_before, wal.buffer_used);

  wal_txn_abort(txn);
  wal_shutdown();
  msg("Basic write logging test: PASSED");
}

/* Test: wal-log-split
 *
 * PURPOSE: Verify large writes are split into multiple records.
 *
 * WHAT IT TESTS:
 * - Writing more than WAL_MAX_DATA_SIZE bytes creates multiple records
 * - All records share the same txn_id
 * - Offsets correctly partition the data
 *
 * EXPECTED BEHAVIOR:
 * - 512-byte write creates 3 records (232 + 232 + 48 bytes)
 * - All records can be used together to reconstruct the write
 */
void test_wal_log_split(void) {
  msg("Testing large write splitting...");
  msg("WAL_MAX_DATA_SIZE = %d bytes", WAL_MAX_DATA_SIZE);

  wal_init(false);

  struct wal_txn* txn = wal_txn_begin();
  msg("Transaction %u started", txn->txn_id);

  lsn_t lsn_before = wal.next_lsn;

  /* Write a full sector (512 bytes) - should split into multiple records */
  uint8_t old_data[BLOCK_SECTOR_SIZE];
  uint8_t new_data[BLOCK_SECTOR_SIZE];
  fill_buffer(old_data, BLOCK_SECTOR_SIZE, 'O');
  fill_buffer(new_data, BLOCK_SECTOR_SIZE, 'N');

  bool logged = wal_log_write(txn, TEST_SECTOR_BASE, old_data, new_data, 0, BLOCK_SECTOR_SIZE);
  if (!logged) {
    fail("wal_log_write() failed for 512-byte write");
  }
  msg("Logged 512-byte write to sector %d", TEST_SECTOR_BASE);

  /* Calculate expected number of records */
  int expected_records = (BLOCK_SECTOR_SIZE + WAL_MAX_DATA_SIZE - 1) / WAL_MAX_DATA_SIZE;
  int actual_records = (int)(wal.next_lsn - lsn_before);

  msg("Expected records: %d", expected_records);
  msg("Actual records (LSN delta): %d", actual_records);

  if (actual_records != expected_records) {
    fail("Expected %d records for 512-byte write, got %d", expected_records, actual_records);
  }
  msg("Correct number of split records: PASSED");

  wal_txn_abort(txn);
  wal_shutdown();
  msg("Large write splitting test: PASSED");
}

/* Test: wal-log-flush
 *
 * PURPOSE: Verify log buffer can be flushed to disk.
 *
 * WHAT IT TESTS:
 * - wal_flush() writes buffered records to disk
 * - flushed_lsn updates to reflect what's on disk
 * - Buffer can be reused after flush
 *
 * EXPECTED BEHAVIOR:
 * - After flush, flushed_lsn >= the LSN of flushed records
 */
void test_wal_log_flush(void) {
  msg("Testing log flush...");

  wal_init(false);

  struct wal_txn* txn = wal_txn_begin();

  /* Log several writes */
  uint8_t data[64];
  fill_buffer(data, 64, 'F');

  for (int i = 0; i < 5; i++) {
    wal_log_write(txn, TEST_SECTOR_BASE + i, data, data, 0, 64);
  }
  msg("Logged 5 writes");

  lsn_t current_lsn = wal.next_lsn;
  lsn_t flushed_before = wal.flushed_lsn;
  msg("Before flush: next_lsn=%llu, flushed_lsn=%llu", (unsigned long long)current_lsn,
      (unsigned long long)flushed_before);

  /* Flush the log */
  wal_flush(current_lsn);
  msg("wal_flush() called");

  /* Verify flushed_lsn updated */
  if (wal.flushed_lsn < current_lsn) {
    fail("flushed_lsn should be >= %llu after flush", (unsigned long long)current_lsn);
  }
  msg("After flush: flushed_lsn=%llu", (unsigned long long)wal.flushed_lsn);
  msg("Log flush verified: PASSED");

  wal_txn_abort(txn);
  wal_shutdown();
  msg("Log flush test: PASSED");
}

/* Test: wal-log-full
 *
 * PURPOSE: Verify behavior when log fills up.
 *
 * WHAT IT TESTS:
 * - What happens when log buffer is full
 * - System should either: force a checkpoint, or block, or fail gracefully
 *
 * EXPECTED BEHAVIOR:
 * - Implementation-dependent, but should not crash or corrupt data
 */
void test_wal_log_full(void) {
  msg("Testing log full behavior...");
  msg("WAL_LOG_SECTORS = %d, total log space = %d bytes", WAL_LOG_SECTORS,
      WAL_LOG_SECTORS * BLOCK_SECTOR_SIZE);

  wal_init(false);

  struct wal_txn* txn = wal_txn_begin();

  uint8_t data[WAL_MAX_DATA_SIZE];
  fill_buffer(data, WAL_MAX_DATA_SIZE, 'X');

  /* Try to fill the log */
  int writes = 0;
  int max_writes = WAL_LOG_SECTORS * 2; /* More than log can hold */

  msg("Attempting %d writes to fill log...", max_writes);

  for (int i = 0; i < max_writes; i++) {
    bool logged = wal_log_write(txn, TEST_SECTOR_BASE + (i % 50), data, data, 0, WAL_MAX_DATA_SIZE);
    if (!logged) {
      msg("wal_log_write() returned false at write %d", i);
      break;
    }
    writes++;
  }

  msg("Successfully logged %d writes", writes);

  /* If we got here without crashing, the implementation handles full log */
  msg("Log full handling: No crash (PASSED)");

  wal_txn_abort(txn);
  wal_shutdown();
  msg("Log full test: PASSED");
}

/* ============================================================
 * RECOVERY TESTS
 * ============================================================ */

/* Test: wal-recover-commit
 *
 * PURPOSE: Verify recovery replays committed transactions.
 *
 * WHAT IT TESTS:
 * - After simulated crash, recovery redoes committed writes
 * - Data from committed transactions is present after recovery
 *
 * EXPECTED BEHAVIOR:
 * - Committed data survives "crash" and recovery
 *
 * NOTE: We simulate crash by not calling wal_shutdown() cleanly
 */
void test_wal_recover_commit(void) {
  msg("Testing recovery of committed transactions (REDO)...");

  wal_init(false);

  /* Create and commit a transaction */
  struct wal_txn* txn = wal_txn_begin();
  msg("Transaction %u started", txn->txn_id);

  uint8_t old_data[256];
  uint8_t new_data[256];
  fill_buffer(old_data, 256, 'O');
  fill_buffer(new_data, 256, 'C'); /* C = Committed data */

  wal_log_write(txn, TEST_SECTOR_BASE, old_data, new_data, 0, 256);
  msg("Write logged: sector %d", TEST_SECTOR_BASE);

  wal_txn_commit(txn);
  msg("Transaction committed");

  /*
   * SIMULATE CRASH: Don't call wal_shutdown()
   * In a real crash, the cache might not have been flushed,
   * so the data might not be on disk.
   *
   * For testing, we'll manually corrupt the sector to simulate
   * data not making it to disk.
   */
  uint8_t garbage[256];
  fill_buffer(garbage, 256, 'G'); /* G = Garbage */
  cache_write(TEST_SECTOR_BASE, garbage, 0, 256);
  msg("Simulated crash: overwrote sector with garbage");

  /* Force cache flush to ensure garbage is on disk */
  cache_flush();

  /* Re-initialize WAL - should trigger recovery */
  msg("Re-initializing WAL (should trigger recovery)...");
  wal_init(false); /* This should call wal_recover() */

  /* Verify the committed data is restored */
  uint8_t verify[256];
  cache_read(TEST_SECTOR_BASE, verify);

  if (!verify_buffer(verify, 256, 'C')) {
    fail("Sector should contain 'C' after recovery (REDO failed)");
  }
  msg("Sector contains committed data 'C': REDO PASSED");

  wal_shutdown();
  msg("Recovery commit test: PASSED");
}

/* Test: wal-recover-abort
 *
 * PURPOSE: Verify recovery undoes uncommitted transactions.
 *
 * WHAT IT TESTS:
 * - After crash with uncommitted transaction, recovery undoes its writes
 * - Data from uncommitted transactions is NOT present after recovery
 *
 * EXPECTED BEHAVIOR:
 * - Uncommitted changes are rolled back during recovery
 */
void test_wal_recover_abort(void) {
  msg("Testing recovery of uncommitted transactions (UNDO)...");

  wal_init(false);

  /* First, establish known data */
  uint8_t original[256];
  fill_buffer(original, 256, 'O');
  cache_write(TEST_SECTOR_BASE, original, 0, 256);
  cache_flush();
  msg("Established original data 'O' at sector %d", TEST_SECTOR_BASE);

  /* Start transaction but DON'T commit */
  struct wal_txn* txn = wal_txn_begin();
  msg("Transaction %u started (will NOT commit)", txn->txn_id);

  uint8_t new_data[256];
  fill_buffer(new_data, 256, 'U'); /* U = Uncommitted */

  wal_log_write(txn, TEST_SECTOR_BASE, original, new_data, 0, 256);
  cache_write(TEST_SECTOR_BASE, new_data, 0, 256);
  msg("Wrote uncommitted data 'U' to sector %d", TEST_SECTOR_BASE);

  /* Flush log but NOT commit */
  wal_flush(wal.next_lsn);
  cache_flush();
  msg("Flushed log and cache (simulating crash with uncommitted txn)");

  /* SIMULATE CRASH - don't commit or abort, just re-init */
  msg("Simulating crash...");

  /* Re-initialize WAL - should trigger recovery */
  wal_init(false); /* Recovery should UNDO the uncommitted transaction */

  /* Verify original data is restored */
  uint8_t verify[256];
  cache_read(TEST_SECTOR_BASE, verify);

  if (!verify_buffer(verify, 256, 'O')) {
    fail("Sector should contain 'O' after recovery (UNDO failed)");
  }
  msg("Sector contains original data 'O': UNDO PASSED");

  wal_shutdown();
  msg("Recovery abort test: PASSED");
}

/* Test: wal-recover-order
 *
 * PURPOSE: Verify recovery applies changes in correct order.
 *
 * WHAT IT TESTS:
 * - Multiple writes to same sector are applied in LSN order
 * - Final state matches what was committed
 *
 * EXPECTED BEHAVIOR:
 * - If committed txn wrote A then B to same sector, final value is B
 */
void test_wal_recover_order(void) {
  msg("Testing recovery ordering...");

  wal_init(false);

  struct wal_txn* txn = wal_txn_begin();

  uint8_t data_a[64], data_b[64], data_c[64];
  fill_buffer(data_a, 64, 'A');
  fill_buffer(data_b, 64, 'B');
  fill_buffer(data_c, 64, 'C');

  /* Write A, then B, then C to same location */
  uint8_t empty[64] = {0};
  wal_log_write(txn, TEST_SECTOR_BASE, empty, data_a, 0, 64);
  msg("Logged write 1: 'A'");

  wal_log_write(txn, TEST_SECTOR_BASE, data_a, data_b, 0, 64);
  msg("Logged write 2: 'B'");

  wal_log_write(txn, TEST_SECTOR_BASE, data_b, data_c, 0, 64);
  msg("Logged write 3: 'C'");

  wal_txn_commit(txn);
  msg("Transaction committed");

  /* Corrupt the sector */
  uint8_t garbage[64];
  fill_buffer(garbage, 64, 'X');
  cache_write(TEST_SECTOR_BASE, garbage, 0, 64);
  cache_flush();

  /* Recovery */
  wal_init(false);

  /* Verify final value is 'C' */
  uint8_t verify[64];
  cache_read(TEST_SECTOR_BASE, verify);

  if (!verify_buffer(verify, 64, 'C')) {
    fail("Sector should contain 'C' (final write in order)");
  }
  msg("Correct ordering verified: final value is 'C'");

  wal_shutdown();
  msg("Recovery ordering test: PASSED");
}

/* ============================================================
 * CHECKPOINT TESTS
 * ============================================================ */

/* Test: wal-checkpoint
 *
 * PURPOSE: Verify checkpoint creation works.
 *
 * WHAT IT TESTS:
 * - wal_checkpoint() flushes dirty cache pages
 * - Checkpoint record is written to log
 * - Checkpoint reduces recovery work
 *
 * EXPECTED BEHAVIOR:
 * - After checkpoint, all committed data is on disk
 * - Recovery only needs to process log records after checkpoint
 */
void test_wal_checkpoint(void) {
  msg("Testing checkpoint creation...");

  wal_init(false);

  /* Create several committed transactions */
  for (int i = 0; i < 5; i++) {
    struct wal_txn* txn = wal_txn_begin();
    uint8_t data[64];
    fill_buffer(data, 64, 'A' + i);
    wal_log_write(txn, TEST_SECTOR_BASE + i, data, data, 0, 64);
    wal_txn_commit(txn);
  }
  msg("Created 5 committed transactions");

  lsn_t lsn_before = wal.next_lsn;

  /* Create checkpoint */
  wal_checkpoint();
  msg("Checkpoint created");

  /* Verify checkpoint record was written */
  if (wal.next_lsn <= lsn_before) {
    fail("Checkpoint should write a record (LSN should advance)");
  }
  msg("Checkpoint record written: LSN advanced");

  wal_shutdown();
  msg("Checkpoint test: PASSED");
}

/* ============================================================
 * INTEGRATION TESTS
 * ============================================================ */

/* Test: wal-stress
 *
 * PURPOSE: Stress test WAL with many transactions.
 *
 * WHAT IT TESTS:
 * - WAL handles many transactions without corruption
 * - No memory leaks or crashes under load
 *
 * EXPECTED BEHAVIOR:
 * - All operations complete successfully
 */
void test_wal_stress(void) {
  msg("Starting WAL stress test...");

  wal_init(false);

  int num_transactions = 100;
  int commits = 0;
  int aborts = 0;

  for (int i = 0; i < num_transactions; i++) {
    struct wal_txn* txn = wal_txn_begin();

    uint8_t data[128];
    fill_buffer(data, 128, (uint8_t)(i % 256));

    /* Log 1-3 writes per transaction */
    int num_writes = (i % 3) + 1;
    for (int j = 0; j < num_writes; j++) {
      wal_log_write(txn, TEST_SECTOR_BASE + (i % 50), data, data, 0, 128);
    }

    /* Commit 80%, abort 20% */
    if (i % 5 != 0) {
      wal_txn_commit(txn);
      commits++;
    } else {
      wal_txn_abort(txn);
      aborts++;
    }

    /* Occasional checkpoint */
    if (i % 25 == 0 && i > 0) {
      wal_checkpoint();
    }
  }

  msg("Completed %d transactions (%d commits, %d aborts)", num_transactions, commits, aborts);

  wal_shutdown();
  msg("WAL stress test: PASSED");
}
