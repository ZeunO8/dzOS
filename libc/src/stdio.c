// stdio.c
#include "stdio.h"
#include <zos/file.h>
#include "stdlib.h"
#include "string.h"
#include "usyscalls.h"
#include <stdint.h>
#include <stdarg.h>

static const char *io_digits = "0123456789abcdef";

static void print_char(int fd, char c)
{
  write(fd, &c, sizeof(c));
}

void print_int(int fd, long long xx, int base, int sign)
{
  char buf[64];
  unsigned long long x;

  if (sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

    // this prints ':' (10)
          // char ci = '0'+base;
          // write(DEFAULT_STDOUT, &ci, 1);
  int i = 0;
  // char k = io_digits[0]; // Is breaking here!!
  //         ci = k; 
  //         write(DEFAULT_STDOUT, &ci, 1); // this never prints
  do {
    buf[i++] = io_digits[x % base]; // this commented code is the original which works in kernelspace 
  } while ((x /= base) != 0);

  if (sign)
    buf[i++] = '-';

  while (--i >= 0)
    print_char(fd, buf[i]);
}

void print_int_padded(int fd, long long xx, int base, int sign, int width, char pad)
{
  char buf[32];
  int i;
  unsigned long long x;

  if (sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = io_digits[x % base];
  } while ((x /= base) != 0);

  if (sign)
    buf[i++] = '-';

  while (i < width)
    buf[i++] = pad;

  while (--i >= 0)
    print_char(fd, buf[i]);
}

void print_float(int fd, double f, int precision)
{
  if (f < 0)
  { print_char(fd, '-'); f = -f; }
  uint64_t int_part = (uint64_t)f;
  double frac_part = f - (double)int_part;
  print_int(fd, int_part, 10, 0);

  print_char(fd, '.');
  for (int i = 0; i < precision; i++)
  {
    frac_part *= 10.0;
    int digit = (int)frac_part;
    print_char(fd, '0' + digit);
    frac_part -= digit;
  }
}

static void sprint_int(char **str, const char *str_end, long long xx, int base,
                       int sign, int padding) {
  char buf[64];
  int i;
  unsigned long long x;

  memset(buf, '0', sizeof(buf));

  if (sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = io_digits[x % base];
  } while ((x /= base) != 0);

  i = i > padding ? i : padding;

  if (sign)
    buf[i++] = '-';

  while (--i >= 0 && *str != str_end) {
    **str = buf[i];
    (*str)++;
  }
}

static void print_ptr(int fd, uint64_t x) {
  print_char(fd, '0');
  print_char(fd, 'x');
  for (size_t i = 0; i < (sizeof(uint64_t) * 2); i++, x <<= 4)
    print_char(fd, io_digits[x >> (sizeof(uint64_t) * 8 - 4)]);
}

void prints(int fd, char* s)
{
  if (s == 0)
    s = "(null)";
  for (; *s; s++)
    print_char(fd, *s);
}

static void sprint_ptr(char **str, const char *str_end, uint64_t x) {
  // Print 0
  if (*str == str_end) // the fuck?
    return;
  **str = '0';
  (*str)++;
  // Print x
  if (*str == str_end)
    return;
  **str = 'x';
  (*str)++;
  // Print the rest in hex
  for (size_t i = 0; i < (sizeof(uint64_t) * 2) && *str != str_end;
       i++, x <<= 4) {
    **str = io_digits[x >> (sizeof(uint64_t) * 8 - 4)];
    (*str)++;
  }
}

// Helper function to parse width field
static int parse_width(const char *fmt, int *idx)
{
  int width = 0;
  while (fmt[*idx] >= '0' && fmt[*idx] <= '9')
  {
    width = width * 10 + (fmt[*idx] - '0');
    (*idx)++;
  }
  return width;
}

void vfprintf(int fd, const char *fmt, va_list ap) {
    int i, c0, c1, c2;
    char *s;
    
    for (i = 0; fmt[i] != 0; i++)
    {
        int cx = fmt[i] & 0xff;
        if (cx != '%')
        {
          print_char(fd, cx);
          continue;
        }
        i++; 
        
        // Check for width specifier with leading zero (e.g., %08x)
        int width = 0;
        char pad = ' ';
        if (fmt[i] == '0')
        {
          pad = '0';
          i++;
        }
        // width = parse_width(fmt, &i);
        
        c0 = fmt[i] & 0xff;
        c1 = 0;
        c2 = 0;
        if (c0) c1 = fmt[i + 1] & 0xff;
        if (c1) c2 = fmt[i + 2] & 0xff;

        if (c0 == 'i')
        {
          int val = va_arg(ap, int);
          print_int(fd, val, 10, 1);  // signed
        }
        else if (c0 == 'd')
        {
          int val = va_arg(ap, int);
          print_int(fd, val, 10, 1);  // signed
        }
        else if (c0 == 'u')
        {
          unsigned int val = va_arg(ap, unsigned int);
          print_int(fd, val, 10, 0);  // unsigned
        }
        else if (c0 == 'l' && c1 == 'd')
        {
          long val = va_arg(ap, long);
          print_int(fd, val, 10, 1);
          i += 1;
        }
        else if (c0 == 'l' && c1 == 'l' && c2 == 'd')
        {
          long long val = va_arg(ap, long long);
          print_int(fd, val, 10, 1);
          i += 2;
        }
        else if (c0 == 'l' && c1 == 'u')
        {
          unsigned long val = va_arg(ap, unsigned long);
          print_int(fd, val, 10, 0);
          i += 1;
        }
        else if (c0 == 'l' && c1 == 'l' && c2 == 'u')
        {
          unsigned long long val = va_arg(ap, unsigned long long);
          print_int(fd, val, 10, 0);
          i += 2;
        }
        else if (c0 == 'z' && c1 == 'u')
        {
          size_t val = va_arg(ap, size_t);
          print_int(fd, val, 10, 0);
          i += 1;
        }
        else if (c0 == 'x')
        {
          unsigned int val = va_arg(ap, unsigned int);
          if (width > 0)
            print_int_padded(fd, val, 16, 0, width, pad);
          else
            print_int(fd, val, 16, 0);
        }
        else if (c0 == 'l' && c1 == 'x')
        {
          unsigned long val = va_arg(ap, unsigned long);
          if (width > 0)
            print_int_padded(fd, val, 16, 0, width, pad);
          else
            print_int(fd, val, 16, 0);
          i += 1;
        }
        else if (c0 == 'l' && c1 == 'l' && c2 == 'x')
        {
          unsigned long long val = va_arg(ap, unsigned long long);
          if (width > 0)
            print_int_padded(fd, val, 16, 0, width, pad);
          else
            print_int(fd, val, 16, 0);
          i += 2;
        }
        else if (c0 == 'p')
        {
          void* ptr = va_arg(ap, void*);
          print_ptr(fd, (uint64_t)ptr);
        }
        else if (c0 == 's')
        {
          s = va_arg(ap, char *);
          prints(fd, s);
        }
        else if (c0 == 'c')
        {
          int ch = va_arg(ap, int);
          print_char(fd, (char)ch);
        }
        else if (c0 == 'f')
        {
          double f = va_arg(ap, double);
          print_float(fd, f, 6);
        }
        else if (c0 == 'l' && c1 == 'f')
        {
          double f = va_arg(ap, double);
          print_float(fd, f, 6);
          i += 1;
        }
        else if (c0 == '%')
          print_char(fd, '%');
        else if (c0 == 0)
          break;
        else
        {
          print_char(fd, '%'); 
          print_char(fd, c0);
        }
    }
}

void fprintf(int fd, const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vfprintf(fd, fmt, ap);
  va_end(ap);
}

void printf(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vfprintf(DEFAULT_STDOUT, fmt, ap);
  va_end(ap);
}

// Rest of the file remains the same...
void vsnprintf(char *str, size_t size, const char *fmt, va_list ap_in) {
  if (!size) return;
  char *string_end = str + size - 1;

  va_list ap;
  va_copy(ap, ap_in);

  int i, cx, c0, c1, c2;
  char *s;

  for (i = 0; (cx = fmt[i] & 0xff) != 0 && str <= string_end; i++) {
    if (cx != '%') { *str++ = cx; continue; }
    i++;
    c0 = fmt[i + 0] & 0xff; c1 = c2 = 0;
    if (c0) c1 = fmt[i + 1] & 0xff;
    if (c1) c2 = fmt[i + 2] & 0xff;

    if (c0 == 'd' || c0 == 'i') {
      int v = va_arg(ap, int);
      sprint_int(&str, string_end, (long long)v, 10, 1, 0);
    } else if (c0 == '.' && c2 == 'd') {
      int padding = c1 - '0';
      int v = va_arg(ap, int);
      sprint_int(&str, string_end, (long long)v, 10, 1, padding);
      i += 2;
    } else if (c0 == 'l' && c1 == 'd') {
      long v = va_arg(ap, long);
      sprint_int(&str, string_end, (long long)v, 10, 1, 0);
      i += 1;
    } else if (c0 == 'l' && c1 == 'l' && c2 == 'd') {
      long long v = va_arg(ap, long long);
      sprint_int(&str, string_end, v, 10, 1, 0);
      i += 2;
    } else if (c0 == 'u') {
      unsigned int v = va_arg(ap, unsigned int);
      sprint_int(&str, string_end, (long long)(unsigned long long)v, 10, 0, 0);
    } else if (c0 == 'l' && c1 == 'u') {
      unsigned long v = va_arg(ap, unsigned long);
      sprint_int(&str, string_end, (long long)(unsigned long long)v, 10, 0, 0);
      i += 1;
    } else if (c0 == 'l' && c1 == 'l' && c2 == 'u') {
      unsigned long long v = va_arg(ap, unsigned long long);
      sprint_int(&str, string_end, (long long)v, 10, 0, 0);
      i += 2;
    } else if (c0 == 'x') {
      unsigned int v = va_arg(ap, unsigned int);
      sprint_int(&str, string_end, (long long)(unsigned long long)v, 16, 0, 0);
    } else if (c0 == 'l' && c1 == 'x') {
      unsigned long v = va_arg(ap, unsigned long);
      sprint_int(&str, string_end, (long long)(unsigned long long)v, 16, 0, 0);
      i += 1;
    } else if (c0 == 'l' && c1 == 'l' && c2 == 'x') {
      unsigned long long v = va_arg(ap, unsigned long long);
      sprint_int(&str, string_end, (long long)v, 16, 0, 0);
      i += 2;
    } else if (c0 == 'p') {
      uint64_t p = (uint64_t)va_arg(ap, void*);
      sprint_ptr(&str, string_end, p);
    } else if (c0 == 's') {
      if ((s = va_arg(ap, char *)) == 0) s = "(null)";
      for (; *s && str <= string_end; s++) { *str++ = *s; }
    } else if (c0 == '%') {
      if (str <= string_end) *str++ = '%';
    } else if (c0 == 'c') {
      if (str <= string_end) *str++ = (char)va_arg(ap, int);
    } else if (c0 == 0) {
      break;
    } else {
      if (str <= string_end) *str++ = '%';
      if (str <= string_end) *str++ = c0;
    }
  }

  *str = '\0';
  va_end(ap);
}

void snprintf(char *str, size_t size, const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(str, size, fmt, ap);
  va_end(ap);
}

void puts(const char *s) {
  for (; *s; s++)
    write(DEFAULT_STDOUT, s, 1);
  const char new_line = '\n';
  write(DEFAULT_STDOUT, &new_line, 1);
}

char *gets(char *buf, int max) {
  char c;
  int i;

  for (i = 0; i + 1 < max;) {
    int cc = read(DEFAULT_STDIN, &c, 1);
    if (cc < 1)
      break;
    if (c == 127) {
      if (i == 0)
        continue;
      i--;
      write(DEFAULT_STDOUT, "\b \b", 3);
      continue;
    }
    buf[i++] = c;
    if (c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

void putchar(char c) { write(DEFAULT_STDOUT, &c, 1); }

void hexdump(const char *buf, size_t size) {
  for (size_t i = 0; i < size; i++) {
    uint8_t data = buf[i];
    putchar(io_digits[(data >> 4) & 0xF]);
    putchar(io_digits[data & 0xF]);
  }
  putchar('\n');
}