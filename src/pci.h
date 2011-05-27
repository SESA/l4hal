#ifndef _PCI_H_
#define _PCI_H_

unsigned char pciConfigRead8 (unsigned char bus, unsigned char slot,
			     unsigned short func, unsigned short offset);
unsigned short pciConfigRead16 (unsigned char bus, unsigned char slot,
			     unsigned short func, unsigned short offset);
unsigned int pciConfigRead32 (unsigned char bus, unsigned char slot,
			     unsigned short func, unsigned short offset);
#endif
