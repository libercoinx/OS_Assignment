#include "trap.h"
#include "defs.h"
#include "panic.h"
#include "string.h"
#include "proc.h"

#define INST_16_MASK 0x3

#define TICK_INTERVAL 100000ULL

static interrupt_handler_t irq_table[IRQ_MAX];
static volatile uint64 ticks;

static void timer_interrupt_handler(void);
static void set_next_timer_tick(void);
static void handle_instruction_page_fault(struct trapframe *tf);
static void handle_load_page_fault(struct trapframe *tf);
static void handle_store_page_fault(struct trapframe *tf);
extern void syscall(struct trapframe *tf, struct pushregs *regs);

static inline void advance_sepc(struct trapframe *tf) {
  tf->epc += 4;
}

extern void kernelvec(void);

void trap_init(void) {
  memset((void *)irq_table, 0, sizeof(irq_table));
  ticks = 0;
  register_interrupt(IRQ_S_TIMER, timer_interrupt_handler);
  w_stvec((uint64)kernelvec);
}

void timer_init(void) {
  enable_interrupt(IRQ_S_TIMER);
  set_next_timer_tick();
  w_sip(r_sip() & ~SIP_STIP);
}

void register_interrupt(int irq, interrupt_handler_t handler) {
  if(irq < 0 || irq >= IRQ_MAX || handler == 0)
    panic("register_interrupt");
  irq_table[irq] = handler;
}

void enable_interrupt(int irq) {
  switch(irq) {
    case IRQ_S_SOFT:
      w_sie(r_sie() | SIE_SSIE);
      break;
    case IRQ_S_TIMER:
      w_sie(r_sie() | SIE_STIE);
      break;
    case IRQ_S_EXTERNAL:
      w_sie(r_sie() | SIE_SEIE);
      break;
    default:
      break;
  }
}

void disable_interrupt(int irq) {
  switch(irq) {
    case IRQ_S_SOFT:
      w_sie(r_sie() & ~SIE_SSIE);
      break;
    case IRQ_S_TIMER:
      w_sie(r_sie() & ~SIE_STIE);
      break;
    case IRQ_S_EXTERNAL:
      w_sie(r_sie() & ~SIE_SEIE);
      break;
    default:
      break;
  }
}

void intr_on(void) {
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

void intr_off(void) {
  w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

uint64 get_time(void) {
  return r_time();
}

uint64 get_ticks(void) {
  return ticks;
}

void *ticks_addr(void) {
  return (void *)&ticks;
}

static void dispatch_interrupt(int irq) {
  if(irq >= 0 && irq < IRQ_MAX && irq_table[irq]) {
    irq_table[irq]();
  } else {
    printf("unexpected interrupt %d\n", irq);
  }
}

void kerneltrap(struct pushregs *regs) {
  uint64 scause = r_scause();
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();

  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");

  if(scause & (1ULL << 63)) {
    int irq = (int)(scause & 0xff);
    dispatch_interrupt(irq);
    if(irq == IRQ_S_TIMER) {
      struct proc *p = myproc();
      if(p && p->state == RUNNING)
        yield();
    }
  } else {
    struct trapframe tf = {
      .epc = sepc,
      .status = sstatus,
      .cause = scause,
      .tval = r_stval()
    };
    handle_exception(&tf, regs);
    sepc = tf.epc;
    sstatus = tf.status;
  }

  sstatus |= SSTATUS_SIE;
  w_sepc(sepc);
  w_sstatus(sstatus);
}

static void set_next_timer_tick(void) {
  uint64 next = get_time() + TICK_INTERVAL;
  w_stimecmp(next);
}

static void timer_interrupt_handler(void) {
  ticks++;
  wakeup((void *)&ticks);
  set_next_timer_tick();
  w_sip(r_sip() & ~SIP_STIP);
}

void handle_exception(struct trapframe *tf, struct pushregs *regs) {
  switch (tf->cause) {
    case 8: /* Environment call from U-mode */
    case 9: /* Environment call from S-mode */
      handle_syscall(tf, regs);
      break;
    case 2: /* Illegal instruction */
      printf("Illegal instruction at 0x%x\n", (int)(tf->epc));
      advance_sepc(tf);
      break;
    case 12: /* Instruction page fault */
      handle_instruction_page_fault(tf);
      break;
    case 13: /* Load page fault */
    case 5:  /* Load access fault */
      handle_load_page_fault(tf);
      break;
    case 15: /* Store/AMO page fault */
    case 7:  /* Store access fault */
      handle_store_page_fault(tf);
      break;
    default:
      printf("Unhandled exception: scause=%d stval=0x%x sepc=0x%x\n",
             (int)tf->cause, (int)(tf->tval), (int)(tf->epc));
      panic("handle_exception");
  }
}

void handle_syscall(struct trapframe *tf, struct pushregs *regs) {
  syscall(tf, regs);
  advance_sepc(tf);
}

static void handle_instruction_page_fault(struct trapframe *tf) {
  printf("Instruction page fault at 0x%x\n", (int)(tf->tval));
  advance_sepc(tf);
}

static void handle_load_page_fault(struct trapframe *tf) {
  printf("Load fault at 0x%x\n", (int)(tf->tval));
  advance_sepc(tf);
}

static void handle_store_page_fault(struct trapframe *tf) {
  printf("Store fault at 0x%x\n", (int)(tf->tval));
  advance_sepc(tf);
}
