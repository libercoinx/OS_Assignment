# 实验 5：进程管理与调度

## 1. 实验目标

- 在既有内核基础上引入可运行的进程抽象，完成内核线程的创建、调度与回收。
- 实现最小调度框架：上下文切换、时间片轮转、睡眠与唤醒原语。
- 通过自写测试覆盖内存分配、页表、异常、进程管理和同步等环节。

## 2. 架构概览

```
kernel/
  proc/proc.c      # 进程表、调度器、sleep/wakeup、kernel thread 创建
  proc/swtch.S     # 保存/恢复 ra/sp/s0-s11
  proc/sysproc.c   # 进程相关“系统调用”包装
  core/main.c      # 初始化后创建测试线程并进入 scheduler
  core/test.c      # 各类单元测试（含新增进程/调度/同步用例）
```

- `struct proc` 增加 `enum procstate`、内核栈、陷阱帧、上下文信息，利用 `struct kthread_info` 保存入口函数与参数。
- 单 CPU (`NCPU=1`)，`scheduler()` 轮询进程表，挑选 `RUNNABLE` 进程并通过 `swtch()` 切换；时钟中断在 `kerneltrap()` 中触发 `yield()`。
- `sleep()`/`wakeup()` 复用了 `struct spinlock` + 进程状态控制，实现条件等待；`push_off()/pop_off()` 用于管理中断嵌套。
- 提供 `create_process/exit_process/wait_process` 及 `sys_getpid/sys_yield/sys_kill` 等接口，方便测试直接调用。

## 3. 测试

- `run_all_tests()` 负责调度全部用例，日志形如 `[TEST] ...` / `[PASS] ...`。
- 既有内存/页表/中断/异常测试仍然执行，确保进程系统引入后行为一致。
- 新增 `test_process_creation_basic`、`test_scheduler_round_robin`、`test_sleep_wakeup_mechanism` 验证调度与同步逻辑。

## 4. 后续方向

- 扩展为真正的用户进程（`fork/exec`、系统调用表）。
- 引入更完善的锁、优先级调度及统计信息。
- 提供更多调试/自检用例，例如资源泄漏检测、死锁检测等。

---

# 实验 6：日志文件系统

## 1. 实验目标

- 参考 xv6 实现一个可运行的日志文件系统，涵盖块缓存、写前日志、inode/目录管理与路径解析。
- 在无真实磁盘的环境下使用 RAMDisk 仿真块设备，仍保持分层设计。
- 暴露内核级文件操作接口，并通过自定义测试验证一致性。

## 2. 核心模块

| 模块 | 说明 |
| --- | --- |
| `fs/bio.c` | LRU 块缓存 + RAMDisk (`uchar ramdisk[FSSIZE][BSIZE]`)，提供 `bread/bwrite/brelse` 等接口。 |
| `fs/log.c` | 写前日志实现，负责 `begin_op/end_op/log_write`、事务提交与恢复。 |
| `fs/fs.c` | 超级块、inode、目录、路径解析；内置 `fs_format()` 在 RAMDisk 上生成根目录，并提供测试辅助函数。 |
| `fs/file.c` | 打开文件表（`filealloc/filedup/fileclose`）及顺序 `read/write`。 |
| `include/fs.h` | 汇总常量 (`BSIZE=1024`, `FSSIZE=1024`, `NDIRECT=12` 等)、结构体（superblock/dinode/inode/buf/file）与外部接口。 |

### 布局与初始化

```
[boot][super][log (30)][inode blocks][bitmap][data blocks]
```

- `fs_format()` 在内核启动时运行：写入超级块、位图、创建根目录（包含 `.`、`..`），并通过 `reserve_block()` 标记元数据占用。
- 在日志初始化前引入 `log_persist()`：若日志尚未就绪则直接 `bwrite()`，完成 `log_init()` 后再切换到 `log_write()`，避免格式化阶段出现递归依赖。

### inode/目录/路径

- 复刻 xv6 设计：`NDIRECT=12`、单级间接块 `NINDIRECT = BSIZE / sizeof(uint)`、`MAXFILE = NDIRECT + NINDIRECT`。
- `ialloc/iget/ilock/iupdate/iput` 形成完整生命周期；`bmap()` 管理逻辑块映射；`dirlookup/dirlink` 与 `namei/nameiparent` 完成目录操作。
- 为测试提供 `fs_write_file/fs_read_file/fs_delete_file/fs_file_size` 辅助函数，在内核态直接操控文件。

### 测试

- `test_filesystem_smoke()`：基础读写/删除流程。
- `test_filesystem_integrity()`：创建-写入-重读-删除，校验大小与数据一致。
- `test_concurrent_access()`：多个内核线程并发创建/删除文件，检查日志与锁的正确性。
- `test_crash_recovery()`：多次强制执行日志恢复，验证幂等性与持久性。
- `test_filesystem_performance()`：比对大量小文件与单个大文件写入时的周期开销。
- 可选 `fs_test_samples()` 便于快速 sanity check。
- 全部用例由 `run_all_tests()` 驱动，方便在 QEMU 控制台观察 `[TEST]/[PASS]` 日志。

## 3. 构建与运行

```
$ make clean && make          # 生成 build/kernel.elf
$ make run                    # QEMU 中运行全部内核测试

qemu-system-riscv64 -machine virt -nographic -bios none \
  -kernel build/kernel.elf -s -S
```

终端输出中可看到进程、调度、同步以及文件系统各项测试，所有 `[PASS]` 表示实验完成。

## 4. 后续展望

- 替换 RAMDisk，接入 virtio-blk 等真实块设备。
- 扩展日志容量/性能，支持并发事务、批写与崩溃恢复测试。
- 实现更多 POSIX 风格接口（`open/read/write`, 符号链接等）以及用户态回归测试。
- 优化目录结构（长文件名、哈希目录等），提升查找性能。
