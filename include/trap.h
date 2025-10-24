#pragma once

#include "riscv.h"

typedef void (*interrupt_handler_t)(void);

enum {
  IRQ_S_SOFT = 1,
  IRQ_S_TIMER = 5,
  IRQ_S_EXTERNAL = 9,
  IRQ_MAX = 32
};

struct trapframe {
  uint64 sepc;
  uint64 sstatus;
  uint64 scause;
  uint64 stval;
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
void handle_exception(struct trapframe *tf);

