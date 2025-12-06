#include "uart.h"
#include "defs.h"
#include "kalloc.h"
#include "vm.h"
#include "trap.h"
#include "proc.h"
#include "panic.h"
#include "string.h"

static void shell_task(void *arg);
static void run_command(const char *cmdline);
static int parse_int(const char *s);
static int shell_readline(char *buf, int max);

void main(void) {
  uart_puts("\nHello, OS!\n");
  kinit();
  kvminit();
  kvminithart();
  procinit();
  timer_init();
  intr_on();
  if(create_process("shell", shell_task, 0) < 0)
    panic("create_process");
  scheduler();
}

static void shell_task(void *arg) {
  (void)arg;
  char buf[128];
  printf("Simple shell ready. Type 'test' to run suites.\n");
  while(1) {
    printf("$ ");
    int n = shell_readline(buf, sizeof(buf) - 1);
    if(n <= 0)
      continue;
    buf[n] = 0;
    // strip trailing newline
    if(n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
      buf[n-1] = 0;
    run_command(buf);
  }
}

static int parse_int(const char *s) {
  int v = 0;
  int neg = 0;
  if(*s == '-') { neg = 1; s++; }
  while(*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    s++;
  }
  return neg ? -v : v;
}

static int shell_readline(char *buf, int max) {
  int i = 0;
  while(i < max) {
    int ch = uart_getc();
    if(ch < 0) {
      yield();
      continue;
    }
    char c = (char)ch;
    if(c == '\r' || c == '\n') {
      console_putc('\n');
      break;
    }
    if(c == '\b' || c == 0x7f) { /* backspace */
      if(i > 0) {
        i--;
        console_putc('\b');
        console_putc(' ');
        console_putc('\b');
      }
      continue;
    }
    console_putc(c);
    buf[i++] = c;
  }
  return i;
}

static void run_command(const char *cmdline) {
  // tokenize by space
  char local[128];
  strlcpy(local, cmdline, sizeof(local));
  char *argv[4] = {0};
  int argc = 0;
  char *p = local;
  while(*p && argc < 4) {
    while(*p == ' ')
      p++;
    if(*p == 0) break;
    argv[argc++] = p;
    while(*p && *p != ' ')
      p++;
    if(*p == 0) break;
    *p++ = 0;
  }

  if(argc == 0)
    return;
  if(strcmp(argv[0], "test") == 0) {
    run_proc_tests(0);
  } else if(strcmp(argv[0], "ps") == 0) {
    ps();
  } else if(strcmp(argv[0], "nice") == 0 && argc == 3) {
    int pid = parse_int(argv[1]);
    int prio = parse_int(argv[2]);
    int r = set_priority(pid, prio);
    printf("set_priority(pid=%d, prio=%d) -> %d\n", pid, prio, r);
  } else if(strcmp(argv[0], "help") == 0) {
    printf("Commands: test | ps | nice <pid> <prio> | help\n");
  } else {
    printf("Unknown command: %s\n", argv[0]);
  }
}
