/* stdio_buf.c - Buffer management and binary I/O (fread, fwrite, fflush) */

#include "stdio_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

/* Forward declaration */
int fflush(FILE* stream);

/* Allocate a buffer for the stream if not already allocated. */
static void ensure_buffer(FILE* fp) {
  /* Already have a buffer? */
  if (fp->buf != NULL)
    return;

  if (fp->flags & _IONBF) {
    /* Unbuffered: use the 1-byte smallbuf */
    fp->buf = fp->smallbuf;
    fp->bufsiz = 1;
  } else {
    /* Buffered: allocate STDIO_BUFSIZ bytes */
    fp->buf = malloc(STDIO_BUFSIZ);
    if (fp->buf == NULL) {
      /* Fallback to unbuffered if malloc fails */
      fp->buf = fp->smallbuf;
      fp->bufsiz = 1;
    } else {
      fp->bufsiz = STDIO_BUFSIZ;
      fp->flags |= _IOMYBUF; /* We own this buffer, must free it */
    }
  }

  fp->ptr = fp->buf;
  fp->cnt = 0;
}

/* Fill the input buffer from the file descriptor.
   Returns first character, or EOF on error/end-of-file. */
int __fillbuf(FILE* fp) {
  /* Already at EOF? */
  if (fp->flags & _IOEOF)
    return EOF;

  ensure_buffer(fp);

  /* Read from file into buffer */
  int n = read(fp->fd, fp->buf, fp->bufsiz);

  if (n <= 0) {
    if (n == 0)
      fp->flags |= _IOEOF;
    else
      fp->flags |= _IOERR;
    return EOF;
  }

  /* Reset buffer pointers */
  fp->ptr = fp->buf;
  fp->cnt = n;

  /* Return first character */
  fp->cnt--;
  return (unsigned char)*fp->ptr++;
}

/* Flush output buffer and write character c (if c >= 0).
   Returns c on success, EOF on error. */
int __flushbuf(int c, FILE* fp) {
  ensure_buffer(fp);

  /* Unbuffered mode: write single byte directly */
  if (fp->flags & _IONBF) {
    if (c >= 0) {
      unsigned char ch = (unsigned char)c;
      if (write(fp->fd, &ch, 1) != 1) {
        fp->flags |= _IOERR;
        return EOF;
      }
    }
    return c;
  }

  /* Flush any existing data in buffer */
  int n = fp->ptr - fp->buf;
  if (n > 0) {
    if (write(fp->fd, fp->buf, n) != n) {
      fp->flags |= _IOERR;
      return EOF;
    }
  }

  /* Reset buffer */
  fp->ptr = fp->buf;
  fp->cnt = fp->bufsiz;

  /* Store new character in buffer */
  if (c >= 0) {
    *fp->ptr++ = (unsigned char)c;
    fp->cnt--;

    /* Line buffered: flush on newline */
    if ((fp->flags & _IOLBF) && c == '\n')
      fflush(fp);
  }

  return c;
}

/* Flush output buffer to file. */
int fflush(FILE* stream) {
  /* NULL means flush all streams - not implemented */
  if (stream == NULL)
    return 0;

  /* Only flush writable streams */
  if (!(stream->flags & (_IOWRITE | _IORW)))
    return 0;

  /* Nothing to flush if buffer not allocated */
  if (stream->buf == NULL)
    return 0;

  /* Flush if there's data in buffer */
  int n = stream->ptr - stream->buf;
  if (n > 0) {
    if (write(stream->fd, stream->buf, n) != n) {
      stream->flags |= _IOERR;
      return EOF;
    }
  }

  /* Reset buffer */
  stream->ptr = stream->buf;
  stream->cnt = stream->bufsiz;

  return 0;
}

/* Read binary data from stream. */
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
  size_t total = size * nmemb;
  if (total == 0)
    return 0;

  unsigned char* dest = (unsigned char*)ptr;
  size_t bytes_read = 0;

  while (bytes_read < total) {
    if (stream->cnt > 0) {
      /* Copy from buffer */
      *dest++ = *stream->ptr++;
      stream->cnt--;
      bytes_read++;
    } else {
      /* Buffer empty, refill */
      int c = __fillbuf(stream);
      if (c == EOF)
        break;
      *dest++ = (unsigned char)c;
      bytes_read++;
    }
  }

  return bytes_read / size;
}

/* Write binary data to stream. */
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
  size_t total = size * nmemb;
  if (total == 0)
    return 0;

  const unsigned char* src = (const unsigned char*)ptr;
  size_t bytes_written = 0;

  while (bytes_written < total) {
    unsigned char c = *src++;

    if (stream->cnt > 0) {
      /* Space in buffer, store there */
      *stream->ptr++ = c;
      stream->cnt--;
      bytes_written++;

      /* Line buffered: flush on newline */
      if ((stream->flags & _IOLBF) && c == '\n')
        fflush(stream);
    } else {
      /* Buffer full, flush and store */
      if (__flushbuf(c, stream) == EOF)
        break;
      bytes_written++;
    }
  }

  return bytes_written / size;
}

/* Set buffering mode for a stream.
   mode: 0=_IOFBF (fully buffered), 1=_IOLBF (line buffered), 2=_IONBF (unbuffered)
   Note: mode values are the PUBLIC API values, not internal flags. */
int setvbuf(FILE* stream, char* buf, int mode, size_t size) {
  /* Can't change buffering after I/O has started */
  if (stream->buf != NULL)
    return -1;

  /* Clear old buffering mode flags */
  stream->flags &= ~(_IO_LINE_BUF | _IO_UNBUF | _IOMYBUF);

  /* Public API constants: _IOFBF=0, _IOLBF=1, _IONBF=2 */
  switch (mode) {
    case 0: /* _IOFBF - Fully buffered */
    case 1: /* _IOLBF - Line buffered */
      if (mode == 1)
        stream->flags |= _IO_LINE_BUF;

      if (buf != NULL && size > 0) {
        /* Use caller-provided buffer */
        stream->buf = (unsigned char*)buf;
        stream->bufsiz = size;
      }
      /* Otherwise, ensure_buffer will allocate later */
      break;

    case 2: /* _IONBF - Unbuffered */
      stream->flags |= _IO_UNBUF;
      stream->buf = stream->smallbuf;
      stream->bufsiz = 1;
      break;

    default:
      return -1;
  }

  stream->ptr = stream->buf;
  stream->cnt = 0;

  return 0;
}
