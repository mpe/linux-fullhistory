/*
 * acenic.c: Linux driver for the Alteon AceNIC Gigabit Ethernet card
 *           and other Tigon based cards.
 *
 * Copyright 1998 by Jes Sorensen, <Jes.Sorensen@cern.ch>.
 *
 * Thanks to Alteon and 3Com for providing hardware and documentation
 * enabling me to write this driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define DEBUG 1
#define RX_DMA_SKBUFF 1
#define PKT_COPY_THRESHOLD 300

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <net/sock.h>
#include <net/ip.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>

#include "acenic.h"

/*
 * These must be defined before the firmware is included.
 */
#define MAX_TEXT_LEN	96*1024
#define MAX_RODATA_LEN	8*1024
#define MAX_DATA_LEN	2*1024

#include "acenic_firmware.h"


/*
 * This driver currently supports Tigon I and Tigon II based cards
 * including the Alteon AceNIC and the 3Com 3C985.
 *
 * This card is really neat, it supports receive hardware checksumming
 * and jumbo frames (up to 9000 bytes) and does a lot of work in the
 * firmware. Also the programming interface is quite neat, except for
 * the parts dealing with the i2c eeprom on the card ;-)
 *
 * A number of standard Ethernet receive skb's are now allocated at
 * init time and not released before the driver is unloaded. This
 * makes it possible to do ifconfig down/up.
 *
 * Using jumbo frames:
 *
 * To enable jumbo frames, simply specify an mtu between 1500 and 9000
 * bytes to ifconfig. Jumbo frames can be enabled or disabled at any time
 * by running `ifconfig eth<X> mtu <MTU>' with <X> being the Ethernet
 * interface number and <MTU> being the MTU value.
 *
 * TODO:
 *
 * - Add multicast support.
 * - Make all the tuning parameters and link speed negotiation, user
 *   settable at driver/module init time.
 */

static const char *version = "acenic.c: v0.13 11/25/98  Jes Sorensen (Jes.Sorensen@cern.ch)\n";

static struct device *root_dev = NULL;

static int ace_load_firmware(struct device *dev);

__initfunc(int acenic_probe (struct device *dev))
{
	static int i = 0;
	int boards_found = 0;
	int version_disp;
	u32 tmp;
	struct ace_private *ap;
	u16 vendor, device;
	u8 pci_bus;
	u8 pci_dev_fun;
	u8 pci_latency;
	u8 irq;

	if (!pci_present())		/* is PCI support present? */
		return -ENODEV;

	version_disp = 0;

	for (; i < 255; i++)
	{
		dev = NULL;

		if (pcibios_find_class(PCI_CLASS_NETWORK_ETHERNET << 8,
				       i, &pci_bus, &pci_dev_fun) !=
		    PCIBIOS_SUCCESSFUL)
			break;

		pcibios_read_config_word(pci_bus, pci_dev_fun,
					 PCI_VENDOR_ID, &vendor);

		pcibios_read_config_word(pci_bus, pci_dev_fun,
					 PCI_DEVICE_ID, &device);

		if ((vendor != PCI_VENDOR_ID_ALTEON) &&
		    !((vendor == PCI_VENDOR_ID_3COM) &&
		      (device == PCI_DEVICE_ID_3COM_3C985)))
			continue;

		dev = init_etherdev(dev, sizeof(struct ace_private));

		if (dev == NULL){
			printk(KERN_ERR "Unable to allocate etherdev "
			       "structure!\n");
			break;
		}

		if (!dev->priv)
			dev->priv = kmalloc(sizeof(*ap), GFP_KERNEL);

		ap = dev->priv;
		ap->vendor = vendor;

		/* Read register base address from
		   PCI Configuration Space */

		pcibios_read_config_dword(pci_bus, pci_dev_fun,
					  PCI_BASE_ADDRESS_0, &tmp);

		pcibios_read_config_byte(pci_bus, pci_dev_fun,
					 PCI_INTERRUPT_LINE, &irq);

		pcibios_read_config_word(pci_bus, pci_dev_fun,
					 PCI_COMMAND, &ap->pci_command);

		if (!(ap->pci_command & PCI_COMMAND_MASTER)){
			ap->pci_command |= PCI_COMMAND_MASTER;
			pcibios_write_config_word(pci_bus, pci_dev_fun,
						  PCI_COMMAND,
						  ap->pci_command);
		}

		if (!(ap->pci_command & PCI_COMMAND_MEMORY)){
			printk(KERN_ERR "Shared mem not enabled - "
			       "unable to configure AceNIC\n");
			break;
		}

		dev->irq = irq;
		ap->pci_bus = pci_bus;
		ap->pci_dev_fun = pci_dev_fun;
#ifdef __SMP__
		spin_lock_init(&ap->lock);
#endif

		dev->open = &ace_open;
		dev->hard_start_xmit = &ace_start_xmit;
		dev->stop = &ace_close;
		dev->get_stats = &ace_get_stats;
		dev->set_multicast_list = &ace_set_multicast_list;
		dev->set_mac_address = &ace_set_mac_addr;
		dev->change_mtu = &ace_change_mtu;

		/*
		 * Dummy value.
		 */
		dev->base_addr = 42;

		/* display version info if adapter is found */
		if (!version_disp)
		{
			/* set display flag to TRUE so that */
			/* we only display this string ONCE */
			version_disp = 1;
			printk(version);
		}

		pcibios_read_config_byte(pci_bus, pci_dev_fun,
					 PCI_LATENCY_TIMER, &pci_latency);
		if (pci_latency <= 0x40){
			pci_latency = 0x40;
			pcibios_write_config_byte(pci_bus, pci_dev_fun,
						  PCI_LATENCY_TIMER,
						  pci_latency);
		}

		switch(ap->vendor){
		case PCI_VENDOR_ID_ALTEON:
			sprintf(ap->name, "AceNIC Gigabit Ethernet");
			printk(KERN_INFO "%s: Alteon AceNIC ", dev->name);
			break;
		case PCI_VENDOR_ID_3COM:
			sprintf(ap->name, "3Com 3C985 Gigabit Ethernet");
			printk(KERN_INFO "%s: 3Com 3C985 ", dev->name);
			break;
		default:
			sprintf(ap->name, "Unknown AceNIC based Gigabit Ethernet");
			printk(KERN_INFO "%s: Unknown AceNIC ", dev->name);
			break;
		}
		printk("Gigabit Ethernet at 0x%08x, irq %i, PCI latency %i "
		       "clks\n", tmp, dev->irq, pci_latency);

		/*
		 * Remap the regs into kernel space.
		 */

		ap->regs = (struct ace_regs *)ioremap(tmp, 0x4000);
		if (!ap->regs){
			printk(KERN_ERR "%s:  Unable to map I/O register, "
			       "AceNIC %i will be disabled.\n", dev->name, i);
			break;
		}

		ace_init(dev);

		boards_found++;

		/*
		 * This is bollocks, but we need to tell the net-init
		 * code that it shall go for the next device.
		 */
		dev->base_addr = 0;
	}

	/*
	 * If we're at this point we're going through ace_probe() for
	 * the first time.  Return success (0) if we've initialized 1
	 * or more boards. Otherwise, return failure (-ENODEV).
	 */

#ifdef MODULE
	return boards_found;
#else
	if (boards_found > 0)
		return 0;
	else
		return -ENODEV;
#endif
}


