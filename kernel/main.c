#include "defs.h"
#include "uart.h"

void main(void) {
  console_init();
  console_puts("\nHello, OS!\n");
  char line[128];
  while(1) {
    console_puts("$ ");
    int n = consoleread(line, sizeof(line) - 1);
    if(n <= 0)
      continue;
    line[n] = 0;
    if(n > 0 && (line[n-1] == '\n' || line[n-1] == '\r'))
      line[n-1] = 0;
    /* simple trim leading spaces */
    char *p = line;
    while(*p == ' ')
      p++;
    if(*p == 0)
      continue;
    if(p[0]=='t'&&p[1]=='e'&&p[2]=='s'&&p[3]=='t'&&p[4]==0) {
      console_puts("[TEST] running printf tests...\n");
      test_printf_basic();
      test_printf_edge_cases();
      console_puts("[PASS] tests finished\n");
    } else if(p[0]=='c'&&p[1]=='l'&&p[2]=='e'&&p[3]=='a'&&p[4]=='r'&&p[5]==0) {
      clear_screen();
      console_puts("\n");
    } else if(p[0]=='h'&&p[1]=='e'&&p[2]=='l'&&p[3]=='p'&&p[4]==0) {
      console_puts("Commands: test | clear | help\n");
    } else {
      console_puts("Unknown command. Try 'help'.\n");
    }
  }
}



