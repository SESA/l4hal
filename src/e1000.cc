#include <l4/sigma0.h>
#include <arch/sysio.h>
#include <l4hal/pci.h>
#include <l4hal/e1000_hw.h>
#include <l4hal/e1000.h>

static struct e1000_hw hw_s;
static u8 e1000_base[1 << 18];
static PciConfig p;
static struct e1000_tx_desc tx_ring[8] __attribute__ ((aligned(16)));

void
e1000_init() {
  int i;
  L4_Fpage_t request_fpage, rcv_fpage, result_fpage;
  u8 *e1000_base2;

  //config
  p.bus = 2;
  p.slot = 0;
  p.func = 0;
  
  hw_s.back = &p;
  hw_s.hw_addr = (u8 *)pciConfigRead32(2,0,0,16);
  //begin mapdevice
  request_fpage = L4_Fpage((u32)hw_s.hw_addr, 1 << 17);
  e1000_base2 = &e1000_base[1 << 17];
  e1000_base2 = (u8 *)(((u32)e1000_base2) & ~((1 << 17) - 1));
  rcv_fpage = L4_Fpage((u32)e1000_base2, 1 << 17);
  L4_Set_Rights(&request_fpage, L4_Readable | L4_Writable);
  L4_Set_Rights(&rcv_fpage, L4_Readable | L4_Writable);
  result_fpage = L4_Sigma0_GetPage_RcvWindow(L4_nilthread, request_fpage,
					     rcv_fpage, 0);
  hw_s.hw_addr = (u8 *)L4_Address(rcv_fpage);
  //end mapdevice
  hw_s.vendor_id = pciConfigRead16(2,0,0,0);
  hw_s.device_id = pciConfigRead16(2,0,0,2);
  hw_s.revision_id = pciConfigRead8(2,0,0,8);
  hw_s.subsystem_vendor_id = pciConfigRead16(2,0,0,0x2c);
  hw_s.subsystem_id = pciConfigRead16(2,0,0,0x2e);
  hw_s.pci_cmd_word = pciConfigRead16(2,0,0,0x4);
  hw_s.max_frame_size = 1500 + ENET_HEADER_SIZE + 
    ETHERNET_FCS_SIZE;
  hw_s.min_frame_size = MINIMUM_ETHERNET_FRAME_SIZE;
  e1000_set_mac_type(&hw_s);
  hw_s.fc_high_water = 0xA9C8;
  hw_s.fc_low_water = 0xA9C0;
  hw_s.fc_pause_time = 0x0680;
  hw_s.fc_send_xon = TRUE;
  hw_s.dma_fairness = TRUE;
  hw_s.media_type = e1000_media_type_copper;
  hw_s.report_tx_early = TRUE;
  hw_s.wait_autoneg_complete = FALSE;
  hw_s.tbi_compatibility_en = FALSE;
  hw_s.adaptive_ifs = TRUE;

  hw_s.mdix = 0;
  hw_s.disable_polarity_correction = FALSE;
  if (e1000_validate_eeprom_checksum(&hw_s) < 0) {
    printf("invalid eeprom,\n");
  }
  e1000_read_mac_addr(&hw_s);
  printf("Mac address = ");
  for (i = 0; i < NODE_ADDRESS_SIZE; i++) {
    if (i > 0)
      printf(":");
    printf("%.02X", hw_s.mac_addr[i]);
  }
  printf("\n");
  e1000_get_bus_info(&hw_s);
  hw_s.fc = hw_s.original_fc = e1000_fc_full;
  hw_s.autoneg = TRUE;
  hw_s.autoneg_advertised = AUTONEG_ADVERTISE_SPEED_DEFAULT;
  //reset here  
  e1000_reset();
  e1000_configure_tx();
  e1000_send_pkt();
}

void e1000_reset() {
  E1000_WRITE_REG(&hw_s, PBA, 0x30);
  hw_s.fc = hw_s.original_fc;
  e1000_reset_hw(&hw_s);
  E1000_WRITE_REG(&hw_s, WUC, 0);
  e1000_init_hw(&hw_s);
  e1000_reset_adaptive(&hw_s);
  E1000_WRITE_REG(&hw_s, TXDCTL, 0x01000000);
  printf("TXDCTL = 0x%08x\n",
	 E1000_READ_REG(&hw_s, TXDCTL));
  printf("RXDCTL = 0x%08x\n",
	 E1000_READ_REG(&hw_s, RXDCTL));
}


