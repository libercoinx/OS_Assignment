#pragma once

#include "riscv.h"

typedef void (*interrupt_handler_t)(void);

enum {
  IRQ_S_SOFT = 1,
  IRQ_S_TIMER = 5,
  IRQ_S_EXTERNAL = 9,
  IRQ_MAX = 32
};

struct pushregs {
  uint64 ra;
  uint64 gp;
  uint64 tp;
  uint64 t0;
  uint64 t1;
  uint64 t2;
  uint64 s0;
  uint64 s1;
  uint64 a0;
  uint64 a1;
  uint64 a2;
  uint64 a3;
  uint64 a4;
  uint64 a5;
  uint64 a6;
  uint64 a7;
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
  uint64 t3;
  uint64 t4;
  uint64 t5;
  uint64 t6;
};

struct trapframe {
  uint64 kernel_satp;
  uint64 kernel_sp;
  uint64 kernel_trap;
  uint64 kernel_hartid;
  uint64 epc;
  uint64 ra;
  uint64 sp;
  uint64 gp;
  uint64 tp;
  uint64 t0;
  uint64 t1;
  uint64 t2;
  uint64 s0;
  uint64 s1;
  uint64 a0;
  uint64 a1;
  uint64 a2;
  uint64 a3;
  uint64 a4;
  uint64 a5;
  uint64 a6;
  uint64 a7;
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
  uint64 t3;
  uint64 t4;
  uint64 t5;
  uint64 t6;
  uint64 status;
  uint64 cause;
  uint64 tval;
};

void trap_init(void);
void timer_init(void);
void register_interrupt(int irq, interrupt_handler_t handler);
void enable_interrupt(int irq);
void disable_interrupt(int irq);
void intr_on(void);
void intr_off(void);
uint64 get_time(void);
uint64 get_ticks(void);
void *ticks_addr(void);
void handle_syscall(struct trapframe *tf, struct pushregs *regs);
void handle_exception(struct trapframe *tf, struct pushregs *regs);
