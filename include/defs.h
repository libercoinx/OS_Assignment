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

// proc.c
void procinit(void);
int create_process(const char *name, void (*fn)(void *), void *arg);
int create_process_prio(const char *name, void (*fn)(void *), void *arg, int priority);
void exit_process(int status);
int wait_process(int *status);
int set_priority(int pid, int prio);
int get_priority(int pid);
void ps(void);
void scheduler(void) __attribute__((noreturn));
int sys_getpid(void);
int sys_yield(void);
int sys_kill(int pid);
int sys_wait(int *status);
int sys_exit(int status);

// test.c
void test_printf_basic();
void test_printf_edge_cases();
void test_physical_memory(void);
void test_pagetable(void);
void test_virtual_memory(void);
void test_timer_interrupt(void);
void test_interrupt_overhead(void);
void test_exception_handling(void);
void test_process_creation(void);
void test_scheduler(void);
void test_scheduler_priority_gap(void);
void test_scheduler_same_priority(void);
void test_scheduler_mixed_priority(void);
void test_synchronization(void);
void debug_proc_table(void);
void run_proc_tests(void *arg);
