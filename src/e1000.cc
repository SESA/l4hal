#include <l4/sigma0.h>
#include <arch/sysio.h>
#include <l4hal/pci.h>
#include <l4hal/e1000.h>
#include <l4hal/e1000_api.h>
#include <l4/kdebug.h>


#define NUM_TX_DESC (128)
#define NUM_RX_DESC (128)

static struct e1000_hw hw_s;
static u8 e1000_base[1 << 18];
static PciConfig p;
static struct e1000_tx_desc tx_ring[NUM_TX_DESC] __attribute__ ((aligned(4096)));
static struct e1000_rx_desc rx_ring[NUM_RX_DESC] __attribute__ ((aligned(4096)));

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
  hw_s.io_base = pciConfigRead32(E1000_BUS,E1000_DEVICE,E1000_FUNC,0x20);
  hw_s.io_base &= ~0x1;

  //end mapdevice
  int irq = pciConfigRead8(E1000_BUS,E1000_DEVICE,E1000_FUNC,0x3c);
  printf("irq = %d\n", irq);

  hw_s.vendor_id = pciConfigRead16(E1000_BUS,E1000_DEVICE,E1000_FUNC,0);
  hw_s.device_id = pciConfigRead16(E1000_BUS,E1000_DEVICE,E1000_FUNC,2);
  hw_s.revision_id = pciConfigRead8(E1000_BUS,E1000_DEVICE,E1000_FUNC,8);
  hw_s.subsystem_vendor_id = pciConfigRead16(E1000_BUS,E1000_DEVICE,E1000_FUNC,0x2c);
  hw_s.subsystem_device_id = pciConfigRead16(E1000_BUS,E1000_DEVICE,E1000_FUNC,0x2e);
  hw_s.bus.pci_cmd_word = pciConfigRead16(E1000_BUS,E1000_DEVICE,E1000_FUNC,0x4);

  hw_s.fc.requested_mode = e1000_fc_default;

  if (e1000_setup_init_funcs(&hw_s, FALSE)) {
    printf("Hardware Init failure\n");
    return;
  }

  //alloc rings

  //irq disable
  E1000_WRITE_REG(&hw_s, E1000_IMC, ~0);
  E1000_WRITE_FLUSH(&hw_s);
  
  e1000_init_mac_params(&hw_s);
  e1000_init_nvm_params(&hw_s);
  e1000_init_phy_params(&hw_s);

  e1000_get_bus_info(&hw_s);

  e1000_init_script_state_82541(&hw_s, TRUE);
  e1000_set_tbi_compatibility_82543(&hw_s, TRUE);
  
  hw_s.phy.autoneg_wait_to_complete = FALSE;
  hw_s.mac.adaptive_ifs = TRUE;

  if (hw_s.phy.media_type == e1000_media_type_copper) {
    hw_s.phy.mdix = 0;
    hw_s.phy.disable_polarity_correction = FALSE;
    hw_s.phy.ms_type = e1000_ms_hw_default;
  }

  if (e1000_check_reset_block(&hw_s)) {
    printf("PHY reset is blocked due to SOL/IDER session.\n");
    return;
  }

  e1000_reset_hw(&hw_s);
  
  if (e1000_validate_nvm_checksum(&hw_s) < 0) {
    printf("The NVM Checksum is Not Valid\n");
    return;
  }

  if (e1000_read_mac_addr(&hw_s)) {
    printf("NVM Read Error\n");
    return;
  }
  
  printf("Mac address = ");
  for (i = 0; i < 6; i++) {
    if (i > 0)
      printf(":");
    printf("%.02X", hw_s.mac.addr[i]);
  }
  printf("\n");

  e1000_reset();
  e1000_power_up_phy(&hw_s);
  e1000_configure_tx();
  e1000_send_pkt();
}

void e1000_reset() {
  u16 hwm;

  E1000_WRITE_REG(&hw_s, E1000_PBA, 0x30);

  hwm = ((0x30 << 10) * 9 / 10);

  hw_s.fc.high_water = hwm * 0xFFF8;
  hw_s.fc.low_water = hw_s.fc.high_water - 8;

  hw_s.fc.pause_time = 0x680;
  hw_s.fc.send_xon = TRUE;
  hw_s.fc.current_mode = hw_s.fc.requested_mode;

  e1000_reset_hw(&hw_s);

  E1000_WRITE_REG(&hw_s, E1000_WUC, 0);

  if (e1000_init_hw(&hw_s)) {
    printf("Hardware Error\n");
  }

  if (hw_s.mac.type >= e1000_82544 &&
      hw_s.mac.type <= e1000_82547_rev_2 &&
      hw_s.mac.autoneg == 1 &&
      hw_s.phy.autoneg_advertised == ADVERTISE_1000_FULL) {
    u32 ctrl = E1000_READ_REG(&hw_s, E1000_CTRL);
    /* clear phy power management bit if we are in gig only mode,
     * which if enabled will attempt negotiation to 100Mb, which
     * can cause a loss of link at power off or driver unload */
    ctrl &= ~E1000_CTRL_SWDPIN3;
    E1000_WRITE_REG(&hw_s, E1000_CTRL, ctrl);
  }

  e1000_reset_adaptive(&hw_s);
  e1000_get_phy_info(&hw_s);
  
}

