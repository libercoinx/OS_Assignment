#include "defs.h"
#include "kalloc.h"
#include "panic.h"
#include "riscv.h"
#include "string.h"
#include "vm.h"
#include "trap.h"
#include "proc.h"
#include "fs.h"

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

static volatile int shared_counter;
static volatile int yield_counts[3];
static struct spinlock sleep_test_lock;
static volatile int sleep_ready;
static volatile int sleep_value;

#define FS_CONCUR_WORKERS 2
#define FS_CONCUR_ITERS   2
#define FS_PERF_SMALL_FILES 16
#define FS_LARGE_BLOCKS   16
static char fs_large_buffer[BSIZE * FS_LARGE_BLOCKS];

static int append_uint(char *dst, int value) {
  char tmp[16];
  int len = 0;
  if(value == 0) {
    tmp[len++] = '0';
  } else {
    while(value > 0 && len < (int)sizeof(tmp)) {
      tmp[len++] = '0' + (value % 10);
      value /= 10;
    }
  }
  for(int i = len - 1; i >= 0; i--)
    *dst++ = tmp[i];
  return len;
}

static void format_name(char *dst, const char *prefix, int idx) {
  int i = 0;
  while(prefix[i] && i < DIRSIZ - 2) {
    dst[i] = prefix[i];
    i++;
  }
  dst[i++] = '_';
  i += append_uint(dst + i, idx);
  if(i >= DIRSIZ)
    i = DIRSIZ - 1;
  dst[i] = 0;
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


void test_filesystem_smoke(void) {
  print_test_banner("filesystem smoke");
  printf("[TEST] filesystem smoke...\n");
  const char *path = "/demo";
  const char *payload = "Hello, filesystem!";
  char buf[64];

  int written = fs_write_file(path, payload, strlen(payload));
  TEST_ASSERT(written == (int)strlen(payload), "smoke write mismatch");
  printf("[INFO] wrote %d bytes to %s: \"%s\"\n", written, path, payload);
  int size = fs_file_size(path);
  TEST_ASSERT(size == (int)strlen(payload), "smoke size mismatch");
  memset(buf, 0, sizeof(buf));
  int read = fs_read_file(path, buf, sizeof(buf));
  TEST_ASSERT(read == (int)strlen(payload), "smoke read mismatch");
  TEST_ASSERT(strncmp(buf, payload, strlen(payload)) == 0, "smoke content mismatch");
  printf("[INFO] read %d bytes from %s: \"%s\"\n", read, path, buf);
  TEST_ASSERT(fs_delete_file(path) == 0, "smoke delete failed");
  printf("[INFO] deleted %s\n", path);
  printf("[PASS] filesystem smoke\n");
}

void test_filesystem_integrity(void) {
  print_test_banner("filesystem integrity");
  printf("[TEST] filesystem integrity...\n");
  const char *path = "/testfile";
  const char *payload = "Hello, filesystem!";
  char buf[64];

  TEST_ASSERT(fs_write_file(path, payload, strlen(payload)) == (int)strlen(payload),
              "integrity write mismatch");
  printf("[INFO] integrity write path=%s payload=\"%s\"\n", path, payload);
  TEST_ASSERT(fs_file_size(path) == (int)strlen(payload), "integrity size mismatch");
  memset(buf, 0, sizeof(buf));
  int bytes = fs_read_file(path, buf, sizeof(buf));
  TEST_ASSERT(bytes == (int)strlen(payload), "integrity read mismatch");
  TEST_ASSERT(strncmp(buf, payload, strlen(payload)) == 0, "integrity content mismatch");
  printf("[INFO] integrity read path=%s bytes=%d data=\"%s\"\n", path, bytes, buf);
  TEST_ASSERT(fs_delete_file(path) == 0, "integrity delete failed");
  printf("[INFO] integrity delete path=%s\n", path);
  printf("[PASS] filesystem integrity\n");
}

static void concurrent_worker(void *arg) {
  int id = (int)(uint64)arg;
  char name[DIRSIZ];
  for(int iter = 0; iter < FS_CONCUR_ITERS; iter++) {
    format_name(name, "concur", id * FS_CONCUR_ITERS + iter);
    uint32 value = (uint32)((id << 16) | iter);
    printf("[INFO] worker %d writing %s value=0x%x\n", id, name, value);
    TEST_ASSERT(fs_write_file(name, (char *)&value, sizeof(value)) == (int)sizeof(value),
                "concurrent write failed");
    TEST_ASSERT(fs_delete_file(name) == 0, "concurrent delete failed");
    printf("[INFO] worker %d deleted %s\n", id, name);
  }
}

void test_concurrent_access(void) {
  print_test_banner("filesystem concurrent");
  printf("[TEST] concurrent filesystem access...\n");
  int pids[FS_CONCUR_WORKERS];
  for(int i = 0; i < FS_CONCUR_WORKERS; i++) {
    pids[i] = create_process("fs-worker", concurrent_worker, (void *)(uint64)i);
    TEST_ASSERT(pids[i] > 0, "fs worker spawn failed");
  }
  int finished[FS_CONCUR_WORKERS] = {0};
  int remaining = FS_CONCUR_WORKERS;
  int status;
  while(remaining > 0) {
    int pid = wait_process(&status);
    TEST_ASSERT(pid > 0, "fs worker wait failed");
    TEST_ASSERT(status == 0, "fs worker exit status");
    int idx = -1;
    for(int i = 0; i < FS_CONCUR_WORKERS; i++) {
      if(pids[i] == pid) {
        idx = i;
        break;
      }
    }
    TEST_ASSERT(idx != -1, "fs worker pid unknown");
    TEST_ASSERT(finished[idx] == 0, "fs worker duplicate wait");
    finished[idx] = 1;
    remaining--;
  }
  printf("[PASS] concurrent filesystem access\n");
}

void test_crash_recovery(void) {
  print_test_banner("filesystem recovery");
  printf("[TEST] crash recovery simulation...\n");
  const char *path = "/fs_recovery";
  const char *payload = "journal-entry";
  char buf[32];

  TEST_ASSERT(fs_write_file(path, payload, strlen(payload)) == (int)strlen(payload),
              "recovery write failed");
  printf("[INFO] wrote \"%s\" to %s, triggering recovery\n", payload, path);
  fs_force_recovery();
  TEST_ASSERT(fs_read_file(path, buf, sizeof(buf)) == (int)strlen(payload),
              "recovery read failed");
  TEST_ASSERT(strncmp(buf, payload, strlen(payload)) == 0, "recovery data mismatch");
  printf("[INFO] after recovery read \"%s\" from %s\n", buf, path);
  TEST_ASSERT(fs_delete_file(path) == 0, "recovery delete failed");
  fs_force_recovery();
  TEST_ASSERT(fs_read_file(path, buf, sizeof(buf)) < 0, "recovery cleanup failed");
  printf("[INFO] verified %s removed after recovery\n", path);
  printf("[PASS] crash recovery\n");
}

void test_filesystem_performance(void) {
  print_test_banner("filesystem performance");
  printf("[TEST] filesystem performance...\n");
  char name[DIRSIZ];
  const char *small_data = "test";

  uint64 start = get_time();
  for(int i = 0; i < FS_PERF_SMALL_FILES; i++) {
    format_name(name, "small", i);
    TEST_ASSERT(fs_write_file(name, small_data, strlen(small_data)) == (int)strlen(small_data),
                "perf small write failed");
  }
  uint64 small_time = get_time() - start;
  for(int i = 0; i < FS_PERF_SMALL_FILES; i++) {
    format_name(name, "small", i);
    TEST_ASSERT(fs_delete_file(name) == 0, "perf small delete failed");
  }

  memset(fs_large_buffer, 0xcd, sizeof(fs_large_buffer));
  start = get_time();
  TEST_ASSERT(fs_write_file("/large_file", fs_large_buffer, sizeof(fs_large_buffer)) ==
              (int)sizeof(fs_large_buffer), "perf large write failed");
  uint64 large_time = get_time() - start;
  TEST_ASSERT(fs_delete_file("/large_file") == 0, "perf large delete failed");

  printf("[INFO] small files (%d x %dB): %d cycles\n",
         FS_PERF_SMALL_FILES, (int)strlen(small_data), (int)small_time);
  printf("[INFO] large file (%dB): %d cycles\n",
         (int)sizeof(fs_large_buffer), (int)large_time);
  printf("[PASS] filesystem performance\n");
}

void debug_filesystem_state(void) {
  print_test_banner("filesystem state");
  struct fs_usage_stats stats;
  TEST_ASSERT(fs_get_usage_stats(&stats) == 0, "fs stats unavailable");
  printf("[INFO] blocks total=%d data=%d free=%d\n",
         (int)stats.total_blocks, (int)stats.data_blocks, (int)stats.free_blocks);
  printf("[INFO] inodes total=%d free=%d\n",
         (int)stats.total_inodes, (int)stats.free_inodes);
}

void debug_inode_usage(void) {
  print_test_banner("inode usage");
  struct fs_inode_usage entries[NINODE];
  int count = fs_collect_inode_usage(entries, NINODE);
  if(count == 0) {
    printf("[INFO] no active inodes\n");
    return;
  }
  for(int i = 0; i < count; i++) {
    printf("INODE:%d ref=%d type=%d size=%d\n",
           entries[i].inum, entries[i].ref, entries[i].type, (int)entries[i].size);
  }
}

void debug_disk_io(void) {
  print_test_banner("disk I/O stats");
  struct fs_cache_counters counters;
  fs_get_cache_counters(&counters);
  printf("[INFO] cache hits=%d misses=%d\n",
         (int)counters.buffer_cache_hits, (int)counters.buffer_cache_misses);
  printf("[INFO] disk reads=%d writes=%d\n",
         (int)counters.disk_read_count, (int)counters.disk_write_count);
}

void run_fs_tests(void *arg) {
  (void)arg;
  printf("[SUITE] running filesystem tests\n");
  test_filesystem_smoke();
  test_filesystem_integrity();
  test_concurrent_access();
  test_crash_recovery();
  test_filesystem_performance();
  debug_filesystem_state();
  debug_inode_usage();
  debug_disk_io();
  printf("[SUITE] filesystem tests finished\n");
}
