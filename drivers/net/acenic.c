/*
 * acenic.c: Linux driver for the Alteon AceNIC Gigabit Ethernet card
 *           and other Tigon based cards.
 *
 * Copyright 1998 by Jes Sorensen, <Jes.Sorensen@cern.ch>.
 *
 * Thanks to Alteon and 3Com for providing hardware and documentation
 * enabling me to write this driver.
 *
 * A mailing list for discussing the use of this driver has been
 * setup, please subscribe to the lists if you have any questions
 * about the driver. Send mail to linux-acenic-help@sunsite.auc.dk to
 * see how to subscribe.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Additional work by Pete Wyckoff <wyckoff@ca.sandia.gov> for initial
 * Alpha and trace dump support.
 */

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
#include <asm/uaccess.h>

#include "acenic.h"

/*
 * These must be defined before the firmware is included.
 */
#define MAX_TEXT_LEN	96*1024
#define MAX_RODATA_LEN	8*1024
#define MAX_DATA_LEN	2*1024

#include "acenic_firmware.h"

#ifndef PCI_VENDOR_ID_ALTEON
#define PCI_VENDOR_ID_ALTEON		0x12ae	
#define PCI_DEVICE_ID_ALTEON_ACENIC	0x0001
#endif
#ifndef PCI_DEVICE_ID_3COM_3C985
#define PCI_DEVICE_ID_3COM_3C985	0x0001
#endif
#ifndef PCI_VENDOR_ID_NETGEAR
#define PCI_VENDOR_ID_NETGEAR		0x1385
#define PCI_DEVICE_ID_NETGEAR_GA620	0x620a
#endif

/*
 * This driver currently supports Tigon I and Tigon II based cards
 * including the Alteon AceNIC and the 3Com 3C985. The driver should
 * also work on the NetGear GA620, however I have not been able to
 * test that myself.
 *
 * This card is really neat, it supports receive hardware checksumming
 * and jumbo frames (up to 9000 bytes) and does a lot of work in the
 * firmware. Also the programming interface is quite neat, except for
 * the parts dealing with the i2c eeprom on the card ;-)
 *
 * Using jumbo frames:
 *
 * To enable jumbo frames, simply specify an mtu between 1500 and 9000
 * bytes to ifconfig. Jumbo frames can be enabled or disabled at any time
 * by running `ifconfig eth<X> mtu <MTU>' with <X> being the Ethernet
 * interface number and <MTU> being the MTU value.
 *
 * Module parameters:
 *
 * When compiled as a loadable module, the driver allows for a number
 * of module parameters to be specified. The driver supports the
 * following module parameters:
 *
 *  trace=<val> - Firmware trace level. This requires special traced
 *                firmware to replace the firmware supplied with
 *                the driver - for debugging purposes only.
 *
 *  link=<val>  - Link state. Normally you want to use the default link
 *                parameters set by the driver. This can be used to
 *                override these in case your switch doesn't negotiate
 *                the link properly. Valid values are:
 *         0x0001 - Force half duplex link.
 *         0x0002 - Do not negotiate line speed with the other end.
 *         0x0010 - 10Mbit/sec link.
 *         0x0020 - 100Mbit/sec link.
 *         0x0040 - 1000Mbit/sec link.
 *         0x0100 - Do not negotiate flow control.
 *         0x0200 - Enable RX flow control Y
 *         0x0400 - Enable TX flow control Y (Tigon II NICs only).
 *                Default value is 0x0270, ie. enable link+flow
 *                control negotiation. Negotiating the highest
 *                possible link speed with RX flow control enabled.
 *
 *                When disabling link speed negotiation, only one link
 *                speed is allowed to be specified!
 *
 *  tx_coal_tick=<val> - number of coalescing clock ticks (us) allowed
 *                to wait for more packets to arive before
 *                interrupting the host, from the time the first
 *                packet arrives.
 *
 *  rx_coal_tick=<val> - number of coalescing clock ticks (us) allowed
 *                to wait for more packets to arive in the transmit ring,
 *                before interrupting the host, after transmitting the
 *                first packet in the ring.
 *
 *  max_tx_desc=<val> - maximum number of transmit descriptors
 *                (packets) transmitted before interrupting the host.
 *
 *  max_rx_desc=<val> - maximum number of receive descriptors
 *                (packets) received before interrupting the host.
 *
 *  tx_ratio=<val> - 7 bit value (0 - 63) specifying the split in 64th
 *                increments of the NIC's on board memory to be used for
 *                transmit and receive buffers. For the 1MB NIC app. 800KB
 *                is available, on the 1/2MB NIC app. 300KB is available.
 *                68KB will always be available as a minimum for both
 *                directions. The default value is a 50/50 split.
 *
 * If you use more than one NIC, specify the parameters for the
 * individual NICs with a comma, ie. trace=0,0x00001fff,0 you want to
 * run tracing on NIC #2 but not on NIC #1 and #3.
 *
 * TODO:
 *
 * - Proper multicast support.
 * - NIC dump support.
 * - More tuning parameters.
 *
 * The mini ring is not used under Linux and I am not sure it makes sense
 * to actually use it.
 */

/*
 * Default values for tuning parameters
 */
#define DEF_TX_RATIO	31
#define DEF_TX_COAL	TICKS_PER_SEC / 500
#define DEF_TX_MAX_DESC	7
#define DEF_RX_COAL	TICKS_PER_SEC / 10000
#define DEF_RX_MAX_DESC	2
#define DEF_TRACE	0
#define DEF_STAT	2 * TICKS_PER_SEC

