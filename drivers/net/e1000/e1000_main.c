/*******************************************************************************

  This software program is available to you under a choice of one of two
  licenses. You may choose to be licensed under either the GNU General Public
  License (GPL) Version 2, June 1991, available at
  http://www.fsf.org/copyleft/gpl.html, or the Intel BSD + Patent License, the
  text of which follows:
  
  Recipient has requested a license and Intel Corporation ("Intel") is willing
  to grant a license for the software entitled Linux Base Driver for the
  Intel(R) PRO/1000 Family of Adapters (e1000) (the "Software") being provided
  by Intel Corporation. The following definitions apply to this license:
  
  "Licensed Patents" means patent claims licensable by Intel Corporation which
  are necessarily infringed by the use of sale of the Software alone or when
  combined with the operating system referred to below.
  
  "Recipient" means the party to whom Intel delivers this Software.
  
  "Licensee" means Recipient and those third parties that receive a license to
  any operating system available under the GNU Public License version 2.0 or
  later.
  
  Copyright (c) 1999 - 2002 Intel Corporation.
  All rights reserved.
  
  The license is provided to Recipient and Recipient's Licensees under the
  following terms.
  
  Redistribution and use in source and binary forms of the Software, with or
  without modification, are permitted provided that the following conditions
  are met:
  
  Redistributions of source code of the Software may retain the above
  copyright notice, this list of conditions and the following disclaimer.
  
  Redistributions in binary form of the Software may reproduce the above
  copyright notice, this list of conditions and the following disclaimer in
  the documentation and/or materials provided with the distribution.
  
  Neither the name of Intel Corporation nor the names of its contributors
  shall be used to endorse or promote products derived from this Software
  without specific prior written permission.
  
  Intel hereby grants Recipient and Licensees a non-exclusive, worldwide,
  royalty-free patent license under Licensed Patents to make, use, sell, offer
  to sell, import and otherwise transfer the Software, if any, in source code
  and object code form. This license shall include changes to the Software
  that are error corrections or other minor changes to the Software that do
  not add functionality or features when the Software is incorporated in any
  version of an operating system that has been distributed under the GNU
  General Public License 2.0 or later. This patent license shall apply to the
  combination of the Software and any operating system licensed under the GNU
  Public License version 2.0 or later if, at the time Intel provides the
  Software to Recipient, such addition of the Software to the then publicly
  available versions of such operating systems available under the GNU Public
  License version 2.0 or later (whether in gold, beta or alpha form) causes
  such combination to be covered by the Licensed Patents. The patent license
  shall not apply to any other combinations which include the Software. NO
  hardware per se is licensed hereunder.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MECHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR IT CONTRIBUTORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  ANY LOSS OF USE; DATA, OR PROFITS; OR BUSINESS INTERUPTION) HOWEVER CAUSED
  AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR
  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

#define __E1000_MAIN__
#include "e1000.h"

char e1000_driver_name[] = "e1000";

char e1000_driver_string[] = "Intel(R) PRO/1000 Network Driver";

char e1000_driver_version[] = "4.2.4-k1";

char e1000_copyright[] = "Copyright (c) 1999-2002 Intel Corporation.";

/* e1000_pci_tbl - PCI Device ID Table
 *
 * Private driver_data field (last one) stores an index into e1000_strings
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, String Index }
 */
static struct pci_device_id e1000_pci_tbl[] __devinitdata = {
	/* Intel(R) PRO/1000 Network Connection */
	{0x8086, 0x1000, 0x8086, 0x1000, 0, 0, 0},
	{0x8086, 0x1001, 0x8086, 0x1003, 0, 0, 0},
	{0x8086, 0x1004, 0x8086, 0x1004, 0, 0, 0},
	{0x8086, 0x1008, 0x8086, 0x1107, 0, 0, 0},
	{0x8086, 0x1009, 0x8086, 0x1109, 0, 0, 0},
	{0x8086, 0x100C, 0x8086, 0x1112, 0, 0, 0},
	/* Compaq Gigabit Ethernet Server Adapter */
	{0x8086, 0x1000, 0x0E11, PCI_ANY_ID, 0, 0, 1},
	{0x8086, 0x1001, 0x0E11, PCI_ANY_ID, 0, 0, 1},
	{0x8086, 0x1004, 0x0E11, PCI_ANY_ID, 0, 0, 1},
	/* IBM Mobile, Desktop & Server Adapters */
	{0x8086, 0x1000, 0x1014, PCI_ANY_ID, 0, 0, 2},
	{0x8086, 0x1001, 0x1014, PCI_ANY_ID, 0, 0, 2},
	{0x8086, 0x1004, 0x1014, PCI_ANY_ID, 0, 0, 2},
	/* Generic */
	{0x8086, 0x1000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x8086, 0x1001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x8086, 0x1004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x8086, 0x1008, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x8086, 0x1009, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x8086, 0x100C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x8086, 0x100D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	/* required last entry */
	{0,}
};

MODULE_DEVICE_TABLE(pci, e1000_pci_tbl);

static char *e1000_strings[] = {
	"Intel(R) PRO/1000 Network Connection",
	"Compaq Gigabit Ethernet Server Adapter",
	"IBM Mobile, Desktop & Server Adapters"
};

/* Local Function Prototypes */

int e1000_up(struct e1000_adapter *adapter);
void e1000_down(struct e1000_adapter *adapter);
static int e1000_init_module(void);
static void e1000_exit_module(void);
static int e1000_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void e1000_remove(struct pci_dev *pdev);
static void e1000_sw_init(struct e1000_adapter *adapter);
static int e1000_open(struct net_device *netdev);
static int e1000_close(struct net_device *netdev);
static int e1000_setup_tx_resources(struct e1000_adapter *adapter);
static int e1000_setup_rx_resources(struct e1000_adapter *adapter);
static void e1000_configure_tx(struct e1000_adapter *adapter);
static void e1000_configure_rx(struct e1000_adapter *adapter);
static void e1000_setup_rctl(struct e1000_adapter *adapter);
static void e1000_clean_tx_ring(struct e1000_adapter *adapter);
static void e1000_clean_rx_ring(struct e1000_adapter *adapter);
static void e1000_free_tx_resources(struct e1000_adapter *adapter);
static void e1000_free_rx_resources(struct e1000_adapter *adapter);
static void e1000_set_multi(struct net_device *netdev);
static void e1000_update_phy_info(unsigned long data);
static void e1000_watchdog(unsigned long data);
static int e1000_xmit_frame(struct sk_buff *skb, struct net_device *netdev);
static void e1000_tx_timeout(struct net_device *dev);
static struct net_device_stats * e1000_get_stats(struct net_device *netdev);
static int e1000_change_mtu(struct net_device *netdev, int new_mtu);
static int e1000_set_mac(struct net_device *netdev, void *p);
static void e1000_update_stats(struct e1000_adapter *adapter);
static inline void e1000_irq_disable(struct e1000_adapter *adapter);
static inline void e1000_irq_enable(struct e1000_adapter *adapter);
static void e1000_intr(int irq, void *data, struct pt_regs *regs);
static void e1000_clean_tx_irq(struct e1000_adapter *adapter);
static void e1000_clean_rx_irq(struct e1000_adapter *adapter);
static void e1000_alloc_rx_buffers(struct e1000_adapter *adapter);
static int e1000_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);
static void e1000_reset(struct e1000_adapter *adapter);
static void e1000_enter_82542_rst(struct e1000_adapter *adapter);
static void e1000_leave_82542_rst(struct e1000_adapter *adapter);
static inline void e1000_rx_checksum(struct e1000_adapter *adapter,
                                     struct e1000_rx_desc *rx_desc,
                                     struct sk_buff *skb);
void e1000_enable_WOL(struct e1000_adapter *adapter);

/* Exported from other modules */

extern void e1000_check_options(struct e1000_adapter *adapter);
extern void e1000_proc_dev_setup(struct e1000_adapter *adapter);
extern void e1000_proc_dev_free(struct e1000_adapter *adapter);
extern int e1000_ethtool_ioctl(struct net_device *netdev, struct ifreq *ifr);

