/*
 * rrunner.c: Linux driver for the Essential RoadRunner HIPPI board.
 *
 * Written 1998 by Jes Sorensen, <Jes.Sorensen@cern.ch>.
 *
 * Thanks to Essential Communication for providing us with hardware
 * and very comprehensive documentation without which I would not have
 * been able to write this driver. A special thank you to John Gibbon
 * for sorting out the legal issues, with the NDA, allowing the code to
 * be released under the GPL.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define DEBUG 1
#define RX_DMA_SKBUFF 1
#define PKT_COPY_THRESHOLD 512

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/hippidevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/cache.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "rrunner.h"


/*
 * Implementation notes:
 *
 * The DMA engine only allows for DMA within physical 64KB chunks of
 * memory. The current approach of the driver (and stack) is to use
 * linear blocks of memory for the skbuffs. However, as the data block
 * is always the first part of the skb and skbs are 2^n aligned so we
 * are guarantted to get the whole block within one 64KB align 64KB
 * chunk.
 *
 * On the long term, relying on being able to allocate 64KB linear
 * chunks of memory is not feasible and the skb handling code and the
 * stack will need to know about I/O vectors or something similar.
 */

static const char *version = "rrunner.c: v0.09 12/14/98  Jes Sorensen (Jes.Sorensen@cern.ch)\n";

static unsigned int read_eeprom(struct rr_private *rrpriv,
				unsigned long offset,
				unsigned char *buf,
				unsigned long length);
static u32 read_eeprom_word(struct rr_private *rrpriv,
			    void * offset);
static int rr_load_firmware(struct device *dev);


/*
 * These are checked at init time to see if they are at least 256KB
 * and increased to 256KB if they are not. This is done to avoid ending
 * up with socket buffers smaller than the MTU size,
 */
extern __u32 sysctl_wmem_max;
extern __u32 sysctl_rmem_max;

__initfunc(int rr_hippi_probe (struct device *dev))
{
	static int i = 0;
	int boards_found = 0;
	int version_disp;	/* was version info already displayed? */
	u8 pci_bus;		/* PCI bus number (0-255) */
	u8 pci_dev_fun;		/* PCI device and function numbers (0-255) */
	u8 pci_latency;
	u16 command;		/* PCI Configuration space Command register */
	unsigned int tmp;
	u8 irq;
	struct rr_private *rrpriv;

	if (!pci_present())		/* is PCI BIOS even present? */
		return -ENODEV;

	version_disp = 0;

	for (; i < 255; i++)
	{
		if (pcibios_find_device(PCI_VENDOR_ID_ESSENTIAL,
					PCI_DEVICE_ID_ESSENTIAL_ROADRUNNER,
					i, &pci_bus, &pci_dev_fun) != 0)
			break;

		pcibios_read_config_word(pci_bus, pci_dev_fun,
					 PCI_COMMAND, &command);

		/* Enable mastering */

		command |= PCI_COMMAND_MASTER;
		pcibios_write_config_word(pci_bus, pci_dev_fun,
					  PCI_COMMAND, command);

		if (!(command & PCI_COMMAND_MEMORY)){
			printk("shared mem not enabled - unable to configure RoadRunner\n");
			break;
		}

		/*
		 * So we found our HIPPI ... time to tell the system.
		 */

		dev = init_hippi_dev(dev, sizeof(struct rr_private));

		if (dev == NULL)
			break;

		if (!dev->priv)
			dev->priv = kmalloc(sizeof(*rrpriv), GFP_KERNEL);

		rrpriv = (struct rr_private *)dev->priv;

		/* Read register base address from
		   PCI Configuration Space */

		pcibios_read_config_dword(pci_bus, pci_dev_fun,
					  PCI_BASE_ADDRESS_0, &tmp);

		pcibios_read_config_byte(pci_bus, pci_dev_fun,
					 PCI_INTERRUPT_LINE, &irq);

		dev->irq = irq;
		rrpriv->pci_bus = pci_bus;
		rrpriv->pci_dev_fun = pci_dev_fun;
		sprintf(rrpriv->name, "RoadRunner serial HIPPI");
#ifdef __SMP__
		spin_lock_init(&rrpriv->lock);
#endif

		dev->open = &rr_open;
		dev->hard_start_xmit = &rr_start_xmit;
		dev->stop = &rr_close;
		dev->get_stats = &rr_get_stats;
		dev->do_ioctl = &rr_ioctl;

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

		printk(KERN_INFO "%s: Essential RoadRunner serial HIPPI at 0x%08x, irq %i\n",
		       dev->name, tmp, dev->irq);

		pcibios_read_config_byte(pci_bus, pci_dev_fun,
					 PCI_LATENCY_TIMER, &pci_latency);
#if 0
		if (pci_latency <= 48){
			printk("  PCI latency counter too low (%i), setting to 48 clocks\n", pci_latency);
			pcibios_write_config_byte(pci_bus, pci_dev_fun,
						  PCI_LATENCY_TIMER, 48);
		}
#else
		if (pci_latency <= 0x58)
			pcibios_write_config_byte(pci_bus, pci_dev_fun,
						  PCI_LATENCY_TIMER, 0x58);
#endif
		/*
		 * Remap the regs into kernel space.
		 */

		rrpriv->regs = (struct rr_regs *)ioremap(tmp, 0x1000);
		if (!rrpriv->regs){
			printk(KERN_ERR "%s:  Unable to map I/O register, RoadRunner %i will be disabled.\n", dev->name, i);
			break;
		}

		/*
		 * Don't access any registes before this point!
		 */
#ifdef __BIG_ENDIAN
		regs->HostCtrl |= NO_SWAP;
#endif
		/*
		 * Need to add a case for little-endian 64-bit hosts here.
		 */

		rr_init(dev);

		boards_found++;
		dev->base_addr = 0;
		dev = NULL;
	}

	/*
	 * If we're at this point we're going through rr_hippi_probe()
	 * for the first time.  Return success (0) if we've initialized
	 * 1 or more boards. Otherwise, return failure (-ENODEV).
	 */

	if (boards_found > 0)
		return 0;
	else
		return -ENODEV;
}


