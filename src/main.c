#include <l4io.h>
#include <l4/kdebug.h>

int main (void) {
  printf("Hello World!\n");
  for (;;)
    L4_KDB_Enter("EOW");
}