#ifdef MODULE
int init_module(void)
{
	int cards;

	root_dev = NULL;

	cards = acenic_probe(NULL);
	return cards ? 0 : -ENODEV;
}

void cleanup_module(void)
{
	struct ace_private *ap;
	struct ace_regs *regs;
	struct device *next;
	short i;
	unsigned long flags;

	while (root_dev) {
		next = ((struct ace_private *)root_dev->priv)->next;
		ap = (struct ace_private *)root_dev->priv;

		regs = ap->regs;
		spin_lock_irqsave(&ap->lock, flags);

		regs->CpuCtrl |= CPU_HALT;
		if (ap->version == 2)
			regs->CpuBCtrl |= CPU_HALT;
		regs->Mb0Lo = 0;

		spin_unlock_irqrestore(&ap->lock, flags);

		/*
		 * Release the RX buffers.
		 */
		for (i = 0; i < RX_STD_RING_ENTRIES; i++) {
			if (ap->rx_std_skbuff[i]) {
				ap->rx_std_ring[i].size = 0;
				ap->rx_std_ring[i].addr = 0;
				dev_kfree_skb(ap->rx_std_skbuff[i]);
			}
		}

		iounmap(regs);
		kfree(ap->info);
		free_irq(root_dev->irq, root_dev);
		unregister_netdev(root_dev);
		kfree(root_dev);

		root_dev = next;
	}
}
#endif


/*
 * Commands are considered to be slow.
 */
static inline void ace_issue_cmd(struct ace_regs *regs, struct cmd *cmd)
{
	u32 idx;

	idx = regs->CmdPrd;

	regs->CmdRng[idx] = *(u32 *)(cmd);
	idx = (idx + 1) % CMD_RING_ENTRIES;

	regs->CmdPrd = idx;
}


