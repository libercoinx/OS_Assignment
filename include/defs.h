#pragma once

// console.c
void console_init(void);
void console_putc(char c);
void console_puts(const char *s);
int consoleread(char *dst, int n);
int consolewrite(const char *src, int n);

/* ANSI 控制 */
void clear_screen(void);          /* \033[2J\033[H */
void goto_xy(int col, int row);   /* \033[{row};{col}H */

// printf.c
int printf(const char *fmt, ...);
void printfint(int x);

// test.c
void test_printf_basic();
void test_printf_edge_cases();
