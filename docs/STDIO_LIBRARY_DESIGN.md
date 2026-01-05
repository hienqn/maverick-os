# User-Space Buffered I/O Library (stdio) Design

## Overview

This document describes the design of a POSIX-compatible stdio library for PintOS user programs. The library provides:

- `FILE*` stream abstraction with buffered I/O
- Global `stdin`, `stdout`, `stderr` streams
- Full formatted I/O: `fprintf`, `fscanf`, `fgets`, etc.
- Positioning and error handling

## Motivation

Currently, PintOS user programs have:
- Raw syscalls: `read()`, `write()`, `open()`, `close()`
- Printf family with 64-byte output buffer
- **No buffered input or `FILE*` streams**

This requires programs to manually buffer I/O and parse input, which is error-prone and verbose.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    User Program                              │
│                                                              │
│   fprintf(fp, "%d", n)    fgets(buf, n, fp)    fread(...)   │
└──────────────┬───────────────────┬──────────────────┬───────┘
               │                   │                  │
               ▼                   ▼                  ▼
┌─────────────────────────────────────────────────────────────┐
│                     stdio Library                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Formatting  │  │   String    │  │   Binary I/O        │  │
│  │ fprintf     │  │   fgets     │  │   fread/fwrite      │  │
│  │ fscanf      │  │   fputs     │  │                     │  │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬──────────┘  │
│         │                │                    │              │
│         ▼                ▼                    ▼              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │              Character I/O Layer                        │ │
│  │         fgetc / fputc / ungetc                          │ │
│  └─────────────────────────┬───────────────────────────────┘ │
│                            │                                 │
│                            ▼                                 │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                  Buffer Management                       │ │
│  │         __fillbuf() / __flushbuf()                       │ │
│  │    ┌─────────────────────────────────────┐              │ │
│  │    │   512-byte I/O buffer per FILE      │              │ │
│  │    └─────────────────────────────────────┘              │ │
│  └─────────────────────────┬───────────────────────────────┘ │
└────────────────────────────┼─────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                   System Call Layer                          │
│              read() / write() / seek() / tell()              │
└─────────────────────────────────────────────────────────────┘
```

---

## FILE Structure

```c
#define STDIO_BUFSIZ 512

struct __file {
    int fd;                     /* Underlying file descriptor (-1 if closed) */
    unsigned char *buf;         /* I/O buffer */
    unsigned char *ptr;         /* Current position in buffer */
    int cnt;                    /* Chars remaining (read) or space left (write) */
    int bufsiz;                 /* Buffer size */
    int flags;                  /* Mode and status flags */
    int ungetc_char;            /* Pushed-back character (-1 if none) */
    unsigned char smallbuf[1];  /* Fallback for unbuffered mode */
};

typedef struct __file FILE;
```

### Flags

| Flag       | Value  | Description                          |
|------------|--------|--------------------------------------|
| `_IOREAD`  | 0x0001 | Stream open for reading              |
| `_IOWRITE` | 0x0002 | Stream open for writing              |
| `_IORW`    | 0x0004 | Stream open for read+write           |
| `_IOEOF`   | 0x0010 | End-of-file reached                  |
| `_IOERR`   | 0x0020 | I/O error occurred                   |
| `_IOMYBUF` | 0x0040 | Buffer was allocated by library      |
| `_IOLBF`   | 0x0080 | Line buffered                        |
| `_IONBF`   | 0x0100 | Unbuffered                           |
| `_IOFBF`   | 0x0000 | Fully buffered (default)             |

---

## Global Streams

```c
/* Pre-allocated FILE structures */
static FILE __stdin  = { .fd = 0, .flags = _IOREAD | _IOLBF, ... };
static FILE __stdout = { .fd = 1, .flags = _IOWRITE | _IOLBF, ... };
static FILE __stderr = { .fd = 2, .flags = _IOWRITE | _IONBF, ... };

