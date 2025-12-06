#pragma once

#include "riscv.h"
#include "stat.h"

#define BSIZE      1024            /* block size in bytes */
#define FSSIZE     1024            /* total blocks in ramdisk */
#define LOGSIZE    30              /* max log blocks */
#define NINODE     64              /* number of in-memory inodes */
#define NFILE      40              /* open files */
#define NBUF       32              /* buffer cache entries */

#define ROOTINO    1               /* root i-number */
#define DIRSIZ     14

/* On-disk layout */
// [boot block][super block][log][inode blocks][free bitmap][data blocks]

struct superblock {
  uint magic;       /* must be FSMAGIC */
  uint size;        /* total blocks */
  uint nblocks;     /* data blocks */
  uint ninodes;     /* total inodes */
  uint nlog;        /* log blocks */
  uint logstart;    /* log start block */
  uint inodestart;  /* inode blocks start */
  uint bmapstart;   /* bitmap start block */
  uint datastart;   /* data region start block */
};

#define FSMAGIC 0x10203040

/* On-disk inode structure */
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXOPBLOCKS 10
#define MAXFILE (NDIRECT + NINDIRECT)

struct dinode {
  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

/* Inode types */
#define T_DIR  1
#define T_FILE 2
#define T_DEV  3

/* Directory entry */
struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

struct buf {
  int valid;
  int disk;       /* does disk "own" buf? */
  uint dev;
  uint blockno;
  struct buf *prev;
  struct buf *next;
  struct spinlock lock;
  int refcnt;
  uchar data[BSIZE];
};

struct inode {
  struct spinlock lock;
  int dev;
  int inum;
  int ref;
  int valid;

  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

enum filetype {
  FD_NONE,
  FD_INODE,
};

struct file {
  enum filetype type;
  int ref;
  char readable;
  char writable;
  struct inode *ip;
  uint off;
};

/* Helper macros */
#define IPB           (BSIZE / sizeof(struct dinode))
#define BPB           (BSIZE * 8)
#define IBLOCK(i, sb)     ((i) / IPB + (sb).inodestart)
#define BBLOCK(lbn, sb)   ((lbn) / BPB + (sb).bmapstart)

/* Filesystem interface */
void fs_init(void);
void begin_op(void);
void end_op(void);
void fs_test_samples(void);
void fs_force_recovery(void);

/* inode/file helpers exposed for tests */
int fs_write_file(const char *path, const char *data, int len);
int fs_read_file(const char *path, char *dst, int max);
int fs_delete_file(const char *path);
int fs_file_size(const char *path);

/* buffer cache */
void binit(void);
struct buf* bread(uint dev, uint blockno);
void bwrite(struct buf *b);
void brelse(struct buf *b);
void bpin(struct buf *b);
void bunpin(struct buf *b);

/* log */
void log_init(int dev, struct superblock *sb);
void log_write(struct buf *b);
void log_force_recover(void);

/* file table */
void fileinit(void);
struct file* filealloc(void);
struct file* filedup(struct file *f);
void fileclose(struct file *f);
int fileread(struct file *f, char *addr, int n);
int filewrite(struct file *f, char *addr, int n);
int filestat(struct file *f, struct stat *st);

struct fs_usage_stats {
  uint total_blocks;
  uint data_blocks;
  uint free_blocks;
  uint total_inodes;
  uint free_inodes;
};

struct fs_cache_counters {
  uint buffer_cache_hits;
  uint buffer_cache_misses;
  uint disk_read_count;
  uint disk_write_count;
};

struct fs_inode_usage {
  int inum;
  int ref;
  short type;
  uint size;
};

int fs_get_usage_stats(struct fs_usage_stats *stats);
void fs_get_cache_counters(struct fs_cache_counters *counters);
int fs_collect_inode_usage(struct fs_inode_usage *entries, int max_entries);

/* inode helpers */
struct inode* namei(char *path);
void ilock(struct inode *ip);
void iunlock(struct inode *ip);
void iupdate(struct inode *ip);
void iput(struct inode *ip);
void iunlockput(struct inode *ip);
struct inode* ialloc(uint dev, short type);
int readi(struct inode *ip, uint64 dst, uint off, uint n);
int writei(struct inode *ip, uint64 src, uint off, uint n);
int dirlink(struct inode *dp, const char *name, uint inum);
int dirlookup(struct inode *dp, const char *name, uint *poff);
struct inode* nameiparent(char *path, char *name);
struct inode* create(const char *path, short type, short major, short minor);
void stati(struct inode *ip, struct stat *st);
