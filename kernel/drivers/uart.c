#include "uart.h"

static inline uint8_t mmio_read8(uintptr_t addr) {
  return *(volatile uint8_t *)addr;
}

static inline void mmio_write8(uintptr_t addr, uint8_t value) {
  *(volatile uint8_t *)addr = value;
}

void uart_init(void) {
  /* 禁用中断 */
  mmio_write8(UART0_BASE + UART_IER, 0x00);

  /* 使能 DLAB，设置波特率除数 = 1 (115200 @ 1843200) 或按 QEMU 默认基时钟 3686400 则除数=2 */
  mmio_write8(UART0_BASE + UART_LCR, 0x80);
  mmio_write8(UART0_BASE + UART_DLL, 0x01); /* 低字节 */
  mmio_write8(UART0_BASE + UART_DLM, 0x00); /* 高字节 */

  /* 8N1：8 位数据，无校验，1 个停止位；清除 DLAB */
  mmio_write8(UART0_BASE + UART_LCR, 0x03);

  /* 启用 FIFO，清除 RX/TX FIFO，触发阈值 14 字节（0xC7） */
  mmio_write8(UART0_BASE + UART_IIR_FCR, 0x07); /* 最小化：使能并清空 */

  /* 置 DTR/RTS，OUT2=1 以使能中断线（即便我们轮询，也不影响）*/
  mmio_write8(UART0_BASE + UART_MCR, 0x0B);
}

void uart_putc(char c) {
  /* 轮询等待 THR 空 (LSR.THRE=1) */
  while ((mmio_read8(UART0_BASE + UART_LSR) & LSR_THRE) == 0) {
    /* busy wait */
  }
  mmio_write8(UART0_BASE + UART_RBR_THR, (uint8_t)c);
}

void uart_puts(const char *s) {
  if (!s) return;
  for (; *s; s++) {
    if (*s == '\n') {
      uart_putc('\r');
    }
    uart_putc(*s);
  }
}

int uart_getc_nonblock(void) {
  if ((mmio_read8(UART0_BASE + UART_LSR) & LSR_DR) == 0)
    return -1;
  return (int)mmio_read8(UART0_BASE + UART_RBR_THR);
}

int uart_getc(void) {
  int ch;
  while ((ch = uart_getc_nonblock()) < 0) { }
  return ch & 0xFF;
}





