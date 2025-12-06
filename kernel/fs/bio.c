#include "fs.h"
#include "defs.h"
#include "string.h"
#include "panic.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct buf head;
} bcache;

static uint buffer_cache_hits;
static uint buffer_cache_misses;
static uint disk_read_count;
static uint disk_write_count;

static uchar ramdisk[FSSIZE][BSIZE];

static void
ramdisk_init(void) {
  memset(ramdisk, 0, sizeof(ramdisk));
}

static void
ramdisk_rw(struct buf *b, int write) {
  if(b->blockno >= FSSIZE)
    panic("ramdisk out of bounds");
  if(write)
    disk_write_count++;
  else
    disk_read_count++;
  if(write) {
    memmove(ramdisk[b->blockno], b->data, BSIZE);
  } else {
    memmove(b->data, ramdisk[b->blockno], BSIZE);
  }
}

void
binit(void) {
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  ramdisk_init();

  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
    initlock(&b->lock, "buf");
  }
}

static struct buf*
bget(uint dev, uint blockno) {
  struct buf *b;

  acquire(&bcache.lock);

  for(b = bcache.head.next; b != &bcache.head; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      buffer_cache_hits++;
      b->refcnt++;
      release(&bcache.lock);
      acquire(&b->lock);
      return b;
    }
  }

  for(b = bcache.head.prev; b != &bcache.head; b = b->prev) {
    if(b->refcnt == 0) {
      buffer_cache_misses++;
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->disk = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquire(&b->lock);
      return b;
    }
  }

  panic("bget: no buffers");
  return 0;
}

struct buf*
bread(uint dev, uint blockno) {
  struct buf *b = bget(dev, blockno);
  if(!b->valid) {
    ramdisk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

void
bwrite(struct buf *b) {
  if(!holding(&b->lock))
    panic("bwrite");
  ramdisk_rw(b, 1);
}

void
brelse(struct buf *b) {
  if(!holding(&b->lock))
    panic("brelse");

  release(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if(b->refcnt == 0) {
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}

void
fs_get_cache_counters(struct fs_cache_counters *counters) {
  if(counters == 0)
    return;
  acquire(&bcache.lock);
  counters->buffer_cache_hits = buffer_cache_hits;
  counters->buffer_cache_misses = buffer_cache_misses;
  counters->disk_read_count = disk_read_count;
  counters->disk_write_count = disk_write_count;
  release(&bcache.lock);
}
