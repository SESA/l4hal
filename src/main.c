#include <l4io.h>
#include <l4/kdebug.h>
#include "pci.h"

int main (void) {
  unsigned short vendor;
  unsigned short device;
  int i;

  printf("Hello World!\n");


  printf("PCI Devices:\n");
  do {
    vendor = pciConfigRead16(0,i,0,0);
    if (vendor == 0xFFFF)
      break;
    printf("PCI Device #%d:\n", i);
    printf("vendor_id = 0x%hx, device_id = 0x%hx\n",
	   vendor, pciConfigRead16(0,i,0,2));
    printf("Header type = 0x%x\n", pciConfigRead8(0,i,0,14));
    
    i++;
  } while (1);
  for (;;)
    L4_KDB_Enter("EOW");
}
