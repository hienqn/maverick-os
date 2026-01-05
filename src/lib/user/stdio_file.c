/* stdio_file.c - File stream management (fopen, fclose, fdopen, global streams) */

#include "stdio_impl.h"
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
  /* TODO: Initialize stdin (fd=0, read-only, line-buffered)
     - Set fd, flags (_IOREAD | _IOLBF)
     - Initialize buf=NULL, ptr=NULL, cnt=0, bufsiz=0
     - Set ungetc_buf = -1 */

  /* TODO: Initialize stdout (fd=1, write-only, line-buffered)
     - Set fd, flags (_IOWRITE | _IOLBF)
     - Initialize buffer fields */

  /* TODO: Initialize stderr (fd=2, write-only, unbuffered)
     - Set fd, flags (_IOWRITE | _IONBF)
     - For unbuffered, use smallbuf as the buffer */
}

/* Flush all open streams.
   Called implicitly on exit(). */
void __stdio_exit(void) {
  /* TODO: Call fflush() on stdout (and any other open streams)
     Note: stderr is unbuffered so doesn't need flushing */
}

/* Open a file and return a FILE stream.
   mode: "r" = read, "w" = write (truncate), "a" = append
         "r+", "w+", "a+" = read+write variants */
FILE* fopen(const char* path, const char* mode) {
  /* TODO:
     1. Parse mode string to determine flags:
        - 'r': _IOREAD, file must exist
        - 'w': _IOWRITE, create/truncate file
        - 'a': _IOWRITE, create/append
        - '+' modifier: add _IORW (read+write)

     2. For 'w' mode: remove(path), create(path, 0)
        For 'a' mode: create if doesn't exist

     3. Call open(path) to get file descriptor
        - Return NULL if open fails

     4. For 'a' mode: seek to end of file

     5. Allocate FILE struct with malloc()
        - Initialize all fields (fd, flags, buf=NULL, etc.)

     6. Return the FILE pointer */

  (void)path;
  (void)mode;
  return NULL;
}

/* Wrap an existing file descriptor in a FILE stream. */
FILE* fdopen(int fd, const char* mode) {
  /* TODO:
     1. Parse mode string for flags (like fopen)
     2. Allocate and initialize FILE struct
     3. Set fd to the provided descriptor
     4. Return FILE pointer

     Note: fd is already open, don't call open() */

  (void)fd;
  (void)mode;
  return NULL;
}

/* Close a file stream. */
int fclose(FILE* stream) {
  /* TODO:
     1. If stream has _IOWRITE flag, call fflush()
     2. Call close(stream->fd)
     3. If _IOMYBUF flag is set, free(stream->buf)
     4. If stream is NOT stdin/stdout/stderr, free(stream)
     5. Return 0 on success, EOF on error */

  (void)stream;
  return -1;
}

/* Return the file descriptor for a stream. */
int fileno(FILE* stream) {
  /* TODO: Simply return stream->fd */

  (void)stream;
  return -1;
}