static struct pci_driver e1000_driver = {
	name:     e1000_driver_name,
	id_table: e1000_pci_tbl,
	probe:    e1000_probe,
	remove:   e1000_remove,
	/* Power Managment Hooks */
	suspend:  NULL,
	resume:   NULL
};

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) PRO/1000 Network Driver");
MODULE_LICENSE("Dual BSD/GPL");

#ifdef EXPORT_SYMTAB
EXPORT_SYMBOL(e1000_init_module);
EXPORT_SYMBOL(e1000_exit_module);
EXPORT_SYMBOL(e1000_probe);
EXPORT_SYMBOL(e1000_remove);
EXPORT_SYMBOL(e1000_open);
EXPORT_SYMBOL(e1000_close);
EXPORT_SYMBOL(e1000_xmit_frame);
EXPORT_SYMBOL(e1000_intr);
EXPORT_SYMBOL(e1000_set_multi);
EXPORT_SYMBOL(e1000_change_mtu);
EXPORT_SYMBOL(e1000_set_mac);
EXPORT_SYMBOL(e1000_get_stats);
EXPORT_SYMBOL(e1000_watchdog);
EXPORT_SYMBOL(e1000_ioctl);
#endif

/**
 * e1000_init_module - Driver Registration Routine
 *
 * e1000_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/

static int __init
e1000_init_module(void)
{
	printk(KERN_INFO "%s - version %s\n",
	       e1000_driver_string, e1000_driver_version);

	printk(KERN_INFO "%s\n", e1000_copyright);

	return pci_module_init(&e1000_driver);
}

module_init(e1000_init_module);

/**
 * e1000_exit_module - Driver Exit Cleanup Routine
 *
 * e1000_exit_module is called just before the driver is removed
 * from memory.
 **/

static void __exit
e1000_exit_module(void)
{
	pci_unregister_driver(&e1000_driver);

	return;
}

module_exit(e1000_exit_module);


int
e1000_up(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if(request_irq(netdev->irq, &e1000_intr, SA_SHIRQ,
	               netdev->name, netdev))
		return -1;

	/* hardware has been reset, we need to reload some things */

	e1000_set_multi(netdev);

	e1000_configure_tx(adapter);
	e1000_setup_rctl(adapter);
	e1000_configure_rx(adapter);
	e1000_alloc_rx_buffers(adapter);

	e1000_clear_hw_cntrs(&adapter->shared);

	mod_timer(&adapter->watchdog_timer, jiffies);
	e1000_irq_enable(adapter);

	return 0;
}

void
e1000_down(struct e1000_adapter *adapter)
{
	struct e1000_shared_adapter *shared = &adapter->shared;
	struct net_device *netdev = adapter->netdev;

	e1000_irq_disable(adapter);
	free_irq(netdev->irq, netdev);
	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_info_timer);
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	/* disable the transmit and receive units */

	E1000_WRITE_REG(shared, RCTL, 0);
	E1000_WRITE_REG(shared, TCTL, E1000_TCTL_PSP);

	/* delay to allow PCI transactions to complete */

	msec_delay(10);

	e1000_clean_tx_ring(adapter);
	e1000_clean_rx_ring(adapter);

	e1000_reset(adapter);
}

static void
e1000_reset(struct e1000_adapter *adapter)
{
	struct e1000_shared_adapter *shared = &adapter->shared;
	uint32_t ctrl_ext;

	/* Repartition Pba for greater than 9k mtu
	 * To take effect CTRL.RST is required.
	 */

	if(adapter->rx_buffer_len > E1000_RXBUFFER_8192)
		E1000_WRITE_REG(shared, PBA, E1000_JUMBO_PBA);
	else
		E1000_WRITE_REG(shared, PBA, E1000_DEFAULT_PBA);

	/* 82542 2.0 needs MWI disabled while issuing a reset */

	if(shared->mac_type == e1000_82542_rev2_0)
		e1000_enter_82542_rst(adapter);

	/* global reset */

	E1000_WRITE_REG(shared, CTRL, E1000_CTRL_RST);
	msec_delay(10);

	/* EEPROM reload */

	ctrl_ext = E1000_READ_REG(shared, CTRL_EXT);
	ctrl_ext |= E1000_CTRL_EXT_EE_RST;
	E1000_WRITE_REG(shared, CTRL_EXT, ctrl_ext);
	msec_delay(5);

	if(shared->mac_type == e1000_82542_rev2_0)
		e1000_leave_82542_rst(adapter);

	shared->tbi_compatibility_on = FALSE;
	shared->fc = shared->original_fc;

	e1000_init_hw(shared);

	e1000_enable_WOL(adapter);

	return;
}

/**
 * e1000_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in e1000_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * e1000_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/

static int __devinit
e1000_probe(struct pci_dev *pdev,
            const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct e1000_adapter *adapter;
	static int cards_found = 0;
	unsigned long mmio_start;
	int mmio_len;
	int i;

	if((i = pci_enable_device(pdev)))
		return i;

	if((i = pci_set_dma_mask(pdev, E1000_DMA_MASK)))
		return i;

	if((i = pci_request_regions(pdev, e1000_driver_name)))
		return i;

	pci_set_master(pdev);

	netdev = alloc_etherdev(sizeof(struct e1000_adapter));
	if(!netdev)
		goto err_alloc_etherdev;

	SET_MODULE_OWNER(netdev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev->priv;
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->shared.back = adapter;

	mmio_start = pci_resource_start(pdev, BAR_0);
	mmio_len = pci_resource_len(pdev, BAR_0);

	adapter->shared.hw_addr = ioremap(mmio_start, mmio_len);
	if(!adapter->shared.hw_addr)
		goto err_ioremap;

	netdev->open = &e1000_open;
	netdev->stop = &e1000_close;
	netdev->hard_start_xmit = &e1000_xmit_frame;
	netdev->get_stats = &e1000_get_stats;
	netdev->set_multicast_list = &e1000_set_multi;
	netdev->set_mac_address = &e1000_set_mac;
	netdev->change_mtu = &e1000_change_mtu;
	netdev->do_ioctl = &e1000_ioctl;
	netdev->tx_timeout = &e1000_tx_timeout;
	netdev->watchdog_timeo = HZ;

	netdev->irq = pdev->irq;
	netdev->base_addr = mmio_start;

	adapter->bd_number = cards_found;
	adapter->id_string = e1000_strings[ent->driver_data];

	/* setup the private structure */

	e1000_sw_init(adapter);

	if(adapter->shared.mac_type >= e1000_82543) {
		netdev->features = NETIF_F_SG |
		                   NETIF_F_IP_CSUM |
		                   NETIF_F_HIGHDMA;
	} else {
		netdev->features = NETIF_F_SG | NETIF_F_HIGHDMA;
	}

	/* make sure the EEPROM is good */

	if(!e1000_validate_eeprom_checksum(&adapter->shared))
		goto err_eeprom;

	/* copy the MAC address out of the EEPROM */

	e1000_read_mac_addr(&adapter->shared);
	memcpy(netdev->dev_addr, adapter->shared.mac_addr, netdev->addr_len);

	if(!is_valid_ether_addr(netdev->dev_addr))
		goto err_eeprom;

	e1000_read_part_num(&adapter->shared, &(adapter->part_num));
	e1000_get_bus_info(&adapter->shared);

	init_timer(&adapter->watchdog_timer);
	adapter->watchdog_timer.function = &e1000_watchdog;
	adapter->watchdog_timer.data = (unsigned long) adapter;

	init_timer(&adapter->phy_info_timer);
	adapter->phy_info_timer.function = &e1000_update_phy_info;
	adapter->phy_info_timer.data = (unsigned long) adapter;

	register_netdev(netdev);

	/* we're going to reset, so assume we have no link for now */
	
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	printk(KERN_INFO "%s: %s\n", netdev->name, adapter->id_string);
	e1000_check_options(adapter);
	e1000_proc_dev_setup(adapter);

	/* reset the hardware with the new settings */

	e1000_reset(adapter);

	cards_found++;
	return 0;

err_eeprom:
	iounmap(adapter->shared.hw_addr);
