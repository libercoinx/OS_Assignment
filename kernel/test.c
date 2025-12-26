#include "defs.h"
#include "kalloc.h"
#include "panic.h"
#include "riscv.h"
#include "string.h"
#include "vm.h"

#define TEST_ASSERT(cond, msg)                                      \
  do {                                                              \
    if(!(cond)) {                                                   \
      printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, (msg));       \
      panic("test failure");                                        \
    }                                                               \
  } while(0)

static void print_test_banner(const char *name) {
  printf("============ %s ============\n", name);
}

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
  print_test_banner("physical memory");
  printf("[TEST] physical memory allocator...\n");

  void *page1 = kalloc();
  TEST_ASSERT(page1 != 0, "kalloc returned null (page1)");
  TEST_ASSERT(((uint64)page1 & (PGSIZE - 1)) == 0, "page1 not page-aligned");
  printf("[INFO] page1=%p\n", page1);

  void *page2 = kalloc();
  TEST_ASSERT(page2 != 0, "kalloc returned null (page2)");
  TEST_ASSERT(page1 != page2, "allocator reused live page");
  printf("[INFO] page2=%p\n", page2);

  memset(page1, 0xAB, PGSIZE);
  TEST_ASSERT(*(uint32*)page1 == 0xABABABAB, "memset pattern mismatch");
  printf("[INFO] write/read test value=0x%x\n", *(uint32*)page1);

  kfree(page1);
  void *page3 = kalloc();
  TEST_ASSERT(page3 != 0, "kalloc returned null (page3)");
  TEST_ASSERT(page3 == page1, "allocator failed to recycle freed page");
  printf("[INFO] page3=%p (recycled from page1)\n", page3);

  kfree(page2);
  kfree(page3);

  printf("[PASS] physical memory allocator\n");
}

void test_pagetable(void) {
  print_test_banner("pagetable");
  printf("[TEST] user pagetable mappings...\n");

  pagetable_t pt = uvmcreate();
  TEST_ASSERT(pt != 0, "uvmcreate failed");

  uint64 newsize = uvmalloc(pt, 0, PGSIZE, PTE_W);
  TEST_ASSERT(newsize == PGSIZE, "uvmalloc returned unexpected size");
  printf("[INFO] allocated user page at va=0x%x size=%d\n", 0, (int)newsize);

  uint64 va = 0;
  pte_t *pte = walk(pt, va, 0);
  TEST_ASSERT(pte != 0, "walk returned null");
  TEST_ASSERT(*pte & PTE_V, "pte not marked valid");
  TEST_ASSERT(*pte & PTE_R, "pte missing read permission");
  TEST_ASSERT(*pte & PTE_W, "pte missing write permission");
  TEST_ASSERT(*pte & PTE_U, "pte missing user permission");
  printf("[INFO] PTE flags=0x%x\n", (int)(*pte & 0x3ff));

  uint64 pa = PTE2PA(*pte);
  volatile uint64 *pa_ptr = (volatile uint64 *)pa;
  *pa_ptr = 0xdeadbeefcafebabeULL;
  TEST_ASSERT(*pa_ptr == 0xdeadbeefcafebabeULL, "physical store/load mismatch");
  printf("[INFO] va=0x%x -> pa=0x%x OK\n", (int)va, (int)pa);

  uvmfree(pt, newsize);

  printf("[PASS] user pagetable mappings\n");
}

void test_virtual_memory(void) {
  print_test_banner("virtual memory");
  printf("[TEST] virtual memory activation...\n");
  printf("[INFO] kernel_pagetable=%p\n", kernel_pagetable);

  TEST_ASSERT(kernel_pagetable != 0, "kernel pagetable not initialised");

  pte_t *text = walk(kernel_pagetable, KERNBASE, 0);
  TEST_ASSERT(text != 0 && (*text & PTE_V), "kernel text not mapped");
  TEST_ASSERT((*text & PTE_X), "kernel text not executable");
  TEST_ASSERT(PTE2PA(*text) == KERNBASE, "kernel text not identity mapped");
  printf("[INFO] text mapping: va=0x%x -> pa=0x%x flags=0x%x\n",
         (int)KERNBASE, (int)PTE2PA(*text), (int)(*text & 0x3ff));

  pte_t *tramp = walk(kernel_pagetable, TRAMPOLINE, 0);
  TEST_ASSERT(tramp != 0 && (*tramp & PTE_V), "trampoline not mapped");
  TEST_ASSERT((*tramp & PTE_X), "trampoline not executable");
  printf("[INFO] trampoline mapping: va=0x%x -> pa=0x%x flags=0x%x\n",
         (int)TRAMPOLINE, (int)PTE2PA(*tramp), (int)(*tramp & 0x3ff));

  volatile uint64 probe = 0x5a5acafeULL;
  volatile uint64 *data_addr = (volatile uint64 *)&probe;
  TEST_ASSERT(*data_addr == 0x5a5acafeULL, "kernel data access failed");
  printf("[INFO] kernel data access OK at %p\n", data_addr);

  printf("[PASS] virtual memory activation\n");
}
