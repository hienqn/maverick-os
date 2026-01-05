#ifndef __LIB_USER_STDIO_H
#define __LIB_USER_STDIO_H

/* Handle-based printf (existing) */
int hprintf(int, const char*, ...) PRINTF_FORMAT(2, 3);
int vhprintf(int, const char*, va_list) PRINTF_FORMAT(2, 0);

/*
 * ============================================================================
 * FILE STREAM API (stdio)
 * ============================================================================
 */

/* Opaque FILE type - defined in stdio_impl.h */
typedef struct __file FILE;

/* Standard streams */
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

/* Constants */
#define EOF (-1)
#define BUFSIZ 512

/* Seek origins */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Buffering modes for setvbuf */
#define _IOFBF 0 /* Fully buffered */
#define _IOLBF 1 /* Line buffered */
#define _IONBF 2 /* Unbuffered */

/* Position type */
typedef long fpos_t;

/*
 * File operations
 */
FILE* fopen(const char* path, const char* mode);
FILE* fdopen(int fd, const char* mode);
int fclose(FILE* stream);
int fileno(FILE* stream);

/*
 * Buffered binary I/O
 */
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int fflush(FILE* stream);
int setvbuf(FILE* stream, char* buf, int mode, size_t size);

/*
 * Character I/O
 */
int fgetc(FILE* stream);
int fputc(int c, FILE* stream);
int getc(FILE* stream);
int putc(int c, FILE* stream);
int getchar(void);
int ungetc(int c, FILE* stream);

/*
 * String I/O
 */
char* fgets(char* s, int n, FILE* stream);
int fputs(const char* s, FILE* stream);
char* gets(char* s); /* Deprecated - avoid use */

/*
 * Formatted output
 */
int fprintf(FILE* stream, const char* format, ...) PRINTF_FORMAT(2, 3);
int vfprintf(FILE* stream, const char* format, va_list ap) PRINTF_FORMAT(2, 0);

/*
 * Formatted input
 */
int fscanf(FILE* stream, const char* format, ...);
int vfscanf(FILE* stream, const char* format, va_list ap);
int scanf(const char* format, ...);
int sscanf(const char* str, const char* format, ...);
int vsscanf(const char* str, const char* format, va_list ap);

/*
 * Positioning
 */
int fseek(FILE* stream, long offset, int whence);
long ftell(FILE* stream);
void rewind(FILE* stream);
int fgetpos(FILE* stream, fpos_t* pos);
int fsetpos(FILE* stream, const fpos_t* pos);

/*
 * Error handling
 */
int feof(FILE* stream);
int ferror(FILE* stream);
void clearerr(FILE* stream);
void perror(const char* s);

#endif /* lib/user/stdio.h */
