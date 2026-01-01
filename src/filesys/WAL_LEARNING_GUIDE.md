# Write-Ahead Logging (WAL) Learning Guide

## What You're Building

You're implementing a Write-Ahead Log for the Pintos filesystem to ensure **crash consistency**. This is the same technique used by:
- PostgreSQL (pg_wal)
- SQLite (journal or WAL mode)
- MySQL/InnoDB (redo log)
- ext4 (journaling)

---

## Core Principle

> **"Never modify the original data until you've logged what you're about to do."**

If the system crashes:
- Without WAL: Data might be half-written, corrupted, or inconsistent
- With WAL: Replay the log to restore consistency

---

## Questions to Guide Your Understanding

### 1. The Fundamental "Why"

**Q1.1:** Consider this scenario without WAL:
```
1. Create a new file (allocate inode sector 42)
2. Add directory entry pointing to sector 42
3. --- CRASH HERE ---
4. (never completed)
```
What's wrong with the filesystem state after recovery?

**Q1.2:** Now with WAL:
```
1. LOG: "BEGIN TXN 1"
2. LOG: "WRITE sector 42: <inode data>"
3. LOG: "WRITE sector 10: <directory entry>"
4. LOG: "COMMIT TXN 1"
5. Actually write sector 42
6. --- CRASH HERE ---
7. Actually write sector 10 (never happened)
```
How does the log help you recover? What do you do during recovery?

---

### 2. UNDO vs REDO Logging

There are two main approaches:

**UNDO Logging (Write-Ahead):**
- Log the OLD value before modifying
- Write actual data to disk before COMMIT record
- Recovery: Undo uncommitted transactions

**REDO Logging:**
- Log the NEW value before modifying
- Flush COMMIT record before writing actual data
- Recovery: Redo committed transactions

**UNDO/REDO Logging (ARIES-style):**
- Log both OLD and NEW values
- Most flexible but uses more space

**Q2.1:** If you only had UNDO logging (old values), could you recover a committed transaction whose data writes weren't flushed? Why or why not?

**Q2.2:** If you only had REDO logging (new values), could you undo an uncommitted transaction? Why or why not?

**Q2.3:** Given the Pintos buffer cache (which delays writes), which approach makes more sense? Why?

---

### 3. The Commit Point

**Q3.1:** When is a transaction truly "committed"?
- When you call wal_txn_commit()?
- When the COMMIT record is in the log buffer?
- When the COMMIT record is on disk?

**Q3.2:** What's "force at commit"? Why is it necessary for durability?

**Q3.3:** Group commit optimization: Multiple transactions share one disk write. Draw a timeline of how this works. What are the tradeoffs?

---

### 4. Checkpoints

**Q4.1:** Without checkpoints, recovery must read the ENTIRE log from the beginning. If your system runs for a year, that's a lot of log! How do checkpoints help?

**Q4.2:** During a checkpoint, can other transactions continue running? What are the options?

**Q4.3:** After a checkpoint completes, which log records can you safely delete?

---

### 5. Log Structure and Layout

**Q5.1:** Your log record struct is ~550 bytes but a disk sector is 512 bytes. What are your options?
- Limit data to fit in one sector
- Span records across sectors
- Variable-length records

What are the tradeoffs of each?

**Q5.2:** How do you map an LSN (Log Sequence Number) to a disk location? Design this mapping.

**Q5.3:** The log is a fixed size (64 sectors = 32KB). What happens when it fills up? Design a strategy.

---

### 6. Recovery Deep Dive

**Q6.1:** Order of log records during recovery matters. Consider:
```
LSN 1: BEGIN TXN 1
LSN 2: WRITE sector 10 (old: A, new: B)
LSN 3: BEGIN TXN 2
LSN 4: WRITE sector 10 (old: B, new: C)  <- same sector!
LSN 5: COMMIT TXN 2
LSN 6: --- CRASH ---
```
What should sector 10 contain after recovery? Walk through your recovery algorithm.

