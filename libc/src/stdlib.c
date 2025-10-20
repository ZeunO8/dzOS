#include "stdlib.h"
#include "usyscalls.h"
#include <stdint.h>

// Entry point: extract argc/argv from stack per SysV AMD64 ABI
void _start(void) {
  extern int main(int, char**);
  uint64_t *sp;
  __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
  int argc = (int)sp[0];
  char **argv = (char**)&sp[1];
  exit(main(argc, argv));
}

int abs(int a) { return a > 0 ? a : -a; }

int atoi(const char *s) {
  int n = 0;
  while ('0' <= *s && *s <= '9')
    n = n * 10 + *s++ - '0';
  return n;
}