err_ioremap:
	pci_release_regions(pdev);
	kfree(netdev);
err_alloc_etherdev:
	return -ENOMEM;
}

/**
 * e1000_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * e1000_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 *
 * This routine is also called to clean up from a failure in
 * e1000_probe.  The Adapter struct and netdev will always exist,
 * all other pointers must be checked for NULL before freeing.
 **/

static void __devexit
e1000_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev->priv;

	unregister_netdev(netdev);

	e1000_phy_hw_reset(&adapter->shared);

	e1000_proc_dev_free(adapter);

	iounmap(adapter->shared.hw_addr);
	pci_release_regions(pdev);

	kfree(netdev);
	return;
}

/**
 * e1000_sw_init - Initialize general software structures (struct e1000_adapter)
 * @adapter: board private structure to initialize
 *
 * e1000_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/

static void __devinit
e1000_sw_init(struct e1000_adapter *adapter)
{
	struct e1000_shared_adapter *shared = &adapter->shared;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;

	/* PCI config space info */

	uint16_t *vendor = &shared->vendor_id;
	uint16_t *device = &shared->device_id;
	uint16_t *subvendor = &shared->subsystem_vendor_id;
	uint16_t *subsystem = &shared->subsystem_id;
	uint8_t  *revision  = &shared->revision_id;

	pci_read_config_word(pdev, PCI_VENDOR_ID, vendor);
	pci_read_config_word(pdev, PCI_DEVICE_ID, device);
	pci_read_config_byte(pdev, PCI_REVISION_ID, revision);
	pci_read_config_word(pdev, PCI_SUBSYSTEM_VENDOR_ID, subvendor);
	pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, subsystem);

	pci_read_config_word(pdev, PCI_COMMAND, &shared->pci_cmd_word);

	adapter->rx_buffer_len = E1000_RXBUFFER_2048;
	shared->max_frame_size = netdev->mtu + ENET_HEADER_SIZE + CRC_LENGTH;
	shared->min_frame_size = MINIMUM_ETHERNET_PACKET_SIZE + CRC_LENGTH;

	/* identify the MAC */

	switch (*device) {
	case E1000_DEV_ID_82542:
		switch (*revision) {
		case E1000_82542_2_0_REV_ID:
			shared->mac_type = e1000_82542_rev2_0;
			break;
		case E1000_82542_2_1_REV_ID:
			shared->mac_type = e1000_82542_rev2_1;
			break;
		default:
			shared->mac_type = e1000_82542_rev2_0;
			E1000_ERR("Could not identify 82542 revision\n");
		}
		break;
	case E1000_DEV_ID_82543GC_FIBER:
	case E1000_DEV_ID_82543GC_COPPER:
		shared->mac_type = e1000_82543;
		break;
	case E1000_DEV_ID_82544EI_COPPER:
	case E1000_DEV_ID_82544EI_FIBER:
	case E1000_DEV_ID_82544GC_COPPER:
	case E1000_DEV_ID_82544GC_LOM:
		shared->mac_type = e1000_82544;
		break;
	default:
		/* should never have loaded on this device */
		BUG();
	}

	/* flow control settings */

	shared->fc_high_water = FC_DEFAULT_HI_THRESH;
	shared->fc_low_water = FC_DEFAULT_LO_THRESH;
	shared->fc_pause_time = FC_DEFAULT_TX_TIMER;
	shared->fc_send_xon = 1;

	/* Media type - copper or fiber */

	if(shared->mac_type >= e1000_82543) {
		uint32_t status = E1000_READ_REG(shared, STATUS);

		if(status & E1000_STATUS_TBIMODE)
			shared->media_type = e1000_media_type_fiber;
		else
			shared->media_type = e1000_media_type_copper;
	} else {
		shared->media_type = e1000_media_type_fiber;
	}

	if(shared->mac_type < e1000_82543)
		shared->report_tx_early = 0;
	else
		shared->report_tx_early = 1;

	shared->wait_autoneg_complete = FALSE;
	shared->tbi_compatibility_en = TRUE;

	atomic_set(&adapter->irq_sem, 1);
	spin_lock_init(&adapter->stats_lock);
}

/**
 * e1000_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/

static int
e1000_open(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev->priv;

	/* allocate transmit descriptors */

	if(e1000_setup_tx_resources(adapter))
		goto err_setup_tx;

	/* allocate receive descriptors */

	if(e1000_setup_rx_resources(adapter))
		goto err_setup_rx;

	if(e1000_up(adapter))
		goto err_up;

	return 0;

err_up:
	e1000_free_rx_resources(adapter);
err_setup_rx:
	e1000_free_tx_resources(adapter);
err_setup_tx:
	e1000_reset(adapter);

	return -EBUSY;
}

/**
 * e1000_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/

static int
e1000_close(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev->priv;

	e1000_down(adapter);

	e1000_free_tx_resources(adapter);
	e1000_free_rx_resources(adapter);

	return 0;
}

/**
 * e1000_setup_tx_resources - allocate Tx resources (Descriptors)
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 *
 * e1000_setup_tx_resources allocates all software transmit resources
 * and enabled the Tx unit of the MAC.
 **/

static int
e1000_setup_tx_resources(struct e1000_adapter *adapter)
{
	struct e1000_desc_ring *txdr = &adapter->tx_ring;
	struct pci_dev *pdev = adapter->pdev;
	int size;

	size = sizeof(struct e1000_buffer) * txdr->count;
	txdr->buffer_info = kmalloc(size, GFP_KERNEL);
	if(!txdr->buffer_info) {
		return -ENOMEM;
	}
	memset(txdr->buffer_info, 0, size);

	/* round up to nearest 4K */

	txdr->size = txdr->count * sizeof(struct e1000_tx_desc);
	E1000_ROUNDUP(txdr->size, 4096);

	txdr->desc = pci_alloc_consistent(pdev, txdr->size, &txdr->dma);
	if(!txdr->desc) {
		kfree(txdr->buffer_info);
		return -ENOMEM;
	}
	memset(txdr->desc, 0, txdr->size);

	atomic_set(&txdr->unused, txdr->count);
	txdr->next_to_use = 0;
	txdr->next_to_clean = 0;

	return 0;
}

/**
 * e1000_configure_tx - Configure 8254x Transmit Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/

static void
e1000_configure_tx(struct e1000_adapter *adapter)
{
	uint64_t tdba = adapter->tx_ring.dma;
	uint32_t tdlen = adapter->tx_ring.count * sizeof(struct e1000_tx_desc);
	uint32_t tctl, tipg;

	E1000_WRITE_REG(&adapter->shared, TDBAL, (tdba & 0x00000000FFFFFFFF));
	E1000_WRITE_REG(&adapter->shared, TDBAH, (tdba >> 32));

	E1000_WRITE_REG(&adapter->shared, TDLEN, tdlen);

	/* Setup the HW Tx Head and Tail descriptor pointers */

	E1000_WRITE_REG(&adapter->shared, TDH, 0);
	E1000_WRITE_REG(&adapter->shared, TDT, 0);

	/* Set the default values for the Tx Inter Packet Gap timer */

	switch (adapter->shared.mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
		tipg = DEFAULT_82542_TIPG_IPGT;
		tipg |= DEFAULT_82542_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
		tipg |= DEFAULT_82542_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
		break;
	default:
		if(adapter->shared.media_type == e1000_media_type_fiber)
			tipg = DEFAULT_82543_TIPG_IPGT_FIBER;
		else
			tipg = DEFAULT_82543_TIPG_IPGT_COPPER;
		tipg |= DEFAULT_82543_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
		tipg |= DEFAULT_82543_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
	}
	E1000_WRITE_REG(&adapter->shared, TIPG, tipg);

	/* Set the Tx Interrupt Delay register */

	E1000_WRITE_REG(&adapter->shared, TIDV, adapter->tx_int_delay);

	/* Program the Transmit Control Register */

	tctl = E1000_TCTL_PSP | E1000_TCTL_EN |
	       (E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);

	if(adapter->link_duplex == FULL_DUPLEX) {
		tctl |= E1000_FDX_COLLISION_DISTANCE << E1000_COLD_SHIFT;
	} else {
		tctl |= E1000_HDX_COLLISION_DISTANCE << E1000_COLD_SHIFT;
	}

	E1000_WRITE_REG(&adapter->shared, TCTL, tctl);