void e1000_configure_tx() {
  u32 tctl;
  int i;

  for(i = 0; i < 8; i++) {
    tx_ring[i].buffer_low = 0;
    tx_ring[i].buffer_high = 0;
    tx_ring[i].lower.data = 0;
    tx_ring[i].upper.data = 0;
  }
  
  E1000_WRITE_REG(&hw_s, TDBAL, (u32)tx_ring);
  E1000_WRITE_REG(&hw_s, TDBAH, 0);
  
  E1000_WRITE_REG(&hw_s, TDLEN, sizeof(tx_ring));


  E1000_WRITE_REG(&hw_s, TDH, 0);
  E1000_WRITE_REG(&hw_s, TDT, 0);

  E1000_WRITE_REG(&hw_s, TIPG,
		  DEFAULT_82543_TIPG_IPGT_COPPER |
		  (DEFAULT_82543_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT) |
		  (DEFAULT_82543_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT));

  E1000_WRITE_REG(&hw_s, TIDV, 32);
  E1000_WRITE_REG(&hw_s, TADV, 128);

  tctl = E1000_READ_REG(&hw_s, TCTL);

  tctl &= ~E1000_TCTL_CT;
  tctl |= E1000_TCTL_EN | E1000_TCTL_PSP |
    (E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);

  E1000_WRITE_REG(&hw_s, TCTL, tctl);
  printf("TCL = 0x%08x\n",
	 E1000_READ_REG(&hw_s, TCTL));

  e1000_config_collision_dist(&hw_s);
}

void e1000_send_pkt() {
  struct e1000_tx_desc *tx_desc;
  u32 vals[16];
  int i;

  for (i = 0; i < 16; i++) {
    vals[i] = 0xdeadbeef;
  }

  for(i = 0; i < 8; i++) {
    tx_desc = &tx_ring[i];
    tx_desc->buffer_high = 0;
    tx_desc->buffer_low = (u32)vals;
    
    tx_desc->lower.flags.length = 64;
    tx_desc->lower.flags.cso = 0;
    tx_desc->lower.flags.cmd |= E1000_TXD_CMD_IFCS |
      E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
    
    E1000_WRITE_REG(&hw_s, TDT, 16*i);
  }
  do {
  } while (!(tx_ring[0].upper.fields.status & E1000_TXD_STAT_DD));
  printf("packet sent\n");
}
     
void
e1000_pci_clear_mwi( struct e1000_hw *hw )
{
    PciConfig *pci_config = (PciConfig *)hw->back;
    u16 command = pciConfigRead16 (pci_config->bus,
				   pci_config->slot,
				   pci_config->func,
				   0x04);
    command &= ~0x10;
    pciConfigWrite16 (pci_config->bus,
		      pci_config->slot,
		      pci_config->func,
		      0x04,
		      command);
}

void
e1000_pci_set_mwi( struct e1000_hw *hw )
{
  PciConfig *pci_config = (PciConfig *)hw->back;
  u16 command = pciConfigRead16 (pci_config->bus,
				 pci_config->slot,
				 pci_config->func,
				 0x04);
  command |= 0x10;
  pciConfigWrite16 (pci_config->bus,
		    pci_config->slot,
		    pci_config->func,
		    0x04,
		    command);
}

void
e1000_read_pci_cfg(struct e1000_hw *hw, u32 reg, u16 *value)
{
  PciConfig *pci_config = (PciConfig *)hw->back;
  *value = pciConfigRead16(pci_config->bus,
			   pci_config->slot,
			   pci_config->func,
			   reg);
}

void
e1000_write_pci_cfg(struct e1000_hw *hw, u32 reg, u16 *value)
{
  PciConfig *pci_config = (PciConfig *)hw->back;
  pciConfigWrite16(pci_config->bus,
		   pci_config->slot,
		   pci_config->func,
		   reg,
		   *value);
}

u32
e1000_io_read(struct e1000_hw *hw, u32 port)
{
  return sysIn32(port);
}

void
e1000_io_write(struct e1000_hw *hw, u32 port, u32 value)
{
  sysOut32(port, value);
}
