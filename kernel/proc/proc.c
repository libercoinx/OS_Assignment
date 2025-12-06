#include "proc.h"
#include "defs.h"
#include "kalloc.h"
#include "panic.h"
#include "string.h"
#include "vm.h"
#include "trap.h"
#include "fs.h"

#define KSTACK_SIZE PGSIZE

struct proc proc[NPROC];
static struct spinlock pid_lock;
static struct spinlock wait_lock;
struct cpu cpus[NCPU];

static int nextpid = 1;

static void proc_entry(void) __attribute__((noreturn));
static void freeproc(struct proc *p);
static int allocpid(void);
static int intr_get(void);

void
procinit(void) {
  initlock(&pid_lock, "pid");
  initlock(&wait_lock, "wait");
  for(int i = 0; i < NPROC; i++) {
    struct proc *p = &proc[i];
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = 0;
  }
}

static int
allocpid(void) {
  int pid;

  push_off();
  pid = nextpid++;
  pop_off();
  return pid;
}

struct cpu*
mycpu(void) {
  return &cpus[0];
}

static int
intr_get(void) {
  return (r_sstatus() & SSTATUS_SIE) != 0;
}

void
push_off(void) {
  int old = intr_get();
  intr_off();
  struct cpu *c = mycpu();
  if(c->noff++ == 0)
    c->intena = old;
}

void
pop_off(void) {
  struct cpu *c = mycpu();
  if(c->noff <= 0)
    panic("pop_off");
  c->noff--;
  if(c->noff == 0 && c->intena)
    intr_on();
}

struct proc*
myproc(void) {
  push_off();
  struct proc *p = mycpu()->proc;
  pop_off();
  return p;
}

static void
freeproc(struct proc *p) {
  if(p->trapframe) {
    kfree(p->trapframe);
    p->trapframe = 0;
  }
  if(p->pagetable) {
    uvmfree(p->pagetable, p->sz);
    p->pagetable = 0;
    p->sz = 0;
  }
  if(p->kstack) {
    kfree((void*)p->kstack);
    p->kstack = 0;
  }
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->pid = 0;
  p->parent = 0;
  for(int i = 0; i < NOFILE; i++)
    p->ofile[i] = 0;
  p->cwd = 0;
  p->name[0] = '\0';
  p->state = UNUSED;
  p->kthread.start = 0;
  p->kthread.arg = 0;
  memset(&p->context, 0, sizeof(p->context));
}

struct proc*
alloc_process(void) {
  for(int i = 0; i < NPROC; i++) {
    struct proc *p = &proc[i];
    acquire(&p->lock);
    if(p->state == UNUSED) {
      p->state = USED;
      p->pid = allocpid();
      p->killed = 0;
      p->xstate = 0;
      p->chan = 0;
      p->parent = 0;
      p->sz = 0;
      p->pagetable = 0;

      if(p->kstack == 0) {
        p->kstack = (uint64)kalloc();
        if(p->kstack == 0) {
          freeproc(p);
          release(&p->lock);
          return 0;
        }
      }

      p->trapframe = (struct trapframe*)kalloc();
      if(p->trapframe == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
      }
      memset(p->trapframe, 0, PGSIZE);

      memset(&p->context, 0, sizeof(p->context));
      p->context.sp = p->kstack + KSTACK_SIZE;
      p->context.ra = (uint64)proc_entry;
      for(int i = 0; i < NOFILE; i++)
        p->ofile[i] = 0;
      char root[] = "/";
      p->cwd = namei(root);
      return p;
    }
    release(&p->lock);
  }
  return 0;
}

static void
proc_entry(void) {
  struct proc *p = myproc();
  if(p == 0)
    panic("proc_entry no proc");

  void (*start)(void *) = p->kthread.start;
  void *arg = p->kthread.arg;

  release(&p->lock);

  if(start)
    start(arg);

  exit_process(0);
}

int
create_process(const char *name, void (*fn)(void *), void *arg) {
  struct proc *p = alloc_process();
  if(p == 0)
    return -1;

  if(name) {
    strlcpy(p->name, name, sizeof(p->name));
  } else {
    p->name[0] = '\0';
  }

  p->kthread.start = fn;
  p->kthread.arg = arg;
  p->parent = myproc();
  p->state = RUNNABLE;
  release(&p->lock);
  return p->pid;
}

void
exit_process(int status) {
  struct proc *p = myproc();
  if(p == 0)
    panic("exit_process");

  for(int fd = 0; fd < NOFILE; fd++) {
    if(p->ofile[fd]) {
      fileclose(p->ofile[fd]);
      p->ofile[fd] = 0;
    }
  }
  if(p->cwd) {
    iput(p->cwd);
    p->cwd = 0;
  }

  acquire(&wait_lock);
  acquire(&p->lock);
  p->xstate = status;
  p->state = ZOMBIE;

  struct proc *parent = p->parent;
  release(&wait_lock);

  if(parent)
    wakeup(parent);

  sched();
  panic("zombie exit");
}

int
wait_process(int *status) {
  struct proc *p = myproc();
  if(p == 0)
    panic("wait_process");

  int havekids;

  acquire(&wait_lock);
  for(;;) {
    havekids = 0;
    for(int i = 0; i < NPROC; i++) {
      struct proc *np = &proc[i];
      if(np == p)
        continue;
      acquire(&np->lock);
      if(np->parent == p) {
        havekids = 1;
        if(np->state == ZOMBIE) {
          int pid = np->pid;
          if(status)
            *status = np->xstate;
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
      }
      release(&np->lock);
    }

    if(!havekids) {
      release(&wait_lock);
      return -1;
    }

    sleep(p, &wait_lock);
  }
}

void
sched(void) {
  struct proc *p = myproc();
  struct cpu *c = mycpu();

  if(p == 0)
    panic("sched no proc");

  int intena = c->intena;
  swtch(&p->context, &c->context);
  c->intena = intena;
}

void
yield(void) {
  struct proc *p = myproc();
  if(p == 0)
    return;
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

void
sleep(void *chan, struct spinlock *lk) {
  struct proc *p = myproc();
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  if(lk != &p->lock) {
    acquire(&p->lock);
    release(lk);
  }

  p->chan = chan;
  p->state = SLEEPING;

  sched();

  p->chan = 0;

  if(lk != &p->lock) {
    release(&p->lock);
    acquire(lk);
  }
}

void
wakeup(void *chan) {
  for(int i = 0; i < NPROC; i++) {
    struct proc *p = &proc[i];
    if(p == myproc())
      continue;
    acquire(&p->lock);
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
    }
    release(&p->lock);
  }
}

int
kill(int pid) {
  for(int i = 0; i < NPROC; i++) {
    struct proc *p = &proc[i];
    acquire(&p->lock);
    if(p->pid == pid && (p->state == SLEEPING || p->state == RUNNABLE || p->state == RUNNING || p->state == USED)) {
      p->killed = 1;
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
scheduler(void) {
  struct cpu *c = mycpu();
  c->proc = 0;
  for(;;) {
    intr_on();
    for(int i = 0; i < NPROC; i++) {
      struct proc *p = &proc[i];
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}
