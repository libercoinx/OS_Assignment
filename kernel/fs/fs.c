#include "fs.h"
#include "defs.h"
#include "string.h"
#include "kalloc.h"
#include "proc.h"
#include "panic.h"

static void readsb(int dev, struct superblock *sb);
static void iinit(void);
static void itrunc(struct inode *ip);
static struct inode* iget(uint dev, uint inum);
static uint bmap(struct inode *ip, uint bn);
static uint balloc(uint dev);
static void bfree(int dev, uint b);
struct inode* create(const char *path, short type, short major, short minor);
static void fs_format(void);
static void bzero(int dev, int bno);
static void log_persist(struct buf *bp);

struct superblock sb;
static int log_ready = 0;

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
fs_init(void) {
  binit();
  fs_format();
  readsb(0, &sb);
  log_init(0, &sb);
  log_ready = 1;
  iinit();
}

static void
readsb(int dev, struct superblock *sbp) {
  struct buf *bp = bread(dev, 1);
  memmove(sbp, bp->data, sizeof(*sbp));
  brelse(bp);
}

static void
fs_format(void) {
  struct superblock newsb;
  newsb.magic = FSMAGIC;
  newsb.size = FSSIZE;
  newsb.ninodes = NINODE;
  newsb.nlog = LOGSIZE;
  newsb.logstart = 2;
  uint inodes = (newsb.ninodes + IPB - 1) / IPB;
  newsb.inodestart = newsb.logstart + newsb.nlog;
  newsb.bmapstart = newsb.inodestart + inodes;
  uint bitmap_blocks = (newsb.size + BPB - 1) / BPB;
  newsb.datastart = newsb.bmapstart + bitmap_blocks;
  newsb.nblocks = newsb.size - newsb.datastart;

  sb = newsb;

  for(uint b = 0; b < FSSIZE; b++) {
    struct buf *bp = bread(0, b);
    memset(bp->data, 0, BSIZE);
    bwrite(bp);
    brelse(bp);
  }

  struct buf *sbp = bread(0, 1);
  memmove(sbp->data, &sb, sizeof(sb));
  bwrite(sbp);
  brelse(sbp);

  struct inode *root = ialloc(0, T_DIR);
  ilock(root);
  root->nlink = 2;
  iupdate(root);
  struct dirent de;
  memset(&de, 0, sizeof(de));
  de.inum = root->inum;
  strncpy(de.name, ".", DIRSIZ);
  writei(root, (uint64)&de, 0, sizeof(de));
  de.inum = root->inum;
  strncpy(de.name, "..", DIRSIZ);
  writei(root, (uint64)&de, sizeof(de), sizeof(de));
  iunlockput(root);
}

static void
iinit(void) {
  initlock(&icache.lock, "icache");
  for(int i = 0; i < NINODE; i++) {
    struct inode *ip = &icache.inode[i];
    initlock(&ip->lock, "inode");
    ip->ref = 0;
    ip->valid = 0;
  }
}

static struct inode*
iget(uint dev, uint inum) {
  struct inode *ip, *empty = 0;

  acquire(&icache.lock);
  for(ip = icache.inode; ip < icache.inode + NINODE; ip++) {
    if(ip->ref > 0 && ip->dev == (int)dev && ip->inum == (int)inum) {
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)
      empty = ip;
  }
  if(empty == 0)
    panic("iget: no inodes");
  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);
  return ip;
}