#ifdef CONFIG_PPC
	if(adapter->shared.mac_type >= e1000_82543) {
		E1000_WRITE_REG(&adapter->shared, TXDCTL, 0x00020000);
	}
#endif

	/* Setup Transmit Descriptor Settings for this adapter */
	adapter->txd_cmd = E1000_TXD_CMD_IFCS;

	if(adapter->tx_int_delay > 0)
		adapter->txd_cmd |= E1000_TXD_CMD_IDE;
	if(adapter->shared.report_tx_early == 1)
		adapter->txd_cmd |= E1000_TXD_CMD_RS;
	else
		adapter->txd_cmd |= E1000_TXD_CMD_RPS;

	return;
}

/**
 * e1000_setup_rx_resources - allocate Rx resources (Descriptors, receive SKBs)
 * @adapter: board private structure
 *
 * Returns 0 on success, negative on failure
 *
 * e1000_setup_rx_resources allocates all software receive resources
 * and network buffers, and enables the Rx unit of the MAC.
 **/

static int
e1000_setup_rx_resources(struct e1000_adapter *adapter)
{
	struct e1000_desc_ring *rxdr = &adapter->rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	int size;

	size = sizeof(struct e1000_buffer) * rxdr->count;
	rxdr->buffer_info = kmalloc(size, GFP_KERNEL);
	if(!rxdr->buffer_info) {
		return -ENOMEM;
	}
	memset(rxdr->buffer_info, 0, size);

	/* Round up to nearest 4K */

	rxdr->size = rxdr->count * sizeof(struct e1000_rx_desc);
	E1000_ROUNDUP(rxdr->size, 4096);

	rxdr->desc = pci_alloc_consistent(pdev, rxdr->size, &rxdr->dma);

	if(!rxdr->desc) {
		kfree(rxdr->buffer_info);
		return -ENOMEM;
	}
	memset(rxdr->desc, 0, rxdr->size);

	rxdr->next_to_clean = 0;
	rxdr->unused_count = rxdr->count;
	rxdr->next_to_use = 0;

	return 0;
}

/**
 * e1000_setup_rctl - configure the receive control register
 * @adapter: Board private structure
 **/

static void
e1000_setup_rctl(struct e1000_adapter *adapter)
{
	uint32_t rctl;

	/* Setup the Receive Control Register */
	rctl = E1000_RCTL_EN | E1000_RCTL_BAM |
	       E1000_RCTL_LBM_NO | E1000_RCTL_RDMTS_HALF |
	       (adapter->shared.mc_filter_type << E1000_RCTL_MO_SHIFT);

	if(adapter->shared.tbi_compatibility_on == 1)
		rctl |= E1000_RCTL_SBP;

	switch (adapter->rx_buffer_len) {
	case E1000_RXBUFFER_2048:
	default:
		rctl |= E1000_RCTL_SZ_2048;
		break;
	case E1000_RXBUFFER_4096:
		rctl |= E1000_RCTL_SZ_4096 | E1000_RCTL_BSEX | E1000_RCTL_LPE;
		break;
	case E1000_RXBUFFER_8192:
		rctl |= E1000_RCTL_SZ_8192 | E1000_RCTL_BSEX | E1000_RCTL_LPE;
		break;
	case E1000_RXBUFFER_16384:
		rctl |= E1000_RCTL_SZ_16384 | E1000_RCTL_BSEX | E1000_RCTL_LPE;
		break;
	}

	E1000_WRITE_REG(&adapter->shared, RCTL, rctl);
}

/**
 * e1000_configure_rx - Configure 8254x Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/

static void
e1000_configure_rx(struct e1000_adapter *adapter)
{
	uint64_t rdba = adapter->rx_ring.dma;
	uint32_t rdlen = adapter->rx_ring.count * sizeof(struct e1000_rx_desc);
	uint32_t rctl;
	uint32_t rxcsum;

	/* make sure receives are disabled while setting up the descriptors */

	rctl = E1000_READ_REG(&adapter->shared, RCTL);
	E1000_WRITE_REG(&adapter->shared, RCTL, rctl & ~E1000_RCTL_EN);

	/* set the Receive Delay Timer Register */

	E1000_WRITE_REG(&adapter->shared, RDTR,
	                adapter->rx_int_delay | E1000_RDT_FPDB);

	/* Setup the Base and Length of the Rx Descriptor Ring */

	E1000_WRITE_REG(&adapter->shared, RDBAL, (rdba & 0x00000000FFFFFFFF));
	E1000_WRITE_REG(&adapter->shared, RDBAH, (rdba >> 32));

	E1000_WRITE_REG(&adapter->shared, RDLEN, rdlen);

	/* Setup the HW Rx Head and Tail Descriptor Pointers */
	E1000_WRITE_REG(&adapter->shared, RDH, 0);
	E1000_WRITE_REG(&adapter->shared, RDT, 0);

	/* Enable 82543 Receive Checksum Offload for TCP and UDP */
	if((adapter->shared.mac_type >= e1000_82543) &&
	   (adapter->rx_csum == TRUE)) {
		rxcsum = E1000_READ_REG(&adapter->shared, RXCSUM);
		rxcsum |= E1000_RXCSUM_TUOFL;
		E1000_WRITE_REG(&adapter->shared, RXCSUM, rxcsum);
	}

#ifdef CONFIG_PPC
	if(adapter->shared.mac_type >= e1000_82543) {
		E1000_WRITE_REG(&adapter->shared, RXDCTL, 0x00020000);
	}
#endif

	/* Enable Receives */

	E1000_WRITE_REG(&adapter->shared, RCTL, rctl);

	return;
}

/**
 * e1000_free_tx_resources - Free Tx Resources
 * @adapter: board private structure
 *
 * Free all transmit software resources
 **/

static void
e1000_free_tx_resources(struct e1000_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;

	e1000_clean_tx_ring(adapter);

	kfree(adapter->tx_ring.buffer_info);
	adapter->tx_ring.buffer_info = NULL;

	pci_free_consistent(pdev, adapter->tx_ring.size,
	                    adapter->tx_ring.desc, adapter->tx_ring.dma);

	adapter->tx_ring.desc = NULL;

	return;
}

/**
 * e1000_clean_tx_ring - Free Tx Buffers
 * @adapter: board private structure
 **/

static void
e1000_clean_tx_ring(struct e1000_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	int i;

	/* Free all the Tx ring sk_buffs */

	for(i = 0; i < adapter->tx_ring.count; i++) {
		if(adapter->tx_ring.buffer_info[i].skb) {

			pci_unmap_page(pdev,
			               adapter->tx_ring.buffer_info[i].dma,
			               adapter->tx_ring.buffer_info[i].length,
			               PCI_DMA_TODEVICE);

			dev_kfree_skb(adapter->tx_ring.buffer_info[i].skb);

			adapter->tx_ring.buffer_info[i].skb = NULL;
		}
	}

	size = sizeof(struct e1000_buffer) * adapter->tx_ring.count;
	memset(adapter->tx_ring.buffer_info, 0, size);

	/* Zero out the descriptor ring */

	memset(adapter->tx_ring.desc, 0, adapter->tx_ring.size);

	atomic_set(&adapter->tx_ring.unused, adapter->tx_ring.count);
	adapter->tx_ring.next_to_use = 0;
	adapter->tx_ring.next_to_clean = 0;

	E1000_WRITE_REG(&adapter->shared, TDH, 0);
	E1000_WRITE_REG(&adapter->shared, TDT, 0);

	return;
}

/**
 * e1000_free_rx_resources - Free Rx Resources
 * @adapter: board private structure
 *
 * Free all receive software resources
 **/

static void
e1000_free_rx_resources(struct e1000_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;

	e1000_clean_rx_ring(adapter);

	kfree(adapter->rx_ring.buffer_info);
	adapter->rx_ring.buffer_info = NULL;

	pci_free_consistent(pdev, adapter->rx_ring.size,
	                    adapter->rx_ring.desc, adapter->rx_ring.dma);

	adapter->rx_ring.desc = NULL;

	return;
}

