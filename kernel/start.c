#include <stdint.h>
#include "defs.h"

extern void main(void);

static inline uint64_t r_mhartid(void){ uint64_t x; asm volatile("csrr %0, mhartid" : "=r"(x)); return x; }
static inline uint64_t r_mie(void){ uint64_t x; asm volatile("csrr %0, mie" : "=r"(x)); return x; }
static inline void    w_mie(uint64_t x){ asm volatile("csrw mie, %0" :: "r"(x)); }
static inline uint64_t r_sie(void){ uint64_t x; asm volatile("csrr %0, sie" : "=r"(x)); return x; }
static inline void    w_sie(uint64_t x){ asm volatile("csrw sie, %0" :: "r"(x)); }
static inline void    w_medeleg(uint64_t x){ asm volatile("csrw medeleg, %0" :: "r"(x)); }
static inline void    w_mideleg(uint64_t x){ asm volatile("csrw mideleg, %0" :: "r"(x)); }
static inline void    w_satp(uint64_t x){ asm volatile("csrw satp, %0" :: "r"(x)); }
static inline void    w_stvec(uint64_t x){ asm volatile("csrw stvec, %0" :: "r"(x)); }

void start(void){
  console_init();
  main();
}


