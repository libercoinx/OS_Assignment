.section .text.entry
.globl _entry
_entry:
  /* 关中断，简化起见 */
  csrw mie, zero
  csrw mstatus, zero

  li t0, 0x10000000 # UART基地址
  li t1, 'S' # 启动标记
  sb t1, 0(t0) # 输出字符S表示启动

  /* 设置临时栈（链接到 BSS 里的 stack0 顶部） */
  la sp, stack0_end
  li t1, 'P' # 栈设置完成标记
  sb t1, 0(t0)

  /* 清零 .bss [bss_start, bss_end) */
  la t0, bss_start
  la t1, bss_end
1:
  bgeu t0, t1, 2f
  sd zero, 0(t0)
  addi t0, t0, 8
  j 1b
2:

  /* 切换到 S 态运行 C 入口 start() */
  /* 设置 mstatus.MPP = S (01b << 11) */
  csrr t0, mstatus
  li   t1, ~(3 << 11)       /* 清除位的掩码（寄存器版） */
  and  t0, t0, t1           /* 清除 MPP 两位 */
  li   t1, (1 << 11)        /* 置 S (01b) */
  or   t0, t0, t1
  csrw mstatus, t0

  /* 委托异常和中断给 S 态 */
  li   t0, 0xffff
  csrw medeleg, t0
  li   t0, (1 << 1) | (1 << 5) | (1 << 9)  /* 软件/时钟/外部中断 */
  csrw mideleg, t0

  /* 启用机器级定时器中断（必要条件之一） */
  csrr t0, mie
  li   t1, (1 << 5)
  or   t0, t0, t1
  csrw mie, t0

  /* 启用 Sstc 扩展，允许 S 态访问 stimecmp */
  csrr t0, menvcfg
  li   t1, (1 << 63)
  or   t0, t0, t1
  csrw menvcfg, t0

  /* 允许 S 态访问 time 寄存器 */
  li   t0, 0x7
  csrw mcounteren, t0

  /* mepc <- start */
  la   t0, start
  csrw mepc, t0

  # 放通 PMP（允许 S 态访问所有物理内存）
  li   t0, 0x3fffffffffffff
  csrw pmpaddr0, t0
  li   t0, 0x0f
  csrw pmpcfg0, t0
  /* mret 切换到 S 态 */
  mret

  .section .bss
  .align 12
  .globl bss_start
bss_start:
  .globl stack0
stack0:
  .space 4096
  .globl stack0_end
stack0_end:
  .globl bss_end
bss_end:
