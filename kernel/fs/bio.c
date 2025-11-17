#include "fs.h"
#include "defs.h"
#include "string.h"
#include "panic.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct buf head;
} bcache;

static uchar ramdisk[FSSIZE][BSIZE];

static void
ramdisk_init(void) {
  memset(ramdisk, 0, sizeof(ramdisk));
}

static void
ramdisk_rw(struct buf *b, int write) {
  if(b->blockno >= FSSIZE)
    panic("ramdisk out of bounds");
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
      b->refcnt++;
      release(&bcache.lock);
      acquire(&b->lock);
      return b;
    }
  }

  for(b = bcache.head.prev; b != &bcache.head; b = b->prev) {
    if(b->refcnt == 0) {
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