__initfunc(static int ace_init(struct device *dev))
{
	struct ace_private *ap;
	struct ace_regs *regs;
	struct ace_info *info;
	u32 tig_ver, mac1 = 0, mac2 = 0, tmp;
	unsigned long tmp_ptr, myjif;
	short i;

	ap = dev->priv;
	regs = ap->regs;

#if 0
	regs->HostCtrl  |= 0x08;
	regs->CpuCtrl = CPU_RESET;
	regs->CpuBCtrl = CPU_RESET;

	{
		long myjif = jiffies + HZ;
		while (myjif > jiffies);
	}
#endif

	/*
	 * Don't access any other registes before this point!
	 */
#ifdef __BIG_ENDIAN
	regs->HostCtrl = ((BYTE_SWAP | WORD_SWAP | CLR_INT) |
			  ((BYTE_SWAP | WORD_SWAP | CLR_INT) << 24));
#else
	regs->HostCtrl = (CLR_INT | WORD_SWAP |
			  ((CLR_INT | WORD_SWAP) << 24));
#endif

#ifdef __LITTLE_ENDIAN
	regs->ModeStat = ACE_BYTE_SWAP_DATA | ACE_WARN | ACE_FATAL
			| ACE_WORD_SWAP;
#else
#error "this driver doesn't run on big-endian machines yet!"
#endif

	/*
	 * Stop the NIC CPU and clear pending interrupts
	 */
	regs->CpuCtrl |= CPU_HALT;
	regs->Mb0Lo = 0;

	tig_ver = regs->HostCtrl >> 28;

	switch(tig_ver){
	case 4:
		printk(KERN_INFO"  Tigon I (Rev. 4), Firmware: %i.%i.%i, ",
		       tigonFwReleaseMajor, tigonFwReleaseMinor,
		       tigonFwReleaseFix);
		regs->LocalCtrl = 0;
		ap->version = 1;
		break;
	case 6:
		printk(KERN_INFO"  Tigon II (Rev. %i), Firmware: %i.%i.%i, ",
		       tig_ver, tigon2FwReleaseMajor, tigon2FwReleaseMinor,
		       tigon2FwReleaseFix);
		regs->CpuBCtrl |= CPU_HALT;
		regs->LocalCtrl = SRAM_BANK_512K;
		regs->MiscCfg = SYNC_SRAM_TIMING;
		ap->version = 2;
		break;
	default:
		printk(KERN_INFO"  Unsupported Tigon version detected (%i), ",
		       tig_ver);
		return -ENODEV;
	}

	for(i = 0; i < 4; i++){
		mac1 = mac1 << 8;
		mac1 |= read_eeprom_byte(regs, 0x8c+i);
	}
	for(i = 4; i < 8; i++){
		mac2 = mac2 << 8;
		mac2 |= read_eeprom_byte(regs, 0x8c+i);
	}

	regs->MacAddrHi = mac1;
	regs->MacAddrLo = mac2;

	printk("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
	       (mac1 >> 8) & 0xff, mac1 & 0xff, (mac2 >> 24) &0xff,
	       (mac2 >> 16) & 0xff, (mac2 >> 8) & 0xff, mac2 & 0xff);

	dev->dev_addr[0] = (mac1 >> 8) & 0xff;
	dev->dev_addr[1] = mac1 & 0xff;
	dev->dev_addr[2] = (mac2 >> 24) & 0xff;
	dev->dev_addr[3] = (mac2 >> 16) & 0xff;
	dev->dev_addr[4] = (mac2 >> 8) & 0xff;
	dev->dev_addr[5] = mac2 & 0xff;

	/*
	 * Set the max DMA transfer size. Seems that for most systems
	 * the performance is better when no MAX parameter is
	 * set. However for systems enabling PCI write and invalidate,
	 * DMA writes must be set to the L1 cache line size to get
	 * optimal performance.
	 */
	tmp = READ_CMD_MEM | WRITE_CMD_MEM;
	if (ap->version == 2){
		tmp |= DMA_WRITE_ALL_ALIGN;
		if (ap->pci_command & PCI_COMMAND_INVALIDATE){
			switch(L1_CACHE_BYTES){
			case 16:
				tmp |= DMA_WRITE_MAX_16;
				break;
			case 32:
				tmp |= DMA_WRITE_MAX_32;
				break;
			case 64:
				tmp |= DMA_WRITE_MAX_64;
				break;
			default:
				printk(KERN_INFO "  Cache line size %i not "
				       "supported, PCI write and invalidate "
				       "disabled\n", L1_CACHE_BYTES);
				ap->pci_command &= ~PCI_COMMAND_INVALIDATE;
				pcibios_write_config_word(ap->pci_bus,
							  ap->pci_dev_fun,
							  PCI_COMMAND,
							  ap->pci_command);
			}
		}
	}
	regs->PciState = tmp;

	if (request_irq(dev->irq, ace_interrupt, SA_SHIRQ, ap->name, dev))
	{
		printk(KERN_WARNING "%s: Requested IRQ %d is busy\n",
		       dev->name, dev->irq);
		return -EAGAIN;
	}

	/*
	 * Initialize the generic info block and the command+event rings
	 * and the control blocks for the transmit and receive rings
	 * as they need to be setup once and for all.
	 */
	if (!(info = kmalloc(sizeof(struct ace_info), GFP_KERNEL | GFP_DMA))){
		free_irq(dev->irq, dev);
		return -EAGAIN;
	}

	ap->info = info;
	memset(info, 0, sizeof(struct ace_info));

	ace_load_firmware(dev);
	ap->fw_running = 0;

	tmp_ptr = virt_to_bus((void *)info);
#if (BITS_PER_LONG == 64)
	regs->InfoPtrHi = (tmp_ptr >> 32);
#else
	regs->InfoPtrHi = 0;
#endif
	regs->InfoPtrLo = ((tmp_ptr) & 0xffffffff);

	memset(ap->evt_ring, 0, EVT_RING_ENTRIES * sizeof(struct event));

	info->evt_ctrl.rngptr = virt_to_bus(ap->evt_ring);
	info->evt_ctrl.flags = 0;

	info->evt_prd_ptr = virt_to_bus(&ap->evt_prd);
	ap->evt_prd = 0;
	regs->EvtCsm = 0;

	info->cmd_ctrl.flags = 0;
	info->cmd_ctrl.rngptr = 0x100;
	info->cmd_ctrl.max_len = 0;

	for (i = 0; i < CMD_RING_ENTRIES; i++) {
		regs->CmdRng[i] = 0;
	}

	regs->CmdPrd = 0;
	regs->CmdCsm = 0;

	info->stats2_ptr = virt_to_bus(&info->s.stats);

	info->rx_std_ctrl.max_len = ACE_STD_MTU + ETH_HLEN + 4;
	info->rx_std_ctrl.rngptr = virt_to_bus(ap->rx_std_ring);
	info->rx_std_ctrl.flags = RX_TCP_UDP_SUM;

	memset(ap->rx_std_ring, 0,
	       RX_STD_RING_ENTRIES * sizeof(struct rx_desc));

	info->rx_jumbo_ctrl.max_len = 0;
	info->rx_jumbo_ctrl.rngptr = virt_to_bus(ap->rx_jumbo_ring);
	info->rx_jumbo_ctrl.flags = RX_TCP_UDP_SUM;

	memset(ap->rx_jumbo_ring, 0,
	       RX_JUMBO_RING_ENTRIES * sizeof(struct rx_desc));

	info->rx_return_ctrl.max_len = 0;
	info->rx_return_ctrl.rngptr = virt_to_bus(ap->rx_return_ring);
	info->rx_return_ctrl.flags = 0;

	memset(ap->rx_return_ring, 0,
	       RX_RETURN_RING_ENTRIES * sizeof(struct rx_desc));

	info->rx_ret_prd_ptr = virt_to_bus(&ap->rx_ret_prd);

	regs->WinBase = TX_RING_BASE;
	ap->tx_ring = (struct tx_desc *)regs->Window;
	memset(ap->tx_ring, 0, TX_RING_ENTRIES * sizeof(struct tx_desc));

	info->tx_ctrl.max_len = TX_RING_ENTRIES;
	info->tx_ctrl.flags = 0;
#if (BITS_PER_LONG == 64) && defined(__BIG_ENDIAN)
	info->tx_ctrl.rngptr = TX_RING_BASE << 32;
#else
	info->tx_ctrl.rngptr = TX_RING_BASE;
#endif

	info->tx_csm_ptr = virt_to_bus(&ap->tx_csm);

	regs->DmaReadCfg = DMA_THRESH_8W;
	regs->DmaWriteCfg = DMA_THRESH_8W;

	regs->MaskInt = 0;
	regs->IfIdx = 1;

#if 0
{
	u32 tmp;
	tmp = regs->AssistState;
	tmp &= ~2;
	tmp |= 1;
	regs->AssistState = tmp;

	tmp = regs->MacRxState;
	tmp &= ~4;
	regs->MacRxState = tmp;
}
#endif
	regs->TuneStatTicks = 2 * TICKS_PER_SEC;
	regs->TuneTxCoalTicks = TICKS_PER_SEC / 500;
	regs->TuneMaxTxDesc = 7;
	regs->TuneRxCoalTicks = TICKS_PER_SEC / 10000;
	regs->TuneMaxRxDesc = 2;
	regs->TuneTrace = 0 /* 0x30001fff */;
	tmp = LNK_ENABLE | LNK_FULL_DUPLEX | LNK_1000MB |
		LNK_RX_FLOW_CTL_Y | LNK_NEG_FCTL | LNK_NEGOTIATE;
	if(ap->version == 1)
		regs->TuneLink = tmp;
	else{
		tmp |= LNK_TX_FLOW_CTL_Y;
		regs->TuneLink = tmp;
		regs->TuneFastLink = tmp;
	}

	if (ap->version == 1)
		regs->Pc = tigonFwStartAddr;
	else if (ap->version == 2)
		regs->Pc = tigon2FwStartAddr;

	regs->Mb0Lo = 0;

	/*
	 * Start the NIC CPU
	 */

	regs->CpuCtrl = (regs->CpuCtrl & ~(CPU_HALT | CPU_TRACE));

	/*
	 * Wait for the firmware to spin up - max 2 seconds.
	 */
	myjif = jiffies + 3 * HZ;
	while ((myjif > jiffies) && !ap->fw_running);
	if (!ap->fw_running){
		printk(KERN_ERR "%s: firmware NOT running!\n", dev->name);
		return -EBUSY;
	}

	ap->next = root_dev;
	root_dev = dev;

	/*
	 * We load the ring here as there seem to be no way to tell the
	 * firmware to wipe the ring without re-initializing it.
	 */
	ace_load_std_rx_ring(dev);

	return 0;
}


