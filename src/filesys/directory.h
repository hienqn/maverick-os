#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/off_t.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * DIRECTORY MANAGEMENT
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Directories are special files containing a sequence of directory entries.
 * Each entry maps a name (up to NAME_MAX chars) to an inode sector.
 *
 * Directory Structure:
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Entry 0: "."     │ sector=self     │ in_use=true                       │
 * ├─────────────────────────────────────────────────────────────────────────┤
 * │  Entry 1: ".."    │ sector=parent   │ in_use=true                       │
 * ├─────────────────────────────────────────────────────────────────────────┤
 * │  Entry 2: "foo"   │ sector=N        │ in_use=true                       │
 * ├─────────────────────────────────────────────────────────────────────────┤
 * │  Entry 3: (free)  │                 │ in_use=false                      │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * Special Entries:
 *   "."  - Self-reference (points to this directory)
 *   ".." - Parent reference (root's ".." points to itself)
 *
 * Iterator Pattern (dir_readdir):
 *   struct dir *dir = dir_open(...);
 *   char name[NAME_MAX + 1];
 *   while (dir_readdir(dir, name)) {
 *     // Process each entry (automatically skips "." and "..")
 *   }
 *   dir_close(dir);
 *
 * Thread Safety: Operations use the underlying inode's lock.
 * ═══════════════════════════════════════════════════════════════════════════
 */

/* Maximum length of a file name component (traditional UNIX limit).
   Full path names may be much longer but each component is limited. */
#define NAME_MAX 14

/* Forward declarations. */
struct inode;
struct dir;

/* ─────────────────────────────────────────────────────────────────────────
 * Directory Creation
 * ───────────────────────────────────────────────────────────────────────── */

/* Creates a basic directory at SECTOR with space for ENTRY_CNT entries.
   Does NOT add "." and ".." entries - use dir_create_with_parent() for that.
   @param sector     Disk sector for the directory inode (must be pre-allocated)
   @param entry_cnt  Initial capacity (number of entries, not bytes)
   @return true on success, false on allocation failure. */
bool dir_create(block_sector_t sector, size_t entry_cnt);

/* Creates a directory at SECTOR with "." and ".." entries initialized.
   For root directory, PARENT_SECTOR should equal SECTOR.
   @param sector         Disk sector for this directory
   @param parent_sector  Disk sector of parent directory
   @param entry_cnt      Initial capacity (automatically increased to at least 2)
   @return true on success, false on failure. */
bool dir_create_with_parent(block_sector_t sector, block_sector_t parent_sector, size_t entry_cnt);

/* ─────────────────────────────────────────────────────────────────────────
 * Opening and Closing
 * ───────────────────────────────────────────────────────────────────────── */

/* Opens and returns a directory for INODE. Takes ownership of INODE.
   If this call fails, INODE is closed automatically.
   @param inode  Inode to open as directory (must have is_dir=1)
   @return Directory handle, or NULL on failure. */
struct dir* dir_open(struct inode* inode);

/* Opens and returns the root directory.
   @return Root directory handle, or NULL on failure. */
struct dir* dir_open_root(void);

/* Reopens DIR, returning a new independent handle to the same directory.
   The new handle has its own position (starts at 0).
   @param dir  Directory to reopen (may be NULL)
   @return New directory handle, or NULL if dir is NULL or on failure. */
struct dir* dir_reopen(struct dir* dir);

/* Closes DIR and frees its resources. Closes the underlying inode.
   @param dir  Directory to close (may be NULL, no-op). */
void dir_close(struct dir* dir);

/* Returns the inode backing DIR. The inode remains owned by DIR.
   @param dir  Directory to query
   @return Inode pointer (do not close separately). */
struct inode* dir_get_inode(struct dir* dir);

/* ─────────────────────────────────────────────────────────────────────────
 * Position Management (for dir_readdir iteration)
 * ───────────────────────────────────────────────────────────────────────── */

/* Returns DIR's current read position (byte offset into directory file).
   @param dir  Directory to query
   @return Current position. */
off_t dir_get_pos(struct dir* dir);

/* Sets DIR's read position to POS bytes into the directory file.
   Use dir_set_pos(dir, 0) to reset iteration to the beginning.
   @param dir  Directory to modify
   @param pos  New position in bytes (should be multiple of entry size). */
void dir_set_pos(struct dir* dir, off_t pos);

/* ─────────────────────────────────────────────────────────────────────────
 * Directory Operations
 * ───────────────────────────────────────────────────────────────────────── */

/* Looks up NAME in DIR and returns its inode.
   @param dir    Directory to search
   @param name   Name to look for (max NAME_MAX chars)
   @param inode  Output: inode for NAME (caller must close), or NULL if not found
   @return true if found, false if not found. */
bool dir_lookup(const struct dir* dir, const char* name, struct inode** inode);

/* Adds an entry mapping NAME to INODE_SECTOR in DIR.
   NAME must not already exist in DIR.
   @param dir          Directory to add entry to
   @param name         Name for the new entry (1 to NAME_MAX chars)
   @param inode_sector Sector of the inode this name refers to
   @return true on success, false if name exists, name invalid, or I/O error. */
bool dir_add(struct dir* dir, const char* name, block_sector_t inode_sector);

/* Removes the entry for NAME from DIR and marks its inode for deletion.
   @param dir   Directory to remove entry from
   @param name  Name to remove
   @return true on success, false if NAME not found. */
bool dir_remove(struct dir* dir, const char* name);

/* Reads the next directory entry into NAME, advancing the position.
   Automatically skips "." and ".." entries.
   @param dir   Directory to read from
   @param name  Output buffer (must be at least NAME_MAX+1 bytes)
   @return true if an entry was read, false if no more entries. */
bool dir_readdir(struct dir* dir, char name[NAME_MAX + 1]);

/* Returns true if DIR contains only "." and ".." (or is completely empty).
   Used to check if a directory can be safely deleted.
   @param dir  Directory to check
   @return true if empty, false if contains user files/subdirectories. */
bool dir_is_empty(struct dir* dir);

#endif /* filesys/directory.h */
