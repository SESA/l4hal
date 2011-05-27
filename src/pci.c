#include "sysio.h"
#include "pci.h"

unsigned int pciConfigRead32 (unsigned char bus, unsigned char slot,     
				unsigned short func, unsigned short offset)
{
  unsigned int address;
  unsigned int lbus = (unsigned int)bus;
  unsigned int lslot = (unsigned int)slot;
  unsigned int lfunc = (unsigned int)func;
  unsigned int ret;
  
  /*  PCI CONFIG_ADDRESS FORMAT  */
  /* |   Bit |         Purpose | */
  /* --------------------------- */
  /* |    31 |      Enable Bit | */
  /* | 30-24 |        Reserved | */
  /* | 23-16 |      Bus Number | */
  /* | 15-11 |   Device Number | */
  /* |  10-8 | Function Number | */
  /* |   7-2 | Register Number | */
  /* |   1-0 |              00 | */
  address = (unsigned int)((lbus << 16) | (lslot << 11) |
			   (lfunc << 8) | (offset & 0xfc) |
			   ((unsigned int)0x80000000));
  
  /* write out the address */
  sysOut32((unsigned short)0xCF8, address);
  
  /* read in the data */
  ret = (unsigned int)sysIn32(0xCFC);
  return ret;
}

unsigned short pciConfigRead16 (unsigned char bus, unsigned char slot,     
				unsigned short func, unsigned short offset)
{
  unsigned int address;
  unsigned int lbus = (unsigned int)bus;
  unsigned int lslot = (unsigned int)slot;
  unsigned int lfunc = (unsigned int)func;
  unsigned short ret;
  
  /*  PCI CONFIG_ADDRESS FORMAT  */
  /* |   Bit |         Purpose | */
  /* --------------------------- */
  /* |    31 |      Enable Bit | */
  /* | 30-24 |        Reserved | */
  /* | 23-16 |      Bus Number | */
  /* | 15-11 |   Device Number | */
  /* |  10-8 | Function Number | */
  /* |   7-2 | Register Number | */
  /* |   1-0 |              00 | */
  address = (unsigned int)((lbus << 16) | (lslot << 11) |
			   (lfunc << 8) | (offset & 0xfc) |
			   ((unsigned int)0x80000000));
  
  /* write out the address */
  sysOut32((unsigned short)0xCF8, address);
  
  /* read in the data */
  ret = (unsigned short)((sysIn32(0xCFC) >> ((offset & 2) * 8)) & 0xffff);
  return ret;
}

unsigned char pciConfigRead8 (unsigned char bus, unsigned char slot,     
			       unsigned short func, unsigned short offset)
{
  unsigned int address;
  unsigned int lbus = (unsigned int)bus;
  unsigned int lslot = (unsigned int)slot;
  unsigned int lfunc = (unsigned int)func;
  unsigned char ret;
  
  /*  PCI CONFIG_ADDRESS FORMAT  */
  /* |   Bit |         Purpose | */
  /* --------------------------- */
  /* |    31 |      Enable Bit | */
  /* | 30-24 |        Reserved | */
  /* | 23-16 |      Bus Number | */
  /* | 15-11 |   Device Number | */
  /* |  10-8 | Function Number | */
  /* |   7-2 | Register Number | */
  /* |   1-0 |              00 | */
  address = (unsigned int)((lbus << 16) | (lslot << 11) |
			   (lfunc << 8) | (offset & 0xfc) |
			   ((unsigned int)0x80000000));
  
  /* write out the address */
  sysOut32((unsigned short)0xCF8, address);
  
  /* read in the data */
  ret = (unsigned char)((sysIn32(0xCFC) >> ((offset & 3) * 8) & 0xff));
  return ret;
}
