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

static void print_test_banner(const char *name) {
  printf("============ %s ============\n", name);
}

#define SHARED_BUFFER_CAP 8
#define PRODUCE_ITEMS 16
#define CPU_TASKS 2
#define CPU_WORK_UNITS 4000000
#define INTERACTIVE_PHASES 3
#define INTERACTIVE_SPIN 0ULL
#define TOTAL_SCHED_TASKS (CPU_TASKS + 1)
#define PRIO_TEST_TASKS 3
#define PRIO_HIGH 0
#define PRIO_MED 2
#define PRIO_LOW 4

static volatile int scheduler_finish_order[TOTAL_SCHED_TASKS];
static volatile int scheduler_finish_count;
static volatile int prio_finish_order[PRIO_TEST_TASKS];
static volatile int prio_finish_count;

static volatile int simple_task_runs;
static volatile int consumer_total;

struct shared_buffer {
  int data[SHARED_BUFFER_CAP];
  int head;
  int tail;
  int count;
  struct spinlock lock;
};

static struct shared_buffer shared_buf;

static void note_task_finish(int id) {
  if(scheduler_finish_count < TOTAL_SCHED_TASKS) {
    scheduler_finish_order[scheduler_finish_count++] = id;
  }
}

static const char *scheduler_task_name(int id) {
  switch(id) {
    case 0: return "interactive";
    case 1: return "cpu-heavy-1";
    case 2: return "cpu-heavy-2";
    default: return "unknown";
  }
}

static void busy_wait_cycles(uint64 cycles) {
  uint64 start = get_time();
  while(get_time() - start < cycles) {
    __asm__ volatile("nop");
  }
}

struct prio_task_cfg {
  int id;
  int work_iters;
};

static void prio_task(void *arg) {
  struct prio_task_cfg *cfg = (struct prio_task_cfg *)arg;
  for(int i = 0; i < cfg->work_iters; i++) {
    for(volatile int spin = 0; spin < 1000; spin++)
      ;
    sys_yield();
  }
  int idx = __sync_fetch_and_add(&prio_finish_count, 1);
  if(idx < PRIO_TEST_TASKS)
    prio_finish_order[idx] = cfg->id;
}

static void simple_task(void *arg) {
  (void)arg;
  simple_task_runs++;
}

static void cpu_intensive_task(void *arg) {
  uint64 packed = (uint64)arg;
  int id = (int)(packed & 0xffffffffu);
  int weight = (int)(packed >> 32);
  volatile uint64 acc = 0;
  for(int i = 0; i < CPU_WORK_UNITS * weight; i++) {
    acc ^= (uint64)i * (uint64)(i + 31);
  }
  (void)acc;
  note_task_finish(id);
}

static void interactive_task(void *arg) {
  int id = (int)(uint64)arg;
  for(int i = 0; i < INTERACTIVE_PHASES; i++) {
    busy_wait_cycles(INTERACTIVE_SPIN);
    yield();
  }
  note_task_finish(id);
}

static void shared_buffer_init(void) {
  initlock(&shared_buf.lock, "shared-buf");
  shared_buf.head = 0;
  shared_buf.tail = 0;
  shared_buf.count = 0;
  consumer_total = 0;
}

static void shared_buffer_put(int value) {
  acquire(&shared_buf.lock);
  while(shared_buf.count == SHARED_BUFFER_CAP)
    sleep((void *)&shared_buf, &shared_buf.lock);
  shared_buf.data[shared_buf.tail] = value;
  shared_buf.tail = (shared_buf.tail + 1) % SHARED_BUFFER_CAP;
  shared_buf.count++;
  wakeup((void *)&shared_buf);
  release(&shared_buf.lock);
}

static int shared_buffer_get(void) {
  acquire(&shared_buf.lock);
  while(shared_buf.count == 0)
    sleep((void *)&shared_buf, &shared_buf.lock);
  int value = shared_buf.data[shared_buf.head];
  shared_buf.head = (shared_buf.head + 1) % SHARED_BUFFER_CAP;
  shared_buf.count--;
  wakeup((void *)&shared_buf);
  release(&shared_buf.lock);
  return value;
}

static void producer_task(void *arg) {
  (void)arg;
  for(int i = 0; i < PRODUCE_ITEMS; i++)
    shared_buffer_put(i);
}

