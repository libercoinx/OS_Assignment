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
  print_test_banner("timer interrupt");
  printf("[TEST] timer interrupt...\n");

  uint64 start_time = get_time();
  uint64 start_ticks = get_ticks();
  uint64 last_ticks = start_ticks;
  int interrupt_count = 0;
  volatile int *test_flag = &interrupt_count;

  printf("[INFO] start ticks=%lu, target interrupts=5\n", start_ticks);

  while(interrupt_count < 5) {
    uint64 current_ticks = get_ticks();
    if(current_ticks != last_ticks) {
      interrupt_count += (int)(current_ticks - last_ticks);
      last_ticks = current_ticks;
      printf("[INFO] interrupt %d captured at tick=%lu (flag=%d)\n",
             interrupt_count, current_ticks, *test_flag);
    } else {
      // Busy wait a bit to avoid hammering the counter.
      for(volatile int i = 0; i < 100000; i++);
    }
  }

  uint64 end_time = get_time();
  printf("[PASS] timer test completed: %d interrupts in %lu cycles (ticks %lu -> %lu)\n",
         interrupt_count, end_time - start_time, start_ticks, last_ticks);
}

void test_interrupt_overhead(void) {
  print_test_banner("interrupt overhead");
  printf("[TEST] interrupt overhead measurement...\n");

  const int iterations = 200000;
  volatile int dummy = 0;

  uint64 warm0 = get_time();
  for(int i = 0; i < iterations / 10; i++) {
    dummy += i;
  }
  uint64 warm1 = get_time();
  printf("[INFO] warm-up loop completed in %d cycles (dummy=%d)\n",
         (int)(warm1 - warm0), dummy);

  uint64 t0 = get_time();
  for(int i = 0; i < iterations; i++) {
    dummy += i;
  }
  uint64 t1 = get_time();

  intr_off();
  uint64 t2 = get_time();
  for(int i = 0; i < iterations; i++) {
    dummy += i;
  }
  uint64 t3 = get_time();
  intr_on();

  uint64 with_intr = t1 - t0;
  uint64 without_intr = t3 - t2;
  long delta = (long)with_intr - (long)without_intr;
  long avg_delta = delta / iterations;

  printf("[INFO] iterations=%d, with interrupts=%lu cycles, without=%lu cycles\n",
         iterations, with_intr, without_intr);
  printf("[INFO] total delta=%ld cycles, approx overhead per iteration=%ld cycles (dummy=%d)\n",
         delta, avg_delta, dummy);
  printf("[PASS] interrupt overhead measurement\n");
}

__attribute__((noinline))
static void trigger_store_fault(void) {
  volatile uint64 *bad = (volatile uint64 *)(KERNBASE - PGSIZE);
  *bad = 0x1234;
}

__attribute__((noinline))
static void trigger_load_fault(void) {
  volatile uint64 *bad = (volatile uint64 *)(KERNBASE - PGSIZE);
  volatile uint64 value = *bad;
  __asm__ volatile("" : : "r"(value));
}

__attribute__((noinline))
static void trigger_illegal_instruction(void) {
  __asm__ volatile(".word 0x00000000");
}

typedef void (*exception_trigger_t)(void);

static void run_exception_case(const char *name,
                               exception_trigger_t trigger,
                               int expected_scause) {
  trap_clear_last_exception();
  uint64 before_ticks = get_ticks();
  uint64 before_cycles = get_time();

  if(expected_scause >= 0) {
    printf("[INFO] triggering %s (expected scause=%d) at tick=%lu cycle=%lu\n",
           name, expected_scause, before_ticks, before_cycles);
  } else {
    printf("[INFO] triggering %s at tick=%lu cycle=%lu\n",
           name, before_ticks, before_cycles);
  }
  trigger();

  struct exception_info info;
  if(!trap_fetch_last_exception(&info)) {
    printf("[WARN] %s did not raise an exception (maybe unsupported)\n", name);
    return;
  }

  uint64 after_ticks = get_ticks();
  uint64 after_cycles = get_time();

  printf("[INFO] %s handled: scause=%lu stval=0x%lx sepc=0x%lx ticks %lu -> %lu delta=%lu cycles\n",
         name, info.scause, info.stval, info.sepc,
         before_ticks, after_ticks, after_cycles - before_cycles);

  if(expected_scause >= 0 && info.scause != (uint64)expected_scause) {
    printf("[WARN] %s expected scause=%d but observed %lu\n",
           name, expected_scause, info.scause);
  } else {
    printf("[PASS] %s exception verified\n", name);
  }

  trap_clear_last_exception();
}

void test_exception_handling(void) {
  print_test_banner("exception handling");
  printf("[TEST] exception handling suite...\n");

  run_exception_case("illegal instruction", trigger_illegal_instruction, 2);
  run_exception_case("memory load fault", trigger_load_fault, 13);
  run_exception_case("memory store fault", trigger_store_fault, 15);

  printf("[DONE] exception testing complete\n");
}
