#include "defs.h"
#include "kalloc.h"
#include "panic.h"
#include "riscv.h"
#include "string.h"
#include "vm.h"
#include "trap.h"
#include "proc.h"

#define TEST_ASSERT(cond, msg)                                      \
  do {                                                              \
    if(!(cond)) {                                                   \
      printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, (msg));       \
      panic("test failure");                                        \
    }                                                               \
  } while(0)

static volatile int shared_counter;
static volatile int yield_counts[3];
static struct spinlock sleep_test_lock;
static volatile int sleep_ready;
static volatile int sleep_value;

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

static void counter_task(void *arg) {
  int delta = (int)(uint64)arg;
  shared_counter += delta;
}

void test_process_creation_basic(void) {
  printf("[TEST] process creation\n");
  shared_counter = 0;
  int pid = create_process("counter-child", counter_task, (void *)1);
  TEST_ASSERT(pid > 0, "create_process failed");

  int status = -1;
  int waited = wait_process(&status);
  TEST_ASSERT(waited == pid, "wait_process returned unexpected pid");
  TEST_ASSERT(status == 0, "child exit status non-zero");
  TEST_ASSERT(shared_counter == 1, "shared counter mismatch");
  printf("[PASS] process creation\n");
}

#define YIELD_TASKS 3
#define YIELD_ITERS 5

static void yield_task(void *arg) {
  int id = (int)(uint64)arg;
  for(int i = 0; i < YIELD_ITERS; i++) {
    yield_counts[id]++;
    sys_yield();
  }
}

void test_scheduler_round_robin(void) {
  printf("[TEST] scheduler round robin\n");
  memset((void *)yield_counts, 0, sizeof(yield_counts));

  int pids[YIELD_TASKS];
  for(int i = 0; i < YIELD_TASKS; i++) {
    pids[i] = create_process("yield-task", yield_task, (void *)(uint64)i);
    TEST_ASSERT(pids[i] > 0, "failed to create yield task");
  }

  int finished[YIELD_TASKS] = {0};
  int remaining = YIELD_TASKS;
  int status;
  while(remaining > 0) {
    int pid = wait_process(&status);
    TEST_ASSERT(pid > 0, "wait_process failed");
    TEST_ASSERT(status == 0, "yield task exit status non-zero");
    int idx = -1;
    for(int j = 0; j < YIELD_TASKS; j++) {
      if(pids[j] == pid) {
        idx = j;
        break;
      }
    }
    TEST_ASSERT(idx != -1, "unexpected child pid");
    TEST_ASSERT(finished[idx] == 0, "duplicate wait on child");
    finished[idx] = 1;
    remaining--;
  }

  for(int i = 0; i < YIELD_TASKS; i++) {
    TEST_ASSERT(yield_counts[i] == YIELD_ITERS, "yield iterations mismatch");
  }
  printf("[PASS] scheduler round robin\n");
}

static void sleeper_task(void *arg) {
  (void)arg;
  acquire(&sleep_test_lock);
  while(!sleep_ready)
    sleep((void *)&sleep_ready, &sleep_test_lock);
  TEST_ASSERT(sleep_value == 0x1234, "sleep value corrupted");
  release(&sleep_test_lock);
}

static void waker_task(void *arg) {
  (void)arg;
  acquire(&sleep_test_lock);
  sleep_value = 0x1234;
  sleep_ready = 1;
  wakeup((void *)&sleep_ready);
  release(&sleep_test_lock);
}

void test_sleep_wakeup_mechanism(void) {
  printf("[TEST] sleep/wakeup\n");
  initlock(&sleep_test_lock, "sleep-test");
  sleep_ready = 0;
  sleep_value = 0;

  int sleeper = create_process("sleeper", sleeper_task, 0);
  TEST_ASSERT(sleeper > 0, "sleeper creation failed");

  sys_yield();

  int waker = create_process("waker", waker_task, 0);
  TEST_ASSERT(waker > 0, "waker creation failed");

  int status;
  int completed = 0;
  while(completed < 2) {
    int pid = wait_process(&status);
    TEST_ASSERT(pid == sleeper || pid == waker, "unexpected child pid");
    TEST_ASSERT(status == 0, "child exit status non-zero");
    completed++;
  }

  TEST_ASSERT(sleep_ready == 1, "sleep flag not set");
  TEST_ASSERT(sleep_value == 0x1234, "sleep value not written");
  printf("[PASS] sleep/wakeup\n");
}

void run_proc_tests(void *arg) {
  (void)arg;
  printf("[SUITE] running kernel tests\n");
  test_process_creation_basic();
  test_scheduler_round_robin();
  test_sleep_wakeup_mechanism();
  printf("[SUITE] all tests finished\n");
}
