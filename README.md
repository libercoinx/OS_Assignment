## Lab2：printf 与 console 模块实验报告

### 1. 实验目标
- 基于 xv6 的输出系统理念，独立实现内核 `printf` 与 `console` 模块：
  - 完整实现的 `printf`（支持 %d/%x/%p/%s/%c/%%）
  - 控制台抽象层 `console`，串接到下层 16550 UART 驱动
  - 提供清屏、光标定位等基本 ANSI 控制
- 理解分层架构：`printf` -> `console` -> `uart` -> 硬件寄存器
- 掌握可变参数、数字转换、ANSI 转义序列等关键技术点

### 2. 架构设计

```
应用/内核调用
  └── printf(fmt, ...)           // 格式化层（解析格式，数字转换）
      └── console_putc/puts      // 控制台层（字符与字符串抽象）
          └── uart_putc/puts     // 硬件抽象层（16550 UART 轮询）
              └── MMIO 寄存器    // 硬件：THR/LSR 等
```

- 分层职责：
  - 格式化层：解析格式串、可变参数处理、整数/指针转字符串，不关心设备细节。
  - 控制台层：统一字符输出入口，提供简单的屏幕控制（清屏/定位）。
  - UART 层：面向硬件寄存器，提供最基本的 putc/puts/getc。

### 3. 关键实现

#### 3.1 UART 驱动（16550）
- 寄存器基址：`0x10000000`
- 关键寄存器：
  - THR(0x00)：发送保持寄存器（写）
  - RBR(0x00)：接收缓冲寄存器（读）
  - LSR(0x05)：线路状态寄存器（读），`THRE` 表示 THR 空，`DR` 表示接收就绪
- 初始化：关闭中断、配置 DLAB、设置波特率（DLL/DLM）、配置 8N1、启用并清空 FIFO、MCR 置 DTR/RTS/OUT2。
- 发送：轮询 `LSR.THRE`，为空后写 THR。
- 接收：轮询 `LSR.DR`，就绪后读 RBR。

#### 3.2 console 层
- 接口：
  - `console_init()`：初始化底层 UART
  - `console_putc(char)` / `console_puts(const char*)`
  - `consoleread(char *dst, int n)` / `consolewrite(const char* src, int n)`
  - ANSI：`clear_screen()` 输出 `"\x1b[2J\x1b[H"`，`goto_xy(col,row)` 输出 `ESC[{row};{col}H`
- 作用：统一字符输出/输入入口，便于将来替换为更复杂的 TTY/缓冲。

#### 3.3 printf 层
- 可变参数：`va_list/va_start/va_arg/va_end`
- 数字转换（非递归）：
  - 使用循环 `do{ digit = x % base; x /= base; }while(x)` 填充到缓冲区，再逆序输出
  - 负数处理：将 `x<0` 时转无符号进行运算，避免 `INT_MIN` 取负溢出
- 支持格式：`%d/%x/%p/%s/%c/%%`
- 示例原型：`int printf(const char *fmt, ...)`