/*
 * Commands are considered to be slow, thus there is no reason to
 * inline this.
 */
static void rr_issue_cmd(struct rr_private *rrpriv, struct cmd *cmd)
{
	struct rr_regs *regs;
	u32 idx;

	regs = rrpriv->regs;
	/*
	 * This is temporary - it will go away in the final version.
	 * We probably also want to make this function inline.
	 */
	if (regs->HostCtrl & NIC_HALTED){
		printk("issuing command for halted NIC, code 0x%x, HostCtrl %08x\n", cmd->code, regs->HostCtrl);
		if (regs->Mode & FATAL_ERR)
			printk("error code %02x\n", regs->Fail1);
	}

	idx = rrpriv->info->cmd_ctrl.pi;

	regs->CmdRing[idx] = *(u32*)(cmd);

	idx = (idx - 1) % CMD_RING_ENTRIES;
	rrpriv->info->cmd_ctrl.pi = idx;

	if (regs->Mode & FATAL_ERR)
		printk("error code %02x\n", regs->Fail1);
}


/*
 * Reset the board in a sensible manner. The NIC is already halted
 * when we get here and a spin-lock is held.
 */
static int rr_reset(struct device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs *regs;
	struct eeprom *hw = NULL;
	u32 start_pc;
	int i;

	rrpriv = (struct rr_private *)dev->priv;
	regs = rrpriv->regs;

	rr_load_firmware(dev);

	regs->TX_state = 0x01000000;
	regs->RX_state = 0xff800000;
	regs->AssistState = 0;
	regs->LocalCtrl = CLEAR_INTA;
	regs->BrkPt = 0x01;
	regs->Timer = 0;
	regs->TimerRef = 0;
	regs->DmaReadState = RESET_DMA;
	regs->DmaWriteState = RESET_DMA;
	regs->DmaWriteHostHi = 0;
	regs->DmaWriteHostLo = 0;
	regs->DmaReadHostHi = 0;
	regs->DmaReadHostLo = 0;
	regs->DmaReadLen = 0;
	regs->DmaWriteLen = 0;
	regs->DmaWriteLcl = 0;
	regs->DmaWriteIPchecksum = 0;
	regs->DmaReadLcl = 0;
	regs->DmaReadIPchecksum = 0;
	regs->PciState = 0; /* 0x90 for GE? */
	regs->Mode = SWAP_DATA;

#if 0
	/*
	 * Don't worry, this is just black magic.
	 */
	regs->RxBase = 0xdf000;
	regs->RxPrd = 0xdf000;
	regs->RxCon = 0xdf000;
	regs->TxBase = 0xce000;
	regs->TxPrd = 0xce000;
	regs->TxCon = 0xce000;
	regs->RxIndPro = 0;
	regs->RxIndCon = 0;
	regs->RxIndRef = 0;
	regs->TxIndPro = 0;
	regs->TxIndCon = 0;
	regs->TxIndRef = 0;
	regs->pad10[0] = 0xcc000;
	regs->DrCmndPro = 0;
	regs->DrCmndCon = 0;
	regs->DwCmndPro = 0;
	regs->DwCmndCon = 0;
	regs->DwCmndRef = 0;
	regs->DrDataPro = 0;
	regs->DrDataCon = 0;
	regs->DrDataRef = 0;
	regs->DwDataPro = 0;
	regs->DwDataCon = 0;
	regs->DwDataRef = 0;
#endif

	regs->MbEvent = 0xffffffff;
	regs->Event = 0;

	regs->TxPi = 0;
	regs->IpRxPi = 0;

	regs->EvtCon = 0;
	regs->EvtPrd = 0;

	rrpriv->info->evt_ctrl.pi = 0;

	for (i = 0; i < CMD_RING_ENTRIES; i++)
		regs->CmdRing[i] = 0;

	regs->PciState = 0;

	start_pc = read_eeprom_word(rrpriv, &hw->rncd_info.FwStart);

#if (DEBUG > 1)
	printk("%s: Executing firmware at address 0x%06x\n",
	       dev->name, start_pc);
#endif

	regs->Pc = start_pc + 0x800;
	udelay(5);

	regs->Pc = start_pc;

	return 0;
}


/*
 * Read a string from the EEPROM.
 */
static unsigned int read_eeprom(struct rr_private *rrpriv,
				unsigned long offset,
				unsigned char *buf,
				unsigned long length)
{
	struct rr_regs *regs = rrpriv->regs;
	u32 misc, io, host, i;

	io = regs->ExtIo;
	regs->ExtIo = 0;
	misc = regs->LocalCtrl;
	regs->LocalCtrl = 0;
	host = regs->HostCtrl;
	regs->HostCtrl |= HALT_NIC;

	for (i = 0; i < length; i++){
		regs->WinBase = (EEPROM_BASE + ((offset+i) << 3));
		buf[i] = (regs->WinData >> 24) & 0xff;
	}

	regs->HostCtrl = host;
	regs->LocalCtrl = misc;
	regs->ExtIo = io;

	return i;
}


