#include "uart.h"
#include "defs.h"
#include "kalloc.h"
#include "vm.h"
#include "trap.h"
#include "proc.h"
#include "panic.h"

void main(void) {
  uart_puts("\nHello, OS!\n");
  kinit();
  kvminit();
  kvminithart();
  procinit();
  timer_init();
  intr_on();
  if(create_process("test-runner", run_proc_tests, 0) < 0)
    panic("create_process");
  scheduler();
}
