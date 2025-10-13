#pragma once

#include <stdint.h>

/* RISC-V 寄存器访问 */
#define r_mhartid() ({ uint64_t x; asm volatile("csrr %0, mhartid" : "=r"(x)); x; })
#define r_mstatus() ({ uint64_t x; asm volatile("csrr %0, mstatus" : "=r"(x)); x; })
#define r_mie() ({ uint64_t x; asm volatile("csrr %0, mie" : "=r"(x)); x; })
#define r_satp() ({ uint64_t x; asm volatile("csrr %0, satp" : "=r"(x)); x; })
#define r_sie() ({ uint64_t x; asm volatile("csrr %0, sie" : "=r"(x)); x; })

#define w_mstatus(x) asm volatile("csrw mstatus, %0" :: "r"(x))
#define w_mie(x) asm volatile("csrw mie, %0" :: "r"(x))
#define w_satp(x) asm volatile("csrw satp, %0" :: "r"(x))
#define w_sie(x) asm volatile("csrw sie, %0" :: "r"(x))
#define w_stvec(x) asm volatile("csrw stvec, %0" :: "r"(x))
#define w_medeleg(x) asm volatile("csrw medeleg, %0" :: "r"(x))
#define w_mideleg(x) asm volatile("csrw mideleg, %0" :: "r"(x))

/* TLB 刷新 */
#define sfence_vma() asm volatile("sfence.vma zero, zero")

/* Sv39 页表相关 */
#define PGSIZE 4096
#define PGSHIFT 12

/* 虚拟地址分解（39位） */
#define VPN_SHIFT(level) (12 + 9 * (level))
#define VPN_MASK(va, level) (((va) >> VPN_SHIFT(level)) & 0x1FF)

/* 页表项（PTE）格式 */
#define PTE_V (1L << 0)  /* Valid */
#define PTE_R (1L << 1)  /* Readable */
#define PTE_W (1L << 2)  /* Writable */
#define PTE_X (1L << 3)  /* Executable */
#define PTE_U (1L << 4)  /* User accessible */
#define PTE_G (1L << 5)  /* Global */
#define PTE_A (1L << 6)  /* Accessed */
#define PTE_D (1L << 7)  /* Dirty */

/* 页表项操作 */
#define PTE_FLAGS(pte) ((pte) & 0x3FF)
#define PTE_PA(pte) ((((pte) >> 10) << 12))
#define PA2PTE(pa) ((((uint64)(pa)) >> 12) << 10)
#define PTE2PA(pte) (((pte) >> 10) << 12)

/* SATP 寄存器格式 */
#define SATP_MODE_MASK (0xFULL << 60)
#define SATP_MODE_SV39 (8ULL << 60)
#define SATP_ASID_MASK (0xFFFFULL << 44)
#define SATP_PPN_MASK (0xFFFFFFFFFULL)

#define MAKE_SATP(pt) (SATP_MODE_SV39 | (((uint64)(pt)) >> 12))

/* 地址对齐 */
#define PGROUNDUP(sz) (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(a) ((a) & ~(PGSIZE - 1))

/* 内存布局（参考 xv6） */
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024)  /* 假设 128MB 物理内存 */

/* 设备地址 */
#define UART0 0x10000000L
#define CLINT 0x2000000L

/* 页表类型 */
typedef uint64_t pte_t;
typedef pte_t* pagetable_t;

/* 类型定义 */
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;

/* 单核版本：简化锁机制 */
struct spinlock {
  int locked;
};

#define initlock(lock, name) ((lock)->locked = 0)
#define acquire(lock) ((lock)->locked = 1)
#define release(lock) ((lock)->locked = 0)

/* 内存布局符号 */
extern char end[];  /* 内核结束地址，由链接脚本提供 */
extern char etext[]; /* 内核代码结束地址 */
extern char trampoline[]; /* 跳板代码地址 */

/* 设备地址补充 */
#define VIRTIO0 0x10008000L
#define PLIC 0x0c000000L
#define TRAMPOLINE (MAXVA - PGSIZE)

/* 最大虚拟地址 */
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))
