#pragma once

#include "riscv.h"

struct stat {
  int dev;
  uint ino;
  short type;
  short nlink;
  uint64 size;
};
