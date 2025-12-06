#include "proc.h"
#include "defs.h"
#include "trap.h"

uint64
sys_getpid(void) {
  struct proc *p = myproc();
  return p ? p->pid : -1;
}

uint64
sys_yield(void) {
  yield();
  return 0;
}

uint64
sys_kill(void) {
  int pid;
  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

uint64
sys_wait(void) {
  uint64 addr;
  if(argaddr(0, &addr) < 0)
    return -1;
  return wait_process((int *)addr);
}

uint64
sys_exit(void) {
  int status;
  if(argint(0, &status) < 0)
    status = -1;
  exit_process(status);
  __builtin_unreachable();
}

uint64
sys_sleep(void) {
  int ticks;
  if(argint(0, &ticks) < 0)
    return -1;
  uint64 start = get_ticks();
  while((int)(get_ticks() - start) < ticks) {
    yield();
  }
  return 0;
}

uint64
sys_uptime(void) {
  return get_ticks();
}
