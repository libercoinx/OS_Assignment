#include "riscv.h"
#include "kalloc.h"
#include "panic.h"
#include "string.h"

/* 简化版本：单核不需要真正的锁 */

struct run {
  struct run *next;
};

struct {
  struct run *freelist;
} kmem;

void kinit() {
  freerange((void*)end, (void*)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void kfree(void *pa) {
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  r->next = kmem.freelist;
  kmem.freelist = r;
}

void *kalloc(void) {
  struct run *r;

  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    memset((char*)r, 5, PGSIZE);
  }
  
  return (void*)r;
}
