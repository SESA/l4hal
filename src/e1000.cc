#include <l4/sigma0.h>
#include <arch/sysio.h>
#include <l4hal/pci.h>
#include <l4hal/e1000_hw.h>
#include <l4hal/e1000.h>

static struct e1000_hw hw_s;
static u8 e1000_base[1 << 18];
static PciConfig p;
static struct e1000_tx_desc tx_ring[256] __attribute__ ((aligned(4096)));

void dump_cntrs(void);

void
e1000_init() {
  int i;
  L4_Fpage_t request_fpage, rcv_fpage, result_fpage;
  u8 *e1000_base2;

  //config
  p.bus = E1000_BUS;
  p.slot = E1000_DEVICE;
  p.func = E1000_FUNC;
  
  hw_s.back = &p;
  hw_s.hw_addr = (u8 *)pciConfigRead32(E1000_BUS,E1000_DEVICE,E1000_FUNC,16);
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
  hw_s.vendor_id = pciConfigRead16(E1000_BUS,E1000_DEVICE,E1000_FUNC,0);
  hw_s.device_id = pciConfigRead16(E1000_BUS,E1000_DEVICE,E1000_FUNC,2);
  hw_s.revision_id = pciConfigRead8(E1000_BUS,E1000_DEVICE,E1000_FUNC,8);
  hw_s.subsystem_vendor_id = pciConfigRead16(E1000_BUS,E1000_DEVICE,E1000_FUNC,0x2c);
  hw_s.subsystem_id = pciConfigRead16(E1000_BUS,E1000_DEVICE,E1000_FUNC,0x2e);
  hw_s.pci_cmd_word = pciConfigRead16(E1000_BUS,E1000_DEVICE,E1000_FUNC,0x4);
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
  int error_code;
  E1000_WRITE_REG(&hw_s, PBA, 0x30);
  hw_s.fc = hw_s.original_fc;
  e1000_reset_hw(&hw_s);
  E1000_WRITE_REG(&hw_s, WUC, 0);
  error_code = e1000_init_hw(&hw_s);
  printf("init_hw error code: %d\n", error_code);
  e1000_reset_adaptive(&hw_s);
//   E1000_WRITE_REG(&hw_s, TXDCTL, 0x01000000);
  printf("TXDCTL = 0x%08x\n",
	 E1000_READ_REG(&hw_s, TXDCTL));
  printf("RXDCTL = 0x%08x\n",
	 E1000_READ_REG(&hw_s, RXDCTL));
}


void e1000_configure_tx() {
  u32 tctl;
  int i;

  for(i = 0; i < 256; i++) {
    tx_ring[i].buffer_low = 0;
    tx_ring[i].buffer_high = 0;
    tx_ring[i].lower.data = 0;
    tx_ring[i].upper.data = 0;
  }
  
//   E1000_WRITE_REG(&hw_s, TDBAL, (u32)tx_ring);
  E1000_WRITE_REG(&hw_s, TDBAL, 0xFFF00000);
  E1000_WRITE_REG(&hw_s, TDBAH, 0);
  
  printf("tdlen = %d\n", sizeof(tx_ring));
  E1000_WRITE_REG(&hw_s, TDLEN, sizeof(tx_ring));


  E1000_WRITE_REG(&hw_s, TDH, 0);
  E1000_WRITE_REG(&hw_s, TDT, 0);

  E1000_WRITE_REG(&hw_s, TIPG,
		  DEFAULT_82543_TIPG_IPGT_COPPER |
		  (DEFAULT_82543_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT) |
		  (DEFAULT_82543_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT));

  E1000_WRITE_REG(&hw_s, TIDV, 32);
  E1000_WRITE_REG(&hw_s, TADV, 128);

//   E1000_WRITE_REG(&hw_s, RCTL, 
// 		  E1000_RCTL_EN |
// 		  E1000_RCTL_SBP |
// 		  E1000_RCTL_UPE);		

  tctl = E1000_READ_REG(&hw_s, TCTL);

  tctl &= ~E1000_TCTL_CT;
  tctl |= E1000_TCTL_EN | E1000_TCTL_PSP |
    (E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);

  E1000_WRITE_REG(&hw_s, TCTL, tctl);
  printf("TCL = 0x%08x\n",
	 E1000_READ_REG(&hw_s, TCTL));

  E1000_WRITE_REG(&hw_s, TDT, 1);

  e1000_config_collision_dist(&hw_s);
}

