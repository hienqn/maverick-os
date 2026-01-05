/* stdio_pos.c - Stream positioning (fseek, ftell, rewind, fgetpos, fsetpos) */

#include "stdio_impl.h"
#include <syscall.h>

/* Seek to a position in the stream.
   whence: SEEK_SET (0) = from start, SEEK_CUR (1) = from current, SEEK_END (2) = from end
   Returns 0 on success, -1 on error. */
int fseek(FILE* stream, long offset, int whence) {
  /* TODO:
     1. If stream has _IOWRITE flag, call fflush(stream)
        - This writes any buffered output before seeking

     2. Invalidate read buffer:
        - Set stream->cnt = 0
        - Set stream->ptr = stream->buf
        - Clear stream->ungetc_buf = -1

     3. Clear _IOEOF flag (seeking past EOF is allowed)

     4. Calculate absolute position:
        - SEEK_SET: pos = offset
        - SEEK_CUR: pos = tell(fd) + offset - buffered_adjustment
        - SEEK_END: pos = filesize(fd) + offset

        Note: PintOS seek() only supports absolute positioning,
        so you need to calculate the absolute position first.

     5. Call seek(stream->fd, pos)

     6. Return 0 on success

     Note: PintOS doesn't have a seek syscall that returns error,
     so error handling may be limited. */

  (void)stream;
  (void)offset;
  (void)whence;
  return -1;
}

/* Return current position in stream.
   Returns position on success, -1 on error. */
long ftell(FILE* stream) {
  /* TODO:
     1. Get raw file position: pos = tell(stream->fd)

     2. Adjust for buffered data:
        - If reading (_IOREAD): subtract stream->cnt
          (we've read ahead but not consumed)
        - If writing (_IOWRITE): add (stream->ptr - stream->buf)
          (we've written to buffer but not flushed)

     3. Adjust for ungetc:
        - If stream->ungetc_buf >= 0, subtract 1

     4. Return adjusted position */

  (void)stream;
  return -1;
}

/* Seek to beginning of stream and clear error indicators. */
void rewind(FILE* stream) {
  /* TODO:
     1. Call fseek(stream, 0, SEEK_SET)
     2. Call clearerr(stream) */

  (void)stream;
}

/* fpos_t type - can just be a long for simple implementation */
typedef long fpos_t;

/* Get current position (alternative API using fpos_t). */
int fgetpos(FILE* stream, fpos_t* pos) {
  /* TODO:
     1. *pos = ftell(stream)
     2. Return 0 on success, -1 if ftell failed */

  (void)stream;
  (void)pos;
  return -1;
}

/* Set position (alternative API using fpos_t). */
int fsetpos(FILE* stream, const fpos_t* pos) {
  /* TODO:
     1. Return fseek(stream, *pos, SEEK_SET) */

  (void)stream;
  (void)pos;
  return -1;
}