/*
 * Shortcut to read one word (4 bytes) out of the EEPROM and convert
 * it to our CPU byte-order.
 */
static u32 read_eeprom_word(struct rr_private *rrpriv,
			    void * offset)
{
	u32 word;

	if ((read_eeprom(rrpriv, (unsigned long)offset,
			 (char *)&word, 4) == 4))
		return be32_to_cpu(word);
	return 0;
}


/*
 * Write a string to the EEPROM.
 *
 * This is only called when the firmware is not running.
 */
static unsigned int write_eeprom(struct rr_private *rrpriv,
				unsigned long offset,
				unsigned char *buf,
				unsigned long length)
{
	struct rr_regs *regs = rrpriv->regs;
	u32 misc, io, data, i, j, ready, error = 0;

	io = regs->ExtIo;
	regs->ExtIo = 0;
	misc = regs->LocalCtrl;
	regs->LocalCtrl = ENABLE_EEPROM_WRITE;

	for (i = 0; i < length; i++){
		regs->WinBase = (EEPROM_BASE + ((offset+i) << 3));
		data = buf[i] << 24;
		/*
		 * Only try to write the data if it is not the same
		 * value already.
		 */
		if ((regs->WinData & 0xff000000) != data){
			regs->WinData = data;
			ready = 0;
			j = 0;
			mb();
			while(!ready){
				udelay(1000);
				if ((regs->WinData & 0xff000000) == data)
					ready = 1;
				if (j++ > 5000){
					printk("data mismatch: %08x, "
					       "WinData %08x\n", data,
					       regs->WinData);
					ready = 1;
					error = 1;
				}
			}
		}
	}

	regs->LocalCtrl = misc;
	regs->ExtIo = io;

	return error;
}


__initfunc(static int rr_init(struct device *dev))
{
	struct rr_private *rrpriv;
	struct rr_regs *regs;
	u32 sram_size, rev;

	rrpriv = (struct rr_private *)dev->priv;
	regs = rrpriv->regs;

	rev = regs->FwRev;
	if (rev > 0x00020024)
		printk("  Firmware revision: %i.%i.%i\n", (rev >> 16),
		       ((rev >> 8) & 0xff), (rev & 0xff));
	else if (rev >= 0x00020000) {
		printk("  Firmware revision: %i.%i.%i (2.0.37 or "
		       "later is recommended)\n", (rev >> 16),
		       ((rev >> 8) & 0xff), (rev & 0xff));
	}else{
		printk("  Firmware revision too old: %i.%i.%i, please "
		       "upgrade to 2.0.37 or later.\n",
		       (rev >> 16), ((rev >> 8) & 0xff), (rev & 0xff));
		return -EFAULT;
		
	}

	printk("  Maximum receive rings %i\n", regs->MaxRxRng);

	sram_size = read_eeprom_word(rrpriv, (void *)8);
	printk("  SRAM size 0x%06x\n", sram_size);

	if (sysctl_rmem_max < 262144){
		printk("  Receive socket buffer limit too low (%i), "
		       "setting to 262144\n", sysctl_rmem_max);
		sysctl_rmem_max = 262144;
	}

	if (sysctl_wmem_max < 262144){
		printk("  Transmit socket buffer limit too low (%i), "
		       "setting to 262144\n", sysctl_wmem_max);
		sysctl_wmem_max = 262144;
	}

	return 0;
}


