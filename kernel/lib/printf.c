#include <stdarg.h>
#include <stdint.h>
#include "defs.h"

static void print_number(long long x, int base, int sign) {
  char buf[32];
  const char *digits = "0123456789abcdef";
  int i = 0;
  unsigned long long ux;

  if (sign && x < 0) {
    ux = (unsigned long long)(-(x + 1)) + 1ULL; /* 处理 INT_MIN/LLONG_MIN */
  } else {
    ux = (unsigned long long)x;
  }

  /* 至少输出一位 */
  do {
    buf[i++] = digits[ux % (unsigned)base];
    ux /= (unsigned)base;
  } while (ux && i < (int)sizeof(buf));

  if (sign && x < 0) {
    buf[i++] = '-';
  }

  while (--i >= 0)
    console_putc(buf[i]);
}

void printfint(int x) {
  print_number((long long)x, 10, 1);
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int cnt = 0;
  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      console_putc(*fmt); cnt++; continue;
    }
    fmt++;
    if (*fmt == 0) break;
    int long_flag = 0;
    while(*fmt == 'l') {
      long_flag = 1;
      fmt++;
    }
    switch (*fmt) {
      case '%': console_putc('%'); cnt++; break;
      case 'd': {
        if(long_flag) {
          long v = va_arg(ap, long);
          print_number((long long)v, 10, 1);
        } else {
          int v = va_arg(ap, int);
          print_number((long long)v, 10, 1); /* 带符号十进制 */
        }
        break;
      }
      case 'u': {
        if(long_flag) {
          unsigned long v = va_arg(ap, unsigned long);
          print_number((long long)(unsigned long long)v, 10, 0);
        } else {
          unsigned v = va_arg(ap, unsigned);
          print_number((long long)(unsigned long long)v, 10, 0);
        }
        break;
      }
      case 'x': {
        if(long_flag) {
          unsigned long v = va_arg(ap, unsigned long);
          print_number((long long)(unsigned long long)v, 16, 0);
        } else {
          unsigned v = va_arg(ap, unsigned);
          print_number((long long)(unsigned long long)v, 16, 0);
        }
        break;
      }
      case 'p': {
        uintptr_t v = (uintptr_t)va_arg(ap, void*);
        console_puts("0x");
        print_number((long long)(unsigned long long)v, 16, 0);
        break;
      }
      case 'c': {
        int ch = va_arg(ap, int);
        console_putc((char)ch);
        break;
      }
      case 's': {
        const char *s = va_arg(ap, const char*);
        if (!s) s = "(null)";
        console_puts(s);
        break;
      }
      default:
        /* 未知格式，原样输出 */
        console_putc('%'); console_putc(*fmt);
        break;
    }
  }
  va_end(ap);
  return cnt;
}

