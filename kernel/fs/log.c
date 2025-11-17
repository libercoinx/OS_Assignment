#include "fs.h"
#include "defs.h"
#include "string.h"
#include "panic.h"

struct {
  struct spinlock lock;
  int dev;
  int start;
  int size;
  int outstanding;
  int committing;

  struct logheader {
    int n;
    int block[LOGSIZE];
  } lh;
} logstate;

static void recover_from_log(void);
static void write_log(void);
static void install_trans(void);

void
log_init(int dev, struct superblock *sb) {
  initlock(&logstate.lock, "log");
  logstate.dev = dev;
  logstate.start = sb->logstart;
  logstate.size = sb->nlog;
  recover_from_log();
}

void
begin_op(void) {
  acquire(&logstate.lock);
  while(1) {
    if(logstate.committing) {
      release(&logstate.lock);
      continue;
    }
    if(logstate.lh.n + (logstate.outstanding + 1) * MAXOPBLOCKS > LOGSIZE) {
      release(&logstate.lock);
      continue;
    } else {
      logstate.outstanding += 1;
      release(&logstate.lock);
      break;
    }
  }
}

void
end_op(void) {
  int do_commit = 0;

  acquire(&logstate.lock);
  logstate.outstanding -= 1;
  if(logstate.committing)
    panic("log committing");
  if(logstate.outstanding == 0) {
    do_commit = 1;
    logstate.committing = 1;
  }
  release(&logstate.lock);

  if(do_commit) {
    write_log();
    install_trans();
    acquire(&logstate.lock);
    logstate.lh.n = 0;
    write_log();
    logstate.committing = 0;
    release(&logstate.lock);
  }
}

void
log_write(struct buf *b) {
  if(logstate.lh.n >= LOGSIZE)
    panic("log_write: too big");
  if(logstate.outstanding < 1)
    panic("log_write outside transaction");

  acquire(&logstate.lock);
  for(int i = 0; i < logstate.lh.n; i++) {
    if(logstate.lh.block[i] == (int)(b->blockno)) {
      release(&logstate.lock);
      return;
    }
  }
  bpin(b);
  logstate.lh.block[logstate.lh.n] = b->blockno;
  logstate.lh.n++;
  release(&logstate.lock);
}

static void
write_log(void) {
  for(int i = 0; i < logstate.lh.n; i++) {
    struct buf *to = bread(logstate.dev, logstate.start + i + 1);
    struct buf *from = bread(logstate.dev, logstate.lh.block[i]);
    memmove(to->data, from->data, BSIZE);
    bwrite(to);
    brelse(from);
    brelse(to);
  }
  struct buf *hb = bread(logstate.dev, logstate.start);
  memmove(hb->data, &logstate.lh, sizeof(logstate.lh));
  bwrite(hb);
  brelse(hb);
}

static void
install_trans(void) {
  for(int i = 0; i < logstate.lh.n; i++) {
    struct buf *lbuf = bread(logstate.dev, logstate.start + i + 1);
    struct buf *dbuf = bread(logstate.dev, logstate.lh.block[i]);
    memmove(dbuf->data, lbuf->data, BSIZE);
    bwrite(dbuf);
    bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

static void
recover_from_log(void) {
  struct buf *b = bread(logstate.dev, logstate.start);
  memmove(&logstate.lh, b->data, sizeof(logstate.lh));
  brelse(b);
  install_trans();
  logstate.lh.n = 0;
  write_log();
}

void
log_force_recover(void) {
  acquire(&logstate.lock);
  recover_from_log();
  release(&logstate.lock);
}