/**
 * e1000_clean_rx_ring - Free Rx Buffers
 * @adapter: board private structure
 **/

static void
e1000_clean_rx_ring(struct e1000_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	int i;

	/* Free all the Rx ring sk_buffs */

	for(i = 0; i < adapter->rx_ring.count; i++) {
		if(adapter->rx_ring.buffer_info[i].skb) {

			pci_unmap_single(pdev,
			                 adapter->rx_ring.buffer_info[i].dma,
			                 adapter->rx_ring.buffer_info[i].length,
			                 PCI_DMA_FROMDEVICE);

			dev_kfree_skb(adapter->rx_ring.buffer_info[i].skb);

			adapter->rx_ring.buffer_info[i].skb = NULL;
		}
	}

	size = sizeof(struct e1000_buffer) * adapter->rx_ring.count;
	memset(adapter->rx_ring.buffer_info, 0, size);

	/* Zero out the descriptor ring */

	memset(adapter->rx_ring.desc, 0, adapter->rx_ring.size);

	adapter->rx_ring.unused_count = adapter->rx_ring.count;
	adapter->rx_ring.next_to_clean = 0;
	adapter->rx_ring.next_to_use = 0;

	E1000_WRITE_REG(&adapter->shared, RDH, 0);
	E1000_WRITE_REG(&adapter->shared, RDT, 0);

	return;
}

/* The 82542 2.0 (revision 2) needs to have the receive unit in reset
 * and memory write and invalidate disabled for certain operations
 */
static void
e1000_enter_82542_rst(struct e1000_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct net_device *netdev = adapter->netdev;
	uint16_t pci_command_word = adapter->shared.pci_cmd_word;
	uint32_t rctl;

	if(pci_command_word & PCI_COMMAND_INVALIDATE) {
		pci_command_word &= ~PCI_COMMAND_INVALIDATE;
		pci_write_config_word(pdev, PCI_COMMAND, pci_command_word);
	}

	rctl = E1000_READ_REG(&adapter->shared, RCTL);
	rctl |= E1000_RCTL_RST;
	E1000_WRITE_REG(&adapter->shared, RCTL, rctl);
	msec_delay(5);

	if(netif_running(netdev))
		e1000_clean_rx_ring(adapter);
	return;
}

static void
e1000_leave_82542_rst(struct e1000_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct net_device *netdev = adapter->netdev;
	uint16_t pci_command_word = adapter->shared.pci_cmd_word;
	uint32_t rctl;

	rctl = E1000_READ_REG(&adapter->shared, RCTL);
	rctl &= ~E1000_RCTL_RST;
	E1000_WRITE_REG(&adapter->shared, RCTL, rctl);
	msec_delay(5);

	if(pci_command_word & PCI_COMMAND_INVALIDATE)
		pci_write_config_word(pdev, PCI_COMMAND, pci_command_word);

	if(netif_running(netdev)) {
		e1000_configure_rx(adapter);
		e1000_alloc_rx_buffers(adapter);
	}
	return;
}

/**
 * e1000_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/

static int
e1000_set_mac(struct net_device *netdev, void *p)
{
	struct e1000_adapter *adapter = netdev->priv;
	struct sockaddr *addr = p;

	/* 82542 2.0 needs to be in reset to write receive address registers */

	if(adapter->shared.mac_type == e1000_82542_rev2_0)
		e1000_enter_82542_rst(adapter);

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(adapter->shared.mac_addr, addr->sa_data, netdev->addr_len);

	e1000_rar_set(&adapter->shared, adapter->shared.mac_addr, 0);

	if(adapter->shared.mac_type == e1000_82542_rev2_0)
		e1000_leave_82542_rst(adapter);

	return 0;
}

/**
 * e1000_set_multi - Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_multi entry point is called whenever the multicast address
 * list or the network interface flags are updated.  This routine is
 * resposible for configuring the hardware for proper multicast,
 * promiscuous mode, and all-multi behavior.
 **/

static void
e1000_set_multi(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev->priv;
	struct e1000_shared_adapter *shared = &adapter->shared;
	struct dev_mc_list *mc_ptr;
	uint32_t rctl;
	uint32_t hash_value;
	int i;

	/* Check for Promiscuous and All Multicast modes */

	rctl = E1000_READ_REG(shared, RCTL);

	if(netdev->flags & IFF_PROMISC) {
		rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
	} else if(netdev->flags & IFF_ALLMULTI) {
		rctl |= E1000_RCTL_MPE;
		rctl &= ~E1000_RCTL_UPE;
	} else {
		rctl &= ~(E1000_RCTL_UPE | E1000_RCTL_MPE);
	}

	E1000_WRITE_REG(shared, RCTL, rctl);

	/* 82542 2.0 needs to be in reset to write receive address registers */

	if(shared->mac_type == e1000_82542_rev2_0)
		e1000_enter_82542_rst(adapter);

	/* load the first 15 multicast address into the exact filters 1-15
	 * RAR 0 is used for the station MAC adddress
	 * if there are not 15 addresses, go ahead and clear the filters
	 */
	mc_ptr = netdev->mc_list;

	for(i = 1; i < E1000_RAR_ENTRIES; i++) {
		if(mc_ptr) {
			e1000_rar_set(shared, mc_ptr->dmi_addr, i);
			mc_ptr = mc_ptr->next;
		} else {
			E1000_WRITE_REG_ARRAY(shared, RA, i << 1, 0);
			E1000_WRITE_REG_ARRAY(shared, RA, (i << 1) + 1, 0);
		}
	}

	/* clear the old settings from the multicast hash table */

	for(i = 0; i < E1000_NUM_MTA_REGISTERS; i++)
		E1000_WRITE_REG_ARRAY(shared, MTA, i, 0);

	/* load any remaining addresses into the hash table */

	for(; mc_ptr; mc_ptr = mc_ptr->next) {
		hash_value = e1000_hash_mc_addr(shared, mc_ptr->dmi_addr);
		e1000_mta_set(shared, hash_value);
	}

	if(shared->mac_type == e1000_82542_rev2_0)
		e1000_leave_82542_rst(adapter);
	return;
}


/* need to wait a few seconds after link up to get diagnostic information from the phy */

static void
e1000_update_phy_info(unsigned long data)
{
	struct e1000_adapter *adapter = (struct e1000_adapter *) data;
	e1000_phy_get_info(&adapter->shared, &adapter->phy_info);
	return;
}

/**
 * e1000_watchdog - Timer Call-back
 * @data: pointer to netdev cast into an unsigned long
 **/

static void
e1000_watchdog(unsigned long data)
{
	struct e1000_adapter *adapter = (struct e1000_adapter *) data;
	struct net_device *netdev = adapter->netdev;

	e1000_check_for_link(&adapter->shared);

	if(E1000_READ_REG(&adapter->shared, STATUS) & E1000_STATUS_LU) {
		if(!netif_carrier_ok(netdev)) {
			e1000_get_speed_and_duplex(&adapter->shared,
			                           &adapter->link_speed,
			                           &adapter->link_duplex);

			printk(KERN_INFO
			       "e1000: %s NIC Link is Up %d Mbps %s\n",
			       netdev->name, adapter->link_speed,
			       adapter->link_duplex == FULL_DUPLEX ?
			       "Full Duplex" : "Half Duplex");

			netif_carrier_on(netdev);
			adapter->trans_finish = jiffies;
			netif_wake_queue(netdev);
			mod_timer(&adapter->phy_info_timer, jiffies + 2 * HZ);
		}
	} else {
		if(netif_carrier_ok(netdev)) {
			adapter->link_speed = 0;
			adapter->link_duplex = 0;
			printk(KERN_INFO
			       "e1000: %s NIC Link is Down\n",
			       netdev->name);
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
		}
	}

	e1000_update_stats(adapter);

	/* Reset the timer */
	mod_timer(&adapter->watchdog_timer, jiffies + 2 * HZ);

	return;
}

