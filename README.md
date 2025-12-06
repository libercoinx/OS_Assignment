# 调度与进程管理速记

- 调度策略：5 级多级反馈队列（0~4，0 最高），时间片依次为 2/4/8/16/32 ticks。用完时间片会降级；睡眠/阻塞返回或新建进程默认按类型设定初始优先级（交互任务优先级高，CPU 密集偏低）；等待时间超过阈值会自动升级，避免饥饿。
- 内核接口：`create_process_prio(name, fn, arg, prio)` 可指定初始优先级；`set_priority(pid, prio)`/`get_priority(pid)` 动态调整；`ps()` 打印当前进程表。
- 时钟中断：`scheduler_tick()` 在每个 tick 里递增当前进程时间片并做 aging，必要时触发 `yield()`。
- 测试用例：`test_scheduler_priority_gap`/`test_scheduler_same_priority`/`test_scheduler_mixed_priority` 覆盖高低优先级抢占、公平性与 aging 收敛。

示例输出（命令行提示符使用 `$`）：

```
$ ps
PID   PRIO  STATE     TICKS
1    0     RUN      1
2    2     READY    0
```

示例“nice”式调用（用户态可封装为工具）：

```
$ nice 5 8   # 提高 PID 5 的优先级
$ nice 6 2   # 降低 PID 6 的优先级
```