static int rr_init1(struct device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs *regs;
	u32 hostctrl;
	unsigned long myjif, flags, tmp_ptr;
	struct cmd cmd;
	short i;

	rrpriv = (struct rr_private *)dev->priv;
	regs = rrpriv->regs;

	spin_lock_irqsave(&rrpriv->lock, flags);

	hostctrl = regs->HostCtrl;
	regs->HostCtrl |= HALT_NIC;

	if (hostctrl & PARITY_ERR){
		printk("%s: Parity error halting NIC - this is serious!\n",
		       dev->name);
		spin_unlock_irqrestore(&rrpriv->lock, flags);
		return -EFAULT;
	}


	memset(rrpriv->rx_ctrl, 0, 256 * sizeof(struct ring_ctrl));
	memset(rrpriv->info, 0, sizeof(struct rr_info));

	tmp_ptr = virt_to_bus((void *)rrpriv->rx_ctrl);
#if (BITS_PER_LONG == 64)
	regs->RxRingHi = (tmp_ptr >> 32);
#else
	regs->RxRingHi = 0;
#endif
	regs->RxRingLo = ((tmp_ptr) & 0xffffffff);

	tmp_ptr = virt_to_bus((void *)rrpriv->info);
#if (BITS_PER_LONG == 64)
	regs->InfoPtrHi = (tmp_ptr >> 32);
#else
	regs->InfoPtrHi = 0;
#endif
	regs->InfoPtrLo = ((tmp_ptr) & 0xffffffff);

	rrpriv->info->evt_ctrl.entry_size = sizeof(struct event);
	rrpriv->info->evt_ctrl.entries = EVT_RING_ENTRIES;
	rrpriv->info->evt_ctrl.mode = 0;
	rrpriv->info->evt_ctrl.pi = 0;
	rrpriv->info->evt_ctrl.rngptr = virt_to_bus(rrpriv->evt_ring);

	rrpriv->info->cmd_ctrl.entry_size = sizeof(struct cmd);
	rrpriv->info->cmd_ctrl.entries = CMD_RING_ENTRIES;
	rrpriv->info->cmd_ctrl.mode = 0;
	rrpriv->info->cmd_ctrl.pi = 15;

	for (i = 0; i < CMD_RING_ENTRIES; i++) {
		regs->CmdRing[i] = 0;
	}

	for (i = 0; i < TX_RING_ENTRIES; i++) {
		rrpriv->tx_ring[i].size = 0;
		rrpriv->tx_ring[i].addr = 0;
		rrpriv->tx_skbuff[i] = 0;
	}

	rrpriv->info->tx_ctrl.entry_size = sizeof(struct tx_desc);
	rrpriv->info->tx_ctrl.entries = TX_RING_ENTRIES;
	rrpriv->info->tx_ctrl.mode = 0;
	rrpriv->info->tx_ctrl.pi = 0;
	rrpriv->info->tx_ctrl.rngptr = virt_to_bus(rrpriv->tx_ring);

	/*
	 * Set dirty_tx before we start receiving interrupts, otherwise
	 * the interrupt handler might think it is supposed to process
	 * tx ints before we are up and running, which may cause a null
	 * pointer access in the int handler.
	 */
	rrpriv->tx_full = 0;
	rrpriv->cur_rx = 0;
	rrpriv->dirty_rx = rrpriv->dirty_tx = 0;

	rr_reset(dev);

	regs->IntrTmr = 0x60;
	regs->WriteDmaThresh = 0x80 | 0x1f;
	regs->ReadDmaThresh = 0x80 | 0x1f;

	rrpriv->fw_running = 0;

	hostctrl &= ~(HALT_NIC | INVALID_INST_B | PARITY_ERR);
	regs->HostCtrl = hostctrl;

	spin_unlock_irqrestore(&rrpriv->lock, flags);

	udelay(1000);

	/*
	 * Now start the FirmWare.
	 */
	cmd.code = C_START_FW;
	cmd.ring = 0;
	cmd.index = 0;

	rr_issue_cmd(rrpriv, &cmd);

	/*
	 * Give the FirmWare time to chew on the `get running' command.
	 */
	myjif = jiffies + 5 * HZ;
	while ((jiffies < myjif) && !rrpriv->fw_running);

	for (i = 0; i < RX_RING_ENTRIES; i++) {
		struct sk_buff *skb;

		rrpriv->rx_ring[i].mode = 0;
		skb = alloc_skb(dev->mtu + HIPPI_HLEN, GFP_ATOMIC);
		rrpriv->rx_skbuff[i] = skb;
		/*
		 * Sanity test to see if we conflict with the DMA
		 * limitations of the Roadrunner.
		 */
		if ((((unsigned long)skb->data) & 0xfff) > ~65320)
			printk("skb alloc error\n");

#if (BITS_PER_LONG == 32)
		rrpriv->rx_ring[i].zero = 0;
#endif
		rrpriv->rx_ring[i].addr = virt_to_bus(skb->data);
		rrpriv->rx_ring[i].size = dev->mtu + HIPPI_HLEN;
	}

	rrpriv->rx_ctrl[4].entry_size = sizeof(struct rx_desc);
	rrpriv->rx_ctrl[4].entries = RX_RING_ENTRIES;
	rrpriv->rx_ctrl[4].mode = 8;
	rrpriv->rx_ctrl[4].pi = 0;
	rrpriv->rx_ctrl[4].rngptr = virt_to_bus(rrpriv->rx_ring);

	cmd.code = C_NEW_RNG;
	cmd.ring = 4;
	cmd.index = 0;
	rr_issue_cmd(rrpriv, &cmd);

#if 0
{
	u32 tmp;
	tmp = regs->ExtIo;
	regs->ExtIo = 0x80;
	
	i = jiffies + 1 * HZ;
	while (jiffies < i);
	regs->ExtIo = tmp;
}
#endif
	dev->tbusy = 0;
#if 0
	dev->interrupt = 0;
#endif
	dev->start = 1;
	return 0;
}


/*
 * All events are considered to be slow (RX/TX ints do not generate
 * events) and are handled here, outside the main interrupt handler,
 * to reduce the size of the handler.
 */
