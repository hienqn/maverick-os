/* stdio_fmt.c - Formatted output (fprintf, vfprintf) */

#include "stdio_impl.h"
#include <stdio.h>
#include <stdarg.h>

/* Wrapper structure to track count while writing. */
struct fprintf_aux {
  FILE* stream;
  int count;
};

/* Callback function for __vprintf that writes to a FILE stream. */
static void fputc_callback(char c, void* aux) {
  struct fprintf_aux* fa = (struct fprintf_aux*)aux;
  if (fputc(c, fa->stream) != EOF)
    fa->count++;
}

/* Formatted output to a stream. */
int vfprintf(FILE* stream, const char* format, va_list ap) {
  struct fprintf_aux fa = {.stream = stream, .count = 0};
  __vprintf(format, ap, fputc_callback, &fa);
  return fa.count;
}

/* Formatted output to a stream (variadic). */
int fprintf(FILE* stream, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  int result = vfprintf(stream, format, ap);
  va_end(ap);
  return result;
}
