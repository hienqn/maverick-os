#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

/* ─────────────────────────────────────────────────────────────────────────
 * Inode Type Constants
 *
 * These constants identify the type of an inode. The type field in inode_disk
 * replaces the old is_dir boolean to support symbolic links.
 * ───────────────────────────────────────────────────────────────────────── */
#define INODE_TYPE_FILE 0    /* Regular file */
#define INODE_TYPE_DIR 1     /* Directory */
#define INODE_TYPE_SYMLINK 2 /* Symbolic link */

/* ═══════════════════════════════════════════════════════════════════════════
 * INODE (INDEX NODE) MANAGEMENT
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Inodes store file metadata and data block locations. This implementation
 * uses multi-level indexing for extensible files up to ~8MB:
 *
 * ┌────────────────────────────────────────────────────────────────────────┐
 * │  inode_disk (on-disk, 512 bytes)                                       │
 * │  ┌──────────────┬───────────────────────────────────────────────────┐ │
 * │  │ direct[12]   │ 12 direct block pointers (48KB directly)          │ │
 * │  ├──────────────┼───────────────────────────────────────────────────┤ │
 * │  │ indirect     │ → 128 more blocks (64KB via single indirection)   │ │
 * │  ├──────────────┼───────────────────────────────────────────────────┤ │
 * │  │ doubly_indir │ → 128 indirect blocks → 16384 blocks (~8MB)       │ │
 * │  ├──────────────┼───────────────────────────────────────────────────┤ │
 * │  │ length       │ File size in bytes                                │ │
 * │  │ is_dir       │ 1 = directory, 0 = regular file                   │ │
 * │  │ magic        │ 0x494e4f44 ("INOD") for validation                │ │
 * │  └──────────────┴───────────────────────────────────────────────────┘ │
 * └────────────────────────────────────────────────────────────────────────┘
 *
 * Inode Lifecycle:
 *   1. inode_create() - Allocate on-disk inode at specified sector
 *   2. inode_open()   - Load into memory (returns cached if already open)
 *   3. inode_read/write_at() - Access file data
 *   4. inode_close()  - Decrement ref count (free when count hits 0)
 *   5. inode_remove() - Mark for deletion (actual deletion on last close)
 *
 * Thread Safety: Each inode has a per-inode lock protecting its metadata.
 * A global lock protects the list of open inodes. Lock order: global first.
 * ═══════════════════════════════════════════════════════════════════════════
 */

/* ─────────────────────────────────────────────────────────────────────────
 * Initialization
 * ───────────────────────────────────────────────────────────────────────── */

/* Initializes the inode module. Must be called before any other inode ops. */
void inode_init(void);

/* ─────────────────────────────────────────────────────────────────────────
 * Inode Creation
 * ───────────────────────────────────────────────────────────────────────── */

/* Creates a new file inode at disk SECTOR with initial LENGTH bytes.
   Allocates and zero-fills all required data blocks (direct, indirect, etc.).
   The sector itself must already be allocated from the free map.
   @param sector  Disk sector for the inode metadata (must be pre-allocated)
   @param length  Initial file size in bytes (blocks allocated accordingly)
   @return true on success, false if disk is full or allocation fails.
   Note: On failure, partially allocated blocks may be leaked. */
bool inode_create(block_sector_t sector, off_t length);

/* Creates a new directory inode at disk SECTOR with initial LENGTH bytes.
   Same as inode_create() but marks the inode as a directory.
   @param sector  Disk sector for the inode metadata
   @param length  Initial size (typically sizeof directory entries)
   @return true on success, false on allocation failure. */
bool inode_create_dir(block_sector_t sector, off_t length);

/* ─────────────────────────────────────────────────────────────────────────
 * Opening and Closing (Reference Counting)
 * ───────────────────────────────────────────────────────────────────────── */

/* Opens the inode at disk SECTOR and returns it.
   If the inode is already open, returns the existing in-memory inode
   with its open_cnt incremented (sharing). Otherwise loads from disk.
   @param sector  Disk sector containing the inode
   @return Pointer to inode, or NULL if allocation fails. */
struct inode* inode_open(block_sector_t sector);

/* Reopens INODE (increments its open count).
   @param inode  Inode to reopen (may be NULL, returns NULL)
   @return The same inode pointer, or NULL if inode was NULL. */
struct inode* inode_reopen(struct inode* inode);

/* Closes INODE by decrementing its open count.
   When the last opener closes, the in-memory inode is freed.
   If the inode was marked for removal, its disk blocks are also freed.
   @param inode  Inode to close (may be NULL, no-op). */
void inode_close(struct inode* inode);

/* ─────────────────────────────────────────────────────────────────────────
 * Inode Properties
 * ───────────────────────────────────────────────────────────────────────── */

