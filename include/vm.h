#pragma once

#include "riscv.h"

extern pagetable_t kernel_pagetable;

void kvminit(void);
void kvminithart(void);
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len);
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);
pagetable_t uvmcreate(void);
void uvmfree(pagetable_t pagetable, uint64 sz);
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free);
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm);
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz);
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz);
void uvmclear(pagetable_t pagetable, uint64 va);
uint64 walkaddr(pagetable_t pagetable, uint64 va);