/* Public pointers */
FILE *stdin  = &__stdin;
FILE *stdout = &__stdout;
FILE *stderr = &__stderr;
```

**Buffering defaults:**
- `stdin`: Line buffered (returns on newline)
- `stdout`: Line buffered (flushes on newline)
- `stderr`: Unbuffered (immediate output)

---

## Core Operations

### Buffer Fill (for reading)

```c
static int __fillbuf(FILE *fp) {
    if (fp->flags & _IOEOF) return EOF;
    if (fp->buf == NULL) {
        /* Allocate buffer on first use */
        fp->buf = malloc(STDIO_BUFSIZ);
        fp->bufsiz = STDIO_BUFSIZ;
        fp->flags |= _IOMYBUF;
    }

    int n = read(fp->fd, fp->buf, fp->bufsiz);
    if (n <= 0) {
        fp->flags |= (n == 0) ? _IOEOF : _IOERR;
        return EOF;
    }

    fp->ptr = fp->buf;
    fp->cnt = n;
    return *fp->ptr++;
}
```

### Buffer Flush (for writing)

```c
static int __flushbuf(int c, FILE *fp) {
    if (fp->buf == NULL) {
        /* Allocate buffer on first use */
        fp->buf = malloc(STDIO_BUFSIZ);
        fp->bufsiz = STDIO_BUFSIZ;
        fp->flags |= _IOMYBUF;
        fp->ptr = fp->buf;
        fp->cnt = fp->bufsiz;
    }

    if (fp->flags & _IONBF) {
        /* Unbuffered: write single char */
        unsigned char ch = c;
        if (write(fp->fd, &ch, 1) != 1) {
            fp->flags |= _IOERR;
            return EOF;
        }
        return c;
    }

    /* Flush current buffer */
    int n = fp->ptr - fp->buf;
    if (n > 0 && write(fp->fd, fp->buf, n) != n) {
        fp->flags |= _IOERR;
        return EOF;
    }

    /* Reset buffer and add new char */
    fp->ptr = fp->buf;
    fp->cnt = fp->bufsiz;
    *fp->ptr++ = c;
    fp->cnt--;

    /* Line buffered: flush on newline */
    if ((fp->flags & _IOLBF) && c == '\n') {
        return fflush(fp) == 0 ? c : EOF;
    }

    return c;
}
```

### Character I/O (Macros for performance)

```c
#define getc(fp) \
    (--(fp)->cnt >= 0 ? *(fp)->ptr++ : __fillbuf(fp))

#define putc(c, fp) \
    (--(fp)->cnt >= 0 ? (*(fp)->ptr++ = (c)) : __flushbuf((c), (fp)))

#define getchar()    getc(stdin)
#define putchar(c)   putc((c), stdout)
```

---

## Formatted Output: fprintf

Leverages the existing `__vprintf()` callback-based formatter:

```c
/* Callback that writes to FILE* */
static void fputc_callback(char c, void *aux) {
    fputc(c, (FILE *)aux);
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    struct printf_aux aux;
    aux.stream = stream;
    aux.count = 0;
    __vprintf(format, ap, fputc_callback, &aux);
    return aux.count;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}