static void consumer_task(void *arg) {
  (void)arg;
  int total = 0;
  for(int i = 0; i < PRODUCE_ITEMS; i++)
    total += shared_buffer_get();
  consumer_total = total;
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

void test_process_creation(void) {
  print_test_banner("process creation");
  printf("[TEST] process creation...\n");

  simple_task_runs = 0;
  int pid = create_process("simple-task", simple_task, 0);
  TEST_ASSERT(pid > 0, "create_process failed");

  int status = -1;
  int waited = wait_process(&status);
  TEST_ASSERT(waited == pid, "wait_process returned unexpected pid");
  TEST_ASSERT(status == 0, "child exit status non-zero");
  TEST_ASSERT(simple_task_runs == 1, "simple task did not run");

  int pids[NPROC];
  int count = 0;
  for(int i = 0; i < NPROC + 5; i++) {
    int npid = create_process("simple-task", simple_task, 0);
    if(npid > 0 && count < NPROC) {
      pids[count++] = npid;
    } else {
      break;
    }
  }
  printf("[INFO] created %d additional processes\n", count);

  for(int i = 0; i < count; i++) {
    int child_status = -1;
    int child = wait_process(&child_status);
    TEST_ASSERT(child > 0, "wait_process failed during cleanup");
    TEST_ASSERT(child_status == 0, "child exit status non-zero");
    int matched = 0;
    for(int j = 0; j < count; j++) {
      if(pids[j] == child) {
        pids[j] = -1;
        matched = 1;
        break;
      }
    }
    TEST_ASSERT(matched, "unexpected child pid observed");
  }

  printf("[PASS] process creation stress complete\n");
}

void test_scheduler(void) {
  print_test_banner("scheduler");
  printf("[TEST] scheduler behaviour...\n");

  scheduler_finish_count = 0;

  const int weights[CPU_TASKS] = {4, 8};
  for(int i = 0; i < CPU_TASKS; i++) {
    uint64 packed = ((uint64)weights[i] << 32) | (uint32)(i + 1);
    int pid = create_process("cpu-task", cpu_intensive_task, (void *)packed);
    TEST_ASSERT(pid > 0, "failed to create cpu task");
  }

  int interactive_pid = create_process("interactive", interactive_task, (void *)(uint64)0);
  TEST_ASSERT(interactive_pid > 0, "failed to create interactive task");

  uint64 start = get_time();
  busy_wait_cycles(1000000ULL);

  int finished = 0;
  int status;
  while(finished < TOTAL_SCHED_TASKS) {
    int pid = wait_process(&status);
    TEST_ASSERT(pid > 0, "wait_process failed in scheduler test");
    TEST_ASSERT(status == 0, "scheduler child exit status non-zero");
    finished++;
  }

  uint64 end = get_time();
  printf("[INFO] finish order: %s -> %s -> %s\n",
         scheduler_task_name(scheduler_finish_order[0]),
         scheduler_task_name(scheduler_finish_order[1]),
         scheduler_task_name(scheduler_finish_order[2]));
  TEST_ASSERT(scheduler_finish_order[0] == 0, "interactive task did not finish first");
  printf("[PASS] scheduler test completed in %lu cycles (MLFQ priority verified)\n",
         end - start);
}

static void reset_prio_results(void) {
  prio_finish_count = 0;
  for(int i = 0; i < PRIO_TEST_TASKS; i++)
    prio_finish_order[i] = -1;
}

void test_scheduler_priority_gap(void) {
  print_test_banner("scheduler priority T1");
  printf("[TEST] priority gap: high vs low...\n");
  reset_prio_results();
  struct prio_task_cfg cfg_low  = { .id = 1, .work_iters = 50 };
  struct prio_task_cfg cfg_high = { .id = 2, .work_iters = 50 };
  int pid_low  = create_process_prio("prio-low", prio_task, &cfg_low, PRIO_LOW);
  int pid_high = create_process_prio("prio-high", prio_task, &cfg_high, PRIO_HIGH);
  TEST_ASSERT(pid_high > 0 && pid_low > 0, "priority test spawn failed");
  printf("[INFO] spawned low-prio pid=%d (q%d, %d iters) BEFORE high-prio pid=%d (q0, %d iters)\n",
         pid_low, PRIO_LOW, cfg_low.work_iters, pid_high, cfg_high.work_iters);
  int status;
  int done = 0;
  while(done < 2) {
    int pid = wait_process(&status);
    TEST_ASSERT(pid == pid_high || pid == pid_low, "unexpected pid in priority gap");
    TEST_ASSERT(status == 0, "priority task exit status");
    done++;
  }
  TEST_ASSERT(prio_finish_order[0] == cfg_high.id, "high priority task did not finish first");
  printf("[INFO] finish order=%d,%d (MLFQ honored higher queue first)\n",
         prio_finish_order[0], prio_finish_order[1]);
  printf("[PASS] high priority finished before low priority\n");
}

void test_scheduler_same_priority(void) {
  print_test_banner("scheduler priority T2");
  printf("[TEST] equal priority fairness...\n");
  reset_prio_results();
  struct prio_task_cfg cfg_a = { .id = 1, .work_iters = 40 };
  struct prio_task_cfg cfg_b = { .id = 2, .work_iters = 40 };
  int pid_a = create_process_prio("prio-eq-a", prio_task, &cfg_a, PRIO_MED);
  int pid_b = create_process_prio("prio-eq-b", prio_task, &cfg_b, PRIO_MED);
  TEST_ASSERT(pid_a > 0 && pid_b > 0, "equal priority spawn failed");
  printf("[INFO] spawned two tasks in queue %d (time slice=%d)\n",
         PRIO_MED, mlfq_quanta[PRIO_MED]);
  int status;
  int done = 0;
  while(done < 2) {
    int pid = wait_process(&status);
    TEST_ASSERT(pid == pid_a || pid == pid_b, "unexpected pid in equal priority");
    TEST_ASSERT(status == 0, "equal priority exit status");
    done++;
  }
  TEST_ASSERT(prio_finish_count == 2, "not all equal priority tasks finished");
  printf("[INFO] finish order=%d,%d (round-robin within same queue)\n",
         prio_finish_order[0], prio_finish_order[1]);
  printf("[PASS] equal priority tasks completed\n");
}

void test_scheduler_mixed_priority(void) {
  print_test_banner("scheduler priority T3");
  printf("[TEST] mixed priority with aging...\n");
  reset_prio_results();
  struct prio_task_cfg cfg_hi = { .id = 3, .work_iters = 60 };
  struct prio_task_cfg cfg_mid = { .id = 2, .work_iters = 80 };
  struct prio_task_cfg cfg_lo = { .id = 1, .work_iters = 120 };
  int pid_lo = create_process_prio("mix-lo", prio_task, &cfg_lo, PRIO_LOW);
  int pid_mid = create_process_prio("mix-mid", prio_task, &cfg_mid, PRIO_MED);
  int pid_hi = create_process_prio("mix-hi", prio_task, &cfg_hi, PRIO_HIGH);
  TEST_ASSERT(pid_hi > 0 && pid_mid > 0 && pid_lo > 0, "mixed priority spawn failed");
  printf("[INFO] spawned order: low pid=%d(q%d) -> mid pid=%d(q%d) -> hi pid=%d(q%d) with quanta %d/%d/%d\n",
         pid_lo, PRIO_LOW, pid_mid, PRIO_MED, pid_hi, PRIO_HIGH,
         mlfq_quanta[PRIO_LOW], mlfq_quanta[PRIO_MED], mlfq_quanta[PRIO_HIGH]);
  int status;
  int done = 0;
  while(done < 3) {
    int pid = wait_process(&status);
    TEST_ASSERT(pid == pid_hi || pid == pid_mid || pid == pid_lo, "unexpected pid in mixed priority");
    TEST_ASSERT(status == 0, "mixed priority exit status");
    done++;
  }
  TEST_ASSERT(prio_finish_count == 3, "not all mixed priority tasks finished");
  TEST_ASSERT(prio_finish_order[0] == cfg_hi.id, "highest priority did not lead");
  printf("[INFO] finish order=%d,%d,%d (aging allows low queue to complete)\n",
         prio_finish_order[0], prio_finish_order[1], prio_finish_order[2]);
  printf("[PASS] mixed priority tasks finished\n");
}

void test_synchronization(void) {
  print_test_banner("synchronization");
  printf("[TEST] synchronization (producer/consumer)...\n");

  shared_buffer_init();
  int prod = create_process("producer", producer_task, 0);
  TEST_ASSERT(prod > 0, "producer creation failed");
  int cons = create_process("consumer", consumer_task, 0);
  TEST_ASSERT(cons > 0, "consumer creation failed");

  int status;
  for(int i = 0; i < 2; i++) {
    int pid = wait_process(&status);
    TEST_ASSERT(pid == prod || pid == cons, "unexpected pid in sync test");
    TEST_ASSERT(status == 0, "sync child exit status non-zero");
  }

  int expected = (PRODUCE_ITEMS - 1) * PRODUCE_ITEMS / 2;
  TEST_ASSERT(consumer_total == expected, "consumer total mismatch");
  printf("[PASS] synchronization test completed (sum=%d)\n", consumer_total);
}

void debug_proc_table(void) {
  print_test_banner("process table");
  printf("[DEBUG] process table snapshot\n");
  for(int i = 0; i < NPROC; i++) {
    struct proc *p = &proc[i];
    if(p->state != UNUSED) {
      printf("PID:%d State:%d Name:%s\n", p->pid, p->state, p->name);
    }
  }
  printf("[DEBUG] end of process table\n");
}

void run_proc_tests(void *arg) {
  (void)arg;
  printf("[SUITE] running kernel process tests\n");
  test_process_creation();
  test_scheduler();
  test_scheduler_priority_gap();
  test_scheduler_same_priority();
  test_scheduler_mixed_priority();
  test_synchronization();
  debug_proc_table();
  printf("[SUITE] all tests finished\n");
}
