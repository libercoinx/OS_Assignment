#pragma once

#include "riscv.h"

struct file;
struct inode;

#define NPROC 32
#define NCPU  1
#define NOFILE 16

enum procstate {
  UNUSED = 0,
  USED,
  SLEEPING,
  RUNNABLE,
  RUNNING,
  ZOMBIE
};

struct context {
  uint64 ra;
  uint64 sp;
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

struct proc;

struct cpu {
  struct proc *proc;
  struct context context;
  int noff;
  int intena;
};

struct kthread_info {
  void (*start)(void *);
  void *arg;
};

struct proc {
  struct spinlock lock;
  enum procstate state;
  void *chan;
  int killed;
  int xstate;
  int pid;

  uint64 kstack;
  uint64 sz;
  pagetable_t pagetable;
  struct trapframe *trapframe;
  struct context context;
  struct proc *parent;
  char name[16];
  struct file *ofile[NOFILE];
  struct inode *cwd;

  struct kthread_info kthread;
};

extern struct proc proc[];

void procinit(void);
void scheduler(void) __attribute__((noreturn));
void sched(void);
void yield(void);
void sleep(void *chan, struct spinlock *lk);
void wakeup(void *chan);
int kill(int pid);
struct proc *myproc(void);
struct cpu *mycpu(void);

struct proc *alloc_process(void);
int create_process(const char *name, void (*fn)(void *), void *arg);
void exit_process(int status) __attribute__((noreturn));
int wait_process(int *status);

uint64 sys_getpid(void);
uint64 sys_yield(void);
uint64 sys_kill(void);
uint64 sys_wait(void);
uint64 sys_exit(void) __attribute__((noreturn));
uint64 sys_sleep(void);
uint64 sys_uptime(void);

void push_off(void);
void pop_off(void);

void swtch(struct context *old, struct context *new);
