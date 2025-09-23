#pragma once

#include <stdint.h>

/* 16550 UART 基地址与寄存器偏移（QEMU virt 平台） */
#define UART0_BASE     0x10000000UL

/* 寄存器偏移（当 LCR.DLAB=0） */
#define UART_RBR_THR   0x00  /* 读: RBR，写: THR */
#define UART_IER       0x01  /* Interrupt Enable */
#define UART_IIR_FCR   0x02  /* 读: IIR，写: FCR */
#define UART_LCR       0x03  /* Line Control */
#define UART_MCR       0x04  /* Modem Control */
#define UART_LSR       0x05  /* Line Status */
#define UART_MSR       0x06  /* Modem Status */
#define UART_SCR       0x07  /* Scratch */

/* 当 LCR.DLAB=1 时：DLL/DLM */
#define UART_DLL       0x00
#define UART_DLM       0x01

/* LSR 位定义 */
#define LSR_THRE       (1 << 5)  /* Transmitter Holding Register Empty */
#define LSR_DR         (1 << 0)  /* Data Ready */

/* 基本输出接口 */
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
int  uart_getc(void);           /* 阻塞读取一个字符 */
int  uart_getc_nonblock(void);  /* 无数据返回 -1 */





