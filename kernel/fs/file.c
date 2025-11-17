#include "fs.h"
#include "defs.h"
#include "panic.h"

struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void) {
  initlock(&ftable.lock, "ftable");
}

struct file*
filealloc(void) {
  acquire(&ftable.lock);
  for(struct file *f = ftable.file; f < ftable.file + NFILE; f++) {
    if(f->ref == 0) {
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

struct file*
filedup(struct file *f) {
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

void
fileclose(struct file *f) {
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  f->ref--;
  if(f->ref > 0) {
    release(&ftable.lock);
    return;
  }
  struct file ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_INODE) {
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

int
fileread(struct file *f, char *addr, int n) {
  if(f->readable == 0)
    return -1;
  if(f->type == FD_INODE) {
    ilock(f->ip);
    int r = readi(f->ip, (uint64)addr, f->off, n);
    if(r > 0)
      f->off += r;
    iunlock(f->ip);
    return r;
  }
  return -1;
}

int
filewrite(struct file *f, char *addr, int n) {
  if(f->writable == 0)
    return -1;
  if(f->type == FD_INODE) {
    int max = MAXOPBLOCKS * BSIZE;
    int i = 0;
    while(i < n) {
      int n1 = n - i;
      if(n1 > max)
        n1 = max;
      begin_op();
      ilock(f->ip);
      int r = writei(f->ip, (uint64)addr + i, f->off, n1);
      if(r > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();
      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  return -1;
}
