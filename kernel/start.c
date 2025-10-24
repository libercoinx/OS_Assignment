#include <stdint.h>
#include "defs.h"
#include "trap.h"

extern void main(void);

void start(void){
  console_init();
  trap_init();
  main();
}
