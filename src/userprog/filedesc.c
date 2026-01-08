#include "userprog/filedesc.h"
#include <debug.h>
#include <stdio.h>
#include "filesys/file.h"
#include "filesys/directory.h"
#include "threads/malloc.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * GLOBAL STATE
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Global list of all open file descriptions. */
static struct list ofd_list;

/* Lock protecting the global OFD list.
   Must be acquired before any ofd->lock to prevent deadlock. */
static struct lock ofd_list_lock;

/* Pre-allocated OFDs for console (stdin/stdout/stderr).
   These are never freed and have special initialization. */
static struct open_file_desc console_stdin;
static struct open_file_desc console_stdout;
static struct open_file_desc console_stderr;

/* ═══════════════════════════════════════════════════════════════════════════
 * INITIALIZATION
 * ═══════════════════════════════════════════════════════════════════════════*/

void ofd_init(void) {
  list_init(&ofd_list);
  lock_init(&ofd_list_lock);

  /* Initialize stdin OFD. */
  console_stdin.type = FD_CONSOLE;
  console_stdin.cmode = CONSOLE_READ;
  console_stdin.file = NULL;
  console_stdin.flags = 0;
  console_stdin.ref_count = 1; /* Never goes to 0. */
  lock_init(&console_stdin.lock);

  /* Initialize stdout OFD. */
  console_stdout.type = FD_CONSOLE;
  console_stdout.cmode = CONSOLE_WRITE;
  console_stdout.file = NULL;
  console_stdout.flags = 0;
  console_stdout.ref_count = 1;
  lock_init(&console_stdout.lock);

  /* Initialize stderr OFD. */
  console_stderr.type = FD_CONSOLE;
  console_stderr.cmode = CONSOLE_WRITE;
  console_stderr.file = NULL;
  console_stderr.flags = 0;
  console_stderr.ref_count = 1;
  lock_init(&console_stderr.lock);

  /* Add console OFDs to global list (for debugging/introspection). */
  lock_acquire(&ofd_list_lock);
  list_push_back(&ofd_list, &console_stdin.elem);
  list_push_back(&ofd_list, &console_stdout.elem);
  list_push_back(&ofd_list, &console_stderr.elem);
  lock_release(&ofd_list_lock);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * OFD CREATION
 * ═══════════════════════════════════════════════════════════════════════════*/

struct open_file_desc* ofd_create_file(struct file* file) {
  ASSERT(file != NULL);

  struct open_file_desc* ofd = malloc(sizeof(struct open_file_desc));
  if (ofd == NULL)
    return NULL;

  ofd->type = FD_FILE;
  ofd->cmode = CONSOLE_READ; /* Not used for files. */
  ofd->file = file;
  ofd->flags = 0;
  ofd->ref_count = 1;
  lock_init(&ofd->lock);

  lock_acquire(&ofd_list_lock);
  list_push_back(&ofd_list, &ofd->elem);
  lock_release(&ofd_list_lock);

  return ofd;
}

struct open_file_desc* ofd_create_dir(struct dir* dir) {
  ASSERT(dir != NULL);

  struct open_file_desc* ofd = malloc(sizeof(struct open_file_desc));
  if (ofd == NULL)
    return NULL;

  ofd->type = FD_DIR;
  ofd->cmode = CONSOLE_READ; /* Not used for directories. */
  ofd->dir = dir;
  ofd->flags = 0;
  ofd->ref_count = 1;
  lock_init(&ofd->lock);

  lock_acquire(&ofd_list_lock);
  list_push_back(&ofd_list, &ofd->elem);
  lock_release(&ofd_list_lock);

  return ofd;
}

struct open_file_desc* ofd_get_console(int fd) {
  switch (fd) {
    case 0:
      return &console_stdin;
    case 1:
      return &console_stdout;
    case 2:
      return &console_stderr;
    default:
      return NULL;
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * OFD REFERENCE COUNTING
 * ═══════════════════════════════════════════════════════════════════════════*/

struct open_file_desc* ofd_dup(struct open_file_desc* ofd) {
  if (ofd == NULL)
    return NULL;

  lock_acquire(&ofd->lock);
  ofd->ref_count++;
  lock_release(&ofd->lock);

  return ofd;
}

void ofd_close(struct open_file_desc* ofd) {
  if (ofd == NULL)
    return;

  /* Check if this is a console OFD - never actually close those. */
  if (ofd == &console_stdin || ofd == &console_stdout || ofd == &console_stderr) {
    lock_acquire(&ofd->lock);
    if (ofd->ref_count > 1)
      ofd->ref_count--;
    /* Never let console ref_count go to 0. */
    lock_release(&ofd->lock);
    return;
  }

  /* Acquire list lock first, then OFD lock (lock ordering). */
  lock_acquire(&ofd_list_lock);
  lock_acquire(&ofd->lock);

  ofd->ref_count--;
  if (ofd->ref_count == 0) {
    /* Remove from global list. */
    list_remove(&ofd->elem);
    lock_release(&ofd->lock);
    lock_release(&ofd_list_lock);

    /* Close the underlying file/directory. */
    if (ofd->type == FD_FILE && ofd->file != NULL) {
      file_close(ofd->file);
    } else if (ofd->type == FD_DIR && ofd->dir != NULL) {
      dir_close(ofd->dir);
    }

    free(ofd);
  } else {
    lock_release(&ofd->lock);
    lock_release(&ofd_list_lock);
  }
}
