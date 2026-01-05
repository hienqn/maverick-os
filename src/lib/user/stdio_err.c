/* stdio_err.c - Error handling (feof, ferror, clearerr, perror) */

#include "stdio_impl.h"
#include <string.h>

/* Check if end-of-file indicator is set.
   Returns non-zero if EOF has been reached. */
int feof(FILE* stream) {
  /* TODO: Return (stream->flags & _IOEOF) != 0 */

  (void)stream;
  return 0;
}

/* Check if error indicator is set.
   Returns non-zero if an error has occurred. */
int ferror(FILE* stream) {
  /* TODO: Return (stream->flags & _IOERR) != 0 */

  (void)stream;
  return 0;
}

/* Clear both EOF and error indicators. */
void clearerr(FILE* stream) {
  /* TODO: stream->flags &= ~(_IOEOF | _IOERR) */

  (void)stream;
}

/* Print error message to stderr.
   Format: "s: error message\n" or just "error message\n" if s is NULL/empty. */
void perror(const char* s) {
  /* TODO:
     1. If s is not NULL and not empty:
        - Write s to stderr
        - Write ": " to stderr
     2. Write "I/O error\n" to stderr

     Note: PintOS doesn't have errno, so we can only print
     a generic error message. In a full implementation,
     you would use strerror(errno). */

  (void)s;
}