**Q6.2:** What if the crash happens while writing a log record itself (torn write)? How does the checksum help?

**Q6.3:** ARIES recovery has three phases: Analysis, Redo, Undo. Why this order? Could you do Undo before Redo?

---

### 7. Integration with Pintos

**Q7.1:** The buffer cache (`cache.c`) already batches writes. Where should you call `wal_log_write()`?
- Option A: In `cache_write()` before modifying the cache
- Option B: In `inode_write_at()` before calling cache functions
- Option C: Both

**Q7.2:** `inode_extend()` allocates multiple blocks. Should this be one transaction or multiple? What happens if allocation fails halfway through?

**Q7.3:** The free-map is crucial for consistency. What happens if:
- You allocate a block but crash before using it (block leak)
- You deallocate a block but crash before clearing its reference (dangling pointer)

How does WAL help with each case?

---

### 8. Performance Considerations

**Q8.1:** Every write now requires TWO disk writes (log + data). How can you minimize performance impact?

**Q8.2:** Should log writes use the buffer cache or bypass it? Consider:
- Cache eviction policy might evict unflushed log records
- Direct writes guarantee durability but are slower

**Q8.3:** "Steal vs No-Steal" policies:
- **Steal:** Dirty pages can be flushed before transaction commits
- **No-Steal:** Dirty pages held until commit

What are the memory implications? Which requires UNDO logging?

---

## Implementation Roadmap

### Phase 1: Basic Infrastructure
1. [ ] Implement `wal_init()` - allocate buffer, initialize state
2. [ ] Implement `wal_append_record()` - add records to buffer
3. [ ] Implement `wal_flush_buffer()` - write buffer to disk
4. [ ] Implement `wal_calculate_checksum()` - detect corruption

### Phase 2: Transaction Lifecycle
5. [ ] Implement `wal_txn_begin()` - create transaction, write BEGIN
6. [ ] Implement `wal_log_write()` - log modifications
7. [ ] Implement `wal_txn_commit()` - write COMMIT, flush log
8. [ ] Implement `wal_txn_abort()` - undo and write ABORT

### Phase 3: Recovery
9. [ ] Implement `wal_read_record()` - read from disk
10. [ ] Implement `wal_recover()` - REDO committed, UNDO uncommitted
11. [ ] Add crash detection in `wal_init()`

### Phase 4: Checkpointing
12. [ ] Implement `wal_checkpoint()` - flush dirty pages, log checkpoint
13. [ ] Modify recovery to start from checkpoint

### Phase 5: Integration
14. [ ] Modify `cache_write()` to call WAL
15. [ ] Wrap filesystem operations in transactions
16. [ ] Add checkpoint trigger (periodic or when log is 50% full)

---

## Testing Your Implementation

### Manual Testing
1. Add printf statements to trace your log operations
2. Deliberately kill Pintos mid-operation and observe recovery
3. Corrupt a log record and verify checksum catches it

### Test Scenarios
- Create file, crash before commit, verify file doesn't exist after recovery
- Create file, commit, crash before data write, verify file exists after recovery
- Multiple concurrent transactions, crash, verify correct commit/abort handling

### Verification Questions
After implementing, you should be able to answer:
1. What's the maximum size file operation you can log?
2. How many transactions can be active simultaneously?
3. How long does recovery take for a full log?
4. What's the performance overhead of WAL?

---

## Resources

- Gray & Reuter, "Transaction Processing: Concepts and Techniques" (the bible)
- [ARIES Paper](https://cs.stanford.edu/people/chr101/cs345/aries.pdf)
- [SQLite WAL Mode Documentation](https://www.sqlite.org/wal.html)
- [PostgreSQL WAL Documentation](https://www.postgresql.org/docs/current/wal.html)

Good luck! Remember: the goal is understanding, not perfection.
