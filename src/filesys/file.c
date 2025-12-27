#include "filesys/file.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                              FILE MODULE                                  ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  This module provides the file abstraction layer, managing:              ║
 * ║  - File position tracking (pos)                                          ║
 * ║  - Reference counting for shared file descriptors (fork)                 ║
 * ║  - Write denial for executable protection                                ║
 * ║                                                                          ║
 * ║  RELATIONSHIP TO OTHER MODULES:                                          ║
 * ║                                                                          ║
 * ║    syscall.c                                                             ║
 * ║        │                                                                 ║
 * ║        ▼                                                                 ║
 * ║    ┌────────┐    inode_read/write    ┌───────┐    cache_read/write       ║
 * ║    │  file  │ ──────────────────────►│ inode │ ──────────────────►disk   ║
 * ║    └────────┘                        └───────┘                           ║
 * ║     (position,                      (data blocks,                        ║
 * ║      ref_count)                      extension)                          ║
 * ║                                                                          ║
 * ║  SYNCHRONIZATION:                                                        ║
 * ║  - ref_count is protected by file_lock for thread-safe dup/close         ║
 * ║  - pos is NOT synchronized (each fd_entry has its own file struct)       ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

/* An open file. */
struct file {
  struct inode* inode; /* File's inode. */
  off_t pos;           /* Current position. */
  bool deny_write;     /* Has file_deny_write() been called? */
  int ref_count;       /* Reference count for shared file descriptors. */
  struct lock lock;    /* Protects ref_count for thread-safe dup/close. */
};

/* Opens a file for the given INODE, of which it takes ownership,
   and returns the new file.  Returns a null pointer if an
   allocation fails or if INODE is null. */
struct file* file_open(struct inode* inode) {
  struct file* file = calloc(1, sizeof *file);
  if (inode != NULL && file != NULL) {
    file->inode = inode;
    file->pos = 0;
    file->deny_write = false;
    file->ref_count = 1;
    lock_init(&file->lock);
    return file;
  } else {
    inode_close(inode);
    free(file);
    return NULL;
  }
}

/* Opens and returns a new file for the same inode as FILE.
   Returns a null pointer if FILE is NULL or if unsuccessful. */
struct file* file_reopen(struct file* file) {
  if (file == NULL)
    return NULL;
  return file_open(inode_reopen(file->inode));
}

/* Duplicates FILE by incrementing its reference count.
   Returns the same file pointer. Used for fork() to share
   file descriptors between parent and child.
   Thread-safe: uses internal lock to protect ref_count. */
struct file* file_dup(struct file* file) {
  if (file != NULL) {
    lock_acquire(&file->lock);
    file->ref_count++;
    lock_release(&file->lock);
  }
  return file;
}

/* Closes FILE. Decrements reference count and only frees
   when count reaches 0.
   Thread-safe: uses internal lock to protect ref_count. */
void file_close(struct file* file) {
  if (file != NULL) {
    lock_acquire(&file->lock);
    file->ref_count--;
    if (file->ref_count == 0) {
      lock_release(&file->lock);
      file_allow_write(file);
      inode_close(file->inode);
      free(file);
    } else {
      lock_release(&file->lock);
    }
  }
}

/* Returns the inode encapsulated by FILE. */
struct inode* file_get_inode(struct file* file) {
  return file->inode;
}

/* Reads SIZE bytes from FILE into BUFFER,
   starting at the file's current position.
   Returns the number of bytes actually read,
   which may be less than SIZE if end of file is reached.
   Advances FILE's position by the number of bytes read. */
off_t file_read(struct file* file, void* buffer, off_t size) {
  off_t bytes_read = inode_read_at(file->inode, buffer, size, file->pos);
  file->pos += bytes_read;
  return bytes_read;
}

/* Reads SIZE bytes from FILE into BUFFER,
   starting at offset FILE_OFS in the file.
   Returns the number of bytes actually read,
   which may be less than SIZE if end of file is reached.
   The file's current position is unaffected. */
off_t file_read_at(struct file* file, void* buffer, off_t size, off_t file_ofs) {
  return inode_read_at(file->inode, buffer, size, file_ofs);
}

/* Writes SIZE bytes from BUFFER into FILE,
   starting at the file's current position.
   Returns the number of bytes actually written, which may be less
   than SIZE if disk space runs out. The file will be extended
   automatically if writing past EOF.
   Advances FILE's position by the number of bytes written. */
off_t file_write(struct file* file, const void* buffer, off_t size) {
  off_t bytes_written = inode_write_at(file->inode, buffer, size, file->pos);
  file->pos += bytes_written;
  return bytes_written;
}

/* Writes SIZE bytes from BUFFER into FILE,
   starting at offset FILE_OFS in the file.
   Returns the number of bytes actually written, which may be less
   than SIZE if disk space runs out. The file will be extended
   automatically if writing past EOF.
   The file's current position is unaffected. */
off_t file_write_at(struct file* file, const void* buffer, off_t size, off_t file_ofs) {
  return inode_write_at(file->inode, buffer, size, file_ofs);
}

/* Prevents write operations on FILE's underlying inode
   until file_allow_write() is called or FILE is closed. */
void file_deny_write(struct file* file) {
  ASSERT(file != NULL);
  if (!file->deny_write) {
    file->deny_write = true;
    inode_deny_write(file->inode);
  }
}

/* Re-enables write operations on FILE's underlying inode.
   (Writes might still be denied by some other file that has the
   same inode open.) */
void file_allow_write(struct file* file) {
  ASSERT(file != NULL);
  if (file->deny_write) {
    file->deny_write = false;
    inode_allow_write(file->inode);
  }
}

/* Returns the size of FILE in bytes. */
off_t file_length(struct file* file) {
  ASSERT(file != NULL);
  return inode_length(file->inode);
}

/* Sets the current position in FILE to NEW_POS bytes from the
   start of the file. */
void file_seek(struct file* file, off_t new_pos) {
  ASSERT(file != NULL);
  ASSERT(new_pos >= 0);
  file->pos = new_pos;
}

/* Returns the current position in FILE as a byte offset from the
   start of the file. */
off_t file_tell(struct file* file) {
  ASSERT(file != NULL);
  return file->pos;
}
