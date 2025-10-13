#include "panic.h"
#include "defs.h"

void panic(const char *msg) {
  printf("panic: %s\n", msg);
  for(;;) {
    /* spin */
  }
}