```

---

## Formatted Input: fscanf

### Format Specifiers Supported

| Specifier | Description                              |
|-----------|------------------------------------------|
| `%d`      | Signed decimal integer                   |
| `%i`      | Integer (auto-detect base from prefix)   |
| `%u`      | Unsigned decimal integer                 |
| `%x`, `%X`| Hexadecimal integer                      |
| `%o`      | Octal integer                            |
| `%s`      | String (whitespace-delimited)            |
| `%c`      | Single character (or N chars with width) |
| `%[...]`  | Character class (scanset)                |
| `%n`      | Store number of chars consumed           |
| `%%`      | Literal percent sign                     |

### Modifiers

| Modifier | Example  | Description                        |
|----------|----------|------------------------------------|
| Width    | `%5d`    | Maximum field width                |
| `*`      | `%*d`    | Suppress assignment (skip field)   |
| `h`      | `%hd`    | short int                          |
| `hh`     | `%hhd`   | signed char                        |
| `l`      | `%ld`    | long int                           |
| `ll`     | `%lld`   | long long int                      |

### Implementation Sketch

```c
int vfscanf(FILE *stream, const char *format, va_list ap) {
    int matched = 0;
    int c;

    while (*format) {
        if (isspace(*format)) {
            /* Skip whitespace in format -> skip whitespace in input */
            while (isspace(c = fgetc(stream))) ;
            if (c != EOF) ungetc(c, stream);
            format++;
            continue;
        }

        if (*format != '%') {
            /* Literal match */
            c = fgetc(stream);
            if (c != *format) {
                if (c != EOF) ungetc(c, stream);
                break;
            }
            format++;
            continue;
        }

        format++;  /* Skip '%' */

        /* Parse flags, width, length, conversion */
        bool suppress = (*format == '*');
        if (suppress) format++;

        int width = 0;
        while (isdigit(*format)) {
            width = width * 10 + (*format++ - '0');
        }

        /* Length modifier */
        int length = 0;  /* 0=int, 1=long, 2=longlong, -1=short, -2=char */
        if (*format == 'l') { length++; format++; if (*format == 'l') { length++; format++; } }
        else if (*format == 'h') { length--; format++; if (*format == 'h') { length--; format++; } }

        /* Conversion specifier */
        switch (*format++) {
            case 'd': case 'i': case 'u': case 'x': case 'o':
                /* Parse integer... */
                break;
            case 's':
                /* Parse string... */
                break;
            case 'c':
                /* Read character(s)... */
                break;
            case '[':
                /* Parse character class... */
                break;
            case 'n':
                /* Store chars consumed */
                break;
        }

        if (!suppress) matched++;
    }

    return matched > 0 ? matched : (feof(stream) ? EOF : 0);
}
```

---

## String I/O

### fgets

```c
char *fgets(char *s, int n, FILE *stream) {
    char *p = s;
    int c;

    if (n <= 0) return NULL;

    while (--n > 0 && (c = fgetc(stream)) != EOF) {
        *p++ = c;
        if (c == '\n') break;
    }

    if (p == s) return NULL;  /* No chars read */
    *p = '\0';
    return s;
}
```

### fputs

```c
int fputs(const char *s, FILE *stream) {
    while (*s) {
        if (fputc(*s++, stream) == EOF)
            return EOF;
    }
    return 0;
}
```

---

## Positioning

```c
int fseek(FILE *stream, long offset, int whence) {
    /* Flush write buffer before seeking */
    if (stream->flags & _IOWRITE) {
        if (fflush(stream) != 0) return -1;
    }

    /* Invalidate read buffer */
    stream->cnt = 0;
    stream->ptr = stream->buf;
    stream->ungetc_char = -1;
    stream->flags &= ~_IOEOF;

    /* Delegate to syscall */
    seek(stream->fd, offset);  /* Note: PintOS seek is SEEK_SET only */
    return 0;
}

long ftell(FILE *stream) {
    long pos = tell(stream->fd);

    /* Adjust for buffered but unread data */
    if (stream->flags & _IOREAD) {
        pos -= stream->cnt;
    }
    /* Adjust for buffered but unwritten data */
    if (stream->flags & _IOWRITE) {
        pos += (stream->ptr - stream->buf);
    }

    return pos;
}

void rewind(FILE *stream) {
    fseek(stream, 0, SEEK_SET);
    clearerr(stream);
}
```

---

## Error Handling

```c
int feof(FILE *stream) {
    return (stream->flags & _IOEOF) != 0;
}

int ferror(FILE *stream) {
    return (stream->flags & _IOERR) != 0;
}

void clearerr(FILE *stream) {
    stream->flags &= ~(_IOEOF | _IOERR);
}

