#include "uart.h"
#include "defs.h"

void main(void) {
  uart_puts("\nHello, OS!\n");
  test_printf_basic();
  test_printf_edge_cases();

  for (;;) {
    /* spin */
  }
}





