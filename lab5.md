# 实验 5：进程管理与调度

## 1. 实验目标

- 在既有内核基础上引入可运行的进程抽象，完成内核线程的创建、调度与回收。
- 实现最小调度框架：上下文切换、时间片轮转、睡眠与唤醒原语。
- 通过自写测试覆盖内存分配、页表、异常、进程管理和同步等环节。

## 2. 代码结构概览

```
include/
  proc.h           # 进程/CPU 数据结构与接口声明
kernel/
  core/
    main.c         # 系统入口，初始化后创建测试进程并进入调度器
    test.c         # 测试集合，新增进程/调度/同步相关用例
  proc/
    proc.c         # 进程表、调度器、sleep/wakeup、kernel 线程创建
    swtch.S        # 保存/恢复上下文的汇编例程
    sysproc.c      # 供测试调用的进程相关“系统调用”包装
  trap/
    trap.c         # 定时器中断触发调度，唤醒在 ticks 上睡眠的进程
```

## 3. 核心实现说明

- **进程控制块 (`struct proc`)**
  - 状态机：`UNUSED → USED → RUNNABLE/RUNNING/SLEEPING → ZOMBIE`。
  - 内核线程入口使用 `struct kthread_info` 存放目标函数与参数。
  - 每个进程拥有独立的内核栈、陷阱帧占位以及调度上下文。

- **CPU & 调度器**
  - 单 CPU (`NCPU=1`)，使用 `struct cpu` 记录当前进程、嵌套禁止中断次数以及恢复前的中断状态。
  - 调度策略升级为 3 级多级反馈队列（MLFQ）：新建/被唤醒的进程进入最高优先级队列，时间片分别为 2/4/8 个时钟；若消耗完整时间片则降级，若因睡眠/阻塞返回则直接回到最高级。
  - 运行队列由 `scheduler()` 通过 `runq_pop()` 获取最高优先级的进程，并在时间片开始时重置计数；时钟中断调用 `scheduler_tick()` 递增片内已用 tick，超限后设置 `timeslice_expired` 触发主动 `yield()`。
  - `proc_entry()` 作为所有内核线程的起点，释放进程锁后执行注册的函数，结束后调用 `exit_process()`。

- **上下文切换 (`swtch.S`)**
  - 保存 `ra/sp/s0-s11`，覆盖 xv6 中 RISC-V 调用约定下需保持的寄存器。
  - 与 `struct context` 定义一致，确保 `sched()` 与 `scheduler()` 间互换正确。

- **同步与睡眠**
  - `sleep(chan, lk)` / `wakeup(chan)` 按 xv6 模式实现，允许在任意自旋锁下阻塞/唤醒。
  - 时钟中断 (`timer_interrupt_handler`) 会唤醒在 `ticks` 地址上等待的进程，并在 `kerneltrap()` 中调用 `scheduler_tick()` 累积时间片、必要时触发当前进程 `yield()`。

- **内核线程接口**
  - `create_process(name, fn, arg)`：分配 PCB、初始化上下文、设为 `RUNNABLE`。
  - `wait_process(status)`：等待任意子进程退出并回收资源。
  - 额外提供 `sys_getpid/sys_yield/sys_kill/sys_wait/sys_exit` 供测试直接调用。

## 4. 测试与验证

入口进程 (`main.c`) 在完成 UART、内存、页表、陷阱初始化后：

1. 调用 `procinit()` 完成进程表初始化；
2. 创建单个测试内核线程 `run_proc_tests`；
3. 进入 `scheduler()`，后续流程完全由调度器驱动。

`kernel/core/test.c` 的 `run_proc_tests` 会顺序执行：

- 既有内存/页表/中断/异常测试；
- 新增测试：
  - `test_process_creation`：创建大量内核线程并回收，覆盖进程表耗尽场景；
  - `test_scheduler`：启动多个计算任务，观察调度切换并统计耗时；
  - `test_synchronization`：使用生产者/消费者模型验证 `sleep/wakeup` 的同步正确性；
  - `debug_proc_table`：打印当前进程表状态，便于调试残留 PCB。

运行方式：

```bash
$ make clean && make          # 生成 build/kernel.elf
$ make run                    # 在 QEMU 中启动并观察测试输出
```

终端将看到 `[SUITE] running kernel tests` 开头、各测试的 `[TEST]/[PASS]` 日志以及结尾 `[SUITE] all tests finished`。

## 5. 后续展望

- 扩展为真正的用户进程：实现 `fork/exec`、用户陷阱返回、系统调用表。
- 引入更完整的锁实现（支持多核与调试检测），并完善 `sleep/wakeup` 的丢失唤醒保护。
- 丰富调度策略：优先级、时间片统计、多级反馈队列，以及对 `ticks` 的延迟测量。
- 编写更多自检用例，例如压力测试、资源回收、死锁检测等。
