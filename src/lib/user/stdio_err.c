/* stdio_err.c - Error handling (feof, ferror, clearerr, perror) */

#include "stdio_impl.h"
#include <stdio.h>
#include <string.h>

/* Check if end-of-file indicator is set.
   Returns non-zero if EOF has been reached. */
int feof(FILE* stream) { return (stream->flags & _IOEOF) != 0; }

/* Check if error indicator is set.
   Returns non-zero if an error has occurred. */
int ferror(FILE* stream) { return (stream->flags & _IOERR) != 0; }

/* Clear both EOF and error indicators. */
void clearerr(FILE* stream) { stream->flags &= ~(_IOEOF | _IOERR); }

/* Print error message to stderr.
   Format: "s: error message\n" or just "error message\n" if s is NULL/empty. */
void perror(const char* s) {
  if (s != NULL && s[0] != '\0') {
    fputs(s, stderr);
    fputs(": ", stderr);
  }
  fputs("I/O error\n", stderr);
}
