/* stdio_file.c - File stream management (fopen, fclose, fdopen, global streams) */

#include "stdio_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

/* Static storage for the three standard streams */
static FILE __stdin_storage;
static FILE __stdout_storage;
static FILE __stderr_storage;

/* Public pointers to standard streams */
FILE* stdin = &__stdin_storage;
FILE* stdout = &__stdout_storage;
FILE* stderr = &__stderr_storage;

/* Initialize the standard streams.
   Called from _start() before main(). */
void __stdio_init(void) {
  /* Initialize stdin (fd=0, read-only, line-buffered) */
  stdin->fd = 0;
  stdin->flags = _IOREAD | _IOLBF;
  stdin->buf = NULL;
  stdin->ptr = NULL;
  stdin->cnt = 0;
  stdin->bufsiz = 0;
  stdin->ungetc_buf = -1;

  /* Initialize stdout (fd=1, write-only, line-buffered) */
  stdout->fd = 1;
  stdout->flags = _IOWRITE | _IOLBF;
  stdout->buf = NULL;
  stdout->ptr = NULL;
  stdout->cnt = 0;
  stdout->bufsiz = 0;
  stdout->ungetc_buf = -1;

  /* Initialize stderr (fd=2, write-only, unbuffered) */
  stderr->fd = 2;
  stderr->flags = _IOWRITE | _IONBF;
  stderr->buf = stderr->smallbuf;
  stderr->ptr = stderr->smallbuf;
  stderr->cnt = 1;
  stderr->bufsiz = 1;
  stderr->ungetc_buf = -1;
}

/* Flush all open streams.
   Called implicitly on exit(). */
void __stdio_exit(void) {
  /* TODO: Call fflush() on stdout (and any other open streams)
     Note: stderr is unbuffered so doesn't need flushing */
}

/* Helper: parse mode string and return flags */
static int parse_mode(const char* mode) {
  if (mode == NULL || mode[0] == '\0')
    return -1;

  int flags = 0;
  switch (mode[0]) {
    case 'r':
      flags = _IOREAD;
      break;
    case 'w':
      flags = _IOWRITE;
      break;
    case 'a':
      flags = _IOWRITE;
      break;
    default:
      return -1;
  }

  /* Check for '+' modifier (read+write) */
  if (mode[1] == '+' || (mode[1] != '\0' && mode[2] == '+')) {
    flags = _IORW;
  }

  return flags;
}

/* Helper: allocate and initialize a FILE struct */
static FILE* alloc_file(int fd, int flags) {
  FILE* fp = malloc(sizeof(FILE));
  if (fp == NULL)
    return NULL;

  fp->fd = fd;
  fp->flags = flags;
  fp->buf = NULL;
  fp->ptr = NULL;
  fp->cnt = 0;
  fp->bufsiz = 0;
  fp->ungetc_buf = -1;

  return fp;
}

/* Open a file and return a FILE stream.
   mode: "r" = read, "w" = write (truncate), "a" = append
         "r+", "w+", "a+" = read+write variants */
FILE* fopen(const char* path, const char* mode) {
  if (path == NULL)
    return NULL;

  int flags = parse_mode(mode);
  if (flags < 0)
    return NULL;

  char mode_char = mode[0];

  /* Handle file creation for 'w' and 'a' modes */
  if (mode_char == 'w') {
    remove(path);
    if (!create(path, 0))
      return NULL;
  } else if (mode_char == 'a') {
    create(path, 0); /* Ignore return - file may exist */
  }

  /* Open the file */
  int fd = open(path);
  if (fd < 0)
    return NULL;

  /* For append mode, seek to end */
  if (mode_char == 'a') {
    int size = filesize(fd);
    if (size >= 0)
      seek(fd, size);
  }

  /* Allocate FILE struct */
  FILE* fp = alloc_file(fd, flags);
  if (fp == NULL) {
    close(fd);
    return NULL;
  }

  return fp;
}

/* Wrap an existing file descriptor in a FILE stream. */
FILE* fdopen(int fd, const char* mode) {
  if (fd < 0)
    return NULL;

  int flags = parse_mode(mode);
  if (flags < 0)
    return NULL;

  return alloc_file(fd, flags);
}

/* Close a file stream. */
int fclose(FILE* stream) {
  if (stream == NULL)
    return EOF;

  /* Flush if writable */
  if (stream->flags & (_IOWRITE | _IORW)) {
    fflush(stream);
  }

  /* Close the underlying fd */
  close(stream->fd);
  stream->fd = -1;

  /* Free buffer if we allocated it */
  if ((stream->flags & _IOMYBUF) && stream->buf != NULL) {
    free(stream->buf);
    stream->buf = NULL;
  }

  /* Free the FILE struct if not a standard stream */
  if (stream != stdin && stream != stdout && stream != stderr) {
    free(stream);
  }

  return 0;
}

/* Return the file descriptor for a stream. */
int fileno(FILE* stream) {
  if (stream == NULL)
    return -1;
  return stream->fd;
}