void e1000_configure_tx() {
  u32 tctl;
  int i;

  for(i = 0; i < NUM_TX_DESC; i++) {
    tx_ring[i].buffer_addr = 0;
    tx_ring[i].lower.data = 0;
    tx_ring[i].upper.data = 0;
  }

  E1000_WRITE_REG(&hw_s, E1000_TDBAL(0), (u32)tx_ring);

  E1000_WRITE_REG(&hw_s, E1000_TDBAH(0), 0);
  
  E1000_WRITE_REG(&hw_s, E1000_TDLEN(0), sizeof(tx_ring));


  E1000_WRITE_REG(&hw_s, E1000_TDH(0), 0);
  E1000_WRITE_REG(&hw_s, E1000_TDT(0), 0);

  E1000_WRITE_REG(&hw_s, E1000_TIPG,
		  DEFAULT_82543_TIPG_IPGT_COPPER |
		  (DEFAULT_82543_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT) |
		  (DEFAULT_82543_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT));

  E1000_WRITE_REG(&hw_s, E1000_TIDV, 32);
  E1000_WRITE_REG(&hw_s, E1000_TADV, 128);

  tctl = E1000_READ_REG(&hw_s, E1000_TCTL);

  tctl &= ~E1000_TCTL_CT;
  tctl |= E1000_TCTL_EN | E1000_TCTL_PSP |
    (E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);

  e1000_config_collision_dist(&hw_s);

  E1000_WRITE_REG(&hw_s, E1000_TCTL, tctl);
}

void e1000_send_pkt() {
  struct e1000_tx_desc *tx_desc;
  u8 vals[64];
  int i;
   
  for (i = 6; i < 12; i++) {
    vals[i] = hw_s.mac.addr[i];
  }
  vals[0] = 0x00;
  vals[1] = 0x1d;
  vals[2] = 0x09;
  vals[3] = 0x17;
  vals[4] = 0xa8;
  vals[5] = 0xd3;
  ((u16 *)vals)[6] = 0x88b5;
  for (i = 14; i < 70; i++) {
    vals[i] = 0x0F;
  }

  i = E1000_READ_REG(&hw_s, E1000_TDT(0));

  tx_desc = &tx_ring[i];
  tx_desc->buffer_addr = (u64)vals;
  
  tx_desc->lower.flags.length = 70;
  tx_desc->lower.flags.cso = 0;
  tx_desc->lower.data |= E1000_TXD_CMD_IFCS |
    E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
  
  tx_desc->upper.data = 0;
  
  E1000_WRITE_REG(&hw_s, E1000_TDT(0), ((i+1)%NUM_TX_DESC));

  do {
  } while (!(tx_ring[i].upper.fields.status & E1000_TXD_STAT_DD));
  printf("packet sent\n");
}

extern "C" void
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

extern "C" void
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

extern "C" void
e1000_read_pci_cfg(struct e1000_hw *hw, u32 reg, u16 *value)
{
  PciConfig *pci_config = (PciConfig *)hw->back;
  *value = pciConfigRead16(pci_config->bus,
			   pci_config->slot,
			   pci_config->func,
			   reg);
}

extern "C" void
e1000_write_pci_cfg(struct e1000_hw *hw, u32 reg, u16 *value)
{
  PciConfig *pci_config = (PciConfig *)hw->back;
  pciConfigWrite16(pci_config->bus,
		   pci_config->slot,
		   pci_config->func,
		   reg,
		   *value);
}

extern "C" s32 
e1000_read_pcie_cap_reg(struct e1000_hw *hw, u32 reg, u16 *value) {
  printf("UNIMPLEMENTED\n");
  return 0;
}

// u32
// e1000_io_read(struct e1000_hw *hw, u32 port)
// {
//   return sysIn32(port);
// }

// void
// e1000_io_write(struct e1000_hw *hw, u32 port, u32 value)
// {
//   sysOut32(port, value);
// }
