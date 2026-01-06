/* stdio_pos.c - Stream positioning (fseek, ftell, rewind, fgetpos, fsetpos) */

#include "stdio_impl.h"
#include <stdio.h>
#include <syscall.h>

/* Forward declaration */
long ftell(FILE* stream);

/* Seek to a position in the stream.
   whence: SEEK_SET (0) = from start, SEEK_CUR (1) = from current, SEEK_END (2) = from end
   Returns 0 on success, -1 on error. */
int fseek(FILE* stream, long offset, int whence) {
  /* For SEEK_CUR, get current logical position BEFORE invalidating buffer */
  long cur_pos = 0;
  if (whence == SEEK_CUR)
    cur_pos = ftell(stream);

  /* Flush write buffer before seeking */
  if (stream->flags & (_IOWRITE | _IORW))
    fflush(stream);

  /* Invalidate read buffer */
  stream->cnt = 0;
  if (stream->buf != NULL)
    stream->ptr = stream->buf;
  stream->ungetc_buf = -1;

  /* Clear EOF flag */
  stream->flags &= ~_IOEOF;

  /* Calculate absolute position */
  long pos;
  switch (whence) {
    case SEEK_SET:
      pos = offset;
      break;
    case SEEK_CUR:
      /* Use pre-computed logical position */
      pos = cur_pos + offset;
      break;
    case SEEK_END:
      pos = filesize(stream->fd) + offset;
      break;
    default:
      return -1;
  }

  if (pos < 0)
    return -1;

  seek(stream->fd, pos);
  return 0;
}

/* Return current position in stream.
   Returns position on success, -1 on error. */
long ftell(FILE* stream) {
  /* Get raw kernel file position */
  long pos = tell(stream->fd);

  /* Adjust for buffered read data (we read ahead) */
  if (stream->flags & _IOREAD)
    pos -= stream->cnt;

  /* Adjust for buffered write data (not flushed yet) */
  if ((stream->flags & _IOWRITE) && stream->buf != NULL)
    pos += (stream->ptr - stream->buf);

  /* Adjust for ungetc (pushed back 1 char) */
  if (stream->ungetc_buf >= 0)
    pos--;

  return pos;
}

/* Seek to beginning of stream and clear error indicators. */
void rewind(FILE* stream) {
  fseek(stream, 0, SEEK_SET);
  clearerr(stream);
}

/* fpos_t type - can just be a long for simple implementation */
typedef long fpos_t;

/* Get current position (alternative API using fpos_t). */
int fgetpos(FILE* stream, fpos_t* pos) {
  long p = ftell(stream);
  if (p < 0)
    return -1;
  *pos = p;
  return 0;
}

/* Set position (alternative API using fpos_t). */
int fsetpos(FILE* stream, const fpos_t* pos) { return fseek(stream, *pos, SEEK_SET); }