/**
 * e1000_xmit_frame - Transmit entry point
 * @skb: buffer with frame data to transmit
 * @netdev: network interface device structure
 *
 * Returns 0 on success, 1 on error
 *
 * e1000_xmit_frame is called by the stack to initiate a transmit.
 * The out of resource condition is checked after each successful Tx
 * so that the stack can be notified, preventing the driver from
 * ever needing to drop a frame.  The atomic operations on
 * tx_ring.unused are used to syncronize with the transmit
 * interrupt processing code without the need for a spinlock.
 **/

#define TXD_USE_COUNT(x) (((x) >> 12) + ((x) & 0x0fff ? 1 : 0))

#define SETUP_TXD_PAGE(L, P, O) do { \
	tx_ring->buffer_info[i].length = (L); \
	tx_ring->buffer_info[i].dma = \
		pci_map_page(pdev, (P), (O), (L), PCI_DMA_TODEVICE); \
	tx_desc->buffer_addr = cpu_to_le64(tx_ring->buffer_info[i].dma); \
	tx_desc->lower.data = cpu_to_le32(txd_lower | (L)); \
	tx_desc->upper.data = cpu_to_le32(txd_upper); \
} while (0)

#define SETUP_TXD_PTR(L, P) \
	SETUP_TXD_PAGE((L), virt_to_page(P), (unsigned long)(P) & ~PAGE_MASK)

#define QUEUE_TXD() do { i = (i + 1) % tx_ring->count; \
                         atomic_dec(&tx_ring->unused); } while (0)


static int
e1000_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev->priv;
	struct e1000_desc_ring *tx_ring = &adapter->tx_ring;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_tx_desc *tx_desc;
	int f, len, offset, txd_needed;
	skb_frag_t *frag;

	int i = tx_ring->next_to_use;
	uint32_t txd_upper = 0;
	uint32_t txd_lower = adapter->txd_cmd;


	/* If controller appears hung, force transmit timeout */

	if (time_after(netdev->trans_start, adapter->trans_finish + HZ) &&
	    /* If transmitting XOFFs, we're not really hung */
	    !(E1000_READ_REG(&adapter->shared, STATUS) & E1000_STATUS_TXOFF)) {
		adapter->trans_finish = jiffies;
		netif_stop_queue(netdev);
		return 1;
	}

	txd_needed = TXD_USE_COUNT(skb->len - skb->data_len);

	for(f = 0; f < skb_shinfo(skb)->nr_frags; f++) {
		frag = &skb_shinfo(skb)->frags[f];
		txd_needed += TXD_USE_COUNT(frag->size);
	}

	if(skb->ip_summed == CHECKSUM_HW)
		txd_needed += 1;

	/* make sure there are enough Tx descriptors available in the ring */

	if(atomic_read(&tx_ring->unused) <= (txd_needed + 1)) {
		adapter->net_stats.tx_dropped++;
		netif_stop_queue(netdev);
		return 1;
	}

	if(skb->ip_summed == CHECKSUM_HW) {
		struct e1000_context_desc *context_desc;
		uint8_t css = skb->h.raw - skb->data;
		uint8_t cso = (skb->h.raw + skb->csum) - skb->data;

		context_desc = E1000_CONTEXT_DESC(*tx_ring, i);

		context_desc->upper_setup.tcp_fields.tucss = css;
		context_desc->upper_setup.tcp_fields.tucso = cso;
		context_desc->upper_setup.tcp_fields.tucse = 0;
		context_desc->tcp_seg_setup.data = 0;
		context_desc->cmd_and_length =
			cpu_to_le32(txd_lower | E1000_TXD_CMD_DEXT);

		QUEUE_TXD();

		txd_upper |= E1000_TXD_POPTS_TXSM << 8;
		txd_lower |= E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D;
	}

	tx_desc = E1000_TX_DESC(*tx_ring, i);
	len = skb->len - skb->data_len;
	offset = 0;

	while(len > 4096) {
		SETUP_TXD_PTR(4096, skb->data + offset);
		QUEUE_TXD();

		tx_desc = E1000_TX_DESC(*tx_ring, i);
		len -= 4096;
		offset += 4096;
	}

	SETUP_TXD_PTR(len, skb->data + offset);

	for(f = 0; f < skb_shinfo(skb)->nr_frags; f++) {
		frag = &skb_shinfo(skb)->frags[f];

		QUEUE_TXD();

		tx_desc = E1000_TX_DESC(*tx_ring, i);
		len = frag->size;
		offset = 0;

		while(len > 4096) {
			SETUP_TXD_PAGE(4096, frag->page,
			               frag->page_offset + offset);
			QUEUE_TXD();

			tx_desc = E1000_TX_DESC(*tx_ring, i);
			len -= 4096;
			offset += 4096;
		}
		SETUP_TXD_PAGE(len, frag->page, frag->page_offset + offset);
	}

	/* EOP and SKB pointer go with the last fragment */

	tx_desc->lower.data |= cpu_to_le32(E1000_TXD_CMD_EOP);
	tx_ring->buffer_info[i].skb = skb;

	QUEUE_TXD();

	tx_ring->next_to_use = i;

	/* Move the HW Tx Tail Pointer */

	E1000_WRITE_REG(&adapter->shared, TDT, i);

	netdev->trans_start = jiffies;

	return 0;
}

#undef TXD_USE_COUNT
#undef SETUP_TXD
#undef QUEUE_TXD

/**
 * e1000_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 **/

static void
e1000_tx_timeout(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev->priv;

	e1000_down(adapter);
	e1000_up(adapter);
}

/**
 * e1000_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 **/

static struct net_device_stats *
e1000_get_stats(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev->priv;

	return &adapter->net_stats;
}

/**
 * e1000_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/

static int
e1000_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct e1000_adapter *adapter = netdev->priv;
	int old_mtu = adapter->rx_buffer_len;
	int max_frame = new_mtu + ENET_HEADER_SIZE + CRC_LENGTH;

	if((max_frame < MINIMUM_ETHERNET_PACKET_SIZE + CRC_LENGTH) ||
	   (max_frame > MAX_JUMBO_FRAME_SIZE + CRC_LENGTH)) {
		E1000_ERR("Invalid MTU setting\n");
		return -EINVAL;
	}

	if(max_frame <= MAXIMUM_ETHERNET_PACKET_SIZE + CRC_LENGTH) {
		adapter->rx_buffer_len = E1000_RXBUFFER_2048;

	} else if(adapter->shared.mac_type < e1000_82543) {
		E1000_ERR("Jumbo Frames not supported on 82542\n");
		return -EINVAL;

	} else if(max_frame <= E1000_RXBUFFER_2048) {
		adapter->rx_buffer_len = E1000_RXBUFFER_2048;

	} else if(max_frame <= E1000_RXBUFFER_4096) {
		adapter->rx_buffer_len = E1000_RXBUFFER_4096;

	} else if(max_frame <= E1000_RXBUFFER_8192) {
		adapter->rx_buffer_len = E1000_RXBUFFER_8192;

	} else {
		adapter->rx_buffer_len = E1000_RXBUFFER_16384;
	}

	if(old_mtu != adapter->rx_buffer_len && netif_running(netdev)) {

		e1000_down(adapter);
		e1000_clean_rx_ring(adapter);
		e1000_clean_tx_ring(adapter);
		e1000_up(adapter);
	}

	netdev->mtu = new_mtu;
	adapter->shared.max_frame_size = max_frame;

	return 0;
}

/**
 * e1000_update_stats - Update the board statistics counters
 * @adapter: board private structure
 **/

