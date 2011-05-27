#include "sysio.h"

unsigned int sysIn32 (unsigned short port) {
  unsigned int ret;
  __asm__ volatile ("inl %w1,%0":"=a"(ret) : "Nd"(port));
  return ret;
}

void sysOut32 (unsigned short port, unsigned int val) {
  __asm__ volatile ("outl %0,%w1"::"a"(val), "Nd" (port));
}