struct inode*
idup(struct inode *ip) {
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

void
ilock(struct inode *ip) {
  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquire(&ip->lock);
  if(ip->valid == 0) {
    struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    struct dinode *dip = (struct dinode*)bp->data + (ip->inum % IPB);
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

void
iunlock(struct inode *ip) {
  if(ip == 0 || !holding(&ip->lock))
    panic("iunlock");
  release(&ip->lock);
}

void
iupdate(struct inode *ip) {
  struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  struct dinode *dip = (struct dinode*)bp->data + (ip->inum % IPB);
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_persist(bp);
  brelse(bp);
}

void
iput(struct inode *ip) {
  acquire(&icache.lock);
  if(ip->ref == 1 && ip->valid && ip->nlink == 0) {
    acquire(&ip->lock);
    release(&icache.lock);
    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    ip->valid = 0;
    release(&ip->lock);
    acquire(&icache.lock);
  }
  ip->ref--;
  release(&icache.lock);
}

void
iunlockput(struct inode *ip) {
  iunlock(ip);
  iput(ip);
}

static void
itrunc(struct inode *ip) {
  for(int i = 0; i < NDIRECT; i++) {
    if(ip->addrs[i]) {
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]) {
    struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
    uint *a = (uint*)bp->data;
    for(int j = 0; j < (int)NINDIRECT; j++) {
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }
  ip->size = 0;
  iupdate(ip);
}

static uint
bmap(struct inode *ip, uint bn) {
  if(bn < NDIRECT) {
    if(ip->addrs[bn] == 0) {
      uint lbn = balloc(ip->dev);
      ip->addrs[bn] = sb.datastart + lbn;
    }
    return ip->addrs[bn];
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT) {
    if(ip->addrs[NDIRECT] == 0) {
      uint lbn = balloc(ip->dev);
      ip->addrs[NDIRECT] = sb.datastart + lbn;
    }
    struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
    uint *a = (uint*)bp->data;
    if(a[bn] == 0) {
      uint lbn = balloc(ip->dev);
      a[bn] = sb.datastart + lbn;
      log_persist(bp);
    }
    uint addr = a[bn];
    brelse(bp);
    return addr;
  }
  panic("bmap: out of range");
}

static uint
balloc(uint dev) {
  struct buf *bp;
  for(uint b = 0; b < sb.nblocks; b += BPB) {
    bp = bread(dev, BBLOCK(b, sb));
    for(int bi = 0; bi < BPB && b + bi < sb.nblocks; bi++) {
      int m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0) {
        bp->data[bi/8] |= m;
        log_persist(bp);
        brelse(bp);
        uint phys = sb.datastart + b + bi;
        bzero(dev, phys);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc");
}

static void
bzero(int dev, int bno) {
  struct buf *bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_persist(bp);
  brelse(bp);
}

static void
bfree(int dev, uint b) {
  if(b < sb.datastart || b >= sb.size)
    panic("bfree range");
  uint lbn = b - sb.datastart;
  struct buf *bp = bread(dev, BBLOCK(lbn, sb));
  int bi = lbn % BPB;
  int m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("bfree");
  bp->data[bi/8] &= ~m;
  log_persist(bp);
  brelse(bp);
}

static void
log_persist(struct buf *bp) {
  if(log_ready)
    log_write(bp);
  else
    bwrite(bp);
}

struct inode*
ialloc(uint dev, short type) {
  struct buf *bp;
  struct dinode *dip;

  for(uint inum = 1; inum < sb.ninodes; inum++) {
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + (inum % IPB);
    if(dip->type == 0) {
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_persist(bp);
      brelse(bp);
      struct inode *ip = iget(dev, inum);
      ilock(ip);
      ip->type = type;
      ip->major = 0;
      ip->minor = 0;
      ip->nlink = 1;
      ip->size = 0;
      memset(ip->addrs, 0, sizeof(ip->addrs));
      iupdate(ip);
      iunlock(ip);
      return ip;
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

int
readi(struct inode *ip, uint64 dst, uint off, uint n) {
  uint tot;
  uint m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot = 0; tot < n; tot += m, off += m, dst += m) {
    bp = bread(ip->dev, bmap(ip, off / BSIZE));
    m = MIN(n - tot, BSIZE - off % BSIZE);
    memmove((void*)dst, bp->data + (off % BSIZE), m);
    brelse(bp);
  }
  return n;
}

int
writei(struct inode *ip, uint64 src, uint off, uint n) {
  uint tot;
  uint m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot = 0; tot < n; tot += m, off += m, src += m) {
    bp = bread(ip->dev, bmap(ip, off / BSIZE));
    m = MIN(n - tot, BSIZE - off % BSIZE);
    memmove(bp->data + (off % BSIZE), (void*)src, m);
    log_persist(bp);
    brelse(bp);
  }
  if(n > 0 && off > ip->size)
    ip->size = off;
  iupdate(ip);
  return n;
}

int
dirlookup(struct inode *dp, const char *name, uint *poff) {
  if(dp->type != T_DIR)
    panic("dirlookup");

  for(uint off = 0; off < dp->size; off += sizeof(struct dirent)) {
    struct dirent de;
    if(readi(dp, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(strncmp(name, de.name, DIRSIZ) == 0) {
      if(poff)
        *poff = off;
      return de.inum;
    }
  }
  return 0;
}

int
dirlink(struct inode *dp, const char *name, uint inum) {
  if(dirlookup(dp, name, 0) != 0)
    return -1;

  struct dirent de = {0};
  for(uint off = 0; off < dp->size; off += sizeof(de)) {
    if(readi(dp, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink");
    if(de.inum == 0) {
      de.inum = inum;
      strncpy(de.name, name, DIRSIZ);
      writei(dp, (uint64)&de, off, sizeof(de));
      return 0;
    }
  }

  de.inum = inum;
  strncpy(de.name, name, DIRSIZ);
  if(writei(dp, (uint64)&de, dp->size, sizeof(de)) != sizeof(de))
    panic("dirlink new");
  return 0;
}

static char*
skipelem(char *path, char *name) {
  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  char *s = path;
  while(*path != '/' && *path != 0)
    path++;
  int len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

static struct inode*
namex(char *path, int nameiparent, char *name) {
  struct inode *ip = iget(0, ROOTINO);
  if(*path == 0)
    return ip;

  char elem[DIRSIZ];
  struct inode *next;
  char *p = path;

  while((p = skipelem(p, elem)) != 0) {
    ilock(ip);
    if(ip->type != T_DIR) {
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *p == '\0') {
      memmove(name, elem, DIRSIZ);
      iunlock(ip);
      return ip;
    }
    uint inum = dirlookup(ip, elem, 0);
    if(inum == 0) {
      iunlockput(ip);
      return 0;
    }
    next = iget(ip->dev, inum);
    iunlockput(ip);
    ip = next;
  }

  if(nameiparent)
    return 0;
  return ip;
}

struct inode*
namei(char *path) {
  char namebuf[DIRSIZ];
  return namex(path, 0, namebuf);
}

struct inode*
nameiparent(char *path, char *name) {
  return namex(path, 1, name);
}

struct inode*
create(const char *path, short type, short major, short minor) {
  char name[DIRSIZ];
  char buf[128];
  strncpy(buf, path, sizeof(buf));
  buf[sizeof(buf)-1] = 0;
  struct inode *dp = nameiparent(buf, name);
  if(dp == 0) {
    dp = iget(0, ROOTINO);
    strncpy(name, buf, DIRSIZ);
    name[DIRSIZ-1] = 0;
  }
  ilock(dp);

  uint inum = dirlookup(dp, name, 0);
  if(inum != 0) {
    struct inode *ip = iget(dp->dev, inum);
    iunlockput(dp);
    ilock(ip);
    return ip;
  }

  struct inode *ip = ialloc(dp->dev, type);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR) {
    dp->nlink++;
    iupdate(dp);
    struct dirent de;
    de.inum = ip->inum;
    strncpy(de.name, ".", DIRSIZ);
    writei(ip, (uint64)&de, 0, sizeof(de));
    de.inum = dp->inum;
    strncpy(de.name, "..", DIRSIZ);
    writei(ip, (uint64)&de, sizeof(de), sizeof(de));
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");
  iunlockput(dp);
  ilock(ip);
  return ip;
}

int
fs_write_file(const char *path, const char *data, int len) {
  begin_op();
  struct inode *ip = create(path, T_FILE, 0, 0);
  if(ip == 0) {
    end_op();
    return -1;
  }
  int wrote = writei(ip, (uint64)data, 0, len);
  iunlockput(ip);
  end_op();
  return wrote;
}

int
fs_read_file(const char *path, char *dst, int max) {
  begin_op();
  char buf[128];
  strncpy(buf, path, sizeof(buf));
  buf[sizeof(buf)-1] = 0;
  struct inode *ip = namei(buf);
  if(ip == 0) {
    end_op();
    return -1;
  }
  ilock(ip);
  int r = readi(ip, (uint64)dst, 0, max);
  iunlockput(ip);
  end_op();
  return r;
}

int
fs_delete_file(const char *path) {
  begin_op();
  char buf[128];
  strncpy(buf, path, sizeof(buf));
  buf[sizeof(buf)-1] = 0;
  char name[DIRSIZ];
  struct inode *dp = nameiparent(buf, name);
  if(dp == 0) {
    end_op();
    return -1;
  }
  ilock(dp);
  uint off;
  uint inum = dirlookup(dp, name, &off);
  if(inum == 0) {
    iunlockput(dp);
    end_op();
    return -1;
  }
  struct inode *ip = iget(dp->dev, inum);
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  struct dirent de = {0};
  writei(dp, (uint64)&de, off, sizeof(de));
  iunlockput(ip);
  iunlockput(dp);
  end_op();
  return 0;
}

int
fs_file_size(const char *path) {
  begin_op();
  char buf[128];
  strncpy(buf, path, sizeof(buf));
  buf[sizeof(buf)-1] = 0;
  struct inode *ip = namei(buf);
  if(ip == 0) {
    end_op();
    return -1;
  }
  ilock(ip);
  int sz = ip->size;
  iunlockput(ip);
  end_op();
  return sz;
}

void
fs_test_samples(void) {
  const char *name = "/fs_hello";
  const char *msg = "Hello, filesystem!";
  char buf[64];

  if(fs_write_file(name, msg, strlen(msg)) != (int)strlen(msg))
    panic("fs_test write");
  memset(buf, 0, sizeof(buf));
  if(fs_read_file(name, buf, sizeof(buf)) < 0)
    panic("fs_test read");
  if(strncmp(msg, buf, strlen(msg)) != 0)
    panic("fs_test cmp");
  if(fs_delete_file(name) < 0)
    panic("fs_test delete");
}

void
fs_force_recovery(void) {
  log_force_recover();
}

static int
count_free_blocks(void) {
  int free = 0;
  for(uint b = 0; b < sb.nblocks; b += BPB) {
    struct buf *bp = bread(0, BBLOCK(b, sb));
    for(int bi = 0; bi < BPB && b + bi < sb.nblocks; bi++) {
      int m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0)
        free++;
    }
    brelse(bp);
  }
  return free;
}

static int
count_free_inodes(void) {
  int free = 0;
  for(uint inum = 1; inum < sb.ninodes; ) {
    struct buf *bp = bread(0, IBLOCK(inum, sb));
    struct dinode *dip = (struct dinode*)bp->data;
    for(uint i = 0; i < IPB && inum < sb.ninodes; i++, inum++) {
      if(dip[i].type == 0)
        free++;
    }
    brelse(bp);
  }
  return free;
}

int
fs_get_usage_stats(struct fs_usage_stats *stats) {
  if(stats == 0)
    return -1;
  stats->total_blocks = sb.size;
  stats->data_blocks = sb.nblocks;
  stats->free_blocks = count_free_blocks();
  stats->total_inodes = sb.ninodes;
  stats->free_inodes = count_free_inodes();
  return 0;
}

int
fs_collect_inode_usage(struct fs_inode_usage *entries, int max_entries) {
  if(entries == 0 || max_entries <= 0)
    return 0;
  acquire(&icache.lock);
  int count = 0;
  for(int i = 0; i < NINODE && count < max_entries; i++) {
    struct inode *ip = &icache.inode[i];
    if(ip->ref > 0) {
      entries[count].inum = ip->inum;
      entries[count].ref = ip->ref;
      entries[count].type = ip->type;
      entries[count].size = ip->size;
      count++;
    }
  }
  release(&icache.lock);
  return count;
}

void
stati(struct inode *ip, struct stat *st) {
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