/*
 * Monitor the card to detect hangs.
 */

static void ace_timer(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct ace_private *ap = (struct ace_private *)dev->priv;
	struct ace_regs *regs = ap->regs;

	/*
	 * We haven't received a stats update event for more than 2.5
	 * seconds and there is data in the transmit queue, thus we
	 * asume the card is stuck.
	 */
	if (ap->tx_csm != ap->tx_ret_csm){
		printk(KERN_WARNING "%s: Transmitter is stuck, %08x\n",
		       dev->name, regs->HostCtrl);
	}

	ap->timer.expires = jiffies + (5/2*HZ);
	add_timer(&ap->timer);
}


/*
 * Load the standard rx ring.
 */
static int ace_load_std_rx_ring(struct device *dev)
{
	struct ace_private *ap;
	struct ace_regs *regs;
	struct ace_info *info;
	unsigned long flags;
	struct cmd cmd;
	short i;

	ap = (struct ace_private *)dev->priv;
	regs = ap->regs;
	info = ap->info;

	spin_lock_irqsave(&ap->lock, flags);

	/*
	 * Set tx_csm before we start receiving interrupts, otherwise
	 * the interrupt handler might think it is supposed to process
	 * tx ints before we are up and running, which may cause a null
	 * pointer access in the int handler.
	 */
	ap->tx_full = 0;
	ap->cur_rx = ap->dirty_rx = 0;
	ap->tx_prd = ap->tx_csm = ap->tx_ret_csm = 0;
	regs->RxRetCsm = 0;

	for (i = 0; i < RX_RING_THRESH; i++) {
		struct sk_buff *skb;

		ap->rx_std_ring[i].flags = 0;
		skb = alloc_skb(ACE_STD_MTU + ETH_HLEN + 6, GFP_ATOMIC);
		ap->rx_std_skbuff[i] = skb;

		/*
		 * Make sure the data contents end up on an aligned address
		 */
		skb_reserve(skb, 2);

		ap->rx_std_ring[i].addr = virt_to_bus(skb->data);
		ap->rx_std_ring[i].size = ACE_STD_MTU + ETH_HLEN + 4;

		ap->rx_std_ring[i].flags = 0;
		ap->rx_std_ring[i].type = DESC_RX;

		ap->rx_std_ring[i].idx = i;
	}

	ap->rx_std_skbprd = i;

	/*
	 * The last descriptor needs to be marked as being special.
	 */
	ap->rx_std_ring[i-1].type = DESC_END;

	cmd.evt = C_SET_RX_PRD_IDX;
	cmd.code = 0;
	cmd.idx = ap->rx_std_skbprd;
	ace_issue_cmd(regs, &cmd);

	spin_unlock_irqrestore(&ap->lock, flags);

	return 0;
}


/*
 * Load the jumbo rx ring, this may happen at any time if the MTU
 * is changed to a value > 1500.
 */
static int ace_load_jumbo_rx_ring(struct device *dev)
{
	struct ace_private *ap;
	struct ace_regs *regs;
	struct cmd cmd;
	unsigned long flags;
	short i;

	ap = (struct ace_private *)dev->priv;
	regs = ap->regs;

	spin_lock_irqsave(&ap->lock, flags);

	for (i = 0; i < RX_RING_THRESH; i++) {
		struct sk_buff *skb;

		ap->rx_jumbo_ring[i].flags = 0;
		skb = alloc_skb(ACE_JUMBO_MTU + ETH_HLEN + 6, GFP_ATOMIC);
		ap->rx_jumbo_skbuff[i] = skb;

		/*
		 * Make sure the data contents end up on an aligned address
		 */
		skb_reserve(skb, 2);

		ap->rx_jumbo_ring[i].addr = virt_to_bus(skb->data);
		ap->rx_jumbo_ring[i].size = ACE_JUMBO_MTU + ETH_HLEN + 4;

		ap->rx_jumbo_ring[i].flags = JUMBO_FLAG;
		ap->rx_jumbo_ring[i].type = DESC_RX;

		ap->rx_jumbo_ring[i].idx = i;
	}

	ap->rx_jumbo_skbprd = i;

	/*
	 * The last descriptor needs to be marked as being special.
	 */
	ap->rx_jumbo_ring[i-1].type = DESC_END;

	cmd.evt = C_SET_RX_JUMBO_PRD_IDX;
	cmd.code = 0;
	cmd.idx = ap->rx_jumbo_skbprd;
	ace_issue_cmd(regs, &cmd);

	spin_unlock_irqrestore(&ap->lock, flags);

	return 0;
}


/*
 * Tell the firmware not to accept jumbos and flush the jumbo ring.
 * This function must be called with the spinlock held.
 */