void perror(const char *s) {
    if (s && *s) {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs("I/O error\n", stderr);  /* Simplified - no errno */
}
```

---

## File Opening/Closing

### fopen

```c
FILE *fopen(const char *path, const char *mode) {
    int flags = 0;

    /* Parse mode string */
    switch (*mode) {
        case 'r': flags = _IOREAD; break;
        case 'w': flags = _IOWRITE; break;
        case 'a': flags = _IOWRITE; break;  /* Append handled below */
        default: return NULL;
    }
    if (mode[1] == '+' || (mode[1] && mode[2] == '+')) {
        flags = _IORW;
    }

    /* Create file for write modes */
    if (*mode == 'w') {
        remove(path);
        if (!create(path, 0)) return NULL;
    }

    /* Open file */
    int fd = open(path);
    if (fd < 0) return NULL;

    /* Seek to end for append */
    if (*mode == 'a') {
        seek(fd, filesize(fd));
    }

    /* Allocate FILE */
    FILE *fp = malloc(sizeof(FILE));
    if (!fp) { close(fd); return NULL; }

    memset(fp, 0, sizeof(FILE));
    fp->fd = fd;
    fp->flags = flags;
    fp->ungetc_char = -1;

    return fp;
}
```

### fclose

```c
int fclose(FILE *stream) {
    int ret = 0;

    /* Flush write buffer */
    if (stream->flags & _IOWRITE) {
        if (fflush(stream) != 0) ret = EOF;
    }

    /* Close file descriptor */
    close(stream->fd);
    stream->fd = -1;

    /* Free buffer if we allocated it */
    if (stream->flags & _IOMYBUF) {
        free(stream->buf);
    }

    /* Don't free stdin/stdout/stderr */
    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }

    return ret;
}
```

---

## Source File Organization

| File                     | Contents                                    |
|--------------------------|---------------------------------------------|
| `lib/user/stdio_impl.h`  | Internal FILE struct, macros, prototypes    |
| `lib/user/stdio_file.c`  | fopen, fclose, fdopen, fileno, globals init |
| `lib/user/stdio_buf.c`   | fread, fwrite, fflush, __fillbuf, __flushbuf|
| `lib/user/stdio_char.c`  | fgetc, fputc, getc, putc, ungetc            |
| `lib/user/stdio_str.c`   | fgets, fputs                                |
| `lib/user/stdio_fmt.c`   | fprintf, vfprintf (reuses __vprintf)        |
| `lib/user/stdio_scan.c`  | fscanf, vfscanf, sscanf                     |
| `lib/user/stdio_pos.c`   | fseek, ftell, rewind, fgetpos, fsetpos      |
| `lib/user/stdio_err.c`   | feof, ferror, clearerr, perror              |

---

## Integration

### Header Updates (lib/stdio.h)

```c
/* Add to existing stdio.h */

typedef struct __file FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define EOF (-1)
#define BUFSIZ 512
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* File operations */
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
int fileno(FILE *stream);

/* Buffered I/O */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);

/* Character I/O */
int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
int getc(FILE *stream);
int putc(int c, FILE *stream);
int getchar(void);
int ungetc(int c, FILE *stream);

/* String I/O */
char *fgets(char *s, int n, FILE *stream);
int fputs(const char *s, FILE *stream);

/* Formatted I/O */
int fprintf(FILE *stream, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list ap);
int fscanf(FILE *stream, const char *format, ...);
int vfscanf(FILE *stream, const char *format, va_list ap);
int sscanf(const char *str, const char *format, ...);

/* Positioning */
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);

/* Error handling */
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
void perror(const char *s);
```

### Startup Initialization

In `lib/user/entry.c`, before calling `main()`:

```c
extern void __stdio_init(void);

void _start(int argc, char **argv) {
    __stdio_init();  /* Set up stdin/stdout/stderr */
    exit(main(argc, argv));
}
```

---

## Implementation Order

1. **Phase 1: Core** - FILE struct, global streams, __fillbuf/__flushbuf
2. **Phase 2: Character I/O** - fgetc, fputc, ungetc
3. **Phase 3: Binary I/O** - fread, fwrite, fflush, fclose
4. **Phase 4: Strings** - fgets, fputs
5. **Phase 5: Formatted output** - fprintf (hook into __vprintf)
6. **Phase 6: Formatted input** - fscanf, sscanf
7. **Phase 7: Positioning** - fseek, ftell, rewind
8. **Phase 8: File opening** - fopen, fdopen
9. **Phase 9: Polish** - setvbuf, error handling, tests

---

## Testing

Test cases to add in `tests/userprog/stdio/`:

| Test              | Description                           |
|-------------------|---------------------------------------|
| `fopen-fclose`    | Open, write, close, reopen, read      |
| `fprintf-basic`   | fprintf to file with %d, %s, %x       |
| `fscanf-basic`    | fscanf integers and strings           |
| `fgets-lines`     | Line-by-line reading                  |
| `feof-test`       | EOF detection after read              |
| `buffering`       | Verify stdout flushes on newline      |
| `fseek-ftell`     | Positioning correctness               |
| `ungetc`          | Character pushback                    |
