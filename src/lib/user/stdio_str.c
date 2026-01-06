/* stdio_str.c - String I/O (fgets, fputs, gets) */

#include "stdio_impl.h"
#include <stdio.h>
#include <string.h>

/* Read a line from stream into buffer s.
   Reads at most n-1 characters, stops at newline (included) or EOF.
   Returns s on success, NULL on error or EOF with no chars read. */
char* fgets(char* s, int n, FILE* stream) {
  if (n <= 0)
    return NULL;

  char* p = s;
  int chars_read = 0;

  while (chars_read < n - 1) {
    int c = fgetc(stream);
    if (c == EOF) {
      /* EOF or error */
      if (chars_read == 0)
        return NULL; /* No chars read */
      break;         /* Return what we have */
    }
    *p++ = (char)c;
    chars_read++;
    if (c == '\n')
      break; /* Stop at newline (include it) */
  }

  *p = '\0'; /* Null-terminate */
  return s;
}

/* Write a string to stream (without trailing newline).
   Returns non-negative on success, EOF on error. */
int fputs(const char* s, FILE* stream) {
  while (*s != '\0') {
    if (fputc(*s, stream) == EOF)
      return EOF;
    s++;
  }
  return 0; /* Success */
}

/* Read a line from stdin into buffer s.
   DEPRECATED: No length limit - buffer overflow risk!
   Provided for compatibility only. */
char* gets(char* s) {
  /* WARNING: This function is dangerous and should not be used. */
  char* p = s;
  int c;
  int chars_read = 0;

  while ((c = fgetc(stdin)) != EOF) {
    if (c == '\n')
      break; /* Don't store newline */
    *p++ = (char)c;
    chars_read++;
  }

  if (c == EOF && chars_read == 0)
    return NULL;

  *p = '\0';
  return s;
}

/* Note: puts() and putchar() are defined in console.c */