static int ace_flush_jumbo_rx_ring(struct device *dev)
{
	struct ace_private *ap;
	struct ace_regs *regs;
	struct cmd cmd;
	short i;

	ap = (struct ace_private *)dev->priv;
	regs = ap->regs;

	if (ap->jumbo){
		cmd.evt = C_RESET_JUMBO_RNG;
		cmd.code = 0;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);

		for (i = 0; i < RX_JUMBO_RING_ENTRIES; i++) {
			if (ap->rx_jumbo_skbuff[i]) {
				ap->rx_jumbo_ring[i].size = 0;
				ap->rx_jumbo_ring[i].addr = 0;
				dev_kfree_skb(ap->rx_jumbo_skbuff[i]);
			}
		}
	}else
		printk(KERN_ERR "%s: Trying to flush Jumbo ring without "
		       "Jumbo support enabled\n", dev->name);

	return 0;
}


/*
 * All events are considered to be slow (RX/TX ints do not generate
 * events) and are handled here, outside the main interrupt handler,
 * to reduce the size of the handler.
 */
static u32 ace_handle_event(struct device *dev, u32 evtcsm, u32 evtprd)
{
	struct ace_private *ap;
	struct ace_regs *regs;

	ap = (struct ace_private *)dev->priv;
	regs = ap->regs;

	while (evtcsm != evtprd){
		switch (ap->evt_ring[evtcsm].evt){
		case E_FW_RUNNING:
			printk(KERN_INFO "%s: Firmware up and running\n",
			       dev->name);
			ap->fw_running = 1;
			break;
		case E_STATS_UPDATED:
			mod_timer(&ap->timer, jiffies + (5/2*HZ));
			break;
		case E_LNK_STATE:
		{
			u16 code = ap->evt_ring[evtcsm].code;
			if (code == E_C_LINK_UP){
				printk("%s: Optical link UP\n", dev->name);
			}
			else if (code == E_C_LINK_DOWN)
				printk(KERN_INFO "%s: Optical link DOWN\n",
				       dev->name);
			else
				printk(KERN_INFO "%s: Unknown optical link "
				       "state %02x\n", dev->name, code);
			break;
		}
		case E_ERROR:
			switch(ap->evt_ring[evtcsm].code){
			case E_C_ERR_INVAL_CMD:
				printk(KERN_ERR "%s: invalid command error\n",
				       dev->name);
				break;
			case E_C_ERR_UNIMP_CMD:
				printk(KERN_ERR "%s: unimplemented command "
				       "error\n", dev->name);
				break;
			case E_C_ERR_BAD_CFG:
				printk(KERN_ERR "%s: bad config error\n",
				       dev->name);
				break;
			default:
				printk(KERN_ERR "%s: unknown error %02x\n",
				       dev->name, ap->evt_ring[evtcsm].code);
			}
			break;
		case E_RESET_JUMBO_RNG:
			break;
		default:
			printk(KERN_ERR "%s: Unhandled event 0x%02x\n",
			       dev->name, ap->evt_ring[evtcsm].evt);
		}
		evtcsm = (evtcsm + 1) % EVT_RING_ENTRIES;
	}

	return evtcsm;
}


static int rx_int(struct device *dev, u32 rxretprd, u32 rxretcsm)
{
	struct ace_private *ap = (struct ace_private *)dev->priv;
	u32 idx, oldidx;
	struct ace_regs *regs = ap->regs;

	idx = rxretcsm;

	while(idx != rxretprd){
		struct sk_buff *skb, *newskb, *oldskb;
		struct rx_desc *newrxdesc, *oldrxdesc;
		u32 prdidx, size;
		unsigned long addr;
		u16 csum;
		int jumbo;

		oldidx = ap->rx_return_ring[idx].idx;
		jumbo = ap->rx_return_ring[idx].flags & JUMBO_FLAG;

		if (jumbo){
			oldskb = ap->rx_jumbo_skbuff[oldidx];
			prdidx = ap->rx_jumbo_skbprd;
			newrxdesc = &ap->rx_jumbo_ring[prdidx];
			oldrxdesc = &ap->rx_jumbo_ring[oldidx];
		}else{
			oldskb = ap->rx_std_skbuff[oldidx];
			prdidx = ap->rx_std_skbprd;
			newrxdesc = &ap->rx_std_ring[prdidx];
			oldrxdesc = &ap->rx_std_ring[oldidx];
		}

		size = oldrxdesc->size;

		if (size < PKT_COPY_THRESHOLD) {
			skb = alloc_skb(size + 2, GFP_ATOMIC);
			if (skb == NULL){
				printk(KERN_ERR "%s: Out of memory\n",
				       dev->name);
				goto error;
			}
			/*
			 * Make sure the real data is aligned
			 */

			skb_reserve(skb, 2);
			memcpy(skb_put(skb, size), oldskb->data, size);
			addr = oldrxdesc->addr;
			newskb = oldskb;
		}else{
			skb = oldskb;

			skb_put(skb, size);

			newskb = alloc_skb(size + 2, GFP_ATOMIC);
			if (newskb == NULL){
				printk(KERN_ERR "%s: Out of memory\n",
				       dev->name);
				goto error;
			}

			/*
			 * Make sure we DMA directly into nicely
			 * aligned receive buffers
			 */
			skb_reserve(newskb, 2);
			addr = virt_to_bus(newskb->data);
		}

		newrxdesc->addr = addr;
		newrxdesc->size = size;

		newrxdesc->flags = oldrxdesc->flags;
		newrxdesc->idx = prdidx;
		newrxdesc->type = DESC_RX;
#if (BITS_PER_LONG == 32)
		newrxdesc->zero = 0;
#endif

		oldrxdesc->size = 0;
		oldrxdesc->addr = 0;

		if (jumbo){
			ap->rx_jumbo_skbuff[oldidx] = NULL;
			ap->rx_jumbo_skbuff[prdidx] = newskb;

			prdidx = (prdidx + 1) % RX_JUMBO_RING_ENTRIES;
			ap->rx_jumbo_skbprd = prdidx;
		}else{
			ap->rx_std_skbuff[oldidx] = NULL;
			ap->rx_std_skbuff[prdidx] = newskb;

			prdidx = (prdidx + 1) % RX_STD_RING_ENTRIES;
			ap->rx_std_skbprd = prdidx;
		}

		/*
		 * Fly baby, fly!
		 */
		csum = ap->rx_return_ring[idx].tcp_udp_csum;

		skb->dev = dev;
		skb->protocol = eth_type_trans(skb, dev);

		/*
		 * If the checksum is correct and this is not a
		 * fragment, tell the stack that the data is correct.
		 */
		if(!(csum ^ 0xffff) &&
		   (!(((struct iphdr *)skb->data)->frag_off &
		      __constant_htons(IP_MF|IP_OFFSET))))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;

		netif_rx(skb);		/* send it up */

		ap->stats.rx_packets++;
		ap->stats.rx_bytes += skb->len;

		if ((prdidx & 0x7) == 0){
			struct cmd cmd;
			if (jumbo)
				cmd.evt = C_SET_RX_JUMBO_PRD_IDX;
			else
				cmd.evt = C_SET_RX_PRD_IDX;
			cmd.code = 0;
			cmd.idx = prdidx;
			ace_issue_cmd(regs, &cmd);
		}

		idx = (idx + 1) % RX_RETURN_RING_ENTRIES;
	}
 out:
	regs->RxRetCsm = idx;
	ap->cur_rx = idx;

	return idx;
 error:
	idx = rxretprd;
	goto out;
}