static u32 rr_handle_event(struct device *dev, u32 prodidx)
{
	struct rr_private *rrpriv;
	struct rr_regs *regs;
	u32 tmp, eidx;

	rrpriv = (struct rr_private *)dev->priv;
	regs = rrpriv->regs;
	eidx = rrpriv->info->evt_ctrl.pi;

	while (prodidx != eidx){
		switch (rrpriv->evt_ring[eidx].code){
		case E_NIC_UP:
			tmp = regs->FwRev;
			printk("%s: Firmware revision %i.%i.%i up and running\n",
			       dev->name, (tmp >> 16), ((tmp >> 8) & 0xff),
			       (tmp & 0xff));
			rrpriv->fw_running = 1;
			break;
		case E_LINK_ON:
			printk("%s: Optical link ON\n", dev->name);
			break;
		case E_LINK_OFF:
			printk("%s: Optical link OFF\n", dev->name);
			break;
		case E_RX_IDLE:
			printk("%s: RX data not moving\n", dev->name);
			break;
		case E_WATCHDOG:
			printk("%s: The watchdog is here to see us\n",
			       dev->name);
			break;
		/*
		 * TX events.
		 */
		case E_CON_REJ:
			printk("%s: Connection rejected\n", dev->name);
			rrpriv->stats.tx_aborted_errors++;
			break;
		case E_CON_TMOUT:
			printk("%s: Connection timeout\n", dev->name);
			break;
		case E_DISC_ERR:
			printk("%s: HIPPI disconnect error\n", dev->name);
			rrpriv->stats.tx_aborted_errors++;
			break;
		case E_TX_IDLE:
			printk("%s: Transmitter idle\n", dev->name);
			break;
		case E_TX_LINK_DROP:
			printk("%s: Link lost during transmit\n", dev->name);
			rrpriv->stats.tx_aborted_errors++;
			break;
		/*
		 * RX events.
		 */
		case E_VAL_RNG:		/* Should be ignored */
#if (DEBUG > 2)
			printk("%s: RX ring valid event\n", dev->name);
#endif
			regs->IpRxPi = RX_RING_ENTRIES - 1;
			break;
		case E_INV_RNG:
			printk("%s: RX ring invalid event\n", dev->name);
			break;
		case E_RX_RNG_OUT:
			printk("%s: Receive ring full\n", dev->name);
			break;

		case E_RX_PAR_ERR:
			printk("%s: Receive parity error.\n", dev->name);
			break;
		case E_RX_LLRC_ERR:
			printk("%s: Receive LLRC error.\n", dev->name);
			break;
		case E_PKT_LN_ERR:
			printk("%s: Receive packet length error.\n",
			       dev->name);
			break;
		default:
			printk("%s: Unhandled event 0x%02x\n",
			       dev->name, rrpriv->evt_ring[eidx].code);
		}
		eidx = (eidx + 1) % EVT_RING_ENTRIES;
	}

	rrpriv->info->evt_ctrl.pi = eidx;
	return eidx;
}


static int rx_int(struct device *dev, u32 rxlimit)
{
	struct rr_private *rrpriv = (struct rr_private *)dev->priv;
	u32 index, pkt_len;
	struct rr_regs *regs = rrpriv->regs;

	index = rrpriv->cur_rx;

	while(index != rxlimit){
		pkt_len = rrpriv->rx_ring[index].size;
#if (DEBUG > 2)
		printk("index %i, rxlimit %i\n", index, rxlimit);
		printk("len %x, mode %x\n", pkt_len,
		       rrpriv->rx_ring[index].mode);
#endif
#if 0
/*
 * I have never seen this occur
 */
		if(!(rrpriv->rx_skbuff[index])){
			printk("Trying to receive in empty skbuff\n");
			goto out;
		}
#endif

		if (pkt_len > 0){
			struct sk_buff *skb;

			if (pkt_len < PKT_COPY_THRESHOLD) {
				skb = alloc_skb(pkt_len, GFP_ATOMIC);
				if (skb == NULL){
					printk("%s: Out of memory deferring "
					       "packet\n", dev->name);
					rrpriv->stats.rx_dropped++;
					goto defer;
				}else
					memcpy(skb_put(skb, pkt_len),
					       rrpriv->rx_skbuff[index]->data,
					       pkt_len);
			}else{
				struct sk_buff *newskb;

				newskb = alloc_skb(dev->mtu + HIPPI_HLEN,
						   GFP_ATOMIC);
				if (newskb){
					skb = rrpriv->rx_skbuff[index];
					skb_put(skb, pkt_len);
					rrpriv->rx_skbuff[index] = newskb;
					rrpriv->rx_ring[index].addr = virt_to_bus(newskb->data);
				}else{
					printk("%s: Out of memory, deferring "
					       "packet\n", dev->name);
					rrpriv->stats.rx_dropped++;
					goto defer;
				}
			}
			skb->dev = dev;
			skb->protocol = hippi_type_trans(skb, dev);

			netif_rx(skb);		/* send it up */

			rrpriv->stats.rx_packets++;
			rrpriv->stats.rx_bytes += skb->len;
		}
	defer:
		rrpriv->rx_ring[index].mode = 0;
		rrpriv->rx_ring[index].size = dev->mtu + HIPPI_HLEN;

		if ((index & 7) == 7)
			regs->IpRxPi = index;

		index = (index + 1) % RX_RING_ENTRIES;
	}

	rrpriv->cur_rx = index;
	return index;
}