static void
e1000_update_stats(struct e1000_adapter *adapter)
{
	struct e1000_shared_adapter *shared = &adapter->shared;
	unsigned long flags;

#define PHY_IDLE_ERROR_COUNT_MASK 0x00FF

	spin_lock_irqsave(&adapter->stats_lock, flags);

	/* these counters are modified from e1000_adjust_tbi_stats,
	 * called from the interrupt context, so they must only
	 * be written while holding adapter->stats_lock
	 */

	adapter->stats.crcerrs += E1000_READ_REG(shared, CRCERRS);
	adapter->stats.gprc += E1000_READ_REG(shared, GPRC);
	adapter->stats.gorcl += E1000_READ_REG(shared, GORCL);
	adapter->stats.gorch += E1000_READ_REG(shared, GORCH);
	adapter->stats.bprc += E1000_READ_REG(shared, BPRC);
	adapter->stats.mprc += E1000_READ_REG(shared, MPRC);
	adapter->stats.roc += E1000_READ_REG(shared, ROC);
	adapter->stats.prc64 += E1000_READ_REG(shared, PRC64);
	adapter->stats.prc127 += E1000_READ_REG(shared, PRC127);
	adapter->stats.prc255 += E1000_READ_REG(shared, PRC255);
	adapter->stats.prc511 += E1000_READ_REG(shared, PRC511);
	adapter->stats.prc1023 += E1000_READ_REG(shared, PRC1023);
	adapter->stats.prc1522 += E1000_READ_REG(shared, PRC1522);

	spin_unlock_irqrestore(&adapter->stats_lock, flags);

	/* the rest of the counters are only modified here */

	adapter->stats.symerrs += E1000_READ_REG(shared, SYMERRS);
	adapter->stats.mpc += E1000_READ_REG(shared, MPC);
	adapter->stats.scc += E1000_READ_REG(shared, SCC);
	adapter->stats.ecol += E1000_READ_REG(shared, ECOL);
	adapter->stats.mcc += E1000_READ_REG(shared, MCC);
	adapter->stats.latecol += E1000_READ_REG(shared, LATECOL);
	adapter->stats.colc += E1000_READ_REG(shared, COLC);
	adapter->stats.dc += E1000_READ_REG(shared, DC);
	adapter->stats.sec += E1000_READ_REG(shared, SEC);
	adapter->stats.rlec += E1000_READ_REG(shared, RLEC);
	adapter->stats.xonrxc += E1000_READ_REG(shared, XONRXC);
	adapter->stats.xontxc += E1000_READ_REG(shared, XONTXC);
	adapter->stats.xoffrxc += E1000_READ_REG(shared, XOFFRXC);
	adapter->stats.xofftxc += E1000_READ_REG(shared, XOFFTXC);
	adapter->stats.fcruc += E1000_READ_REG(shared, FCRUC);
	adapter->stats.gptc += E1000_READ_REG(shared, GPTC);
	adapter->stats.gotcl += E1000_READ_REG(shared, GOTCL);
	adapter->stats.gotch += E1000_READ_REG(shared, GOTCH);
	adapter->stats.rnbc += E1000_READ_REG(shared, RNBC);
	adapter->stats.ruc += E1000_READ_REG(shared, RUC);
	adapter->stats.rfc += E1000_READ_REG(shared, RFC);
	adapter->stats.rjc += E1000_READ_REG(shared, RJC);
	adapter->stats.torl += E1000_READ_REG(shared, TORL);
	adapter->stats.torh += E1000_READ_REG(shared, TORH);
	adapter->stats.totl += E1000_READ_REG(shared, TOTL);
	adapter->stats.toth += E1000_READ_REG(shared, TOTH);
	adapter->stats.tpr += E1000_READ_REG(shared, TPR);
	adapter->stats.tpt += E1000_READ_REG(shared, TPT);
	adapter->stats.ptc64 += E1000_READ_REG(shared, PTC64);
	adapter->stats.ptc127 += E1000_READ_REG(shared, PTC127);
	adapter->stats.ptc255 += E1000_READ_REG(shared, PTC255);
	adapter->stats.ptc511 += E1000_READ_REG(shared, PTC511);
	adapter->stats.ptc1023 += E1000_READ_REG(shared, PTC1023);
	adapter->stats.ptc1522 += E1000_READ_REG(shared, PTC1522);
	adapter->stats.mptc += E1000_READ_REG(shared, MPTC);
	adapter->stats.bptc += E1000_READ_REG(shared, BPTC);

	if(adapter->shared.mac_type >= e1000_82543) {
		adapter->stats.algnerrc += E1000_READ_REG(shared, ALGNERRC);
		adapter->stats.rxerrc += E1000_READ_REG(shared, RXERRC);
		adapter->stats.tncrs += E1000_READ_REG(shared, TNCRS);
		adapter->stats.cexterr += E1000_READ_REG(shared, CEXTERR);
		adapter->stats.tsctc += E1000_READ_REG(shared, TSCTC);
		adapter->stats.tsctfc += E1000_READ_REG(shared, TSCTFC);
	}

	/* Fill out the OS statistics structure */

	adapter->net_stats.rx_packets = adapter->stats.gprc;
	adapter->net_stats.tx_packets = adapter->stats.gptc;
	adapter->net_stats.rx_bytes = adapter->stats.gorcl;
	adapter->net_stats.tx_bytes = adapter->stats.gotcl;
	adapter->net_stats.multicast = adapter->stats.mprc;
	adapter->net_stats.collisions = adapter->stats.colc;

	/* Rx Errors */

	adapter->net_stats.rx_errors = adapter->stats.rxerrc +
		adapter->stats.crcerrs + adapter->stats.algnerrc +
		adapter->stats.rlec + adapter->stats.rnbc +
		adapter->stats.mpc + adapter->stats.cexterr;
	adapter->net_stats.rx_dropped = adapter->stats.rnbc;
	adapter->net_stats.rx_length_errors = adapter->stats.rlec;
	adapter->net_stats.rx_crc_errors = adapter->stats.crcerrs;
	adapter->net_stats.rx_frame_errors = adapter->stats.algnerrc;
	adapter->net_stats.rx_fifo_errors = adapter->stats.mpc;
	adapter->net_stats.rx_missed_errors = adapter->stats.mpc;

	/* Tx Errors */

	adapter->net_stats.tx_errors = adapter->stats.ecol +
	                               adapter->stats.latecol;
	adapter->net_stats.tx_aborted_errors = adapter->stats.ecol;
	adapter->net_stats.tx_window_errors = adapter->stats.latecol;

	/* Tx Dropped needs to be maintained elsewhere */

	if(adapter->shared.media_type == e1000_media_type_copper) {
		adapter->phy_stats.idle_errors +=
			(e1000_read_phy_reg(shared, PHY_1000T_STATUS)
			 & PHY_IDLE_ERROR_COUNT_MASK);
		adapter->phy_stats.receive_errors +=
			e1000_read_phy_reg(shared, M88E1000_RX_ERR_CNTR);
	}
	return;
}

/**
 * e1000_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 **/

static inline void
e1000_irq_disable(struct e1000_adapter *adapter)
{
	atomic_inc(&adapter->irq_sem);
	E1000_WRITE_REG(&adapter->shared, IMC, ~0);
	synchronize_irq();
	return;
}

/**
 * e1000_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 **/

static inline void
e1000_irq_enable(struct e1000_adapter *adapter)
{
	if(atomic_dec_and_test(&adapter->irq_sem))
		E1000_WRITE_REG(&adapter->shared, IMS, IMS_ENABLE_MASK);
	return;
}

/**
 * e1000_intr - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 * @pt_regs: CPU registers structure
 **/

static void
e1000_intr(int irq, void *data, struct pt_regs *regs)
{
	struct net_device *netdev = data;
	struct e1000_adapter *adapter = netdev->priv;
	uint32_t icr;
	int i = E1000_MAX_INTR;

	while(i && (icr = E1000_READ_REG(&adapter->shared, ICR))) {

		if(icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
			/* run the watchdog ASAP */
			adapter->shared.get_link_status = 1;
			mod_timer(&adapter->watchdog_timer, jiffies);
		}

		e1000_clean_rx_irq(adapter);
		e1000_clean_tx_irq(adapter);
		i--;
	}

	return;
}

/**
 * e1000_clean_tx_irq - Reclaim resources after transmit completes
 * @adapter: board private structure
 **/

