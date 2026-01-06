/* stdio_scan.c - Formatted input (fscanf, vfscanf, sscanf) */

#include "stdio_impl.h"
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Helper: Convert hex digit to value (0-15), or -1 if not hex digit. */
static int hex_digit(int c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* Helper: Check if character is octal digit. */
static bool is_octal(int c) { return c >= '0' && c <= '7'; }

/* Core scanf implementation for FILE streams. */
int vfscanf(FILE* stream, const char* format, va_list ap) {
  int matched = 0;
  int c;

  while (*format != '\0') {
    /* Whitespace in format: skip whitespace in input */
    if (isspace((unsigned char)*format)) {
      while (isspace((unsigned char)*format))
        format++;
      /* Skip whitespace in input */
      while ((c = fgetc(stream)) != EOF && isspace(c))
        ;
      if (c != EOF)
        ungetc(c, stream);
      continue;
    }

    /* Literal character (not %) */
    if (*format != '%') {
      c = fgetc(stream);
      if (c == EOF || c != *format) {
        if (c != EOF)
          ungetc(c, stream);
        break;
      }
      format++;
      continue;
    }

    /* Format specifier starting with % */
    format++; /* Skip '%' */

    /* Check for %% */
    if (*format == '%') {
      c = fgetc(stream);
      if (c != '%') {
        if (c != EOF)
          ungetc(c, stream);
        break;
      }
      format++;
      continue;
    }

    /* Parse optional suppression '*' */
    bool suppress = false;
    if (*format == '*') {
      suppress = true;
      format++;
    }

    /* Parse optional width */
    int width = 0;
    while (isdigit((unsigned char)*format)) {
      width = width * 10 + (*format - '0');
      format++;
    }

    /* Parse optional length modifier (ignored for simplicity) */
    while (*format == 'h' || *format == 'l')
      format++;

    /* Parse conversion specifier */
    char spec = *format++;
    if (spec == '\0')
      break;

    switch (spec) {
      case 'd':
      case 'i':
      case 'u': {
        /* Skip leading whitespace */
        while ((c = fgetc(stream)) != EOF && isspace(c))
          ;
        if (c == EOF)
          goto done;

        /* Parse sign */
        bool negative = false;
        if (c == '-') {
          negative = true;
          c = fgetc(stream);
        } else if (c == '+') {
          c = fgetc(stream);
        }

        if (c == EOF || !isdigit(c)) {
          if (c != EOF)
            ungetc(c, stream);
          goto done;
        }

        /* Parse digits */
        unsigned long value = 0;
        int digits = 0;
        while (c != EOF && isdigit(c)) {
          value = value * 10 + (c - '0');
          digits++;
          if (width > 0 && digits >= width)
            break;
          c = fgetc(stream);
        }
        if (c != EOF && (width == 0 || digits < width))
          ungetc(c, stream);

        if (!suppress) {
          int* p = va_arg(ap, int*);
          *p = negative ? -(int)value : (int)value;
          matched++;
        }
        break;
      }

      case 'x':
      case 'X': {
        /* Skip leading whitespace */
        while ((c = fgetc(stream)) != EOF && isspace(c))
          ;
        if (c == EOF)
          goto done;

        /* Skip optional 0x prefix */
        if (c == '0') {
          int next = fgetc(stream);
          if (next == 'x' || next == 'X') {
            c = fgetc(stream);
          } else {
            if (next != EOF)
              ungetc(next, stream);
          }
        }

        if (c == EOF || hex_digit(c) < 0) {
          if (c != EOF)
            ungetc(c, stream);
          goto done;
        }

        /* Parse hex digits */
        unsigned long value = 0;
        int digits = 0;
        while (c != EOF && hex_digit(c) >= 0) {
          value = value * 16 + hex_digit(c);
          digits++;
          if (width > 0 && digits >= width)
            break;
          c = fgetc(stream);
        }
        if (c != EOF && (width == 0 || digits < width))
          ungetc(c, stream);

        if (!suppress) {
          unsigned* p = va_arg(ap, unsigned*);
          *p = (unsigned)value;
          matched++;
        }
        break;
      }

      case 'o': {
        /* Skip leading whitespace */
        while ((c = fgetc(stream)) != EOF && isspace(c))
          ;
        if (c == EOF)
          goto done;

        if (!is_octal(c)) {
          ungetc(c, stream);
          goto done;
        }

        /* Parse octal digits */
        unsigned long value = 0;
        int digits = 0;
        while (c != EOF && is_octal(c)) {
          value = value * 8 + (c - '0');
          digits++;
          if (width > 0 && digits >= width)
            break;
          c = fgetc(stream);
        }
        if (c != EOF && (width == 0 || digits < width))
          ungetc(c, stream);

        if (!suppress) {
          unsigned* p = va_arg(ap, unsigned*);
          *p = (unsigned)value;
          matched++;
        }
        break;
      }

      case 's': {
        /* Skip leading whitespace */
        while ((c = fgetc(stream)) != EOF && isspace(c))
          ;
        if (c == EOF)
          goto done;

        char* dest = suppress ? NULL : va_arg(ap, char*);
        int chars = 0;

        while (c != EOF && !isspace(c)) {
          if (!suppress)
            *dest++ = (char)c;
          chars++;
          if (width > 0 && chars >= width)
            break;
          c = fgetc(stream);
        }
        if (c != EOF && (width == 0 || chars < width))
          ungetc(c, stream);

        if (chars == 0)
          goto done;

        if (!suppress) {
          *dest = '\0';
          matched++;
        }
        break;
      }

      case 'c': {
        /* %c does NOT skip whitespace unless preceded by space in format */
        int count = (width > 0) ? width : 1;
        char* dest = suppress ? NULL : va_arg(ap, char*);

        for (int i = 0; i < count; i++) {
          c = fgetc(stream);
          if (c == EOF) {
            if (i == 0)
              goto done;
            break;
          }
          if (!suppress)
            *dest++ = (char)c;
        }
        if (!suppress)
          matched++;
        break;
      }

      case 'n': {
        /* %n doesn't consume input or count toward matches */
        /* Not implemented - would need to track chars consumed */
        if (!suppress) {
          int* p = va_arg(ap, int*);
          *p = 0; /* Placeholder */
        }
        break;
      }

      default:
        /* Unknown specifier, stop parsing */
        goto done;
    }
  }

done:
  return (matched == 0 && feof(stream)) ? EOF : matched;
}

/* Formatted input from stream (variadic). */
int fscanf(FILE* stream, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  int result = vfscanf(stream, format, ap);
  va_end(ap);
  return result;
}

/* Formatted input from stdin. */
int scanf(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  int result = vfscanf(stdin, format, ap);
  va_end(ap);
  return result;
}

/* String stream for sscanf */
struct string_file {
  const char* ptr; /* Current position */
  const char* end; /* End of string */
  int ungetc_char; /* Pushed back char, or -1 */
};

/* Get character from string stream. */
static int sgetc(struct string_file* sf) {
  if (sf->ungetc_char >= 0) {
    int c = sf->ungetc_char;
    sf->ungetc_char = -1;
    return c;
  }
  if (sf->ptr >= sf->end)
    return EOF;
  return (unsigned char)*sf->ptr++;
}

/* Push back character to string stream. */
static void sungetc(int c, struct string_file* sf) {
  if (c != EOF)
    sf->ungetc_char = c;
}

/* Core sscanf implementation. */
int vsscanf(const char* str, const char* format, va_list ap) {
  struct string_file sf = {.ptr = str, .end = str + strlen(str), .ungetc_char = -1};

  int matched = 0;
  int c;

  while (*format != '\0') {
    /* Whitespace in format: skip whitespace in input */
    if (isspace((unsigned char)*format)) {
      while (isspace((unsigned char)*format))
        format++;
      while ((c = sgetc(&sf)) != EOF && isspace(c))
        ;
      if (c != EOF)
        sungetc(c, &sf);
      continue;
    }

    /* Literal character */
    if (*format != '%') {
      c = sgetc(&sf);
      if (c == EOF || c != *format) {
        if (c != EOF)
          sungetc(c, &sf);
        break;
      }
      format++;
      continue;
    }

    /* Format specifier */
    format++;
    if (*format == '%') {
      c = sgetc(&sf);
      if (c != '%') {
        if (c != EOF)
          sungetc(c, &sf);
        break;
      }
      format++;
      continue;
    }

    bool suppress = false;
    if (*format == '*') {
      suppress = true;
      format++;
    }

    int width = 0;
    while (isdigit((unsigned char)*format)) {
      width = width * 10 + (*format - '0');
      format++;
    }

    while (*format == 'h' || *format == 'l')
      format++;

    char spec = *format++;
    if (spec == '\0')
      break;

    switch (spec) {
      case 'd':
      case 'i':
      case 'u': {
        while ((c = sgetc(&sf)) != EOF && isspace(c))
          ;
        if (c == EOF)
          goto sdone;

        bool negative = false;
        if (c == '-') {
          negative = true;
          c = sgetc(&sf);
        } else if (c == '+') {
          c = sgetc(&sf);
        }

        if (c == EOF || !isdigit(c)) {
          if (c != EOF)
            sungetc(c, &sf);
          goto sdone;
        }

        unsigned long value = 0;
        int digits = 0;
        while (c != EOF && isdigit(c)) {
          value = value * 10 + (c - '0');
          digits++;
          if (width > 0 && digits >= width)
            break;
          c = sgetc(&sf);
        }
        if (c != EOF && (width == 0 || digits < width))
          sungetc(c, &sf);

        if (!suppress) {
          int* p = va_arg(ap, int*);
          *p = negative ? -(int)value : (int)value;
          matched++;
        }
        break;
      }

      case 'x':
      case 'X': {
        while ((c = sgetc(&sf)) != EOF && isspace(c))
          ;
        if (c == EOF)
          goto sdone;

        if (c == '0') {
          int next = sgetc(&sf);
          if (next == 'x' || next == 'X') {
            c = sgetc(&sf);
          } else {
            if (next != EOF)
              sungetc(next, &sf);
          }
        }

        if (c == EOF || hex_digit(c) < 0) {
          if (c != EOF)
            sungetc(c, &sf);
          goto sdone;
        }

        unsigned long value = 0;
        int digits = 0;
        while (c != EOF && hex_digit(c) >= 0) {
          value = value * 16 + hex_digit(c);
          digits++;
          if (width > 0 && digits >= width)
            break;
          c = sgetc(&sf);
        }
        if (c != EOF && (width == 0 || digits < width))
          sungetc(c, &sf);

        if (!suppress) {
          unsigned* p = va_arg(ap, unsigned*);
          *p = (unsigned)value;
          matched++;
        }
        break;
      }

      case 's': {
        while ((c = sgetc(&sf)) != EOF && isspace(c))
          ;
        if (c == EOF)
          goto sdone;

        char* dest = suppress ? NULL : va_arg(ap, char*);
        int chars = 0;

        while (c != EOF && !isspace(c)) {
          if (!suppress)
            *dest++ = (char)c;
          chars++;
          if (width > 0 && chars >= width)
            break;
          c = sgetc(&sf);
        }
        if (c != EOF && (width == 0 || chars < width))
          sungetc(c, &sf);

        if (chars == 0)
          goto sdone;

        if (!suppress) {
          *dest = '\0';
          matched++;
        }
        break;
      }

      case 'c': {
        int count = (width > 0) ? width : 1;
        char* dest = suppress ? NULL : va_arg(ap, char*);

        for (int i = 0; i < count; i++) {
          c = sgetc(&sf);
          if (c == EOF) {
            if (i == 0)
              goto sdone;
            break;
          }
          if (!suppress)
            *dest++ = (char)c;
        }
        if (!suppress)
          matched++;
        break;
      }

      default:
        goto sdone;
    }
  }

sdone:
  return matched;
}

int sscanf(const char* str, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  int result = vsscanf(str, format, ap);
  va_end(ap);
  return result;
}
