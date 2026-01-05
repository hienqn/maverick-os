/* stdio_impl.h - Internal implementation details for stdio library.
   This file is NOT part of the public API. */

#ifndef LIB_USER_STDIO_IMPL_H
#define LIB_USER_STDIO_IMPL_H

#include <stddef.h>
#include <stdarg.h>

/* Buffer size for stdio streams */
#define STDIO_BUFSIZ 512

/* FILE structure flags */
#define _IOREAD 0x0001  /* Stream is open for reading */
#define _IOWRITE 0x0002 /* Stream is open for writing */
#define _IORW 0x0004    /* Stream is open for read+write */
#define _IOEOF 0x0010   /* End-of-file has been reached */
#define _IOERR 0x0020   /* An I/O error has occurred */
#define _IOMYBUF 0x0040 /* Buffer was allocated by library */
#define _IOLBF 0x0080   /* Line buffered */
#define _IONBF 0x0100   /* Unbuffered */
#define _IOFBF 0x0000   /* Fully buffered (default, no flag set) */

/* Internal FILE structure.
   Users see this as an opaque type via the FILE typedef. */
struct __file {
  int fd;                    /* Underlying file descriptor (-1 if closed) */
  unsigned char* buf;        /* I/O buffer (NULL until first use) */
  unsigned char* ptr;        /* Current position in buffer */
  int cnt;                   /* For read: chars remaining in buffer
                                  For write: space remaining in buffer */
  int bufsiz;                /* Size of allocated buffer */
  int flags;                 /* Mode and status flags (_IO* constants) */
  int ungetc_buf;            /* Pushed-back character from ungetc (-1 if none) */
  unsigned char smallbuf[1]; /* 1-byte buffer for unbuffered mode */
};

/* Make FILE an alias for struct __file */
typedef struct __file FILE;

/* Global standard streams (defined in stdio_file.c) */
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

/* Internal functions - not part of public API */

/* Fill the input buffer from the file descriptor.
   Returns first character read, or EOF on error/end-of-file.
   Called when cnt <= 0 during a read operation. */
int __fillbuf(FILE* fp);

/* Flush the output buffer and optionally write character c.
   If c >= 0, writes c after flushing. If c < 0, just flushes.
   Returns c on success, EOF on error.
   Called when cnt <= 0 during a write operation. */
int __flushbuf(int c, FILE* fp);

/* Initialize the stdio library.
   Called from _start() before main(). */
void __stdio_init(void);

/* Clean up stdio (flush all streams).
   Called implicitly by exit(). */
void __stdio_exit(void);

/* Internal printf engine (defined in lib/stdio.c).
   We use this for vfprintf implementation. */
void __vprintf(const char* format, va_list args, void (*output)(char, void*), void* aux);

#endif /* lib/user/stdio_impl.h */
