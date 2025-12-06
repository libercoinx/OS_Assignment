## 系统调用与用户态陷阱支持

本阶段在内核线程 + 日志文件系统的基础上补齐了系统调用通路，核心改动如下：

- **陷阱路径**：`kernelvec.S` 记录通用寄存器到 `struct pushregs`，`trap.c` 在遇到 ecall 时转入 `handle_syscall()`，并提供 `struct trapframe`/`struct pushregs` 便于单元测试直接驱动。补全了 `trampoline.S` 的 `uservec/userret`，为后续真正的用户态切换打好桩。
- **系统调用分发**：新增 `proc/syscall.c`，用 `a7` 作为号，通过 `argint/argaddr/argstr` 取参并分发到表。支持的调用包含 `getpid/exit/wait/kill/yield/sleep/uptime` 以及 `open/read/write/close/dup/fstat/chdir/mkdir/mknod/unlink` 等文件相关接口，未实现的保持返回 `-1`。
- **文件描述符层**：`struct proc` 增加 `ofile[]/cwd`，`sysfile.c` 复用了 `create/namei/file{read,write,dup,stat}` 等接口，将用户缓冲区复制到内核后读写，stdout/stderr 在未显式打开时仍可直接输出到控制台。
- **测试用例**：`test.c` 中的 syscall 自测涵盖四个场景，均直接构造 `pushregs` 调用 `handle_syscall`：
  - `test_basic_syscalls`：验证 `getpid/sleep` 正常返回；未实现的 `fork/wait/exit` 给出跳过提示。
  - `test_parameter_passing`：打开/写入/重开/读取文件，核对路径、长度和内容，最后对无效 fd 写入返回错误。
  - `test_security`：对非法 fd 的读写和删除不存在文件，确认返回 `-1` 而不崩溃。
  - `test_syscall_performance`：测量 10k 次 `getpid` 系统调用的周期耗时。
  启动入口 `run_syscall_tests` 会依次运行上述场景。

## 构建与运行

```bash
make clean && make           # 生成 build/kernel.elf / build/kernel.bin
make run                     # 使用 QEMU 启动（需本地 riscv64-unknown-elf 工具链）
```

控制台会依次输出系统调用与文件系统相关的 `[TEST]/[PASS]` 日志。