static void ace_interrupt(int irq, void *dev_id, struct pt_regs *ptregs)
{
	struct ace_private *ap;
	struct ace_regs *regs;
	struct device *dev = (struct device *)dev_id;
	u32 txcsm, rxretcsm, rxretprd;
	u32 evtcsm, evtprd;

	ap = (struct ace_private *)dev->priv;
	regs = ap->regs;

	spin_lock(&ap->lock);

	/*
	 * In case of PCI shared interrupts or spurious interrupts,
	 * we want to make sure it is actually our interrupt before
	 * spending any time in here.
	 */
	if (!(regs->HostCtrl & IN_INT)){
		spin_unlock(&ap->lock);
		return;
	}

#if 0
	/*
	 * Since we are also using a spinlock, I wonder if this is
	 * actually worth it.
	 */
	if (test_and_set_bit(0, (void*)&dev->interrupt) != 0) {
		printk(KERN_WARNING "%s: Re-entering the interrupt handler.\n",
		       dev->name);
		return;
	}
#endif

	/*
	 * Tell the card not to generate interrupts while we are in here.
	 */
	regs->Mb0Lo = 1;

	txcsm = ap->tx_csm;
	if (txcsm != ap->tx_ret_csm) {
		u32 idx = ap->tx_ret_csm;

		do {
			ap->stats.tx_packets++;
			ap->stats.tx_bytes += ap->tx_skbuff[idx]->len;
			dev_kfree_skb(ap->tx_skbuff[idx]);

			ap->tx_skbuff[idx] = NULL;

			ap->tx_ring[idx].size = 0;
			ap->tx_ring[idx].addr = 0;
			ap->tx_ring[idx].flags = 0;

			idx = (idx + 1) % TX_RING_ENTRIES;
		} while (idx != txcsm);

		if (ap->tx_full && dev->tbusy &&
		    (((ap->tx_prd + 1) % TX_RING_ENTRIES) != txcsm)){
			ap->tx_full = 0;
			dev->tbusy = 0;
			mark_bh(NET_BH);
		}

		ap->tx_ret_csm = txcsm;
	}

	rxretprd = ap->rx_ret_prd;
	rxretcsm = ap->cur_rx;

	if (rxretprd != rxretcsm)
		rxretprd = rx_int(dev, rxretprd, rxretcsm);

	evtcsm = regs->EvtCsm;
	evtprd = ap->evt_prd;

	if (evtcsm != evtprd){
		evtcsm = ace_handle_event(dev, evtcsm, evtprd);
	}

	regs->EvtCsm = evtcsm;
	regs->Mb0Lo = 0;

	spin_unlock(&ap->lock);
#if 0
	dev->interrupt = 0;
#endif
}


static int ace_open(struct device *dev)
{
	struct ace_private *ap;
	struct ace_regs *regs;
	struct cmd cmd;

	ap = dev->priv;
	regs = ap->regs;

	if (!(ap->fw_running)){
		printk(KERN_WARNING "%s: firmware not running!\n", dev->name);
		return -EBUSY;
	}

	regs->IfMtu = dev->mtu + ETH_HLEN + 4;

	cmd.evt = C_HOST_STATE;
	cmd.code = C_C_STACK_UP;
	cmd.idx = 0;
	ace_issue_cmd(regs, &cmd);

	if (ap->jumbo)
		ace_load_jumbo_rx_ring(dev);

	if (dev->flags & IFF_PROMISC){
		cmd.evt = C_SET_PROMISC_MODE;
		cmd.code = C_C_PROMISC_ENABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);

		ap->promisc = 1;
	}else
		ap->promisc = 0;

#if 0
	{ long myjif = jiffies + HZ;
	while (jiffies < myjif);
	}

	cmd.evt = C_LNK_NEGOTIATION;
	cmd.code = 0;
	cmd.idx = 0;
	ace_issue_cmd(regs, &cmd);
#endif

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	MOD_INC_USE_COUNT;

	/*
	 * Setup the timer
	 */
	init_timer(&ap->timer);
	ap->timer.expires = jiffies + 5/2 * HZ;
	ap->timer.data = (unsigned long)dev;
	ap->timer.function = &ace_timer;
	add_timer(&ap->timer);
	return 0;
}