static void
e1000_clean_tx_irq(struct e1000_adapter *adapter)
{
	struct e1000_desc_ring *tx_ring = &adapter->tx_ring;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_tx_desc *tx_desc;
	int i;

	i = tx_ring->next_to_clean;
	tx_desc = E1000_TX_DESC(*tx_ring, i);

	while(tx_desc->upper.data & cpu_to_le32(E1000_TXD_STAT_DD)) {

		if(tx_ring->buffer_info[i].dma) {

			pci_unmap_page(pdev,
			               tx_ring->buffer_info[i].dma,
			               tx_ring->buffer_info[i].length,
			               PCI_DMA_TODEVICE);

			tx_ring->buffer_info[i].dma = 0;
		}

		if(tx_ring->buffer_info[i].skb) {

			dev_kfree_skb_irq(tx_ring->buffer_info[i].skb);

			tx_ring->buffer_info[i].skb = NULL;
		}

		memset(tx_desc, 0, sizeof(struct e1000_tx_desc));
		mb();

		atomic_inc(&tx_ring->unused);
		i = (i + 1) % tx_ring->count;
		tx_desc = E1000_TX_DESC(*tx_ring, i);

		adapter->trans_finish = jiffies;
	}

	tx_ring->next_to_clean = i;

	if(netif_queue_stopped(netdev) && netif_carrier_ok(netdev) &&
	   (atomic_read(&tx_ring->unused) > E1000_TX_QUEUE_WAKE)) {

		netif_wake_queue(netdev);
	}
	return;
}

/**
 * e1000_clean_rx_irq - Send received data up the network stack,
 * @adapter: board private structure
 **/

static void
e1000_clean_rx_irq(struct e1000_adapter *adapter)
{
	struct e1000_desc_ring *rx_ring = &adapter->rx_ring;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_rx_desc *rx_desc;
	struct sk_buff *skb;
	unsigned long flags;
	uint32_t length;
	uint8_t last_byte;
	int i;

	i = rx_ring->next_to_clean;
	rx_desc = E1000_RX_DESC(*rx_ring, i);

	while(rx_desc->status & E1000_RXD_STAT_DD) {

		pci_unmap_single(pdev,
		                 rx_ring->buffer_info[i].dma,
		                 rx_ring->buffer_info[i].length,
		                 PCI_DMA_FROMDEVICE);

		skb = rx_ring->buffer_info[i].skb;
		length = le16_to_cpu(rx_desc->length);

		if(!(rx_desc->status & E1000_RXD_STAT_EOP)) {

			/* All receives must fit into a single buffer */

			E1000_DBG("Receive packet consumed multiple buffers\n");

			dev_kfree_skb_irq(skb);
			memset(rx_desc, 0, 16);
			mb();
			rx_ring->buffer_info[i].skb = NULL;

			rx_ring->unused_count++;

			i = (i + 1) % rx_ring->count;

			rx_desc = E1000_RX_DESC(*rx_ring, i);
			continue;
		}

		if(rx_desc->errors & E1000_RXD_ERR_FRAME_ERR_MASK) {

			last_byte = *(skb->data + length - 1);

			if(TBI_ACCEPT(&adapter->shared, rx_desc->special,
			              rx_desc->errors, length, last_byte)) {

				spin_lock_irqsave(&adapter->stats_lock, flags);

				e1000_tbi_adjust_stats(&adapter->shared,
				                       &adapter->stats,
				                       length, skb->data);

				spin_unlock_irqrestore(&adapter->stats_lock,
				                       flags);
				length--;
			} else {

				dev_kfree_skb_irq(skb);
				memset(rx_desc, 0, 16);
				mb();
				rx_ring->buffer_info[i].skb = NULL;

				rx_ring->unused_count++;
				i = (i + 1) % rx_ring->count;

				rx_desc = E1000_RX_DESC(*rx_ring, i);
				continue;
			}
		}

		/* Good Receive */
		skb_put(skb, length - CRC_LENGTH);

		/* Receive Checksum Offload */
		e1000_rx_checksum(adapter, rx_desc, skb);

		skb->protocol = eth_type_trans(skb, netdev);
		netif_rx(skb);

		memset(rx_desc, 0, sizeof(struct e1000_rx_desc));
		mb();
		rx_ring->buffer_info[i].skb = NULL;

		rx_ring->unused_count++;

		i = (i + 1) % rx_ring->count;

		rx_desc = E1000_RX_DESC(*rx_ring, i);
	}

	rx_ring->next_to_clean = i;

	e1000_alloc_rx_buffers(adapter);

	return;
}

/**
 * e1000_alloc_rx_buffers - Replace used receive buffers
 * @data: address of board private structure
 **/

static void
e1000_alloc_rx_buffers(struct e1000_adapter *adapter)
{
	struct e1000_desc_ring *rx_ring = &adapter->rx_ring;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_rx_desc *rx_desc;
	struct sk_buff *skb;
	int reserve_len;
	int i;

	if(!netif_running(netdev))
		return;

	reserve_len = 2;

	i = rx_ring->next_to_use;

	while(!rx_ring->buffer_info[i].skb) {
		rx_desc = E1000_RX_DESC(*rx_ring, i);

		skb = alloc_skb(adapter->rx_buffer_len + reserve_len,
		                GFP_ATOMIC);

		if(!skb) {
			/* Better luck next round */
			break;
		}

		/* Make buffer alignment 2 beyond a 16 byte boundary
		 * this will result in a 16 byte aligned IP header after
		 * the 14 byte MAC header is removed
		 */
		skb_reserve(skb, reserve_len);

		skb->dev = netdev;

		rx_ring->buffer_info[i].skb = skb;
		rx_ring->buffer_info[i].length = adapter->rx_buffer_len;
		rx_ring->buffer_info[i].dma =
			pci_map_single(pdev,
			               skb->data,
			               adapter->rx_buffer_len,
			               PCI_DMA_FROMDEVICE);

		rx_desc->buffer_addr = cpu_to_le64(rx_ring->buffer_info[i].dma);

		/* move tail */
		E1000_WRITE_REG(&adapter->shared, RDT, i);

		atomic_dec(&rx_ring->unused);

		i = (i + 1) % rx_ring->count;
	}

	rx_ring->next_to_use = i;
	return;
}

/**
 * e1000_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 **/

static int
e1000_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCETHTOOL:
		return e1000_ethtool_ioctl(netdev, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * e1000_rx_checksum - Receive Checksum Offload for 82543
 * @adapter: board private structure
 * @rx_desc: receive descriptor
 * @sk_buff: socket buffer with received data
 **/

static inline void
e1000_rx_checksum(struct e1000_adapter *adapter,
                  struct e1000_rx_desc *rx_desc,
                  struct sk_buff *skb)
{
	/* 82543 or newer only */
	if((adapter->shared.mac_type < e1000_82543) ||
	/* Ignore Checksum bit is set */
	(rx_desc->status & E1000_RXD_STAT_IXSM) ||
	/* TCP Checksum has not been calculated */
	(!(rx_desc->status & E1000_RXD_STAT_TCPCS))) {
		skb->ip_summed = CHECKSUM_NONE;
		return;
	}

	/* At this point we know the hardware did the TCP checksum */
	/* now look at the TCP checksum error bit */
	if(rx_desc->errors & E1000_RXD_ERR_TCPE) {
		/* let the stack verify checksum errors */
		skb->ip_summed = CHECKSUM_NONE;
		adapter->hw_csum_err++;
	} else {
	/* TCP checksum is good */
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		adapter->hw_csum_good++;
	}
	return;
}


/**
 * e1000_enable_WOL - Wake On Lan Support (Magic Pkt)
 * @adapter: Adapter structure
 **/

void
e1000_enable_WOL(struct e1000_adapter *adapter)
{
	uint32_t wuc;

	if(adapter->shared.mac_type < e1000_82544)
		return;

	if(adapter->wol) {
		wuc = E1000_WUC_APME | E1000_WUC_PME_EN |
		      E1000_WUC_PME_STATUS | E1000_WUC_APMPME;

		E1000_WRITE_REG(&adapter->shared, WUC, wuc);

		E1000_WRITE_REG(&adapter->shared, WUFC, adapter->wol);
	}

	return;
}

void
e1000_write_pci_cfg(struct e1000_shared_adapter *shared,
                    uint32_t reg, uint16_t *value)
{
	struct e1000_adapter *adapter = shared->back;

	pci_write_config_word(adapter->pdev, reg, *value);
	return;
}

/* e1000_main.c */
