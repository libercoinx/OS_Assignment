# Lab3：页表与内存管理实验报告

## 1. 实验目标
- 深入理解 RISC-V Sv39 页表机制和虚拟内存工作原理
- 实现物理内存分配器（kalloc/kfree）
- 实现虚拟内存管理系统（页表遍历、映射建立、地址转换）
- 启用内核虚拟内存并验证功能

## 2. Sv39 页表机制

### 2.1 虚拟地址分解
```
39位虚拟地址分解：
| 38-30 | 29-21 | 20-12 | 11-0 |
| VPN[2]| VPN[1]| VPN[0]|offset|
|  9位  |  9位  |  9位  | 12位 |
```

- **VPN[2]**: 第3级页表索引（9位，512个条目）
- **VPN[1]**: 第2级页表索引（9位，512个条目）  
- **VPN[0]**: 第1级页表索引（9位，512个条目）
- **offset**: 页内偏移（12位，4KB页）

### 2.2 页表项（PTE）格式
```
| 63-54 | 53-28 | 27-19 | 18-10 | 9-8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| 保留  | PPN[2]| PPN[1]| PPN[0]| 保留| D | A | G | U | X | W | R | V |
```

- **V**: 有效位
- **R/W/X**: 读/写/执行权限
- **U**: 用户态访问权限
- **G**: 全局页
- **A**: 访问位
- **D**: 脏位
- **PPN**: 物理页号（44位）

## 3. 物理内存分配器实现

### 3.1 核心数据结构
```c
struct run {
  struct run *next;  // 空闲页链表
};

struct {
  struct spinlock lock;
  struct run *freelist;  // 空闲页链表头
} kmem;
```

### 3.2 关键函数实现

#### kinit() - 初始化
- 初始化自旋锁
- 调用 `freerange()` 将可用内存加入空闲链表

#### kalloc() - 分配页
- 获取锁保护
- 从空闲链表取出一页
- 用垃圾数据填充（调试用）
- 返回页地址

#### kfree() - 释放页
- 检查地址有效性（页对齐、范围检查）
- 用垃圾数据填充
- 将页加入空闲链表

### 3.3 内存工具函数
- 新增 `kernel/string.c` 封装 `memset`/`memmove`/`memcpy`
- 统一由自实现函数提供内存操作，避免在不同源文件重复定义
- `memmove` 支持重叠区域拷贝，供页表复制（`uvmcopy` 等）使用

## 4. 虚拟内存管理实现

### 4.1 页表遍历 - walk()
```c
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[VPN_MASK(va, level)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc)
        return 0;
      pagetable_t next = (pagetable_t)kalloc();
      if(next == 0)
        return 0;
      memset(next, 0, PGSIZE);
      *pte = PA2PTE(next) | PTE_V;
      pagetable = next;
    }
  }
  return &pagetable[VPN_MASK(va, 0)];
}
```

### 4.2 映射建立 - mappages()
- 按页对齐处理地址范围
- 对每个页调用 `walk()` 获取页表项
- 设置页表项：物理地址 + 权限 + 有效位

### 4.3 内核页表初始化 - kvminit()
```c
void kvminit(void) {
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // UART
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT / PLIC
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // 内核代码段（只读+执行）
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // 内核数据段（读写）
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // 跳板页，供陷阱切换使用
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}
```

### 4.4 页表释放 - freewalk()
- 递归释放各级页表页，防止 `uvmfree()` 只释放叶子导致内存泄漏
- 对仍然带有 R/W/X 权限的条目触发 `panic`，帮助定位遗漏的映射清理

```c
static void freewalk(pagetable_t pagetable) {
  for(int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V) {
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}
```

## 5. 关键宏定义

### 5.1 地址操作
```c
#define PGROUNDUP(sz) (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(a) ((a) & ~(PGSIZE - 1))
#define VPN_MASK(va, level) (((va) >> VPN_SHIFT(level)) & 0x1FF)
```

### 5.2 页表项操作
```c
#define PTE_PA(pte) (((pte) >> 10) << 12)
#define PA2PTE(pa) ((((uint64)(pa)) >> 12) << 10)
#define MAKE_SATP(pt) (SATP_MODE_SV39 | (((uint64)(pt)) >> 12))
```

## 6. 测试与验证

### 6.1 测试断言与框架
```c
#define TEST_ASSERT(cond, msg)                                      \
  do {                                                              \
    if(!(cond)) {                                                   \
      printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, (msg));       \
      panic("test failure");                                        \
    }                                                               \
  } while(0)
```

### 6.2 物理内存分配测试
```c
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
```

### 6.3 页表功能测试
```c
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
```

### 6.4 内核虚拟内存测试
```c
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
```

## 7. 调试要点

### 7.1 常见问题
- **页表遍历失败**: 检查地址范围、页表项有效性
- **映射冲突**: 确保不重复映射同一虚拟地址
- **权限错误**: 检查 R/W/X 位设置是否合理
- **TLB 刷新**: 修改页表后调用 `sfence_vma()`

### 7.2 调试工具
```c
void dump_pagetable(pagetable_t pt, int level) {
  // 递归打印页表内容
  // 显示虚拟地址到物理地址的映射关系
  // 标明权限位设置
}
```

## 8. 性能优化

### 8.1 分配器优化
- **伙伴系统**: 支持不同大小分配
- **SLAB 分配器**: 小对象快速分配
- **NUMA 感知**: 多核系统内存局部性

### 8.2 页表优化
- **大页支持**: 减少页表层级
- **TLB 预取**: 减少地址转换开销
- **页表共享**: 进程间共享只读页表

## 9. 扩展功能

### 9.1 内存统计
- 跟踪已分配/空闲页数量
- 内存使用率监控
- 内存泄漏检测

### 9.2 高级特性
- **写时复制**: 进程 fork 优化
- **内存压缩**: 减少物理内存使用
- **内存热插拔**: 动态内存管理

## 10. 结论

本实验成功实现了 RISC-V Sv39 页表机制下的物理内存分配器和虚拟内存管理系统。通过分层设计，实现了从硬件抽象到高级内存管理的完整栈，为后续进程管理、文件系统等功能奠定了基础。

关键收获：
- 深入理解了虚拟内存的工作原理
- 掌握了页表遍历和映射建立的技术细节
- 学会了内存管理器的设计和实现方法
- 理解了操作系统内存管理的复杂性
