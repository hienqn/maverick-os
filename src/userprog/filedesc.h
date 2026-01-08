#ifndef USERPROG_FILEDESC_H
#define USERPROG_FILEDESC_H

#include <list.h>
#include <stdbool.h>
#include "threads/synch.h"

/* Forward declarations. */
struct file;
struct dir;

/* ═══════════════════════════════════════════════════════════════════════════
 * OPEN FILE DESCRIPTION (OFD) - POSIX "open file description"
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * An Open File Description represents a single open() of a file. Multiple
 * file descriptors (within a process or across processes after fork) can
 * share the same OFD.
 *
 * Sharing an OFD means sharing:
 *   - File position (via the underlying struct file)
 *   - File status flags (O_APPEND, O_NONBLOCK, etc.)
 *
 * OFDs are created by:
 *   - open() syscall (creates new OFD)
 *   - Opening stdin/stdout/stderr (uses global console OFDs)
 *
 * OFDs are shared by:
 *   - dup() / dup2() - within same process
 *   - fork() - between parent and child
 *
 * SYNCHRONIZATION:
 *   - Global OFD list protected by ofd_list_lock
 *   - Per-OFD data protected by ofd->lock
 *   - Lock ordering: ofd_list_lock BEFORE ofd->lock (like inode pattern)
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

/* File descriptor types. */
enum fd_type {
  FD_NONE,   /* Unused slot (should not occur for valid OFD). */
  FD_FILE,   /* Regular file. */
  FD_DIR,    /* Directory. */
  FD_CONSOLE /* Console device (stdin/stdout/stderr). */
};

/* Console I/O mode (only used when type == FD_CONSOLE). */
enum console_mode {
  CONSOLE_READ, /* stdin - read from keyboard. */
  CONSOLE_WRITE /* stdout/stderr - write to display. */
};

/* Open File Description structure. */
struct open_file_desc {
  struct list_elem elem;   /* Element in global OFD list. */
  enum fd_type type;       /* Type: file, directory, or console. */
  enum console_mode cmode; /* Console mode (if type == FD_CONSOLE). */

  union {
    struct file* file; /* Underlying file (type == FD_FILE). */
    struct dir* dir;   /* Underlying directory (type == FD_DIR). */
  };

  int flags;        /* File status flags (O_APPEND, etc. for future fcntl). */
  int ref_count;    /* Number of FDs referencing this OFD. */
  struct lock lock; /* Per-OFD lock for thread-safe operations. */
};

/* Per-process file descriptor entry. */
struct fd_entry {
  struct open_file_desc* ofd; /* Pointer to OFD, NULL if unused. */
};

/* ═══════════════════════════════════════════════════════════════════════════
 * GLOBAL OFD TABLE API
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Initialize the global OFD table (call once at boot). */
void ofd_init(void);

/* Create a new OFD for a regular file. Returns NULL on failure. */
struct open_file_desc* ofd_create_file(struct file* file);

/* Create a new OFD for a directory. Returns NULL on failure. */
struct open_file_desc* ofd_create_dir(struct dir* dir);

/* Get the global console OFD (stdin, stdout, or stderr).
   fd must be STDIN_FILENO (0), STDOUT_FILENO (1), or STDERR_FILENO (2). */
struct open_file_desc* ofd_get_console(int fd);

/* Increment OFD reference count (for dup/fork).
   Thread-safe. Returns the same OFD pointer. */
struct open_file_desc* ofd_dup(struct open_file_desc* ofd);

/* Decrement OFD reference count. Closes underlying file/dir when count
   reaches 0 and removes from global list. Thread-safe. */
void ofd_close(struct open_file_desc* ofd);

#endif /* userprog/filedesc.h */
