# 实验 4：中断处理与时钟管理

## 1. 实验目标
- 理解 RISC-V 特权级中的中断委托、寄存器组合（`medeleg`/`mideleg`、`mie`/`sie`、`mcause`/`scause` 等）。
- 实现一个可运行于单核的内核陷阱框架，完成上下文保存、时钟中断处理、异常分发。
- 在简单调度场景下驱动时钟中断，提供基础的测量、调试与测试用例。

## 2. 架构设计与关键模块

### 2.1 启动阶段（`entry.S` 与 `start.c`）
- 在机器态（`_entry`）清理 BSS、建立栈并设置 `mstatus.MPP = S`，然后配置 PMP 允许 S 态访问物理内存。
- 提前委托全部异常（`medeleg=0xffff`）以及软件/时钟/外部中断（`mideleg` 对应位），并开放 `mcounteren` 以便 S 态读取 `time`。
- S 态入口 `start()` 初始化控制台、调用 `trap_init()` 安装陷阱向量，然后 `timer_init()` 设置首个时钟事件，最后跳入 `main()`。

### 2.2 内核陷阱向量 `kernelvec.S`
- 对进入 S 态的陷阱进行上下文保存：按照 RISC-V ABI，将 ra/gp/tp/a0-a7/s0-s11/t0-t6 等全部保存到当前内核栈。
- 调用 C 语言的 `kerneltrap()` 进行实际分发，返回时按相同顺序恢复寄存器，最终执行 `sret` 回到被抢占的位置。

### 2.3 中断管理（`trap.c`）
- 维护 `interrupt_handler_t irq_table[IRQ_MAX]`，允许注册/启用/禁用 Supervisor 级的软、时钟、外部中断处理函数。
- `trap_init()`
  - 清空中断向量表并安装 `kernelvec` 到 `stvec`。
  - 注册时钟中断处理器 `timer_interrupt_handler()`。
- `timer_init()`
  - 通过 CLINT 将下一次触发时间写入 `mtimecmp`，清除 `sip.STIP`，同时打开发起方的 `sie.STIE`。
- `kerneltrap()`
  - 读取 `scause`、`sepc`、`sstatus` 判定陷阱来源。
  - 如果是中断（最高位为 1），根据低 8 位号分发到注册表；若是异常则构造 `trapframe` 交给 `handle_exception()`。
  - 处理完毕后恢复 `sepc` 与 `sstatus`，等待 `sret` 返回原流程。
- 提供 `intr_on()/intr_off()` 控制全局 S 态中断使能，以及 `get_time()`/`get_ticks()` 供测试统计。

### 2.4 时钟与异常处理
- 时钟中断使用 CLINT `mtime/mtimecmp` 触发，固定间隔 `TICK_INTERVAL=100000`，每次中断：
  1. `ticks++` 记录累计次数。
  2. 重新写入下一触发点。
  3. 清理 `sip` 的 STIP 位，防止重复触发。
- 异常处理按照需求拆分为多个函数：
  - `handle_syscall()`：打印提示并跳过当前指令。
  - `handle_instruction_page_fault()`、`handle_load_page_fault()`、`handle_store_page_fault()`：记录 `stval`、`sepc` 信息，简单地将 `sepc += 4` 跳过故障指令，保持内核存活。
  - 未覆盖的异常直接 `panic()`，便于调试。

## 3. 测试与验证

### 3.1 核心测试用例（`kernel/test.c`）
- `test_physical_memory()` / `test_pagetable()` / `test_virtual_memory()`：沿用实验 3 的检查，确保页表与分配器仍然正常。
- `test_timer_interrupt()`：等待 `ticks` 自增 5 次，统计从 `get_time()` 读取的周期差，确认时钟中断在后台持续触发。
- `test_interrupt_overhead()`：在中断使能与关闭状态下分别执行同样的空循环，比较所需时间，验证 `intr_on()/intr_off()` 与定时器可控。
- `test_exception_handling()`：故意向未映射地址（`KERNBASE - PGSIZE`）写入以触发存储异常，检查 `handle_store_page_fault()` 能够捕获并恢复执行。

### 3.2 输出示例
运行 `make clean && make` 生成 `kernel.elf`，通过 QEMU 启动后可看到类似输出：

```
Hello, OS!
[TEST] physical memory allocator
[PASS] physical memory allocator
...
[TEST] timer interrupt
[PASS] timer interrupts 0 -> 5 (delta XXXX cycles)
[TEST] exception handling
Store fault at 0x7fffffff000
[PASS] exception handled, ticks ...
```

### 3.3 调试要点
- 如遇 `panic("kerneltrap: not from supervisor mode")`，说明在 U 态或 SPP 置位错误时进入陷阱，需检查 `stvec` 与 `sstatus`。
- 时钟不触发时，重点检查 `mideleg` 是否在机器态正确配置、`sie.STIE` 是否开启、`mtimecmp` 写入是否生效。
- 异常处理中为防止重复陷阱，务必在对应分支中 `sepc += 4`，否则返回后会再次执行相同故障指令。

## 4. 构建与运行
```
$ make clean && make       # 生成 kernel.elf/kernel.bin
$ qemu-system-riscv64 -machine virt -nographic -bios none -kernel kernel.elf
```

## 5. 下一步展望
- 扩充中断向量表以支持 PLIC 外部中断，并实现共享中断的注册机制。
- 在时钟中断中接入调度器（例如最简单的时间片轮转），并统计上下文切换的开销。
- 丰富异常处理策略，加入按需缺页填充与系统调用框架，为用户态程序打基础。