static void rr_interrupt(int irq, void *dev_id, struct pt_regs *ptregs)
{
	struct rr_private *rrpriv;
	struct rr_regs *regs;
	struct device *dev = (struct device *)dev_id;
	u32 prodidx, eidx, txcsmr, rxlimit, txcon;
	unsigned long flags;

	rrpriv = (struct rr_private *)dev->priv;
	regs = rrpriv->regs;

	if (!(regs->HostCtrl & RR_INT))
		return;

#if 0
	if (test_and_set_bit(0, (void*)&dev->interrupt) != 0) {
		printk("%s: Re-entering the interrupt handler.\n", dev->name);
		return;
	}
#endif

	spin_lock_irqsave(&rrpriv->lock, flags);

	prodidx = regs->EvtPrd;
	txcsmr = (prodidx >> 8) & 0xff;
	rxlimit = (prodidx >> 16) & 0xff;
	prodidx &= 0xff;

#if (DEBUG > 2)
	printk("%s: interrupt, prodidx = %i, eidx = %i\n", dev->name,
	       prodidx, rrpriv->info->evt_ctrl.pi);
#endif

	txcon = rrpriv->dirty_tx;
	if (txcsmr != txcon) {
		do {
			rrpriv->stats.tx_packets++;
			rrpriv->stats.tx_bytes +=rrpriv->tx_skbuff[txcon]->len;
			dev_kfree_skb(rrpriv->tx_skbuff[txcon]);

			rrpriv->tx_skbuff[txcon] = NULL;
			rrpriv->tx_ring[txcon].size = 0;
			rrpriv->tx_ring[txcon].addr = 0;
			rrpriv->tx_ring[txcon].mode = 0;

			txcon = (txcon + 1) % TX_RING_ENTRIES;
		} while (txcsmr != txcon);

		rrpriv->dirty_tx = txcon;
		if (rrpriv->tx_full && dev->tbusy &&
		    (((rrpriv->info->tx_ctrl.pi + 1) % TX_RING_ENTRIES)
		     != rrpriv->dirty_tx)){
			rrpriv->tx_full = 0;
			dev->tbusy = 0;
			mark_bh(NET_BH);
		}
	}

	rx_int(dev, rxlimit);

	eidx = rrpriv->info->evt_ctrl.pi;

	if (prodidx != eidx)
		eidx = rr_handle_event(dev, prodidx);

	eidx |= ((txcsmr << 8) | (rxlimit << 16));
	regs->EvtCon = eidx;

	spin_unlock_irqrestore(&rrpriv->lock, flags);

#if 0
	dev->interrupt = 0;
#endif
}


static int rr_open(struct device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs *regs;

	rrpriv = (struct rr_private *)dev->priv;
	regs = rrpriv->regs;

#if 0
	regs->HostCtrl |= (HALT_NIC | RR_CLEAR_INT);
#endif

	if (request_irq(dev->irq, rr_interrupt, SA_SHIRQ, rrpriv->name, dev))
	{
		printk(KERN_WARNING "%s: Requested IRQ %d is busy\n",
		       dev->name, dev->irq);
		return -EAGAIN;
	}

	rrpriv->rx_ctrl = kmalloc(256*sizeof(struct ring_ctrl),
				  GFP_KERNEL | GFP_DMA);
	rrpriv->info = kmalloc(sizeof(struct rr_info), GFP_KERNEL | GFP_DMA);

	rr_init1(dev);

	dev->tbusy = 0;
#if 0
	dev->interrupt = 0;
#endif
	dev->start = 1;

	MOD_INC_USE_COUNT;
	return 0;
}


static void rr_dump(struct device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs *regs;
	u32 index, cons;
	short i;
	int len;

	rrpriv = (struct rr_private *)dev->priv;
	regs = rrpriv->regs;

	printk("%s: dumping NIC TX rings\n", dev->name);

	printk("RxPrd %08x, TxPrd %02x, EvtPrd %08x, TxPi %02x, TxCtrlPi %02x\n",
	       regs->RxPrd, regs->TxPrd, regs->EvtPrd, regs->TxPi,
	       rrpriv->info->tx_ctrl.pi);

	printk("Error code 0x%x\n", regs->Fail1);

	index = (((regs->EvtPrd >> 8) & 0xff ) - 1) % EVT_RING_ENTRIES;
	cons = rrpriv->dirty_tx;
	printk("TX ring index %i, TX consumer %i\n",
	       index, cons);

	if (rrpriv->tx_skbuff[index]){
		len = min(0x80, rrpriv->tx_skbuff[index]->len);
		printk("skbuff for index %i is valid - dumping data (0x%x bytes - DMA len 0x%x)\n", index, len, rrpriv->tx_ring[index].size);
		for (i = 0; i < len; i++){
			if (!(i & 7))
				printk("\n");
			printk("%02x ", (unsigned char) rrpriv->tx_skbuff[index]->data[i]);
		}
		printk("\n");
	}

	if (rrpriv->tx_skbuff[cons]){
		len = min(0x80, rrpriv->tx_skbuff[cons]->len);
		printk("skbuff for cons %i is valid - dumping data (0x%x bytes - skbuff len 0x%x)\n", cons, len, rrpriv->tx_skbuff[cons]->len);
		printk("mode 0x%x, size 0x%x,\n phys %08x (virt %08x), skbuff-addr %08x, truesize 0x%x\n",
		       rrpriv->tx_ring[cons].mode,
		       rrpriv->tx_ring[cons].size,
		       rrpriv->tx_ring[cons].addr,
		       (unsigned int)bus_to_virt(rrpriv->tx_ring[cons].addr),
		       (unsigned int)rrpriv->tx_skbuff[cons]->data,
		       (unsigned int)rrpriv->tx_skbuff[cons]->truesize);
		for (i = 0; i < len; i++){
			if (!(i & 7))
				printk("\n");
			printk("%02x ", (unsigned char)rrpriv->tx_ring[cons].size);
		}
		printk("\n");
	}

	printk("dumping TX ring info:\n");
	for (i = 0; i < TX_RING_ENTRIES; i++)
		printk("mode 0x%x, size 0x%x, phys-addr %08x\n",
		       rrpriv->tx_ring[i].mode,
		       rrpriv->tx_ring[i].size,
		       rrpriv->tx_ring[i].addr);

}


