#include "uart.h"
#include "defs.h"
#include "kalloc.h"
#include "vm.h"

void main(void) {
  uart_puts("\nHello, OS!\n");
  kinit();
  kvminit();
  kvminithart();

  test_physical_memory();
  test_pagetable();
  test_virtual_memory();
  // test_printf_basic();
  // test_printf_edge_cases();

  for (;;) {
    /* spin */
  }
}