static int ace_close(struct device *dev)
{
	struct ace_private *ap;
	struct ace_regs *regs;
	struct cmd cmd;
	unsigned long flags;
	short i;

	dev->start = 0;
	set_bit(0, (void*)&dev->tbusy);

	ap = (struct ace_private *)dev->priv;
	regs = ap->regs;

	del_timer(&ap->timer);

	if (ap->promisc){
		cmd.evt = C_SET_PROMISC_MODE;
		cmd.code = C_C_PROMISC_DISABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
		ap->promisc = 0;
	}

	cmd.evt = C_HOST_STATE;
	cmd.code = C_C_STACK_DOWN;
	cmd.idx = 0;
	ace_issue_cmd(regs, &cmd);

	spin_lock_irqsave(&ap->lock, flags);

	for (i = 0; i < TX_RING_ENTRIES; i++) {
		if (ap->tx_skbuff[i]) {
			ap->tx_ring[i].size = 0;
			ap->tx_ring[i].addr = 0;
			dev_kfree_skb(ap->tx_skbuff[i]);
		}
	}

	if (ap->jumbo)
		ace_flush_jumbo_rx_ring(dev);

	spin_unlock_irqrestore(&ap->lock, flags);

	MOD_DEC_USE_COUNT;
	return 0;
}


static int ace_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct ace_private *ap = (struct ace_private *)dev->priv;
	struct ace_regs *regs = ap->regs;
	unsigned long flags;
	u32 idx;

	spin_lock_irqsave(&ap->lock, flags);

	idx = ap->tx_prd;

	ap->tx_skbuff[idx] = skb;
	ap->tx_ring[idx].addr = virt_to_bus(skb->data);
	ap->tx_ring[idx].size = skb->len;
	ap->tx_ring[idx].flags = DESC_END;
	idx = (idx + 1) % TX_RING_ENTRIES;

	ap->tx_prd = idx;
	regs->TxPrd = idx;

	if ((idx + 1) % TX_RING_ENTRIES == ap->tx_ret_csm){
		ap->tx_full = 1;
		set_bit(0, (void*)&dev->tbusy);
	}

	spin_unlock_irqrestore(&ap->lock, flags);

	dev->trans_start = jiffies;
	return 0;
}


static int ace_change_mtu(struct device *dev, int new_mtu)
{
	struct ace_private *ap = dev->priv;
	struct ace_regs *regs = ap->regs;

	if ((new_mtu < 68) || (new_mtu > ACE_JUMBO_MTU))
		return -EINVAL;

	regs->IfMtu = new_mtu + ETH_HLEN + 4;
	dev->mtu = new_mtu;

	if (new_mtu > ACE_STD_MTU){
		if (!(ap->jumbo)){
			printk(KERN_INFO "%s: Enabling Jumbo frame "
			       "support\n", dev->name);
			ap->jumbo = 1;
			ace_load_jumbo_rx_ring(dev);
		}
		ap->jumbo = 1;
	}else{
		if (ap->jumbo){
			ace_flush_jumbo_rx_ring(dev);

			printk(KERN_INFO "%s: Disabling Jumbo frame support\n",
			       dev->name);
		}
		ap->jumbo = 0;
	}

	return 0;
}


/*
 * Set the hardware MAC address.
 */
static int ace_set_mac_addr(struct device *dev, void *p)
{
	struct sockaddr *addr=p;
	struct ace_regs *regs;
	u16 *da;
	struct cmd cmd;

	if(dev->start)
		return -EBUSY;
	memcpy(dev->dev_addr, addr->sa_data,dev->addr_len);

	da = (u16 *)dev->dev_addr;

	regs = ((struct ace_private *)dev->priv)->regs;
	regs->MacAddrHi = da[0];
	regs->MacAddrLo = (da[1] << 16) | da[2];

	cmd.evt = C_SET_MAC_ADDR;
	cmd.code = 0;
	cmd.idx = 0;
	ace_issue_cmd(regs, &cmd);

	return 0;
}


static void ace_set_multicast_list(struct device *dev)
{
	struct ace_private *ap = dev->priv;
	struct ace_regs *regs = ap->regs;
	struct cmd cmd;

	if ((dev->flags & IFF_PROMISC) && !(ap->promisc)) {
		cmd.evt = C_SET_PROMISC_MODE;
		cmd.code = C_C_PROMISC_ENABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);

		ap->promisc = 1;
	}else if (!(dev->flags & IFF_PROMISC) && (ap->promisc)){
		cmd.evt = C_SET_PROMISC_MODE;
		cmd.code = C_C_PROMISC_DISABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);

		ap->promisc = 0;
	}
}


static struct net_device_stats *ace_get_stats(struct device *dev)
{
	struct ace_private *ap = dev->priv;

	return(&ap->stats);
}


__initfunc(int ace_copy(struct ace_regs *regs, void *src, u32 dest, int size))
{
	int tsize;
	u32 tdest;

	while(size > 0){
		tsize = min(((~dest & (ACE_WINDOW_SIZE - 1)) + 1),
			    min(size, ACE_WINDOW_SIZE));
		tdest = dest & (ACE_WINDOW_SIZE - 1);
		regs->WinBase = dest & ~(ACE_WINDOW_SIZE - 1);
#ifdef __BIG_ENDIAN
#error "data must be swapped here"
#else
#if 0
		printk("copying %04x from %08x to %06x (Window addr %08x), winbase %02x\n", tsize, (unsigned)src, dest, (unsigned) ((void *)regs->Window + tdest), regs->WinBase);
#endif
		memcpy((void *)((void *)regs->Window + tdest), src, tsize);
#endif
		dest += tsize;
		src += tsize;
		size -= tsize;
	}

	return 0;
}


__initfunc(int ace_clear(struct ace_regs *regs, u32 dest, int size))
{
	int tsize;
	u32 tdest;

	while(size > 0){
		tsize = min(((~dest & (ACE_WINDOW_SIZE - 1)) + 1),
			    min(size, ACE_WINDOW_SIZE));
		tdest = dest & (ACE_WINDOW_SIZE - 1);
		regs->WinBase = dest & ~(ACE_WINDOW_SIZE - 1);

		memset((void *)((void *)regs->Window + tdest), 0, tsize);

		dest += tsize;
		size -= tsize;
	}

	return 0;
}


/*
 * Download the firmware into the SRAM on the NIC
 *
 * This operation requires the NIC to be halted and is performed with
 * interrupts disabled and with the spinlock hold.
 */
