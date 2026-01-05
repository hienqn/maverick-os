/* stdio_str.c - String I/O (fgets, fputs, gets) */

#include "stdio_impl.h"
#include <string.h>

/* Read a line from stream into buffer s.
   Reads at most n-1 characters, stops at newline (included) or EOF.
   Returns s on success, NULL on error or EOF with no chars read. */
char* fgets(char* s, int n, FILE* stream) {
  /* TODO:
     1. If n <= 0, return NULL
     2. Loop reading characters:
        a. Call fgetc(stream)
        b. If EOF:
           - If no chars read yet, return NULL
           - Otherwise break out of loop
        c. Store character: *p++ = c
        d. If c == '\n', break
        e. If we've read n-1 chars, break
     3. Null-terminate: *p = '\0'
     4. Return s */

  (void)s;
  (void)n;
  (void)stream;
  return NULL;
}

/* Write a string to stream (without trailing newline).
   Returns non-negative on success, EOF on error. */
int fputs(const char* s, FILE* stream) {
  /* TODO:
     1. For each character in s:
        - Call fputc(c, stream)
        - If fputc returns EOF, return EOF
     2. Return 0 (or any non-negative value) */

  (void)s;
  (void)stream;
  return -1; /* EOF */
}

/* Read a line from stdin into buffer s.
   DEPRECATED: No length limit - buffer overflow risk!
   Provided for compatibility only. */
char* gets(char* s) {
  /* TODO:
     1. Loop calling fgetc(stdin)
     2. If EOF on first char, return NULL
     3. If '\n', break (don't store newline)
     4. Store character
     5. Null-terminate and return s

     WARNING: This function is dangerous and should not be used. */

  (void)s;
  return NULL;
}
