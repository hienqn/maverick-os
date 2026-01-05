/* stdio_fmt.c - Formatted output (fprintf, vfprintf) */

#include "stdio_impl.h"
#include <stdarg.h>

/* Callback function for __vprintf that writes to a FILE stream. */
static void fputc_callback(char c, void* aux) {
  /* TODO: Call fputc(c, (FILE *)aux) */

  (void)c;
  (void)aux;
}

/* Formatted output to a stream. */
int vfprintf(FILE* stream, const char* format, va_list ap) {
  /* TODO:
     1. Call __vprintf(format, ap, fputc_callback, stream)
     2. The __vprintf function handles all format parsing
     3. Return number of characters written

     Note: You may need to track the count. Consider wrapping
     the callback to count characters, or modifying the aux struct. */

  (void)stream;
  (void)format;
  (void)ap;
  return 0;
}

/* Formatted output to a stream (variadic). */
int fprintf(FILE* stream, const char* format, ...) {
  /* TODO:
     1. va_start(ap, format)
     2. Call vfprintf(stream, format, ap)
     3. va_end(ap)
     4. Return result */

  (void)stream;
  (void)format;
  return 0;
}
