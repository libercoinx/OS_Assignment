#include "defs.h"
#include "fs.h"
#include "fcntl.h"
#include "proc.h"
#include "vm.h"
#include "string.h"

static int
fdalloc(struct proc *p, struct file *f) {
  for(int fd = 0; fd < NOFILE; fd++) {
    if(p->ofile[fd] == 0) {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

static int
copy_from_user(struct proc *p, uint64 src, char *dst, int len) {
  if(p->pagetable == 0) {
    memmove(dst, (void *)src, len);
    return 0;
  }
  return copyin(p->pagetable, dst, src, len);
}

static int
copy_to_user(struct proc *p, uint64 dst, char *src, int len) {
  if(p->pagetable == 0) {
    memmove((void *)dst, src, len);
    return 0;
  }
  return copyout(p->pagetable, dst, src, len);
}

uint64
sys_dup(void) {
  int fd;
  if(argint(0, &fd) < 0)
    return -1;
  struct proc *p = myproc();
  if(fd < 0 || fd >= NOFILE || p->ofile[fd] == 0)
    return -1;
  struct file *f = filedup(p->ofile[fd]);
  int newfd = fdalloc(p, f);
  if(newfd < 0) {
    fileclose(f);
    return -1;
  }
  return newfd;
}

uint64
sys_read(void) {
  int fd, n;
  uint64 dst;
  if(argint(0, &fd) < 0 || argaddr(1, &dst) < 0 || argint(2, &n) < 0)
    return -1;
  if(n < 0)
    return -1;
  struct proc *p = myproc();
  if(fd < 0 || fd >= NOFILE)
    return -1;
  struct file *f = p->ofile[fd];
  if(f == 0)
    return -1;

  char buf[BSIZE];
  int total = 0;
  while(total < n) {
    int m = MIN(n - total, (int)sizeof(buf));
    int r = fileread(f, buf, m);
    if(r < 0)
      return total ? total : -1;
    if(r == 0)
      break;
    if(copy_to_user(p, dst + total, buf, r) < 0)
      return -1;
    total += r;
    if(r != m)
      break;
  }
  return total;
}

uint64
sys_write(void) {
  int fd, n;
  uint64 src;
  if(argint(0, &fd) < 0 || argaddr(1, &src) < 0 || argint(2, &n) < 0)
    return -1;
  if(n < 0)
    return -1;
  struct proc *p = myproc();

  // allow stdout/stderr even if not explicitly opened
  if((fd == 1 || fd == 2) && (fd >= NOFILE || p->ofile[fd] == 0)) {
    char tmp[128];
    int written = 0;
    while(written < n) {
      int m = MIN(n - written, (int)sizeof(tmp));
      if(copy_from_user(p, src + written, tmp, m) < 0)
        return -1;
      for(int i = 0; i < m; i++)
        console_putc(tmp[i]);
      written += m;
    }
    return written;
  }

  if(fd < 0 || fd >= NOFILE)
    return -1;
  struct file *f = p->ofile[fd];
  if(f == 0)
    return -1;

  char buf[BSIZE];
  int total = 0;
  while(total < n) {
    int m = MIN(n - total, (int)sizeof(buf));
    if(copy_from_user(p, src + total, buf, m) < 0)
      return -1;
    int r = filewrite(f, buf, m);
    if(r < 0)
      return total ? total : -1;
    total += r;
    if(r != m)
      break;
  }
  return total;
}

uint64
sys_close(void) {
  int fd;
  if(argint(0, &fd) < 0)
    return -1;
  struct proc *p = myproc();
  if(fd < 0 || fd >= NOFILE)
    return -1;
  struct file *f = p->ofile[fd];
  if(f == 0)
    return -1;
  p->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void) {
  int fd;
  uint64 addr;
  if(argint(0, &fd) < 0 || argaddr(1, &addr) < 0)
    return -1;
  struct proc *p = myproc();
  if(fd < 0 || fd >= NOFILE)
    return -1;
  struct file *f = p->ofile[fd];
  if(f == 0)
    return -1;
  struct stat st;
  if(filestat(f, &st) < 0)
    return -1;
  if(copy_to_user(p, addr, (char *)&st, sizeof(st)) < 0)
    return -1;
  return 0;
}

uint64
sys_open(void) {
  char path[128];
  int omode;
  if(argstr(0, path, sizeof(path)) < 0 || argint(1, &omode) < 0)
    return -1;

  struct file *f;
  struct inode *ip;

  begin_op();

  if(omode & O_CREATE) {
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0) {
      end_op();
      return -1;
    }
  } else {
    ip = namei(path);
    if(ip == 0) {
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY) {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if((f = filealloc()) == 0) {
    iunlockput(ip);
    end_op();
    return -1;
  }
  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  int fd = fdalloc(myproc(), f);
  if(fd < 0) {
    fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  iunlock(ip);
  end_op();
  return fd;
}

uint64
sys_chdir(void) {
  char path[128];
  if(argstr(0, path, sizeof(path)) < 0)
    return -1;
  struct proc *p = myproc();
  struct inode *ip = namei(path);
  if(ip == 0)
    return -1;
  ilock(ip);
  if(ip->type != T_DIR) {
    iunlockput(ip);
    return -1;
  }
  iunlock(ip);
  if(p->cwd)
    iput(p->cwd);
  p->cwd = ip;
  return 0;
}

uint64
sys_mkdir(void) {
  char path[128];
  if(argstr(0, path, sizeof(path)) < 0)
    return -1;
  begin_op();
  struct inode *ip = create(path, T_DIR, 0, 0);
  if(ip == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void) {
  char path[128];
  int major, minor;
  if(argstr(0, path, sizeof(path)) < 0 || argint(1, &major) < 0 || argint(2, &minor) < 0)
    return -1;
  begin_op();
  struct inode *ip = create(path, T_DEV, major, minor);
  if(ip == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_unlink(void) {
  char path[128];
  if(argstr(0, path, sizeof(path)) < 0)
    return -1;
  return fs_delete_file(path);
}
