#include "uart.h"
#include "defs.h"
#include "kalloc.h"
#include "vm.h"
#include "trap.h"
#include "proc.h"
#include "fs.h"
#include "panic.h"

void main(void) {
  uart_puts("\nHello, OS!\n");
  kinit();
  kvminit();
  kvminithart();
  fileinit();
  fs_init();

  procinit();
  timer_init();
  intr_on();
  if(create_process("fs-tests", run_syscall_tests, 0) < 0)
    panic("create_process");
  scheduler();  // 不会返回
}