static int rr_close(struct device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs *regs;
	u32 tmp;
	short i;

	dev->start = 0;
	set_bit(0, (void*)&dev->tbusy);

	rrpriv = (struct rr_private *)dev->priv;
	regs = rrpriv->regs;

	/*
	 * Lock to make sure we are not cleaning up while another CPU
	 * handling interrupts.
	 */
	spin_lock(&rrpriv->lock);

	tmp = regs->HostCtrl;
	if (tmp & NIC_HALTED){
		printk("%s: NIC already halted\n", dev->name);
		rr_dump(dev);
	}else
		tmp |= HALT_NIC;
	regs->HostCtrl = tmp;

	rrpriv->fw_running = 0;

	regs->TxPi = 0;
	regs->IpRxPi = 0;

	regs->EvtCon = 0;
	regs->EvtPrd = 0;

	for (i = 0; i < CMD_RING_ENTRIES; i++)
		regs->CmdRing[i] = 0;

	rrpriv->info->tx_ctrl.entries = 0;
	rrpriv->info->cmd_ctrl.pi = 0;
	rrpriv->info->evt_ctrl.pi = 0;
	rrpriv->rx_ctrl[4].entries = 0;

	for (i = 0; i < TX_RING_ENTRIES; i++) {
		if (rrpriv->tx_skbuff[i]) {
			rrpriv->tx_ring[i].size = 0;
			rrpriv->tx_ring[i].addr = 0;
			dev_kfree_skb(rrpriv->tx_skbuff[i]);
		}
	}

	for (i = 0; i < RX_RING_ENTRIES; i++) {
		if (rrpriv->rx_skbuff[i]) {
			rrpriv->rx_ring[i].size = 0;
			rrpriv->rx_ring[i].addr = 0;
			dev_kfree_skb(rrpriv->rx_skbuff[i]);
		}
	}

	kfree(rrpriv->rx_ctrl);
	kfree(rrpriv->info);

	free_irq(dev->irq, dev);
	spin_unlock(&rrpriv->lock);

	MOD_DEC_USE_COUNT;
	return 0;
}


static int rr_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct rr_private *rrpriv = (struct rr_private *)dev->priv;
	struct rr_regs *regs = rrpriv->regs;
	struct ring_ctrl *txctrl;
	unsigned long flags;
	u32 index, len = skb->len;
	u32 *ifield;
	struct sk_buff *new_skb;

	/*
	 * We probably need to deal with tbusy here to prevent overruns.
	 */

	if (skb_headroom(skb) < 8){
		printk("incoming skb too small - reallocating\n");
		if (!(new_skb = dev_alloc_skb(len + 8))) {
			dev_kfree_skb(skb);
			dev->tbusy = 0;
			return -EBUSY;
		}
		skb_reserve(new_skb, 8);
		skb_put(new_skb, len);
		memcpy(new_skb->data, skb->data, len);
		dev_kfree_skb(skb);
		skb = new_skb;
	}

	ifield = (u32 *)skb_push(skb, 8);

	ifield[0] = 0;
	ifield[1] = skb->private.ifield;

	/*
	 * We don't need the lock before we are actually going to start
	 * fiddling with the control blocks.
	 */
	spin_lock_irqsave(&rrpriv->lock, flags);

	txctrl = &rrpriv->info->tx_ctrl;

	index = txctrl->pi;

	rrpriv->tx_skbuff[index] = skb;
	rrpriv->tx_ring[index].addr = virt_to_bus(skb->data);
	rrpriv->tx_ring[index].size = len + 8; /* include IFIELD */
	rrpriv->tx_ring[index].mode = PACKET_START | PACKET_END;
	txctrl->pi = (index + 1) % TX_RING_ENTRIES;
	regs->TxPi = txctrl->pi;

	if (txctrl->pi == rrpriv->dirty_tx){
		rrpriv->tx_full = 1;
		set_bit(0, (void*)&dev->tbusy);
	}

	spin_unlock_irqrestore(&rrpriv->lock, flags);

	dev->trans_start = jiffies;
	return 0;
}


static struct net_device_stats *rr_get_stats(struct device *dev)
{
	struct rr_private *rrpriv;

	rrpriv = (struct rr_private *)dev->priv;

	return(&rrpriv->stats);
}


/*
 * Read the firmware out of the EEPROM and put it into the SRAM
 * (or from user space - later)
 *
 * This operation requires the NIC to be halted and is performed with
 * interrupts disabled and with the spinlock hold.
 */