__initfunc(int ace_load_firmware(struct device *dev))
{
	struct ace_private *ap;
	struct ace_regs *regs;

	ap = (struct ace_private *)dev->priv;
	regs = ap->regs;

	if (!(regs->CpuCtrl & CPU_HALTED)){
		printk(KERN_ERR "%s: trying to download firmware while the "
		       "CPU is running!\n", dev->name);
		return -EFAULT;
	}

	ace_clear(regs, 0x2000, 0x100000-0x2000);
	if (ap->version == 1){
		ace_copy(regs, tigonFwText, tigonFwTextAddr, tigonFwTextLen);
		ace_copy(regs, tigonFwData, tigonFwDataAddr, tigonFwDataLen);
		ace_copy(regs, tigonFwRodata, tigonFwRodataAddr,
			 tigonFwRodataLen);
		ace_clear(regs, tigonFwBssAddr, tigonFwBssLen);
		ace_clear(regs, tigonFwSbssAddr, tigonFwSbssLen);
	}else if (ap->version == 2){
		ace_clear(regs, tigon2FwBssAddr, tigon2FwBssLen);
		ace_clear(regs, tigon2FwSbssAddr, tigon2FwSbssLen);
		ace_copy(regs, tigon2FwText, tigon2FwTextAddr,tigon2FwTextLen);
		ace_copy(regs, tigon2FwRodata, tigon2FwRodataAddr,
			 tigon2FwRodataLen);
		ace_copy(regs, tigon2FwData, tigon2FwDataAddr,tigon2FwDataLen);
	}

	return 0;
}


/*
 * The eeprom on the AceNIC is an Atmel i2c EEPROM.
 *
 * Accessing the EEPROM is `interesting' to say the least - don't read
 * this code right after dinner.
 *
 * This is all about black magic and bit-banging the device .... I
 * wonder in what hospital they have put the guy who designed the i2c
 * specs.
 *
 * Oh yes, this is only the beginning!
 */
static void eeprom_start(struct ace_regs *regs)
{
	udelay(1);
	regs->LocalCtrl |= (EEPROM_DATA_OUT | EEPROM_WRITE_ENABLE);
	udelay(1);
	regs->LocalCtrl |= EEPROM_CLK_OUT;
	udelay(1);
	regs->LocalCtrl &= ~EEPROM_DATA_OUT;
	udelay(1);
	regs->LocalCtrl &= ~EEPROM_CLK_OUT;
}


static void eeprom_prep(struct ace_regs *regs, u8 magic)
{
	short i;

	udelay(2);
	regs->LocalCtrl &= ~EEPROM_DATA_OUT;
	regs->LocalCtrl |= EEPROM_WRITE_ENABLE;

	for (i = 0; i < 8; i++, magic <<= 1) {
		udelay(2);
		if (magic & 0x80) 
			regs->LocalCtrl |= EEPROM_DATA_OUT;
		else
			regs->LocalCtrl &= ~EEPROM_DATA_OUT;

		udelay(1);
		regs->LocalCtrl |= EEPROM_CLK_OUT;
		udelay(1);
		regs->LocalCtrl &= ~(EEPROM_CLK_OUT | EEPROM_DATA_OUT);
	}
}


static int eeprom_check_ack(struct ace_regs *regs)
{
	int state;
    
	regs->LocalCtrl &= ~EEPROM_WRITE_ENABLE;
	udelay(2);
	regs->LocalCtrl |= EEPROM_CLK_OUT;
	udelay(1);
	/* sample data in middle of high clk */
	state = (regs->LocalCtrl & EEPROM_DATA_IN) != 0;
	udelay(1);
	regs->LocalCtrl &= ~EEPROM_CLK_OUT;

	return state;
}


static void eeprom_stop(struct ace_regs *regs)
{
	regs->LocalCtrl |= EEPROM_WRITE_ENABLE;
	udelay(1);
	regs->LocalCtrl &= ~EEPROM_DATA_OUT;
	udelay(1);
	regs->LocalCtrl |= EEPROM_CLK_OUT;
	udelay(1);
	regs->LocalCtrl |= EEPROM_DATA_OUT;
	udelay(2);
	regs->LocalCtrl &= ~EEPROM_CLK_OUT;
}


/*
 * Read a whole byte from the EEPROM.
 */
static u8 read_eeprom_byte(struct ace_regs *regs, unsigned long offset)
{
	u32 i;
	u8 result = 0;

	if (!regs){
		printk(KERN_ERR "No regs!\n");
		return 0;
	}

	eeprom_start(regs);

	eeprom_prep(regs, EEPROM_WRITE_SELECT);
	if (eeprom_check_ack(regs)){
		printk("Unable to sync eeprom\n");
		return 0;
	}

	eeprom_prep(regs, (offset >> 8) & 0xff);
	if (eeprom_check_ack(regs))
		return 0;

	eeprom_prep(regs, offset & 0xff);
	if (eeprom_check_ack(regs))
		return 0;

	eeprom_start(regs);
	eeprom_prep(regs, EEPROM_READ_SELECT);
	if (eeprom_check_ack(regs))
		return 0;

	for (i = 0; i < 8; i++) {
		regs->LocalCtrl &= ~EEPROM_WRITE_ENABLE;
		udelay(2);
		regs->LocalCtrl |= EEPROM_CLK_OUT;
		udelay(1);
		/* sample data mid high clk */
		result = (result << 1) |
			((regs->LocalCtrl & EEPROM_DATA_IN) != 0);
		udelay(1);
		regs->LocalCtrl &= ~EEPROM_CLK_OUT;
		if (i == 7)
			regs->LocalCtrl |= EEPROM_WRITE_ENABLE;
	}

	regs->LocalCtrl |= EEPROM_DATA_OUT;
	udelay(1);
	regs->LocalCtrl |= EEPROM_CLK_OUT;
	udelay(2);
	regs->LocalCtrl &= ~EEPROM_CLK_OUT;
	eeprom_stop(regs);

	return result;
}


/*
 * Local variables:
 * compile-command: "gcc -D__KERNEL__ -D__SMP__ -I/data/home/jes/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -pipe -fno-strength-reduce -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -DCPU=686 -DMODULE -DMODVERSIONS -include /data/home/jes/linux/include/linux/modversions.h   -c -o acenic.o acenic.c"
 * End:
 */