/* Returns the disk sector number (inode number) for INODE.
   The sector number uniquely identifies an inode on the filesystem.
   @param inode  Inode to query
   @return Sector number of the inode's on-disk location. */
block_sector_t inode_get_inumber(const struct inode* inode);

/* Returns the size of INODE's data in bytes.
   @param inode  Inode to query
   @return Current file length. */
off_t inode_length(const struct inode* inode);

/* Returns true if INODE represents a directory, false for regular file.
   @param inode  Inode to query
   @return true if directory, false if regular file. */
bool inode_is_dir(struct inode* inode);

/* Returns true if INODE represents a symbolic link.
   @param inode  Inode to query
   @return true if symbolic link, false otherwise. */
bool inode_is_symlink(struct inode* inode);

/* Returns true if INODE represents a regular file (not dir, not symlink).
   @param inode  Inode to query
   @return true if regular file, false otherwise. */
bool inode_is_file(struct inode* inode);

/* ─────────────────────────────────────────────────────────────────────────
 * Hard Link Support (Link Count)
 * ───────────────────────────────────────────────────────────────────────── */

/* Returns the hard link count for INODE.
   @param inode  Inode to query
   @return Current link count. */
uint32_t inode_get_nlink(struct inode* inode);

/* Increments the hard link count for INODE.
   Call this when creating a new hard link to the inode.
   @param inode  Inode to modify */
void inode_inc_nlink(struct inode* inode);

/* Decrements the hard link count for INODE.
   Call this when removing a directory entry pointing to the inode.
   @param inode  Inode to modify */
void inode_dec_nlink(struct inode* inode);

/* ─────────────────────────────────────────────────────────────────────────
 * Symbolic Link Creation
 * ───────────────────────────────────────────────────────────────────────── */

/* Creates a symbolic link inode at SECTOR pointing to TARGET.
   The target path is stored as the inode's data content.
   @param sector  Disk sector for the inode metadata (must be pre-allocated)
   @param target  Target path string (will be copied into the inode)
   @return true on success, false on allocation failure. */
bool inode_create_symlink(block_sector_t sector, const char* target);

/* Returns true if INODE has been marked for removal.
   A removed inode will be fully deleted when its last opener closes it.
   @param inode  Inode to query
   @return true if marked for removal, false otherwise. */
bool inode_is_removed(const struct inode* inode);

/* Sets whether INODE should skip WAL (Write-Ahead Log) logging.
   Used for internal metadata (e.g., free map inode) to prevent
   infinite recursion when WAL logging triggers more metadata writes.
   @param inode  Inode to configure
   @param skip   true to skip WAL logging, false for normal logging. */
void inode_set_skip_wal(struct inode* inode, bool skip);

/* ─────────────────────────────────────────────────────────────────────────
 * Removal
 * ───────────────────────────────────────────────────────────────────────── */

/* Marks INODE for deletion. The inode remains usable until closed.
   When the last opener calls inode_close(), the inode's disk blocks
   are freed and the inode is fully deleted.
   @param inode  Inode to mark for removal. */
void inode_remove(struct inode* inode);

/* ─────────────────────────────────────────────────────────────────────────
 * Reading and Writing Data
 * ───────────────────────────────────────────────────────────────────────── */

/* Reads SIZE bytes from INODE into BUFFER, starting at byte OFFSET.
   Does not extend the file - stops at EOF.
   @param inode   Inode to read from
   @param buffer  Destination buffer (must have SIZE bytes available)
   @param size    Number of bytes to read
   @param offset  Starting byte position in file
   @return Number of bytes actually read (may be less if EOF reached). */
off_t inode_read_at(struct inode* inode, void* buffer, off_t size, off_t offset);

/* Writes SIZE bytes from BUFFER into INODE, starting at byte OFFSET.
   Automatically extends the file if writing past current EOF.
   Respects write denial (returns 0 if writes are denied).
   @param inode   Inode to write to
   @param buffer  Source data
   @param size    Number of bytes to write
   @param offset  Starting byte position in file
   @return Number of bytes written (may be 0 if denied or disk full). */
off_t inode_write_at(struct inode* inode, const void* buffer, off_t size, off_t offset);

/* ─────────────────────────────────────────────────────────────────────────
 * Write Denial (for Executables)
 * ───────────────────────────────────────────────────────────────────────── */

/* Prevents writes to INODE. Used to protect executing programs.
   Each call must be paired with a later inode_allow_write() call.
   May be called at most once per inode opener.
   @param inode  Inode to protect from writes. */
void inode_deny_write(struct inode* inode);

/* Re-enables writes to INODE after inode_deny_write().
   Must be called once per prior inode_deny_write() call,
   and must be called before inode_close().
   @param inode  Inode to re-enable writes on. */
void inode_allow_write(struct inode* inode);

#endif /* filesys/inode.h */