#define DUMPREG(reg) \
  ({volatile uint32_t temp;			\
  temp = E1000_READ_REG(&hw_s, reg);		\
  if (temp != 0)				\
    printf(#reg " = %d\n", temp);		\
  })

void dump_cntrs() {
    DUMPREG(CRCERRS);
    DUMPREG(SYMERRS);
    DUMPREG(MPC);
    DUMPREG(SCC);
    DUMPREG(ECOL);
    DUMPREG(MCC);
    DUMPREG(LATECOL);
    DUMPREG(COLC);
    DUMPREG(DC);
    DUMPREG(SEC);
    DUMPREG(RLEC);
    DUMPREG(XONRXC);
    DUMPREG(XONTXC);
    DUMPREG(XOFFRXC);
    DUMPREG(XOFFTXC);
    DUMPREG(FCRUC);
    DUMPREG(PRC64);
    DUMPREG(PRC127);
    DUMPREG(PRC255);
    DUMPREG(PRC511);
    DUMPREG(PRC1023);
    DUMPREG(PRC1522);
    DUMPREG(GPRC);
    DUMPREG(BPRC);
    DUMPREG(MPRC);
    DUMPREG(GPTC);
    DUMPREG(GORCL);
    DUMPREG(GORCH);
    DUMPREG(GOTCL);
    DUMPREG(GOTCH);
    DUMPREG(RNBC);
    DUMPREG(RUC);
    DUMPREG(RFC);
    DUMPREG(ROC);
    DUMPREG(RJC);
    DUMPREG(TORL);
    DUMPREG(TORH);
    DUMPREG(TOTL);
    DUMPREG(TOTH);
    DUMPREG(TPR);
    DUMPREG(TPT);
    DUMPREG(PTC64);
    DUMPREG(PTC127);
    DUMPREG(PTC255);
    DUMPREG(PTC511);
    DUMPREG(PTC1023);
    DUMPREG(PTC1522);
    DUMPREG(MPTC);
    DUMPREG(BPTC);
    DUMPREG(ALGNERRC);
    DUMPREG(RXERRC);
    DUMPREG(TNCRS);
    DUMPREG(CEXTERR);
    DUMPREG(TSCTC);
    DUMPREG(TSCTFC);
    DUMPREG(MGTPRC);
    DUMPREG(MGTPDC);
    DUMPREG(MGTPTC);
}  

void e1000_send_pkt() {
  struct e1000_tx_desc *tx_desc;
  u8 vals[60];
  int i, val;
  
//   e1000_setup_led(&hw_s);
//   do {
//     e1000_led_on(&hw_s);
//     msec_delay(10);
//     e1000_led_off(&hw_s);
//   } while(1);
  dump_cntrs();

  val = E1000_READ_REG(&hw_s, STATUS);
  printf("STATUS = 0x%X\n", val);
  
  for (i = 0; i < 6; i++) {
    vals[i] = hw_s.mac_addr[i];
    vals[i+6] = hw_s.mac_addr[i];
  }
  ((u16 *)vals)[6] = 0x88b5;
  for (i = 14; i < 60; i++) {
    vals[i] = 0x0F;
  }

  for(i = 0; i < 8; i++) {
    tx_desc = &tx_ring[i];
    tx_desc->buffer_high = 0;
    tx_desc->buffer_low = (u32)vals;
    
    tx_desc->lower.flags.length = 60;
    tx_desc->lower.flags.cso = 0;
    tx_desc->lower.flags.cmd |= E1000_TXD_CMD_IFCS |
      E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
    
    E1000_WRITE_REG(&hw_s, TDT, i);
  }

  printf("packet should have been sent\n");

  do {
    i = E1000_READ_REG(&hw_s, TDH);
    if (i != 0) {
      printf("TDH = %d\n", i);
    }
    i = E1000_READ_REG(&hw_s, STATUS);
    if (i != val) {
      printf("STATUS = 0x%X\n", i);
      val = i;
    }
    dump_cntrs();
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
