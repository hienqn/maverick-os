/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                           DIRECTORY MODULE                                ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  This module implements directory operations for the hierarchical file   ║
 * ║  system. Directories are stored as special files containing a sequence   ║
 * ║  of directory entries.                                                   ║
 * ║                                                                          ║
 * ║  DIRECTORY STRUCTURE ON DISK:                                            ║
 * ║  ─────────────────────────────                                           ║
 * ║                                                                          ║
 * ║    Directory Inode (is_dir=1)                                            ║
 * ║         │                                                                ║
 * ║         ▼                                                                ║
 * ║    ┌─────────────────────────────────────────────────────────┐           ║
 * ║    │ Entry 0: "."   │ sector=self   │ in_use=true            │           ║
 * ║    ├─────────────────────────────────────────────────────────┤           ║
 * ║    │ Entry 1: ".."  │ sector=parent │ in_use=true            │           ║
 * ║    ├─────────────────────────────────────────────────────────┤           ║
 * ║    │ Entry 2: "foo" │ sector=N      │ in_use=true            │           ║
 * ║    ├─────────────────────────────────────────────────────────┤           ║
 * ║    │ Entry 3: (unused)              │ in_use=false           │           ║
 * ║    ├─────────────────────────────────────────────────────────┤           ║
 * ║    │ ...                                                     │           ║
 * ║    └─────────────────────────────────────────────────────────┘           ║
 * ║                                                                          ║
 * ║  SPECIAL ENTRIES:                                                        ║
 * ║  ────────────────                                                        ║
 * ║  - "."  : Points to the directory itself (self-reference)               ║
 * ║  - ".." : Points to the parent directory (root's .. points to itself)   ║
 * ║                                                                          ║
 * ║  KEY OPERATIONS:                                                         ║
 * ║  ───────────────                                                         ║
 * ║  - dir_lookup: Find an entry by name                                     ║
 * ║  - dir_add: Add a new entry (fails if name exists)                       ║
 * ║  - dir_remove: Mark entry as unused and remove its inode                 ║
 * ║  - dir_readdir: Iterate through entries (skips . and ..)                 ║
 * ║                                                                          ║
 * ║  SYNCHRONIZATION:                                                        ║
 * ║  ────────────────                                                        ║
 * ║  Directory operations use the underlying inode's lock for protection.   ║
 * ║  Individual read/write operations are atomic, but composite operations  ║
 * ║  (check-then-add) may see interleaved updates from other threads.       ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* A directory.
   Wraps an inode and maintains a position for readdir iteration. */
struct dir {
  struct inode* inode; /* Backing store (must have is_dir=1). */
  off_t pos;           /* Current position for dir_readdir(). */
};

/* A single directory entry.
   Each entry is NAME_MAX+1+4+1 = 20 bytes, so ~25 entries per sector. */
struct dir_entry {
  block_sector_t inode_sector; /* Sector number of the entry's inode. */
  char name[NAME_MAX + 1];     /* Null-terminated file name (max 14 chars). */
  bool in_use;                 /* True if entry is valid, false if deleted/empty. */
};

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure.
   Note: This creates a basic directory without . and .. entries.
   For creating subdirectories, use dir_create_with_parent(). */
bool dir_create(block_sector_t sector, size_t entry_cnt) {
  return inode_create_dir(sector, entry_cnt * sizeof(struct dir_entry));
}

/* Creates a directory at SECTOR with . and .. entries.
   PARENT_SECTOR is the sector of the parent directory.
   For the root directory, PARENT_SECTOR should equal SECTOR.
   Returns true if successful, false on failure. */
bool dir_create_with_parent(block_sector_t sector, block_sector_t parent_sector, size_t entry_cnt) {
  /* Ensure we have room for at least . and .. plus requested entries */
  if (entry_cnt < 2)
    entry_cnt = 2;

  /* Create the directory inode */
  if (!inode_create_dir(sector, entry_cnt * sizeof(struct dir_entry)))
    return false;

  /* Open the new directory to add . and .. entries */
  struct dir* dir = dir_open(inode_open(sector));
  if (dir == NULL)
    return false;

  /* Add "." entry pointing to self */
  bool success = dir_add(dir, ".", sector);

  /* Add ".." entry pointing to parent */
  if (success)
    success = dir_add(dir, "..", parent_sector);

  dir_close(dir);

  return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir* dir_open(struct inode* inode) {
  struct dir* dir = calloc(1, sizeof *dir);
  if (inode != NULL && dir != NULL) {
    dir->inode = inode;
    dir->pos = 0;
    return dir;
  } else {
    inode_close(inode);
    free(dir);
    return NULL;
  }
}

/* Opens the root directory and returns a directory for it.
   Returns a null pointer on failure. */
struct dir* dir_open_root(void) {
  return dir_open(inode_open(ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer if DIR is NULL or on failure. */
struct dir* dir_reopen(struct dir* dir) {
  if (dir == NULL)
    return NULL;
  return dir_open(inode_reopen(dir->inode));
}

/* Destroys DIR and frees associated resources. */
void dir_close(struct dir* dir) {
  if (dir != NULL) {
    inode_close(dir->inode);
    free(dir);
  }
}

/* Returns the inode encapsulated by DIR. */
struct inode* dir_get_inode(struct dir* dir) {
  return dir->inode;
}

/* Returns the current position in DIR. */
off_t dir_get_pos(struct dir* dir) { return dir->pos; }

/* Sets the current position in DIR to POS. */
void dir_set_pos(struct dir* dir, off_t pos) { dir->pos = pos; }

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool lookup(const struct dir* dir, const char* name, struct dir_entry* ep, off_t* ofsp) {
  struct dir_entry e;
  size_t ofs;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
    if (e.in_use && !strcmp(name, e.name)) {
      if (ep != NULL)
        *ep = e;
      if (ofsp != NULL)
        *ofsp = ofs;
      return true;
    }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool dir_lookup(const struct dir* dir, const char* name, struct inode** inode) {
  struct dir_entry e;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  if (lookup(dir, name, &e, NULL))
    *inode = inode_open(e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool dir_add(struct dir* dir, const char* name, block_sector_t inode_sector) {
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen(name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup(dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy(e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at(dir->inode, &e, sizeof e, ofs) == sizeof e;

done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool dir_remove(struct dir* dir, const char* name) {
  struct dir_entry e;
  struct inode* inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  /* Find directory entry. */
  if (!lookup(dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open(e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at(dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Decrement link count. Only mark for removal when no links remain. */
  inode_dec_nlink(inode);
  if (inode_get_nlink(inode) == 0) {
    inode_remove(inode);
  }
  success = true;

done:
  inode_close(inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool dir_readdir(struct dir* dir, char name[NAME_MAX + 1]) {
  struct dir_entry e;

  while (inode_read_at(dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
    dir->pos += sizeof e;
    if (e.in_use) {
      strlcpy(name, e.name, NAME_MAX + 1);
      return true;
    }
  }
  return false;
}

/* Returns true if DIR is empty (contains only . and .. entries),
   false otherwise. */
bool dir_is_empty(struct dir* dir) {
  struct dir_entry e;
  off_t ofs;

  for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e) {
    if (e.in_use) {
      /* Skip "." and ".." entries */
      if (strcmp(e.name, ".") != 0 && strcmp(e.name, "..") != 0) {
        return false; /* Found a non-special entry */
      }
    }
  }
  return true; /* Only . and .. (or nothing) found */
}
