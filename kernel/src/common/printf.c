/**
 * Mostly from xv6-riscv:
 * https://github.com/mit-pdos/xv6-riscv/blob/de247db5e6384b138f270e0a7c745989b5a9c23b/kernel/printf.c#L26C1-L51C1
 */

#include "term.h"
#include "printf.h"
#include "cpu/asm.h"
#include "device/rtc.h"
#include "device/serial_port.h"
#include "common/spinlock.h"

static struct spinlock print_lock;

static const char *digits = "0123456789abcdef";

static enum output_mode current_output = OUTPUT_SERIAL;

void set_output_mode(enum output_mode mode)
{
    current_output = mode;
}

void kputc(char c)
{
  switch (current_output)
  {
    case OUTPUT_SERIAL: serial_putc(c); break;
    case OUTPUT_FLANTERM: term_putc(c); break;
    case OUTPUT_FRAMEBUFFER: break;
  }
}

int cputc(char* dest, int* rem, char c)
{
  if (rem && *rem >= 1)
  {
    *dest = c;
    --*rem;
    return 1;
  }
  return 0;
}

bool c_time_print = false;

void kprintint(long long xx, int base, int sign)
{
  
  kprints(c_time_print ? COLOR_BRIGHT_YELLOW_FG : COLOR_MAGENTA_FG);
  char buf[20];
  int i;
  unsigned long long x;

  if (sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (sign)
    buf[i++] = '-';

  while (--i >= 0)
    kputc(buf[i]);
  
  kprints(COLOR_RESET);
}

void kprintptr(uint64_t x)
{
  
  kprints(c_time_print ? COLOR_BRIGHT_YELLOW_FG : COLOR_MAGENTA_FG);
  kputc('0');
  kputc('x');
  for (size_t i = 0; i < (sizeof(uint64_t) * 2); i++, x <<= 4)
    kputc(digits[x >> (sizeof(uint64_t) * 8 - 4)]);
  
  kprints(COLOR_RESET);
}

double first_print_now = 0.0;

void kprintfloat(double f, int precision)
{
  if (f < 0)
  { kputc('-'); f = -f; }
  uint64_t int_part = (uint64_t)f;
  double frac_part = f - (double)int_part;
  kprintint(int_part, 10, 0);
  
  kprints(c_time_print ? COLOR_BRIGHT_YELLOW_FG : COLOR_MAGENTA_FG);
  kputc('.');
  for (int i = 0; i < precision; i++)
  {
    frac_part *= 10.0;
    int digit = (int)frac_part;
    kputc('0' + digit);
    frac_part -= digit;
  }
  
  kprints(COLOR_RESET);
}

int cprintint(char* dest, int* rem, long long xx, int base, int sign)
{
  int len = 0;
  int tlen = cprintf(dest, rem, c_time_print ? COLOR_BRIGHT_YELLOW_FG : COLOR_MAGENTA_FG);
  dest += tlen;
  len += tlen;
  char buf[20];
  int i;
  unsigned long long x;

  if (sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (sign)
    buf[i++] = '-';

  while (--i >= 0)
    len += cputc(dest++, rem, buf[i]);
  tlen = cprintf(dest, rem, COLOR_RESET);
  dest += tlen;
  len += tlen;
  return len;
}

int cprintptr(char* dest, int* rem, uint64_t x)
{
  int len = 0;
  int tlen = cprintf(dest, rem, c_time_print ? COLOR_BRIGHT_YELLOW_FG : COLOR_MAGENTA_FG);
  dest += tlen;
  len += tlen;
  len += cputc(dest++, rem, '0');
  len += cputc(dest++, rem, 'x');
  for (size_t i = 0; i < (sizeof(uint64_t) * 2); i++, x <<= 4)
    len += cputc(dest++, rem, digits[x >> (sizeof(uint64_t) * 8 - 4)]);
  tlen = cprintf(dest, rem, COLOR_RESET);
  dest += tlen;
  len += tlen;
  return len;
}

int cprintfloat(char* dest, int* rem, double f, int precision)
{
  int len = 0;
  if (f < 0)
  {
    len += cputc(dest++, rem, '-');
    f = -f;
  }
  uint64_t int_part = (uint64_t)f;
  double frac_part = f - (double)int_part;
  int tlen = cprintint(dest, rem, int_part, 10, 0);
  dest += tlen;
  len += tlen;

  tlen = cprintf(dest, rem, c_time_print ? COLOR_BRIGHT_YELLOW_FG : COLOR_MAGENTA_FG);
  dest += tlen;
  len += tlen;
  len += cputc(dest++, rem, '.');
  for (int i = 0; i < precision; i++)
  {
    frac_part *= 10.0;
    int digit = (int)frac_part;
    len += cputc(dest++, rem, '0' + digit);
    frac_part -= digit;
  }
  tlen = cprintf(dest, rem, COLOR_RESET);
  dest += tlen;
  len += tlen;
  return len;
}

void kprints(char* s)
{
  if (s == 0)
    s = "(null)";
  for (; *s; s++)
    kputc(*s);
}

int kvprintf(const char *fmt, va_list ap)
{
    int i, cx, c0, c1, c2; char *s;
    for (i = 0; (cx = fmt[i] & 0xff) != 0; i++)
    {
        if (cx != '%')
        {
          kputc(cx);
          continue;
        }
        i++; c0 = fmt[i + 0] & 0xff; c1 = c2 = 0;
        if (c0) c1 = fmt[i + 1] & 0xff;
        if (c1) c2 = fmt[i + 2] & 0xff;
        if (c0 == 'd')
          kprintint(va_arg(ap, int), 10, 1);
        else if (c0 == 'l' && c1 == 'd')
        {
          kprintint(va_arg(ap, uint64_t), 10, 1);
          i += 1;
        }
        else if (c0 == 'l' && c1 == 'l' && c2 == 'd')
        {
          kprintint(va_arg(ap, uint64_t), 10, 1);
          i += 2;
        }
        else if (c0 == 'u' || c0 == 'i')
          kprintint(va_arg(ap, int), 10, 0);
        else if (c0 == 'l' && c1 == 'u')
        {
          kprintint(va_arg(ap, uint64_t), 10, 0);
          i += 1;
        }
        else if (c0 == 'l' && c1 == 'l' && c2 == 'u')
        {
          kprintint(va_arg(ap, uint64_t), 10, 0);
          i += 2;
        }
        else if (c0 == 'x')
          kprintint(va_arg(ap, int), 16, 0);
        else if (c0 == 'l' && c1 == 'x')
        {
          kprintint(va_arg(ap, uint64_t), 16, 0);
          i += 1;
        }
        else if (c0 == 'l' && c1 == 'l' && c2 == 'x')
        {
          kprintint(va_arg(ap, uint64_t), 16, 0);
          i += 2;
        }
        else if (c0 == 'p')
          kprintptr(va_arg(ap, uint64_t));
        else if (c0 == 's')
        {
          s = va_arg(ap, char *);
          kprints(s);
        }
        else if (c0 == 'f')
        {
          double f = va_arg(ap, double);
          kprintfloat(f, 6);
        }
        else if (c0 == 'l' && c1 == 'f')
        {
          double f = va_arg(ap, double);
          kprintfloat(f, 6);
          i += 1;
        }
        // else if (c0 == 'L' && c1 == 'f')
        // {
        //   long double f = va_arg(ap, long double);
        //   kprintfloat(f, 6);
        //   i += 1;
        // }
        else if (c0 == '%')
          kputc('%');
        else if (c0 == 0)
          break;
        else
        {
          kputc('%'); kputc(c0);
        }
    }
    return 0;
}

int cvprintf(char* dest, int* rem, const char *fmt, va_list ap)
{
    int len = 0;
    int i, cx, c0, c1, c2; char *s;
    for (i = 0; (cx = fmt[i] & 0xff) != 0; i++)
    {
        if (cx != '%')
        {
          len += cputc(dest++, rem, cx);
          continue;
        }
        i++;
        c0 = fmt[i + 0] & 0xff;
        c1 = c2 = 0;
        if (c0)
          c1 = fmt[i + 1] & 0xff;
        if (c1)
          c2 = fmt[i + 2] & 0xff;
        if (c0 == 'd')
        {
          int tlen = cprintint(dest, rem, va_arg(ap, int), 10, 1);
          dest += tlen;
          len += tlen;
        }
        else if (c0 == 'l' && c1 == 'd')
        {
          int tlen = cprintint(dest, rem, va_arg(ap, uint64_t), 10, 1);
          dest += tlen;
          len += tlen;
          i += 1;
        }
        else if (c0 == 'l' && c1 == 'l' && c2 == 'd')
        {
          int tlen = cprintint(dest, rem, va_arg(ap, uint64_t), 10, 1);
          dest += tlen;
          len += tlen;
          i += 2;
        }
        else if (c0 == 'u' || c0 == 'i')
        {
          int tlen = cprintint(dest, rem, va_arg(ap, int), 10, 0);
          dest += tlen;
          len += tlen;
        }
        else if (c0 == 'l' && c1 == 'u')
        {
          int tlen = cprintint(dest, rem, va_arg(ap, uint64_t), 10, 0);
          dest += tlen;
          len += tlen;
          i += 1;
        }
        else if (c0 == 'l' && c1 == 'l' && c2 == 'u')
        {
          int tlen = cprintint(dest, rem, va_arg(ap, uint64_t), 10, 0);
          dest += tlen;
          len += tlen;
          i += 2;
        }
        else if (c0 == 'x')
        {
          int tlen = cprintint(dest, rem, va_arg(ap, int), 16, 0);
          dest += tlen;
          len += tlen;
        }
        else if (c0 == 'l' && c1 == 'x')
        {
          int tlen = cprintint(dest, rem, va_arg(ap, uint64_t), 16, 0);
          dest += tlen;
          len += tlen;
          i += 1;
        }
        else if (c0 == 'l' && c1 == 'l' && c2 == 'x')
        {
          int tlen = cprintint(dest, rem, va_arg(ap, uint64_t), 16, 0);
          dest += tlen;
          len += tlen;
          i += 2;
        }
        else if (c0 == 'p')
        {
          int tlen = cprintptr(dest, rem, va_arg(ap, uint64_t));
          dest += tlen;
          len += tlen;
        }
        else if (c0 == 's')
        {
            if ((s = va_arg(ap, char *)) == 0)
              s = "(null)";
            for (; *s; s++)
              len += cputc(dest++, rem, *s);
        }
        else if (c0 == 'f')
        {
          double f = va_arg(ap, double);
          int tlen = cprintfloat(dest, rem, f, 6);
          dest += tlen;
          len += tlen;
        }
        else if (c0 == 'l' && c1 == 'f')
        {
          double f = va_arg(ap, double);
          int tlen = cprintfloat(dest, rem, f, 6);
          dest += tlen;
          len += tlen;
          i += 1;
        }
        // else if (c0 == 'L' && c1 == 'f')
        // {
        //   long double f = va_arg(ap, long double);
        //   kprintfloat(f, 6);
        //   i += 1;
        // }
        else if (c0 == '%') len += cputc(dest++, rem, '%');
        else if (c0 == 0) break;
        else
        {
          len += cputc(dest++, rem, '%');
          len += cputc(dest++, rem, c0);
        }
    }
    return len;
}

int kprintf(const char *fmt, ...)
{
    va_list ap;
    spinlock_lock(&print_lock);
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    spinlock_unlock(&print_lock);
    return 0;
}

int cprintf(char* dest, int* rem, const char *fmt, ...)
{
    int len = 0;
    int tlen = 0;
    va_list ap;
    va_start(ap, fmt);
    tlen = cvprintf(dest, rem, fmt, ap);
    dest += tlen;
    len += tlen;
    va_end(ap);
    return len;
}

int ktprintf(const char *fmt, ...)
{
    double now_s = rtc_now_seconds();
    if (first_print_now == 0.0) first_print_now = now_s;
    double diff = now_s - first_print_now;
    c_time_print = true;
    kprintf("[%lf] ", diff);
    c_time_print = false;

    spinlock_lock(&print_lock);
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    spinlock_unlock(&print_lock);
    return 0;
}

int ctprintf(char* dest, int* rem, const char *fmt, ...)
{
    int len = 0;
    double now_s = rtc_now_seconds();
    if (first_print_now == 0.0) first_print_now = now_s;
    double diff = now_s - first_print_now;
    c_time_print = true;
    int tlen = cprintf(dest, rem, "[%lf] ", diff);
    dest += tlen;
    len += tlen;
    c_time_print = false;

    va_list ap;
    va_start(ap, fmt);
    tlen = cvprintf(dest, rem, fmt, ap);
    dest += tlen;
    len += tlen;
    va_end(ap);
    return len;
}


void khexdump(const char *buf, size_t size)
{
  for (size_t i = 0; i < size; i++)
  {
    uint8_t data = buf[i];
    kputc(digits[(data >> 4) & 0xF]);
    kputc(digits[data & 0xF]);
  }
  kputc('\n');
}

void panic(const char *s)
{
  cli();
  kprintf("panic: %s\n", s);
  for (;;)
    ;
}

int chexdump(char* dest, int* rem, const char *buf, size_t size)
{
  int len = 0;
  for (size_t i = 0; i < size; i++)
  {
    uint8_t data = buf[i];
    len += cputc(dest++, rem, digits[(data >> 4) & 0xF]);
    len += cputc(dest++, rem, digits[data & 0xF]);
  }
  len += cputc(dest++, rem, '\n');
  return len;
}