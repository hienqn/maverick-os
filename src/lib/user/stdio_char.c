/* stdio_char.c - Character I/O (fgetc, fputc, getc, putc, ungetc) */

#include "stdio_impl.h"

/* Read a single character from stream.
   Returns character as unsigned char cast to int, or EOF on error/end. */
int fgetc(FILE* stream) {
  /* TODO:
     1. Check ungetc_buf first:
        - If stream->ungetc_buf >= 0, save it, set ungetc_buf = -1, return saved
     2. If stream->cnt > 0:
        - Decrement cnt, return *stream->ptr++ as unsigned char
     3. Otherwise call __fillbuf(stream) and return result */

  (void)stream;
  return -1; /* EOF */
}

/* Equivalent to fgetc - can be implemented as macro for performance. */
int getc(FILE* stream) { return fgetc(stream); }

/* Read character from stdin. */
int getchar(void) { return fgetc(stdin); }

/* Write a single character to stream.
   Returns character written, or EOF on error. */
int fputc(int c, FILE* stream) {
  /* TODO:
     1. If stream->cnt > 0:
        - Store: *stream->ptr++ = (unsigned char)c
        - Decrement stream->cnt
        - If _IOLBF and c == '\n', call fflush(stream)
        - Return c
     2. Otherwise call __flushbuf(c, stream) and return result */

  (void)c;
  (void)stream;
  return -1; /* EOF */
}

/* Equivalent to fputc - can be implemented as macro for performance. */
int putc(int c, FILE* stream) { return fputc(c, stream); }

/* Push a character back onto the input stream.
   Only one character of pushback is guaranteed. */
int ungetc(int c, FILE* stream) {
  /* TODO:
     1. If c == EOF, return EOF (can't push back EOF)
     2. If stream->ungetc_buf >= 0, return EOF (already have pushback)
     3. Store c in stream->ungetc_buf
     4. Clear _IOEOF flag (we have data now)
     5. Return c */

  (void)c;
  (void)stream;
  return -1; /* EOF */
}
