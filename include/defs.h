#include "riscv.h"

#pragma once

// console.c
void console_init(void);
void console_putc(char c);
void console_puts(const char *s);

/* ANSI 控制 */
void clear_screen(void);          /* \033[2J\033[H */
void goto_xy(int col, int row);   /* \033[{row};{col}H */

// printf.c
int printf(const char *fmt, ...);
void printfint(int x);

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

// syscall.c helpers
int argint(int n, int *ip);
int argaddr(int n, uint64 *ip);
int argstr(int n, char *buf, int max);

// proc.c
void procinit(void);
int create_process(const char *name, void (*fn)(void *), void *arg);
void exit_process(int status);
int wait_process(int *status);
void scheduler(void) __attribute__((noreturn));
uint64 sys_getpid(void);
uint64 sys_yield(void);
uint64 sys_kill(void);
uint64 sys_wait(void);
uint64 sys_exit(void) __attribute__((noreturn));
uint64 sys_sleep(void);
uint64 sys_uptime(void);

// test.c
void test_printf_basic();
void test_printf_edge_cases();
void test_physical_memory(void);
void test_pagetable(void);
void test_virtual_memory(void);
void test_timer_interrupt(void);
void test_interrupt_overhead(void);
void test_exception_handling(void);
void test_filesystem_smoke(void);
void test_filesystem_integrity(void);
void test_concurrent_access(void);
void test_crash_recovery(void);
void test_filesystem_performance(void);
void run_syscall_tests(void* arg);

// fs.c
void fs_init(void);
int fs_write_file(const char *path, const char *data, int len);
int fs_read_file(const char *path, char *dst, int max);
int fs_delete_file(const char *path);
int fs_file_size(const char *path);
void fs_test_samples(void);
void fs_force_recovery(void);

// file.c
void fileinit(void);