static int rr_load_firmware(struct device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs *regs;
	int i, j;
	u32 localctrl, eptr, sptr, segptr, len, tmp;
	u32 p2len, p2size, nr_seg, revision, io, sram_size;
	struct eeprom *hw = NULL;

	rrpriv = (struct rr_private *)dev->priv;
	regs = rrpriv->regs;

	if (dev->flags & IFF_UP)
		return -EBUSY;

	if (!(regs->HostCtrl & NIC_HALTED)){
		printk("%s: Trying to load firmware to a running NIC.\n", 
		       dev->name);
		return -EBUSY;
	}

	localctrl = regs->LocalCtrl;
	regs->LocalCtrl = 0;

	regs->EvtPrd = 0;
	regs->RxPrd = 0;
	regs->TxPrd = 0;

	/*
	 * First wipe the entire SRAM, otherwise we might run into all
	 * kinds of trouble ... sigh, this took almost all afternoon
	 * to track down ;-(
	 */
	io = regs->ExtIo;
	regs->ExtIo = 0;
	sram_size = read_eeprom_word(rrpriv, (void *)8);

	for (i = 200; i < sram_size / 4; i++){
		regs->WinBase = i * 4;
		regs->WinData = 0;
	}
	regs->ExtIo = io;

	eptr = read_eeprom_word(rrpriv, &hw->rncd_info.AddrRunCodeSegs);
	eptr = ((eptr & 0x1fffff) >> 3);

	p2len = read_eeprom_word(rrpriv, (void *)(0x83*4));
	p2len = (p2len << 2);
	p2size = read_eeprom_word(rrpriv, (void *)(0x84*4));
	p2size = ((p2size & 0x1fffff) >> 3);

	if ((eptr < p2size) || (eptr > (p2size + p2len))){
		printk("%s: eptr is invalid\n", dev->name);
		goto out;
	}

	revision = read_eeprom_word(rrpriv, &hw->manf.HeaderFmt);

	if (revision != 1){
		printk("%s: invalid firmware format (%i)\n",
		       dev->name, revision);
		goto out;
	}

	nr_seg = read_eeprom_word(rrpriv, (void *)eptr);
	eptr +=4;
#if (DEBUG > 1)
	printk("%s: nr_seg %i\n", dev->name, nr_seg);
#endif

	for (i = 0; i < nr_seg; i++){
		sptr = read_eeprom_word(rrpriv, (void *)eptr);
		eptr += 4;
		len = read_eeprom_word(rrpriv, (void *)eptr);
		eptr += 4;
		segptr = read_eeprom_word(rrpriv, (void *)eptr);
		segptr = ((segptr & 0x1fffff) >> 3);
		eptr += 4;
#if (DEBUG > 1)
		printk("%s: segment %i, sram address %06x, length %04x, segptr %06x\n",
		       dev->name, i, sptr, len, segptr);
#endif
		for (j = 0; j < len; j++){
			tmp = read_eeprom_word(rrpriv, (void *)segptr);
			regs->WinBase = sptr;
			regs->WinData = tmp;
			segptr += 4;
			sptr += 4;
		}
	}

out:
	regs->LocalCtrl = localctrl;
	return 0;
}


static int rr_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
	struct rr_private *rrpriv;
	unsigned char *image, *oldimage;
	unsigned int i;
	int error = -EOPNOTSUPP;

	rrpriv = (struct rr_private *)dev->priv;

	spin_lock(&rrpriv->lock);

	switch(cmd){
	case SIOCRRGFW:
		if (!suser()){
			error = -EPERM;
			goto out;
		}

		if (rrpriv->fw_running){
			printk("%s: Firmware already running\n", dev->name);
			error = -EPERM;
			goto out;
		}

		image = kmalloc(EEPROM_WORDS * sizeof(u32), GFP_KERNEL);
		if (!image){
			printk(KERN_ERR "%s: Unable to allocate memory "
			       "for EEPROM image\n", dev->name);
			error = -ENOMEM;
			goto out;
		}
		i = read_eeprom(rrpriv, 0, image, EEPROM_BYTES);
		if (i != EEPROM_BYTES){
			kfree(image);
			printk(KERN_ERR "%s: Error reading EEPROM\n",
			       dev->name);
			error = -EFAULT;
			goto out;
		}
		error = copy_to_user(rq->ifr_data, image, EEPROM_BYTES);
		if (error)
			error = -EFAULT;
		kfree(image);
		break;
	case SIOCRRPFW:
		if (!suser()){
			error = -EPERM;
			goto out;
		}

		if (rrpriv->fw_running){
			printk("%s: Firmware already running\n", dev->name);
			error = -EPERM;
			goto out;
		}

		image = kmalloc(EEPROM_WORDS * sizeof(u32), GFP_KERNEL);
		if (!image){
			printk(KERN_ERR "%s: Unable to allocate memory "
			       "for EEPROM image\n", dev->name);
			error = -ENOMEM;
			goto out;
		}

		oldimage = kmalloc(EEPROM_WORDS * sizeof(u32), GFP_KERNEL);
		if (!image){
			printk(KERN_ERR "%s: Unable to allocate memory "
			       "for old EEPROM image\n", dev->name);
			error = -ENOMEM;
			goto out;
		}

		error = copy_from_user(image, rq->ifr_data, EEPROM_BYTES);
		if (error)
			error = -EFAULT;

		printk("%s: Updating EEPROM firmware\n", dev->name);

		error = write_eeprom(rrpriv, 0, image, EEPROM_BYTES);
		if (error)
			printk(KERN_ERR "%s: Error writing EEPROM\n",
			       dev->name);

		i = read_eeprom(rrpriv, 0, oldimage, EEPROM_BYTES);
		if (i != EEPROM_BYTES)
			printk(KERN_ERR "%s: Error reading back EEPROM "
			       "image\n", dev->name);

		error = memcmp(image, oldimage, EEPROM_BYTES);
		if (error){
			printk(KERN_ERR "%s: Error verifying EEPROM image\n",
			       dev->name);
			error = -EFAULT;
		}

		kfree(image);
		kfree(oldimage);
		break;
	case SIOCRRID:
		error = put_user(0x52523032, (int *)(&rq->ifr_data[0]));
		if (error)
			error = -EFAULT;
		break;
	default:
	}

 out:
	spin_unlock(&rrpriv->lock);
	return error;
}


/*
 * Local variables:
 * compile-command: "gcc -D__SMP__ -D__KERNEL__ -I../../include -Wall -Wstrict-prototypes -O2 -pipe -fomit-frame-pointer -fno-strength-reduce -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -DCPU=686 -c rrunner.c"
 * End:
 */
