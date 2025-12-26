#include <stdint.h>
#include "uart.h"
#include "defs.h"

void console_init(void) {
  uart_init();
}

void console_putc(char c) {
  uart_putc(c);
}

void console_puts(const char *s) {
  uart_puts(s);
}

void clear_screen(void) {
  uart_puts("\x1b[2J\x1b[H");
}

void goto_xy(int col, int row) {
  /* 简单生成 ESC[{row};{col}H */
  char buf[32];
  int i = 0;
  buf[i++] = '\x1b';
  buf[i++] = '[';
  /* row */
  if (row <= 0) row = 1;
  if (col <= 0) col = 1;
  int r = row, c = col;
  char tmp[16]; int t = 0;
  do { tmp[t++] = '0' + (r % 10); r /= 10; } while (r);
  while (t) buf[i++] = tmp[--t];
  buf[i++] = ';';
  t = 0;
  do { tmp[t++] = '0' + (c % 10); c /= 10; } while (c);
  while (t) buf[i++] = tmp[--t];
  buf[i++] = 'H';
  buf[i] = '\0';
  uart_puts(buf);
}

int consoleread(char *dst, int n) {
  int i = 0;
  while (i < n) {
    int ch = uart_getc();
    if (ch < 0)
      continue;
    char c = (char)ch;
    if (c == '\b' || c == 0x7f) { /* backspace */
      if (i > 0) {
        i--;
        uart_putc('\b');
        uart_putc(' ');
        uart_putc('\b');
      }
      continue;
    }
    if (c == '\r' || c == '\n') {
      uart_putc('\n');
      dst[i++] = '\n';
      break;
    }
    /* echo normal characters */
    uart_putc(c);
    dst[i++] = c;
  }
  return i;
}

int consolewrite(const char *src, int n) {
  int i;
  for (i = 0; i < n; i++)
    uart_putc(src[i]);
  return i;
}