static int link[8] = {0, };
static int trace[8] = {0, };
static int tx_coal_tick[8] = {0, };
static int rx_coal_tick[8] = {0, };
static int max_tx_desc[8] = {0, };
static int max_rx_desc[8] = {0, };
static int tx_ratio[8] = {0, };

static const char __initdata *version = "acenic.c: v0.32 03/15/99  Jes Sorensen (Jes.Sorensen@cern.ch)\n";

static struct net_device *root_dev = NULL;

static int probed __initdata = 0;

int __init acenic_probe (struct net_device *dev)
{
	int boards_found = 0;
	int version_disp;
	struct ace_private *ap;
	u8 pci_latency;
#if 0
	u16 vendor, device;
	u8 pci_bus;
	u8 pci_dev_fun;
	u8 irq;
#endif
	struct pci_dev *pdev = NULL;

	if (probed)
		return -ENODEV;
	probed ++;

	if (!pci_present())		/* is PCI support present? */
		return -ENODEV;

	version_disp = 0;

	while ((pdev = pci_find_class(PCI_CLASS_NETWORK_ETHERNET<<8, pdev))){
		dev = NULL;

		if (!((pdev->vendor == PCI_VENDOR_ID_ALTEON) &&
		      (pdev->device == PCI_DEVICE_ID_ALTEON_ACENIC)) &&
		    !((pdev->vendor == PCI_VENDOR_ID_3COM) &&
		      (pdev->device == PCI_DEVICE_ID_3COM_3C985)) &&
		    !((pdev->vendor == PCI_VENDOR_ID_NETGEAR) &&
		      (pdev->device == PCI_DEVICE_ID_NETGEAR_GA620)))
			continue;

		dev = init_etherdev(dev, sizeof(struct ace_private));

		if (dev == NULL){
			printk(KERN_ERR "Unable to allocate etherdev "
			       "structure!\n");
			break;
		}

		if (!dev->priv)
			dev->priv = kmalloc(sizeof(*ap), GFP_KERNEL);
		if (!dev->priv)
			return -ENOMEM;

		ap = dev->priv;
		ap->pdev = pdev;
		ap->vendor = pdev->vendor;

		dev->irq = pdev->irq;
#ifdef __SMP__
		spin_lock_init(&ap->lock);
#endif

		dev->open = &ace_open;
		dev->hard_start_xmit = &ace_start_xmit;
		dev->stop = &ace_close;
		dev->get_stats = &ace_get_stats;
		dev->set_multicast_list = &ace_set_multicast_list;
#if 0
		dev->do_ioctl = &ace_ioctl;
#endif
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

		pci_read_config_word(pdev, PCI_COMMAND, &ap->pci_command);

		pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &pci_latency);
		if (pci_latency <= 0x40){
			pci_latency = 0x40;
			pci_write_config_byte(pdev, PCI_LATENCY_TIMER,
					      pci_latency);
		}

		pci_set_master(pdev);

		switch(ap->vendor){
		case PCI_VENDOR_ID_ALTEON:
			sprintf(ap->name, "AceNIC Gigabit Ethernet");
			printk(KERN_INFO "%s: Alteon AceNIC ", dev->name);
			break;
		case PCI_VENDOR_ID_3COM:
			sprintf(ap->name, "3Com 3C985 Gigabit Ethernet");
			printk(KERN_INFO "%s: 3Com 3C985 ", dev->name);
			break;
		case PCI_VENDOR_ID_NETGEAR:
			sprintf(ap->name, "NetGear GA620 Gigabit Ethernet");
			printk(KERN_INFO "%s: NetGear GA620 ", dev->name);
			break;
		default:
			sprintf(ap->name, "Unknown AceNIC based Gigabit Ethernet");
			printk(KERN_INFO "%s: Unknown AceNIC ", dev->name);
			break;
		}
		printk("Gigabit Ethernet at 0x%08lx, irq %i, PCI latency %i "
		       "clks\n", pdev->resource[0].start, dev->irq, pci_latency);

		/*
		 * Remap the regs into kernel space.
		 */

		ap->regs = (struct ace_regs *)ioremap(pdev->resource[0].start,
						      0x4000);
		if (!ap->regs){
			printk(KERN_ERR "%s:  Unable to map I/O register, "
			       "AceNIC %i will be disabled.\n",
			       dev->name, boards_found);
			break;
		}

#ifdef MODULE
		if (ace_init(dev, boards_found))
			continue;
#else
		if (ace_init(dev, -1))
			continue;
#endif

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
#if LINUX_VERSION_CODE > 0x20118
MODULE_AUTHOR("Jes Sorensen <Jes.Sorensen@cern.ch>");
MODULE_DESCRIPTION("AceNIC/3C985 Gigabit Ethernet driver");
MODULE_PARM(link, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(trace, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(tx_coal_tick, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(max_tx_desc, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(rx_coal_tick, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(max_rx_desc, "1-" __MODULE_STRING(8) "i");
#endif

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
	struct net_device *next;
	short i;
	unsigned long flags;

	while (root_dev){
		next = ((struct ace_private *)root_dev->priv)->next;
		ap = (struct ace_private *)root_dev->priv;

		regs = ap->regs;
		spin_lock_irqsave(&ap->lock, flags);

		writel(readl(&regs->CpuCtrl) | CPU_HALT, &regs->CpuCtrl);
		if (ap->version == 2)
			writel(readl(&regs->CpuBCtrl) | CPU_HALT,
			       &regs->CpuBCtrl);
		writel(0, &regs->Mb0Lo);

		spin_unlock_irqrestore(&ap->lock, flags);

		/*
		 * Release the RX buffers.
		 */
		for (i = 0; i < RX_STD_RING_ENTRIES; i++) {
			if (ap->rx_std_skbuff[i]) {
				ap->rx_std_ring[i].size = 0;
				set_aceaddr_bus(&ap->rx_std_ring[i].addr, 0);
				dev_kfree_skb(ap->rx_std_skbuff[i]);
			}
		}

		iounmap(regs);
		if(ap->trace_buf)
			kfree(ap->trace_buf);
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

	idx = readl(&regs->CmdPrd);

	writel(*(u32 *)(cmd), &regs->CmdRng[idx]);
	idx = (idx + 1) % CMD_RING_ENTRIES;

	writel(idx, &regs->CmdPrd);
}


static int __init ace_init(struct net_device *dev, int board_idx)
{
	struct ace_private *ap;
	struct ace_regs *regs;
	struct ace_info *info;
	u32 tig_ver, mac1, mac2, tmp;
	unsigned long tmp_ptr, myjif;
	short i;

	ap = dev->priv;
	regs = ap->regs;

	/*
	 * Don't access any other registes before this point!
	 */
#ifdef __BIG_ENDIAN
	writel(((BYTE_SWAP | WORD_SWAP | CLR_INT) |
		((BYTE_SWAP | WORD_SWAP | CLR_INT) << 24)),
	       &regs->HostCtrl);
#else
	writel((CLR_INT | WORD_SWAP | ((CLR_INT | WORD_SWAP) << 24)),
	       &regs->HostCtrl);
#endif
	mb();

	/*
	 * Stop the NIC CPU and clear pending interrupts
	 */
	writel(readl(&regs->CpuCtrl) | CPU_HALT, &regs->CpuCtrl);
	writel(0, &regs->Mb0Lo);

	tig_ver = readl(&regs->HostCtrl) >> 28;

	switch(tig_ver){
	case 4:
		printk(KERN_INFO"  Tigon I (Rev. 4), Firmware: %i.%i.%i, ",
		       tigonFwReleaseMajor, tigonFwReleaseMinor,
		       tigonFwReleaseFix);
		writel(0, &regs->LocalCtrl);
		ap->version = 1;
		break;
	case 6:
		printk(KERN_INFO"  Tigon II (Rev. %i), Firmware: %i.%i.%i, ",
		       tig_ver, tigon2FwReleaseMajor, tigon2FwReleaseMinor,
		       tigon2FwReleaseFix);
		writel(readl(&regs->CpuBCtrl) | CPU_HALT, &regs->CpuBCtrl);
		writel(SRAM_BANK_512K, &regs->LocalCtrl);
		writel(SYNC_SRAM_TIMING, &regs->MiscCfg);
		ap->version = 2;
		break;
	default:
		printk(KERN_INFO"  Unsupported Tigon version detected (%i), ",
		       tig_ver);
		return -ENODEV;
	}

	/*
	 * ModeStat _must_ be set after the SRAM settings as this change
	 * seems to corrupt the ModeStat and possible other registers.
	 * The SRAM settings survive resets and setting it to the same
	 * value a second time works as well. This is what caused the
	 * `Firmware not running' problem on the Tigon II.
	 */
#ifdef __LITTLE_ENDIAN
	writel(ACE_BYTE_SWAP_DATA | ACE_WARN | ACE_FATAL |
	       ACE_WORD_SWAP | ACE_NO_JUMBO_FRAG, &regs->ModeStat);
#else
#error "this driver doesn't run on big-endian machines yet!"
#endif

	mac1 = 0;
	for(i = 0; i < 4; i++){
		mac1 = mac1 << 8;
		mac1 |= read_eeprom_byte(regs, 0x8c+i);
	}
	mac2 = 0;
	for(i = 4; i < 8; i++){
		mac2 = mac2 << 8;
		mac2 |= read_eeprom_byte(regs, 0x8c+i);
	}

	writel(mac1, &regs->MacAddrHi);
	writel(mac2, &regs->MacAddrLo);

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
#if 0
		/*
		 * According to the documentation this enables writes
		 * to all PCI regs - NOT good.
		 */
		tmp |= DMA_WRITE_ALL_ALIGN;
#endif
		tmp |= MEM_READ_MULTIPLE;
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
				pci_write_config_word(ap->pdev, PCI_COMMAND,
						      ap->pci_command);
			}
		}
	}
	writel(tmp, &regs->PciState);

	if (request_irq(dev->irq, ace_interrupt, SA_SHIRQ, ap->name, dev)) {
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

	/*
	 * Register the device here to be able to catch allocated
	 * interrupt handlers in case the firmware doesn't come up.
	 */
	ap->next = root_dev;
	root_dev = dev;

	ap->info = info;
	memset(info, 0, sizeof(struct ace_info));

	ace_load_firmware(dev);
	ap->fw_running = 0;

	tmp_ptr = virt_to_bus((void *)info);
#if (BITS_PER_LONG == 64)
	writel(tmp_ptr >> 32, &regs->InfoPtrHi);
#else
	writel(0, &regs->InfoPtrHi);
#endif
	writel(tmp_ptr & 0xffffffff, &regs->InfoPtrLo);

	memset(ap->evt_ring, 0, EVT_RING_ENTRIES * sizeof(struct event));

	set_aceaddr(&info->evt_ctrl.rngptr, ap->evt_ring);
	info->evt_ctrl.flags = 0;

	set_aceaddr(&info->evt_prd_ptr, &ap->evt_prd);
	ap->evt_prd = 0;
	writel(0, &regs->EvtCsm);

	info->cmd_ctrl.flags = 0;
	set_aceaddr_bus(&info->cmd_ctrl.rngptr, (void *)0x100);
	info->cmd_ctrl.max_len = 0;

	for (i = 0; i < CMD_RING_ENTRIES; i++)
		writel(0, &regs->CmdRng[i]);

	writel(0, &regs->CmdPrd);
	writel(0, &regs->CmdCsm);

	set_aceaddr(&info->stats2_ptr, &info->s.stats);

	info->rx_std_ctrl.max_len = ACE_STD_MTU + ETH_HLEN + 4;
	set_aceaddr(&info->rx_std_ctrl.rngptr, ap->rx_std_ring);
	info->rx_std_ctrl.flags = FLG_RX_TCP_UDP_SUM;

	memset(ap->rx_std_ring, 0,
	       RX_STD_RING_ENTRIES * sizeof(struct rx_desc));

	info->rx_jumbo_ctrl.max_len = 0;
	set_aceaddr(&info->rx_jumbo_ctrl.rngptr, ap->rx_jumbo_ring);
	info->rx_jumbo_ctrl.flags = FLG_RX_TCP_UDP_SUM;

	memset(ap->rx_jumbo_ring, 0,
	       RX_JUMBO_RING_ENTRIES * sizeof(struct rx_desc));

	info->rx_mini_ctrl.max_len = 0;
#if 0
	set_aceaddr(&info->rx_mini_ctrl.rngptr, ap->rx_mini_ring);
#else
	set_aceaddr_bus(&info->rx_mini_ctrl.rngptr, 0);
#endif
	info->rx_mini_ctrl.flags = FLG_RNG_DISABLED;

#if 0
	memset(ap->rx_mini_ring, 0,
	       RX_MINI_RING_ENTRIES * sizeof(struct rx_desc));
#endif

	set_aceaddr(&info->rx_return_ctrl.rngptr, ap->rx_return_ring);
	info->rx_return_ctrl.flags = 0;
	info->rx_return_ctrl.max_len = RX_RETURN_RING_ENTRIES;

	memset(ap->rx_return_ring, 0,
	       RX_RETURN_RING_ENTRIES * sizeof(struct rx_desc));

	set_aceaddr(&info->rx_ret_prd_ptr, &ap->rx_ret_prd);

	writel(TX_RING_BASE, &regs->WinBase);
	ap->tx_ring = (struct tx_desc *)regs->Window;
	for (i = 0; i < (TX_RING_ENTRIES * sizeof(struct tx_desc) / 4); i++){
		writel(0, (unsigned long)ap->tx_ring + i * 4);
	}

	info->tx_ctrl.max_len = TX_RING_ENTRIES;
	info->tx_ctrl.flags = 0;
	set_aceaddr_bus(&info->tx_ctrl.rngptr, (void *)TX_RING_BASE);

	set_aceaddr(&info->tx_csm_ptr, &ap->tx_csm);

	/*
	 * Potential item for tuning parameter
	 */
	writel(DMA_THRESH_8W, &regs->DmaReadCfg);
	writel(DMA_THRESH_8W, &regs->DmaWriteCfg);

	writel(0, &regs->MaskInt);
	writel(1, &regs->IfIdx);
	writel(1, &regs->AssistState);

	writel(DEF_STAT, &regs->TuneStatTicks);

	writel(DEF_TX_COAL, &regs->TuneTxCoalTicks);
	writel(DEF_TX_MAX_DESC, &regs->TuneMaxTxDesc);
	writel(DEF_RX_COAL, &regs->TuneRxCoalTicks);
	writel(DEF_RX_MAX_DESC, &regs->TuneMaxRxDesc);
	writel(DEF_TRACE, &regs->TuneTrace);
	writel(DEF_TX_RATIO, &regs->TxBufRat);

	if (board_idx >= 8) {
		printk(KERN_WARNING "%s: more then 8 NICs detected, "
		       "ignoring module parameters!\n", dev->name);
		board_idx = -1;
	}

	if (board_idx >= 0) {
		if (tx_coal_tick[board_idx])
			writel(tx_coal_tick[board_idx],
			       &regs->TuneTxCoalTicks);
		if (max_tx_desc[board_idx])
			writel(max_tx_desc[board_idx], &regs->TuneMaxTxDesc);

		if (rx_coal_tick[board_idx])
			writel(rx_coal_tick[board_idx],
			       &regs->TuneRxCoalTicks);
		if (max_rx_desc[board_idx])
			writel(max_rx_desc[board_idx], &regs->TuneMaxRxDesc);

		if (trace[board_idx])
			writel(trace[board_idx], &regs->TuneTrace);

		if ((tx_ratio[board_idx] >= 0) && (tx_ratio[board_idx] < 64))
			writel(tx_ratio[board_idx], &regs->TxBufRat);
	}

	/*
	 * Default link parameters
	 */
	tmp = LNK_ENABLE | LNK_FULL_DUPLEX | LNK_1000MB | LNK_100MB |
		LNK_10MB | LNK_RX_FLOW_CTL_Y | LNK_NEG_FCTL | LNK_NEGOTIATE;
	if(ap->version == 2)
		tmp |= LNK_TX_FLOW_CTL_Y;

	/*
	 * Override link default parameters
	 */
	if ((board_idx >= 0) && link[board_idx]) {
		int option = link[board_idx];

		tmp = LNK_ENABLE;

		if (option & 0x01){
			printk(KERN_INFO "%s: Setting half duplex link\n",
			       dev->name);
			tmp &= ~LNK_FULL_DUPLEX;
		}
		if (option & 0x02)
			tmp &= ~LNK_NEGOTIATE;
		if (option & 0x10)
			tmp |= LNK_10MB;
		if (option & 0x20)
			tmp |= LNK_100MB;
		if (option & 0x40)
			tmp |= LNK_1000MB;
		if ((option & 0x70) == 0){
			printk(KERN_WARNING "%s: No media speed specified, "
			       "forcing auto negotiation\n", dev->name);
			tmp |= LNK_NEGOTIATE | LNK_1000MB |
				LNK_100MB | LNK_10MB;
		}
		if ((option & 0x100) == 0)
			tmp |= LNK_NEG_FCTL;
		else
			printk(KERN_INFO "%s: Disabling flow control "
			       "negotiation\n", dev->name);
		if (option & 0x200)
			tmp |= LNK_RX_FLOW_CTL_Y;
		if ((option & 0x400) && (ap->version == 2)){
			printk(KERN_INFO "%s: Enabling TX flow control\n",
			       dev->name);
			tmp |= LNK_TX_FLOW_CTL_Y;
		}
	}

	writel(tmp, &regs->TuneLink);
	if (ap->version == 2)
		writel(tmp, &regs->TuneFastLink);

	if (ap->version == 1)
		writel(tigonFwStartAddr, &regs->Pc);
	else if (ap->version == 2)
		writel(tigon2FwStartAddr, &regs->Pc);

	writel(0, &regs->Mb0Lo);

	/*
	 * Start the NIC CPU
	 */

	writel(readl(&regs->CpuCtrl) & ~(CPU_HALT|CPU_TRACE), &regs->CpuCtrl);

	/*
	 * Wait for the firmware to spin up - max 3 seconds.
	 */
	myjif = jiffies + 3 * HZ;
	while (time_before(jiffies, myjif) && !ap->fw_running);
	if (!ap->fw_running){
		printk(KERN_ERR "%s: Firmware NOT running!\n", dev->name);
		ace_dump_trace(ap);
		writel(readl(&regs->CpuCtrl) | CPU_HALT, &regs->CpuCtrl);
		return -EBUSY;
	}

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
	struct net_device *dev = (struct net_device *)data;
	struct ace_private *ap = (struct ace_private *)dev->priv;
	struct ace_regs *regs = ap->regs;

	/*
	 * We haven't received a stats update event for more than 2.5
	 * seconds and there is data in the transmit queue, thus we
	 * asume the card is stuck.
	 */
	if (ap->tx_csm != ap->tx_ret_csm){
		printk(KERN_WARNING "%s: Transmitter is stuck, %08x\n",
		       dev->name, (unsigned int)readl(&regs->HostCtrl));
	}

	ap->timer.expires = jiffies + (5/2*HZ);
	add_timer(&ap->timer);
}


/*
 * Copy the contents of the NIC's trace buffer to kernel memory.
 */
static void ace_dump_trace(struct ace_private *ap)
{
#if 0
	if (!ap->trace_buf)
		if (!(ap->trace_buf = kmalloc(ACE_TRACE_SIZE, GFP_KERNEL)));
		    return;
#endif
}


/*
 * Load the standard rx ring.
 */
static int ace_load_std_rx_ring(struct net_device *dev)
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
	writel(0, &regs->RxRetCsm);

	for (i = 0; i < RX_RING_THRESH; i++) {
		struct sk_buff *skb;

		ap->rx_std_ring[i].flags = 0;
		skb = alloc_skb(ACE_STD_MTU + ETH_HLEN + 6, GFP_ATOMIC);
		ap->rx_std_skbuff[i] = skb;

		/*
		 * Make sure the data contents end up on an aligned address
		 */
		skb_reserve(skb, 2);

		set_aceaddr(&ap->rx_std_ring[i].addr, skb->data);
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
static int ace_load_jumbo_rx_ring(struct net_device *dev)
{
	struct ace_private *ap;
	struct ace_regs *regs;
	struct cmd cmd;
	unsigned long flags;
	short i;

	ap = (struct ace_private *)dev->priv;
	regs = ap->regs;

	spin_lock_irqsave(&ap->lock, flags);

	for (i = 0; i < RX_RING_JUMBO_THRESH; i++) {
		struct sk_buff *skb;

		ap->rx_jumbo_ring[i].flags = 0;
		skb = alloc_skb(ACE_JUMBO_MTU + ETH_HLEN + 6, GFP_ATOMIC);
		ap->rx_jumbo_skbuff[i] = skb;

		/*
		 * Make sure the data contents end up on an aligned address
		 */
		skb_reserve(skb, 2);

		set_aceaddr(&ap->rx_jumbo_ring[i].addr, skb->data);
		ap->rx_jumbo_ring[i].size = ACE_JUMBO_MTU + ETH_HLEN + 4;

		ap->rx_jumbo_ring[i].flags = DFLG_RX_JUMBO;
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
static int ace_flush_jumbo_rx_ring(struct net_device *dev)
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
				set_aceaddr_bus(&ap->rx_jumbo_ring[i].addr, 0);
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
static u32 ace_handle_event(struct net_device *dev, u32 evtcsm, u32 evtprd)
{
	struct ace_private *ap;

	ap = (struct ace_private *)dev->priv;

	while (evtcsm != evtprd){
		switch (ap->evt_ring[evtcsm].evt){
		case E_FW_RUNNING:
			printk(KERN_INFO "%s: Firmware up and running\n",
			       dev->name);
			ap->fw_running = 1;
			break;
		case E_STATS_UPDATED:
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


static int ace_rx_int(struct net_device *dev, u32 rxretprd, u32 rxretcsm)
{
	struct ace_private *ap = (struct ace_private *)dev->priv;
	struct ace_regs *regs = ap->regs;
	u32 idx, oldidx;

	idx = rxretcsm;

	while (idx != rxretprd){
		struct sk_buff *skb, *newskb, *oldskb;
		struct rx_desc *newrxdesc, *oldrxdesc;
		u32 prdidx, size;
		void *addr;
		u16 csum;
		int jumbo;

		oldidx = ap->rx_return_ring[idx].idx;
		jumbo = ap->rx_return_ring[idx].flags & DFLG_RX_JUMBO;

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
			addr = get_aceaddr_bus(&oldrxdesc->addr);
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
			addr = (void *)virt_to_bus(newskb->data);
		}

		set_aceaddr_bus(&newrxdesc->addr, addr);
		newrxdesc->size = size;

		newrxdesc->flags = oldrxdesc->flags;
		newrxdesc->idx = prdidx;
		newrxdesc->type = DESC_RX;
#if (BITS_PER_LONG == 32)
		newrxdesc->addr.addrhi = 0;
#endif

		oldrxdesc->size = 0;
		set_aceaddr_bus(&oldrxdesc->addr, 0);

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
	/*
	 * According to the documentation RxRetCsm is obsolete with
	 * the 12.3.x Firmware - my Tigon I NIC's seem to disagree!
	 */
	writel(idx, &regs->RxRetCsm);
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
	struct net_device *dev = (struct net_device *)dev_id;
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
	if (!(readl(&regs->HostCtrl) & IN_INT)){
		spin_unlock(&ap->lock);
		return;
	}

	/*
	 * Tell the card not to generate interrupts while we are in here.
	 */
	writel(1, &regs->Mb0Lo);

	/*
	 * Service RX ints before TX
	 */
	rxretprd = ap->rx_ret_prd;
	rxretcsm = ap->cur_rx;

	if (rxretprd != rxretcsm)
		rxretprd = ace_rx_int(dev, rxretprd, rxretcsm);

	txcsm = ap->tx_csm;
	if (txcsm != ap->tx_ret_csm) {
		u32 idx = ap->tx_ret_csm;

		do {
			ap->stats.tx_packets++;
			ap->stats.tx_bytes += ap->tx_skbuff[idx]->len;
			dev_kfree_skb(ap->tx_skbuff[idx]);

			ap->tx_skbuff[idx] = NULL;

#if (BITS_PER_LONG == 64)
			writel(0, &ap->tx_ring[idx].addr.addrhi);
#endif
			writel(0, &ap->tx_ring[idx].addr.addrlo);
			writel(0, &ap->tx_ring[idx].flagsize);

			idx = (idx + 1) % TX_RING_ENTRIES;
		} while (idx != txcsm);

		if (ap->tx_full && dev->tbusy &&
		    (((ap->tx_prd + 1) % TX_RING_ENTRIES) != txcsm)){
			ap->tx_full = 0;
			dev->tbusy = 0;
			mark_bh(NET_BH);

			/*
			 * TX ring is no longer full, aka the
			 * transmitter is working fine - kill timer.
			 */
			del_timer(&ap->timer);
		}

		ap->tx_ret_csm = txcsm;
	}

	evtcsm = readl(&regs->EvtCsm);
	evtprd = ap->evt_prd;

	if (evtcsm != evtprd){
		evtcsm = ace_handle_event(dev, evtcsm, evtprd);
	}

	writel(evtcsm, &regs->EvtCsm);
	writel(0, &regs->Mb0Lo);

	spin_unlock(&ap->lock);
}


static int ace_open(struct net_device *dev)
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

	writel(dev->mtu + ETH_HLEN + 4, &regs->IfMtu);

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
	ap->mcast_all = 0;

#if 0
	{ long myjif = jiffies + HZ;
	while (time_before(jiffies, myjif));
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
	ap->timer.data = (unsigned long)dev;
	ap->timer.function = ace_timer;
	return 0;
}


static int ace_close(struct net_device *dev)
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
			writel(0, &ap->tx_ring[i].addr.addrhi);
			writel(0, &ap->tx_ring[i].addr.addrlo);
			writel(0, &ap->tx_ring[i].flagsize);
			dev_kfree_skb(ap->tx_skbuff[i]);
		}
	}

	if (ap->jumbo)
		ace_flush_jumbo_rx_ring(dev);

	spin_unlock_irqrestore(&ap->lock, flags);

	MOD_DEC_USE_COUNT;
	return 0;
}


static int ace_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ace_private *ap = (struct ace_private *)dev->priv;
	struct ace_regs *regs = ap->regs;
	unsigned long flags;
	unsigned long addr;
	u32 idx, flagsize;

	spin_lock_irqsave(&ap->lock, flags);

	idx = ap->tx_prd;

	ap->tx_skbuff[idx] = skb;
	addr = virt_to_bus(skb->data);
#if (BITS_PER_LONG == 64)
	writel(addr >> 32, &ap->tx_ring[idx].addr.addrhi);
#endif
	writel(addr & 0xffffffff, &ap->tx_ring[idx].addr.addrlo);
	flagsize = (skb->len << 16) | (DESC_END) ;
	writel(flagsize, &ap->tx_ring[idx].flagsize);
	mb();
	idx = (idx + 1) % TX_RING_ENTRIES;

	ap->tx_prd = idx;
	writel(idx, &regs->TxPrd);

	if ((idx + 1) % TX_RING_ENTRIES == ap->tx_ret_csm){
		ap->tx_full = 1;
		set_bit(0, (void*)&dev->tbusy);
		/*
		 * Queue is full, add timer to detect whether the
		 * transmitter is stuck. Use mod_timer as we can get
		 * into the situation where we risk adding several
		 * timers.
		 */
		mod_timer(&ap->timer, jiffies + (3 * HZ));
	}

	spin_unlock_irqrestore(&ap->lock, flags);

	dev->trans_start = jiffies;
	return 0;
}


static int ace_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ace_private *ap = dev->priv;
	struct ace_regs *regs = ap->regs;

	if ((new_mtu < 68) || (new_mtu > ACE_JUMBO_MTU))
		return -EINVAL;

	writel(new_mtu + ETH_HLEN + 4, &regs->IfMtu);
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
static int ace_set_mac_addr(struct net_device *dev, void *p)
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
	writel(da[0], &regs->MacAddrHi);
	writel((da[1] << 16) | da[2], &regs->MacAddrLo);

	cmd.evt = C_SET_MAC_ADDR;
	cmd.code = 0;
	cmd.idx = 0;
	ace_issue_cmd(regs, &cmd);

	return 0;
}


static void ace_set_multicast_list(struct net_device *dev)
{
	struct ace_private *ap = dev->priv;
	struct ace_regs *regs = ap->regs;
	struct cmd cmd;

	if ((dev->flags & IFF_ALLMULTI) && !(ap->mcast_all)) {
		cmd.evt = C_SET_MULTICAST_MODE;
		cmd.code = C_C_MCAST_ENABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
		ap->mcast_all = 1;
	} else if (ap->mcast_all){
		cmd.evt = C_SET_MULTICAST_MODE;
		cmd.code = C_C_MCAST_ENABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
		ap->mcast_all = 0;
	}

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

	/*
	 * For the time being multicast relies on the upper layers
	 * filtering it properly. The Firmware does not allow one to
	 * set the entire multicast list at a time and keeping track of
	 * it here is going to be messy.
	 */
	if ((dev->mc_count) && !(ap->mcast_all)) {
		cmd.evt = C_SET_MULTICAST_MODE;
		cmd.code = C_C_MCAST_ENABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
	}else if (!ap->mcast_all) {
		cmd.evt = C_SET_MULTICAST_MODE;
		cmd.code = C_C_MCAST_DISABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
	}
}


static struct net_device_stats *ace_get_stats(struct net_device *dev)
{
	struct ace_private *ap = dev->priv;

	return(&ap->stats);
}


void __init ace_copy(struct ace_regs *regs, void *src, u32 dest, int size)
{
	unsigned long tdest;
	u32 *wsrc;
	short tsize, i;

	if (size <= 0)
		return;

	while (size > 0){
		tsize = min(((~dest & (ACE_WINDOW_SIZE - 1)) + 1),
			    min(size, ACE_WINDOW_SIZE));
		tdest = (unsigned long)&regs->Window +
			(dest & (ACE_WINDOW_SIZE - 1));
		writel(dest & ~(ACE_WINDOW_SIZE - 1), &regs->WinBase);
#ifdef __BIG_ENDIAN
#error "data must be swapped here"
#else
/*
 * XXX - special memcpy needed here!!!
 */
		wsrc = src;
		for (i = 0; i < (tsize / 4); i++){
			writel(wsrc[i], tdest + i*4);
		}
#endif
		dest += tsize;
		src += tsize;
		size -= tsize;
	}

	return;
}


void __init ace_clear(struct ace_regs *regs, u32 dest, int size)
{
	unsigned long tdest;
	short tsize = 0, i;

	if (size <= 0)
		return;

	while (size > 0){
		tsize = min(((~dest & (ACE_WINDOW_SIZE - 1)) + 1),
			    min(size, ACE_WINDOW_SIZE));
		tdest = (unsigned long)&regs->Window +
			(dest & (ACE_WINDOW_SIZE - 1));
		writel(dest & ~(ACE_WINDOW_SIZE - 1), &regs->WinBase);

		for (i = 0; i < (tsize / 4); i++){
			writel(0, tdest + i*4);
		}

		dest += tsize;
		size -= tsize;
	}

	return;
}


/*
 * Download the firmware into the SRAM on the NIC
 *
 * This operation requires the NIC to be halted and is performed with
 * interrupts disabled and with the spinlock hold.
 */
int __init ace_load_firmware(struct net_device *dev)
{
	struct ace_private *ap;
	struct ace_regs *regs;

	ap = (struct ace_private *)dev->priv;
	regs = ap->regs;

	if (!(readl(&regs->CpuCtrl) & CPU_HALTED)){
		printk(KERN_ERR "%s: trying to download firmware while the "
		       "CPU is running!\n", dev->name);
		return -EFAULT;
	}

	/*
	 * Do not try to clear more than 512KB or we end up seeing
	 * funny things on NICs with only 512KB SRAM
	 */
	ace_clear(regs, 0x2000, 0x80000-0x2000);
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
	u32 local = readl(&regs->LocalCtrl);

	udelay(1);
	local |= EEPROM_DATA_OUT | EEPROM_WRITE_ENABLE;
	writel(local, &regs->LocalCtrl);
	mb();
	udelay(1);
	local |= EEPROM_CLK_OUT;
	writel(local, &regs->LocalCtrl);
	mb();
	udelay(1);
	local &= ~EEPROM_DATA_OUT;
	writel(local, &regs->LocalCtrl);
	mb();
	udelay(1);
	local &= ~EEPROM_CLK_OUT;
	writel(local, &regs->LocalCtrl);
	mb();
}


static void eeprom_prep(struct ace_regs *regs, u8 magic)
{
	short i;
	u32 local;

	udelay(2);
	local = readl(&regs->LocalCtrl);
	local &= ~EEPROM_DATA_OUT;
	local |= EEPROM_WRITE_ENABLE;
	writel(local, &regs->LocalCtrl);
	mb();

	for (i = 0; i < 8; i++, magic <<= 1) {
		udelay(2);
		if (magic & 0x80) 
			local |= EEPROM_DATA_OUT;
		else
			local &= ~EEPROM_DATA_OUT;
		writel(local, &regs->LocalCtrl);
		mb();

		udelay(1);
		local |= EEPROM_CLK_OUT;
		writel(local, &regs->LocalCtrl);
		mb();
		udelay(1);
		local &= ~(EEPROM_CLK_OUT | EEPROM_DATA_OUT);
		writel(local, &regs->LocalCtrl);
		mb();
	}
}


static int eeprom_check_ack(struct ace_regs *regs)
{
	int state;
	u32 local;

	local = readl(&regs->LocalCtrl);
	local &= ~EEPROM_WRITE_ENABLE;
	writel(local, &regs->LocalCtrl);
	mb();
	udelay(2);
	local |= EEPROM_CLK_OUT;
	writel(local, &regs->LocalCtrl);
	mb();
	udelay(1);
	/* sample data in middle of high clk */
	state = (readl(&regs->LocalCtrl) & EEPROM_DATA_IN) != 0;
	udelay(1);
	mb();
	writel(readl(&regs->LocalCtrl) & ~EEPROM_CLK_OUT, &regs->LocalCtrl);
	mb();

	return state;
}


static void eeprom_stop(struct ace_regs *regs)
{
	u32 local;

	local = readl(&regs->LocalCtrl);
	local |= EEPROM_WRITE_ENABLE;
	writel(local, &regs->LocalCtrl);
	mb();
	udelay(1);
	local &= ~EEPROM_DATA_OUT;
	writel(local, &regs->LocalCtrl);
	mb();
	udelay(1);
	local |= EEPROM_CLK_OUT;
	writel(local, &regs->LocalCtrl);
	mb();
	udelay(1);
	local |= EEPROM_DATA_OUT;
	writel(local, &regs->LocalCtrl);
	mb();
	udelay(2);
	local &= ~EEPROM_CLK_OUT;
	writel(local, &regs->LocalCtrl);
	mb();
}


/*
 * Read a whole byte from the EEPROM.
 */
static u8 read_eeprom_byte(struct ace_regs *regs, unsigned long offset)
{
	u32 local;
	short i;
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
		local = readl(&regs->LocalCtrl);
		local &= ~EEPROM_WRITE_ENABLE;
		writel(local, &regs->LocalCtrl);
		udelay(2);
		mb();
		local |= EEPROM_CLK_OUT;
		writel(local, &regs->LocalCtrl);
		udelay(1);
		mb();
		/* sample data mid high clk */
		result = (result << 1) |
			((readl(&regs->LocalCtrl) & EEPROM_DATA_IN) != 0);
		udelay(1);
		mb();
		local = readl(&regs->LocalCtrl);
		local &= ~EEPROM_CLK_OUT;
		writel(local, &regs->LocalCtrl);
		mb();
		if (i == 7){
			local |= EEPROM_WRITE_ENABLE;
			writel(local, &regs->LocalCtrl);
			mb();
		}
	}

	local |= EEPROM_DATA_OUT;
	writel(local, &regs->LocalCtrl);
	udelay(1);
	writel(readl(&regs->LocalCtrl) | EEPROM_CLK_OUT, &regs->LocalCtrl);
	udelay(2);
	writel(readl(&regs->LocalCtrl) & ~EEPROM_CLK_OUT, &regs->LocalCtrl);
	eeprom_stop(regs);

	return result;
}


/*
 * Local variables:
 * compile-command: "gcc -D__KERNEL__ -D__SMP__ -DMODULE -I/data/home/jes/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -pipe -fno-strength-reduce -DMODVERSIONS -include /data/home/jes/linux/include/linux/modversions.h   -c -o acenic.o acenic.c"
 * End:
 */
