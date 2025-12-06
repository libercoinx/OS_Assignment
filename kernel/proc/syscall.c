#include "defs.h"
#include "syscall.h"
#include "proc.h"
#include "trap.h"
#include "fs.h"
#include "vm.h"

static struct pushregs *current_regs;

static uint64
argraw(int n) {
  switch(n) {
    case 0:
      return current_regs->a0;
    case 1:
      return current_regs->a1;
    case 2:
      return current_regs->a2;
    case 3:
      return current_regs->a3;
    case 4:
      return current_regs->a4;
    case 5:
      return current_regs->a5;
    default:
      return 0;
  }
}

int
argint(int n, int *ip) {
  *ip = (int)argraw(n);
  return 0;
}

int
argaddr(int n, uint64 *ip) {
  *ip = argraw(n);
  return 0;
}

static int
fetchstr(uint64 addr, char *buf, int max) {
  struct proc *p = myproc();
  if(p == 0)
    return -1;
  if(p->pagetable == 0) {
    int i = 0;
    char *s = (char *)addr;
    for(; i < max; i++) {
      buf[i] = s[i];
      if(s[i] == 0)
        return 0;
    }
    return -1;
  }
  return copyinstr(p->pagetable, buf, addr, max);
}

int
argstr(int n, char *buf, int max) {
  uint64 addr;
  if(argaddr(n, &addr) < 0)
    return -1;
  return fetchstr(addr, buf, max);
}

static uint64 sys_unimplemented(void) { return (uint64)-1; }
uint64 sys_read(void);
uint64 sys_write(void);
uint64 sys_open(void);
uint64 sys_close(void);
uint64 sys_dup(void);
uint64 sys_fstat(void);
uint64 sys_chdir(void);
uint64 sys_mkdir(void);
uint64 sys_mknod(void);
uint64 sys_unlink(void);

uint64 sys_sleep(void);
uint64 sys_uptime(void);

static uint64 (*syscalls[])(void) = {
  [SYS_fork]    sys_unimplemented,
  [SYS_exit]    sys_exit,
  [SYS_wait]    sys_wait,
  [SYS_pipe]    sys_unimplemented,
  [SYS_read]    sys_read,
  [SYS_kill]    sys_kill,
  [SYS_exec]    sys_unimplemented,
  [SYS_fstat]   sys_fstat,
  [SYS_chdir]   sys_chdir,
  [SYS_dup]     sys_dup,
  [SYS_getpid]  sys_getpid,
  [SYS_sbrk]    sys_unimplemented,
  [SYS_sleep]   sys_sleep,
  [SYS_uptime]  sys_uptime,
  [SYS_open]    sys_open,
  [SYS_write]   sys_write,
  [SYS_mknod]   sys_mknod,
  [SYS_unlink]  sys_unlink,
  [SYS_link]    sys_unimplemented,
  [SYS_mkdir]   sys_mkdir,
  [SYS_close]   sys_close,
  [SYS_yield]   sys_yield,
};

void
syscall(struct trapframe *tf, struct pushregs *regs) {
  (void)tf;
  current_regs = regs;
  int num = (int)regs->a7;
  uint64 ret = (uint64)-1;
  if(num > 0 && num < (int)(sizeof(syscalls)/sizeof(syscalls[0])) && syscalls[num]) {
    ret = syscalls[num]();
  } else {
    printf("unknown sys call %d\n", num);
  }
  regs->a0 = ret;
  current_regs = 0;
}
