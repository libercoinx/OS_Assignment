# RISC-V 单核最小内核 (riscv-os)

## 启动流程图（单核）

```text
Reset/Boot ROM
   |
   v
machine mode (M-mode) 入口 `_entry` (entry.S)
   - 关闭中断，设置临时栈
   - 清零 .bss
   - 配置 mstatus.MPP = S
   - mepc <- start (C 入口)
   - mret 切到 S 态
   |
   v
supervisor mode (S-mode) `start()` (start.c)
   - 初始化uart
   - 跳转 main()
   |
   v
`main()`
   - print Hello OS
   - 进入调度/空转循环
```

## 内存布局方案

- 物理内存假设：从 0x80000000 起一段连续 DRAM（如 QEMU virt）。
- 链接地址：内核链接到 0x80000000。
- 段顺序：`.text` -> `.rodata` -> `.data` -> `.bss` -> `end`。
- 导出符号：`etext`（代码段末）、`end`（镜像末，用作早期分配器起点）。

## 必需硬件初始化步骤

- 关中断，设置栈（M 态）。
- 清零 `.bss`（M 态或 S 态早期）。
- 配置 `mstatus.MPP=S`，`mepc=start` 并 `mret` 切到 S 态。
- 委托异常/中断到 S 态：`medeleg/mideleg`；开启 `sie` 中的定时/外部中断位（按需）。
- 配置 PMP 允许 S 态访问全部内存。
- 初始化 UART（MMIO 地址写入波特率/控制寄存器），用于日志输出。

## 目录结构

```
riscv-os/
  └─ kernel/
    ├─ kernel.ld        # 链接脚本：内存布局
    ├─ entry.S          # 启动汇编（M 态），清 BSS，切 S 态
    └─ start.c          # 早期 C 入口（S 态），硬件与子系统初始化
```
