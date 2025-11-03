#include "uart.h"
#include "defs.h"
#include "kalloc.h"
#include "vm.h"
#include "trap.h"

void main(void) {
  uart_puts("\nHello, OS!\n");
  kinit();
  kvminit();
  kvminithart();
  timer_init();
  intr_on();

  // test_physical_memory();
  // test_pagetable();
  // test_virtual_memory();
  test_timer_interrupt();
  test_interrupt_overhead();
  test_exception_handling();
  // test_printf_basic();
  // test_printf_edge_cases();

  for (;;) {
    /* spin */
  }
}
