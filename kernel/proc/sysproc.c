#include "proc.h"

int
sys_getpid(void) {
  struct proc *p = myproc();
  return p ? p->pid : -1;
}

int
sys_yield(void) {
  yield();
  return 0;
}

int
sys_kill(int pid) {
  return kill(pid);
}

int
sys_wait(int *status) {
  return wait_process(status);
}

int
sys_exit(int status) {
  exit_process(status);
  __builtin_unreachable();
}
