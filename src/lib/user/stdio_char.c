/* stdio_char.c - Character I/O (fgetc, fputc, getc, putc, ungetc) */

#include "stdio_impl.h"
#include <stdio.h>

/* Read a single character from stream.
   Returns character as unsigned char cast to int, or EOF on error/end. */
int fgetc(FILE* stream) {
  /* Check for pushed-back character first */
  if (stream->ungetc_buf >= 0) {
    int c = stream->ungetc_buf;
    stream->ungetc_buf = -1;
    return c;
  }

  /* If buffer has data, return from buffer */
  if (stream->cnt > 0) {
    stream->cnt--;
    return (unsigned char)*stream->ptr++;
  }

  /* Buffer empty, refill it */
  return __fillbuf(stream);
}

/* Equivalent to fgetc - can be implemented as macro for performance. */
int getc(FILE* stream) { return fgetc(stream); }

/* Read character from stdin. */
int getchar(void) { return fgetc(stdin); }

/* Write a single character to stream.
   Returns character written, or EOF on error. */
int fputc(int c, FILE* stream) {
  /* If buffer has space, store there */
  if (stream->cnt > 0) {
    *stream->ptr++ = (unsigned char)c;
    stream->cnt--;

    /* Line buffered: flush on newline */
    if ((stream->flags & _IOLBF) && c == '\n')
      fflush(stream);

    return c;
  }

  /* Buffer full, flush and store */
  return __flushbuf(c, stream);
}

/* Equivalent to fputc - can be implemented as macro for performance. */
int putc(int c, FILE* stream) { return fputc(c, stream); }

/* Push a character back onto the input stream.
   Only one character of pushback is guaranteed. */
int ungetc(int c, FILE* stream) {
  /* Can't push back EOF */
  if (c == EOF)
    return EOF;

  /* Already have a pushed-back character */
  if (stream->ungetc_buf >= 0)
    return EOF;

  /* Store the character */
  stream->ungetc_buf = c;

  /* Clear EOF flag - we have data now */
  stream->flags &= ~_IOEOF;

  return c;
}
