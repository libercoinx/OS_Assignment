#include "defs.h"
#include "kalloc.h"
#include "panic.h"
#include "riscv.h"
#include "string.h"
#include "vm.h"
#include "trap.h"

#define TEST_ASSERT(cond, msg)                                      \
  do {                                                              \
    if(!(cond)) {                                                   \
      printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, (msg));       \
      panic("test failure");                                        \
    }                                                               \
  } while(0)

void test_printf_basic(void) {
  printf("Testing integer: %d\n", 42);
  printf("Testing negative: %d\n", -123);
  printf("Testing zero: %d\n", 0);
  printf("Testing hex: 0x%x\n", 0xABC);
  printf("Testing string: %s\n", "Hello");
  printf("Testing char: %c\n", 'X');
  printf("Testing percent: %%\n");
}

void test_printf_edge_cases(void) {
  printf("INT_MAX: %d\n", 2147483647);
  printf("INT_MIN: %d\n", -2147483648);
  printf("NULL string: %s\n", (char*)0);
  printf("Empty string: %s\n", "");
}

void test_physical_memory(void) {
  printf("[TEST] physical memory allocator\n");

  void *page1 = kalloc();
  TEST_ASSERT(page1 != 0, "kalloc returned null (page1)");
  TEST_ASSERT(((uint64)page1 & (PGSIZE - 1)) == 0, "page1 not page-aligned");

  void *page2 = kalloc();
  TEST_ASSERT(page2 != 0, "kalloc returned null (page2)");
  TEST_ASSERT(page1 != page2, "allocator reused live page");

  memset(page1, 0xAB, PGSIZE);
  TEST_ASSERT(*(uint32*)page1 == 0xABABABAB, "memset pattern mismatch");

  kfree(page1);
  void *page3 = kalloc();
  TEST_ASSERT(page3 != 0, "kalloc returned null (page3)");
  TEST_ASSERT(page3 == page1, "allocator failed to recycle freed page");

  kfree(page2);
  kfree(page3);

  printf("[PASS] physical memory allocator\n");
}

void test_pagetable(void) {
  printf("[TEST] user pagetable mappings\n");

  pagetable_t pt = uvmcreate();
  TEST_ASSERT(pt != 0, "uvmcreate failed");

  uint64 newsize = uvmalloc(pt, 0, PGSIZE, PTE_W);
  TEST_ASSERT(newsize == PGSIZE, "uvmalloc returned unexpected size");

  uint64 va = 0;
  pte_t *pte = walk(pt, va, 0);
  TEST_ASSERT(pte != 0, "walk returned null");
  TEST_ASSERT(*pte & PTE_V, "pte not marked valid");
  TEST_ASSERT(*pte & PTE_R, "pte missing read permission");
  TEST_ASSERT(*pte & PTE_W, "pte missing write permission");
  TEST_ASSERT(*pte & PTE_U, "pte missing user permission");

  uint64 pa = PTE2PA(*pte);
  volatile uint64 *pa_ptr = (volatile uint64 *)pa;
  *pa_ptr = 0xdeadbeefcafebabeULL;
  TEST_ASSERT(*pa_ptr == 0xdeadbeefcafebabeULL, "physical store/load mismatch");

  uvmfree(pt, newsize);

  printf("[PASS] user pagetable mappings\n");
}

void test_virtual_memory(void) {
  printf("[TEST] kernel pagetable mappings\n");

  TEST_ASSERT(kernel_pagetable != 0, "kernel pagetable not initialised");

  pte_t *text = walk(kernel_pagetable, KERNBASE, 0);
  TEST_ASSERT(text != 0 && (*text & PTE_V), "kernel text not mapped");
  TEST_ASSERT((*text & PTE_X), "kernel text not executable");
  TEST_ASSERT(PTE2PA(*text) == KERNBASE, "kernel text not identity mapped");

  pte_t *tramp = walk(kernel_pagetable, TRAMPOLINE, 0);
  TEST_ASSERT(tramp != 0 && (*tramp & PTE_V), "trampoline not mapped");
  TEST_ASSERT((*tramp & PTE_X), "trampoline not executable");

  printf("[PASS] kernel pagetable mappings\n");
}

void test_timer_interrupt(void) {
  printf("[TEST] timer interrupt\n");
  uint64 start_ticks = get_ticks();
  uint64 target = start_ticks + 5;
  uint64 start_time = get_time();

  while(get_ticks() < target) {
    __asm__ volatile("wfi");
  }

  uint64 end_time = get_time();
  printf("[PASS] timer interrupts %d -> %d (delta %d cycles)\n",
         (int)start_ticks, (int)get_ticks(), (int)(end_time - start_time));
}

void test_interrupt_overhead(void) {
  printf("[TEST] interrupt overhead measurement\n");

  volatile int dummy = 0;
  uint64 t0 = get_time();
  for(int i = 0; i < 100000; i++) {
    dummy += i;
  }
  uint64 t1 = get_time();

  intr_off();
  uint64 t2 = get_time();
  for(int i = 0; i < 100000; i++) {
    dummy += i;
  }
  uint64 t3 = get_time();
  intr_on();

  printf("[INFO] with interrupts: %d cycles, without: %d cycles (dummy=%d)\n",
         (int)(t1 - t0), (int)(t3 - t2), dummy);
  printf("[PASS] interrupt overhead measurement\n");
}

__attribute__((noinline))
static void trigger_store_fault(void) {
  volatile uint64 *bad = (volatile uint64 *)(KERNBASE - PGSIZE);
  *bad = 0x1234;
}

void test_exception_handling(void) {
  printf("[TEST] exception handling\n");
  uint64 before = get_ticks();
  trigger_store_fault();
  printf("[PASS] exception handled, ticks %d -> %d\n",
         (int)before, (int)get_ticks());
}
