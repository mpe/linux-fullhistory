/*
 *  sktr.c: A network driver for the SysKonnect Token Ring ISA/PCI Adapters.
 *
 *  Written 1997 by Christoph Goos
 *
 *  A fine result of the Linux Systems Network Architecture Project.
 *  http://samba.anu.edu.au/linux-sna/
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU Public License, incorporated herein by reference.
 *
 *  This device driver works with the following SysKonnect adapters:
 *	- SysKonnect TR4/16(+) ISA	(SK-4190)
 *	- SysKonnect TR4/16(+) PCI	(SK-4590)
 *	- SysKonnect TR4/16 PCI		(SK-4591)
 *
 *  Sources:
 *  	- The hardware related parts of this driver are take from
 *  	  the SysKonnect Token Ring driver for Windows NT.
 *  	- I used the IBM Token Ring driver 'ibmtr.c' as a base for this
 *  	  driver, as well as the 'skeleton.c' driver by Donald Becker.
 *  	- Also various other drivers in the linux source tree were taken
 *  	  as samples for some tasks.
 *
 *  Maintainer(s):
 *    JS        Jay Schulist            jschlst@samba.anu.edu.au
 *    CG	Christoph Goos		cgoos@syskonnect.de
 *
 *  Modification History:
 *	29-Aug-97	CG	Created
 *	04-Apr-98	CG	Fixed problems caused by tok_timer_check
 *	10-Apr-98	CG	Fixed lockups at cable disconnection
 *	27-May-98	JS	Formated to Linux Kernel Format
 *	31-May-98	JS	Hacked in PCI support
 *	16-Jun-98	JS	Modulized for multiple cards with one driver
 *
 *  To do:
 *    1. Selectable 16 Mbps or 4Mbps
 *    2. Multi/Broadcast packet handling
 *
 */

static const char *version = "sktr.c: v1.01 08/29/97 by Christoph Goos\n";

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/time.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/trdevice.h>

#include "sktr.h"		/* Our Stuff */
#include "sktr_firmware.h"	/* SysKonnect adapter firmware */

/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int sktr_portlist[] __initdata = {
	0x0A20, 0x1A20, 0x0B20, 0x1B20, 0x0980, 0x1980, 0x0900, 0x1900,
	0
};

/* A zero-terminated list of IRQs to be probed. 
 * Used again after initial probe for sktr_chipset_init, called from sktr_open.
 */
static unsigned short sktr_irqlist[] = {
	3, 5, 9, 10, 11, 12, 15,
	0
};

/* A zero-terminated list of DMAs to be probed. */
static int sktr_dmalist[] __initdata = {
	5, 6, 7,
	0
};

/* Card names */
static char *pci_cardname = "SK NET TR 4/16 PCI\0";
static char *isa_cardname = "SK NET TR 4/16 ISA\0";
static char *AdapterName;

/* Use 0 for production, 1 for verification, 2 for debug, and
 * 3 for very verbose debug.
 */
#ifndef SKTR_DEBUG
#define SKTR_DEBUG 1
#endif
static unsigned int sktr_debug = SKTR_DEBUG;

/* The number of low I/O ports used by the tokencard. */
#define SKTR_IO_EXTENT 32

/* Index to functions, as function prototypes.
 * Alphabetical by function name.
 */

/* "B" */
static int      sktr_bringup_diags(struct device *dev);
/* "C" */
static void	sktr_cancel_tx_queue(struct net_local* tp);
static int 	sktr_chipset_init(struct device *dev);
static void 	sktr_chk_irq(struct device *dev);
static unsigned char sktr_chk_frame(struct device *dev, unsigned char *Addr);
static void 	sktr_chk_outstanding_cmds(struct device *dev);
static void 	sktr_chk_src_addr(unsigned char *frame, unsigned char *hw_addr);
static unsigned char sktr_chk_ssb(struct net_local *tp, unsigned short IrqType);
static int 	sktr_close(struct device *dev);
static void 	sktr_cmd_status_irq(struct device *dev);
/* "D" */
static void 	sktr_disable_interrupts(struct device *dev);
static void 	sktr_dump(unsigned char *Data, int length);
/* "E" */
static void 	sktr_enable_interrupts(struct device *dev);
static void 	sktr_exec_cmd(struct device *dev, unsigned short Command);
static void 	sktr_exec_sifcmd(struct device *dev, unsigned int WriteValue);
/* "F" */
static unsigned char *sktr_fix_srouting(unsigned char *buf, short *FrameLen);
/* "G" */
static struct enet_statistics *sktr_get_stats(struct device *dev);
/* "H" */
static void 	sktr_hardware_send_packet(struct device *dev,
			struct net_local* tp);
/* "I" */
static int 	sktr_init_adapter(struct device *dev);
static int 	sktr_init_card(struct device *dev);
static void 	sktr_init_ipb(struct net_local *tp);
static void 	sktr_init_net_local(struct device *dev);
static void 	sktr_init_opb(struct net_local *tp);
static void 	sktr_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int 	sktr_isa_chk_card(struct device *dev, int ioaddr);
static int      sktr_isa_chk_ioaddr(int ioaddr);
/* "O" */
static int 	sktr_open(struct device *dev);
static void	sktr_open_adapter(struct device *dev);
/* "P" */
static int 	sktr_pci_chk_card(struct device *dev);
int 		sktr_probe(struct device *dev);
static int 	sktr_probe1(struct device *dev, int ioaddr);
/* "R" */
static void 	sktr_rcv_status_irq(struct device *dev);
static void 	sktr_read_addr(struct device *dev, unsigned char *Address);
static void 	sktr_read_ptr(struct device *dev);
static void 	sktr_read_ram(struct device *dev, unsigned char *Data,
			unsigned short Address, int Length);
static int 	sktr_reset_adapter(struct device *dev);
static void 	sktr_reset_interrupt(struct device *dev);
static void 	sktr_ring_status_irq(struct device *dev);
/* "S" */
static int 	sktr_send_packet(struct sk_buff *skb, struct device *dev);
static void 	sktr_set_multicast_list(struct device *dev);
/* "T" */
static void 	sktr_timer_chk(unsigned long data);
static void 	sktr_timer_end_wait(unsigned long data);
static void 	sktr_tx_status_irq(struct device *dev);
/* "U" */
static void 	sktr_update_rcv_stats(struct net_local *tp,
			unsigned char DataPtr[], unsigned int Length);
/* "W" */
static void 	sktr_wait(unsigned long time);
static void 	sktr_write_rpl_status(RPL *rpl, unsigned int Status);
static void 	sktr_write_tpl_status(TPL *tpl, unsigned int Status);

/*
 * Check for a network adapter of this type, and return '0' if one exists.
 * If dev->base_addr == 0, probe all likely locations.
 * If dev->base_addr == 1, always return failure.
 */
__initfunc(int sktr_probe(struct device *dev))
{
	int i;
	int base_addr = dev ? dev->base_addr : 0;

	if(base_addr > 0x1ff)    /* Check a single specified location. */
		return (sktr_probe1(dev, base_addr));
	else if(base_addr != 0)  /* Don't probe at all. */
		return (-ENXIO);

	for(i = 0; sktr_portlist[i]; i++)
	{
		int ioaddr = sktr_portlist[i];
		if(check_region(ioaddr, SKTR_IO_EXTENT))
			continue;
		if(sktr_probe1(dev, ioaddr))
		{
#ifndef MODULE
                        tr_freedev(dev);
#endif
                }
		else
			return (0);
	}

	return (-ENODEV);
}

/*
 * Detect and setup the PCI SysKonnect TR cards in slot order.
 */
__initfunc(static int sktr_pci_chk_card(struct device *dev))
{
	static int pci_index = 0;
	unsigned char pci_bus, pci_device_fn;

	if(!pci_present())
		return (-1);	/* No PCI present. */

	for(; pci_index < 0xff; pci_index++)
	{
		unsigned int pci_irq_line;
		struct pci_dev *pdev;
		unsigned short pci_command, new_command, vendor, device;
		unsigned int pci_ioaddr;

		if(pcibios_find_class(PCI_CLASS_NETWORK_TOKEN_RING << 8,
			pci_index, &pci_bus, &pci_device_fn)
			!= PCIBIOS_SUCCESSFUL)
		{
			break;
		}

		pcibios_read_config_word(pci_bus, pci_device_fn,
						PCI_VENDOR_ID, &vendor);
		pcibios_read_config_word(pci_bus, pci_device_fn,
						PCI_DEVICE_ID, &device);

		pdev		= pci_find_slot(pci_bus, pci_device_fn);
		pci_irq_line 	= pdev->irq;
		pci_ioaddr 	= pdev->base_address[0];

		pcibios_read_config_word(pci_bus, pci_device_fn,
						PCI_COMMAND, &pci_command);

		/* Remove I/O space marker in bit 0. */
		pci_ioaddr &= ~3;

		if(vendor != PCI_VENDOR_ID_SK)
			continue;
		if(device != PCI_DEVICE_ID_SK_TR)
			continue;
		if(check_region(pci_ioaddr, SKTR_IO_EXTENT))
			continue;
		request_region(pci_ioaddr, SKTR_IO_EXTENT, pci_cardname);
		if(request_irq(pdev->irq, sktr_interrupt, SA_SHIRQ,
				pci_cardname, dev))
			return (-ENODEV); /* continue; ?? */

		AdapterName = pci_cardname;

		new_command = (pci_command|PCI_COMMAND_MASTER|PCI_COMMAND_IO);

		if(pci_command != new_command)
		{
			printk("The PCI BIOS has not enabled this"
				"device! Updating PCI command %4.4x->%4.4x.\n",
			  	pci_command, new_command);
			pcibios_write_config_word(pci_bus, pci_device_fn,
				PCI_COMMAND, new_command);
		}

		/* At this point we have found a valid PCI TR card. */
		dev->base_addr	= pci_ioaddr;
		dev->irq 	= pci_irq_line;
		dev->dma	= 0;

		printk("%s: %s found at %#4x, using IRQ %d.\n",
			dev->name, AdapterName, pci_ioaddr, dev->irq);

		return (0);
	}

	return (-1);
}

/*
 * Detect and setup the ISA SysKonnect TR cards.
 */
__initfunc(static int sktr_isa_chk_card(struct device *dev, int ioaddr))
{
	int i, err;
	unsigned long flags;

	err = sktr_isa_chk_ioaddr(ioaddr);
	if(err < 0)
		return (-ENODEV);

        if(virt_to_bus((void*)((unsigned long)dev->priv+sizeof(struct net_local)))
		> ISA_MAX_ADDRESS)
        {
                printk("%s: Memory not accessible for DMA\n", dev->name);
                kfree(dev->priv);
                return (-EAGAIN);
        }

	AdapterName = isa_cardname;

        /* Grab the region so that no one else tries to probe our ioports. */
        request_region(ioaddr, SKTR_IO_EXTENT, AdapterName);
        dev->base_addr = ioaddr;

        /* Autoselect IRQ and DMA if dev->irq == 0 */
        if(dev->irq == 0)
        {
                for(i = 0; sktr_irqlist[i] != 0; i++)
                {
                        dev->irq = sktr_irqlist[i];
                        err = request_irq(dev->irq, &sktr_interrupt, 0, AdapterName, dev);
                        if(!err)
				break;
                }

                if(sktr_irqlist[i] == 0)
                {
                        printk("%s: AutoSelect no IRQ available\n", dev->name);
                        return (-EAGAIN);
                }
        }
        else
        {
                err = request_irq(dev->irq, &sktr_interrupt, 0, AdapterName, dev);
		if(err)
                {
                        printk("%s: Selected IRQ not available\n", dev->name);
                        return (-EAGAIN);
                }
        }

        /* Always allocate the DMA channel after IRQ and clean up on failure */
        if(dev->dma == 0)
        {
                for(i = 0; sktr_dmalist[i] != 0; i++)
                {
			dev->dma = sktr_dmalist[i];
                        err = request_dma(dev->dma, AdapterName);
                        if(!err)
                                break;
                }

                if(dev->dma == 0)
                {
                        printk("%s: AutoSelect no DMA available\n", dev->name);
                        free_irq(dev->irq, NULL);
                        return (-EAGAIN);
                }
        }
        else
        {
                err = request_dma(dev->dma, AdapterName);
                if(err)
                {
                        printk("%s: Selected DMA not available\n", dev->name);
                        free_irq(dev->irq, NULL);
                        return (-EAGAIN);
                }
        }

	flags=claim_dma_lock();
	disable_dma(dev->dma);
        set_dma_mode(dev->dma, DMA_MODE_CASCADE);
        enable_dma(dev->dma);
        release_dma_lock(flags);

	printk("%s: %s found at %#4x, using IRQ %d and DMA %d.\n",
                dev->name, AdapterName, ioaddr, dev->irq, dev->dma);

	return (0);
}

__initfunc(static int sktr_probe1(struct device *dev, int ioaddr))
{
	static unsigned version_printed = 0;
	struct net_local *tp;
	int err;

	if(sktr_debug && version_printed++ == 0)
		printk("%s", version);

#ifndef MODULE
	dev = init_trdev(dev, 0);
	if(dev == NULL)
		return (-ENOMEM);
#endif

	err = sktr_pci_chk_card(dev);
	if(err < 0)
	{
		err = sktr_isa_chk_card(dev, ioaddr);
		if(err < 0)
			return (-ENODEV);
	}

	/* Setup this devices private information structure */
	tp = (struct net_local *)kmalloc(sizeof(struct net_local), GFP_KERNEL | GFP_DMA);
	if(tp == NULL)
		return (-ENOMEM);
	memset(tp, 0, sizeof(struct net_local));

	dev->priv		= tp;
	dev->init               = sktr_init_card;
        dev->open               = sktr_open;
        dev->stop               = sktr_close;
        dev->hard_start_xmit    = sktr_send_packet;
        dev->get_stats          = sktr_get_stats;
        dev->set_multicast_list = &sktr_set_multicast_list;

        return (0);
}

/* Dummy function */
__initfunc(static int sktr_init_card(struct device *dev))
{
	if(sktr_debug > 3)
		printk("%s: sktr_init_card\n", dev->name);

	return (0);
}

/*
 * This function tests if an adapter is really installed at the
 * given I/O address. Return negative if no adapter at IO addr.
 */
__initfunc(static int sktr_isa_chk_ioaddr(int ioaddr))
{
	unsigned char old, chk1, chk2;

	old = inb(ioaddr + SIFADR);	/* Get the old SIFADR value */

	chk1 = 0;	/* Begin with check value 0 */
	do {
		/* Write new SIFADR value */
		outb(chk1, ioaddr + SIFADR);

		/* Read, invert and write */
		chk2 = inb(ioaddr + SIFADD);
		chk2 ^= 0x0FE;
		outb(chk2, ioaddr + SIFADR);

		/* Read, invert and compare */
		chk2 = inb(ioaddr + SIFADD);
		chk2 ^= 0x0FE;

		if(chk1 != chk2)
			return (-1);	/* No adapter */

		chk1 -= 2;
	} while(chk1 != 0);	/* Repeat 128 times (all byte values) */

    	/* Restore the SIFADR value */
	outb(old, ioaddr + SIFADR);

	return (0);
}

/*
 * Open/initialize the board. This is called sometime after
 * booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
static int sktr_open(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;
	int err;

	/* Reset the hardware here. Don't forget to set the station address. */
	err = sktr_chipset_init(dev);
	if(err)
	{
		printk(KERN_INFO "%s: Chipset initialization error\n", 
			dev->name);
		return (-1);
	}

	dev->addr_len = 6;
	sktr_read_addr(dev, (unsigned char*)dev->dev_addr);

	init_timer(&tp->timer);
	tp->timer.expires	= jiffies + 30*HZ;
	tp->timer.function	= sktr_timer_end_wait;
	tp->timer.data		= (unsigned long)dev;
	tp->timer.next		= NULL;
	tp->timer.prev		= NULL;
	add_timer(&tp->timer);

	sktr_read_ptr(dev);
	sktr_enable_interrupts(dev);
	sktr_open_adapter(dev);

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 0;

	/* Wait for interrupt from hardware. If interrupt does not come,
	 * there will be a timeout from the timer.
	 */
	tp->Sleeping = 1;
	interruptible_sleep_on(&tp->wait_for_tok_int);
	del_timer(&tp->timer);

	/* If AdapterVirtOpenFlag is 1, the adapter is now open for use */
	if(tp->AdapterVirtOpenFlag == 0)
	{
		sktr_disable_interrupts(dev);
		return (-1);
	}

	dev->start = 1;

	tp->StartTime = jiffies;

	/* Start function control timer */
	tp->timer.expires	= jiffies + 2*HZ;
	tp->timer.function	= sktr_timer_chk;
	tp->timer.data		= (unsigned long)dev;
	add_timer(&tp->timer);

#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif

	return (0);
}

/*
 * Timeout function while waiting for event
 */
static void sktr_timer_end_wait(unsigned long data)
{
	struct device *dev = (struct device*)data;
	struct net_local *tp = (struct net_local *)dev->priv;

	if(tp->Sleeping)
	{
		tp->Sleeping = 0;
		wake_up_interruptible(&tp->wait_for_tok_int);
	}

	return;
}

/*
 * Initialize the chipset
 */
static int sktr_chipset_init(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;
	unsigned char PosReg, Tmp;
	int i, err;

	sktr_init_ipb(tp);
	sktr_init_opb(tp);
	sktr_init_net_local(dev);

	/* Set pos register: selects irq and dma channel.
	 * Only for ISA bus adapters.
	 */
	if(dev->dma > 0)
	{
		PosReg = 0;
		for(i = 0; sktr_irqlist[i] != 0; i++)
		{
			if(sktr_irqlist[i] == dev->irq)
				break;
		}

		/* Choose default cycle time, 500 nsec   */
		PosReg |= CYCLE_TIME << 2;
		PosReg |= i << 4;
		i = dev->dma - 5;
		PosReg |= i;

		if(tp->DataRate == SPEED_4)
			PosReg |= LINE_SPEED_BIT;
		else
			PosReg &= ~LINE_SPEED_BIT;

		outb(PosReg, dev->base_addr + POSREG);
		Tmp = inb(dev->base_addr + POSREG);
		if((Tmp & ~CYCLE_TIME) != (PosReg & ~CYCLE_TIME))
			printk(KERN_INFO "%s: POSREG error\n", dev->name);
	}

	err = sktr_reset_adapter(dev);
	if(err < 0)
		return (-1);

	err = sktr_bringup_diags(dev);
	if(err < 0)
		return (-1);

	err = sktr_init_adapter(dev);
	if(err < 0)
		return (-1);

	return (0);
}

/*
 * Initializes the net_local structure.
 */
static void sktr_init_net_local(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;
	int i;

	tp->scb.CMD	= 0;
	tp->scb.Parm[0] = 0;
	tp->scb.Parm[1] = 0;

	tp->ssb.STS	= 0;
	tp->ssb.Parm[0] = 0;
	tp->ssb.Parm[1] = 0;
	tp->ssb.Parm[2] = 0;

	tp->CMDqueue	= 0;

	tp->AdapterOpenFlag	= 0;
	tp->AdapterVirtOpenFlag = 0;
	tp->ScbInUse		= 0;
	tp->OpenCommandIssued	= 0;
	tp->ReOpenInProgress	= 0;
	tp->HaltInProgress	= 0;
	tp->TransmitHaltScheduled = 0;
	tp->LobeWireFaultLogged	= 0;
	tp->LastOpenStatus	= 0;
	tp->MaxPacketSize	= DEFAULT_PACKET_SIZE;

	skb_queue_head_init(&tp->SendSkbQueue);
	tp->QueueSkb = MAX_TX_QUEUE;

	/* Create circular chain of transmit lists */
	for (i = 0; i < TPL_NUM; i++)
	{
		tp->Tpl[i].NextTPLAddr = htonl((unsigned long) virt_to_bus(&tp->Tpl[(i+1) % TPL_NUM]));
		tp->Tpl[i].Status	= 0;
		tp->Tpl[i].FrameSize	= 0;
		tp->Tpl[i].FragList[0].DataCount	= 0;
		tp->Tpl[i].FragList[0].DataAddr		= 0;
		tp->Tpl[i].NextTPLPtr	= &tp->Tpl[(i+1) % TPL_NUM];
		tp->Tpl[i].MData	= NULL;
		tp->Tpl[i].TPLIndex	= i;
		tp->Tpl[i].BusyFlag	= 0;
	}

	tp->TplFree = tp->TplBusy = &tp->Tpl[0];

	/* Create circular chain of receive lists */
	for (i = 0; i < RPL_NUM; i++)
	{
		tp->Rpl[i].NextRPLAddr = htonl((unsigned long) virt_to_bus(&tp->Rpl[(i+1) % RPL_NUM]));
		tp->Rpl[i].Status = (RX_VALID | RX_START_FRAME | RX_END_FRAME | RX_FRAME_IRQ);
		tp->Rpl[i].FrameSize = 0;
		tp->Rpl[i].FragList[0].DataCount = SWAPB(tp->MaxPacketSize);

		/* Alloc skb and point adapter to data area */
		tp->Rpl[i].Skb = dev_alloc_skb(tp->MaxPacketSize);

		/* skb == NULL ? then use local buffer */
		if(tp->Rpl[i].Skb == NULL)
		{
			tp->Rpl[i].SkbStat = SKB_UNAVAILABLE;
			tp->Rpl[i].FragList[0].DataAddr = htonl(virt_to_bus(tp->LocalRxBuffers[i]));
			tp->Rpl[i].MData = tp->LocalRxBuffers[i];
		}
		else	/* SKB != NULL */
		{
			tp->Rpl[i].Skb->dev = dev;
			skb_put(tp->Rpl[i].Skb, tp->MaxPacketSize);

			/* data unreachable for DMA ? then use local buffer */
			if(virt_to_bus(tp->Rpl[i].Skb->data) + tp->MaxPacketSize > ISA_MAX_ADDRESS)
			{
				tp->Rpl[i].SkbStat = SKB_DATA_COPY;
				tp->Rpl[i].FragList[0].DataAddr = htonl(virt_to_bus(tp->LocalRxBuffers[i]));
				tp->Rpl[i].MData = tp->LocalRxBuffers[i];
			}
			else	/* DMA directly in skb->data */
			{
				tp->Rpl[i].SkbStat = SKB_DMA_DIRECT;
				tp->Rpl[i].FragList[0].DataAddr = htonl(virt_to_bus(tp->Rpl[i].Skb->data));
				tp->Rpl[i].MData = tp->Rpl[i].Skb->data;
			}
		}

		tp->Rpl[i].NextRPLPtr = &tp->Rpl[(i+1) % RPL_NUM];
		tp->Rpl[i].RPLIndex = i;
	}

	tp->RplHead = &tp->Rpl[0];
	tp->RplTail = &tp->Rpl[RPL_NUM-1];
	tp->RplTail->Status = (RX_START_FRAME | RX_END_FRAME | RX_FRAME_IRQ);

	return;
}

/*
 * Initializes the initialisation parameter block.
 */
static void sktr_init_ipb(struct net_local *tp)
{
	tp->ipb.Init_Options	= BURST_MODE;
	tp->ipb.CMD_Status_IV	= 0;
	tp->ipb.TX_IV		= 0;
	tp->ipb.RX_IV		= 0;
	tp->ipb.Ring_Status_IV	= 0;
	tp->ipb.SCB_Clear_IV	= 0;
	tp->ipb.Adapter_CHK_IV	= 0;
	tp->ipb.RX_Burst_Size	= BURST_SIZE;
	tp->ipb.TX_Burst_Size	= BURST_SIZE;
	tp->ipb.DMA_Abort_Thrhld = DMA_RETRIES;
	tp->ipb.SCB_Addr	= 0;
	tp->ipb.SSB_Addr	= 0;

	return;
}

/*
 * Initializes the open parameter block.
 */
static void sktr_init_opb(struct net_local *tp)
{
	unsigned long Addr;
	unsigned short RplSize    = RPL_SIZE;
	unsigned short TplSize    = TPL_SIZE;
	unsigned short BufferSize = BUFFER_SIZE;

	tp->ocpl.OPENOptions 	 = 0;
	tp->ocpl.OPENOptions 	|= ENABLE_FULL_DUPLEX_SELECTION;
	tp->ocpl.OPENOptions 	|= PAD_ROUTING_FIELD;
	tp->ocpl.FullDuplex 	 = 0;
	tp->ocpl.FullDuplex 	|= OPEN_FULL_DUPLEX_OFF;

	/* Fixme: If mac address setable:
	 * for (i=0; i<LENGTH_OF_ADDRESS; i++)
	 *	mac->Vam->ocpl.NodeAddr[i] = mac->CurrentAddress[i];
	 */

	tp->ocpl.GroupAddr	 = 0;
	tp->ocpl.FunctAddr	 = 0;
	tp->ocpl.RxListSize	 = SWAPB(RplSize);
	tp->ocpl.TxListSize	 = SWAPB(TplSize);
	tp->ocpl.BufSize	 = SWAPB(BufferSize);
	tp->ocpl.Reserved	 = 0;
	tp->ocpl.TXBufMin	 = TX_BUF_MIN;
	tp->ocpl.TXBufMax	 = TX_BUF_MAX;

	Addr = htonl(virt_to_bus(tp->ProductID));

	tp->ocpl.ProdIDAddr[0]	 = LOWORD(Addr);
	tp->ocpl.ProdIDAddr[1]	 = HIWORD(Addr);

	return;
}

/*
 * Send OPEN command to adapter
 */
static void sktr_open_adapter(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;

	if(tp->OpenCommandIssued)
		return;

	tp->OpenCommandIssued = 1;
	sktr_exec_cmd(dev, OC_OPEN);

	return;
}

/*
 * Clear the adapter's interrupt flag. Clear system interrupt enable
 * (SINTEN): disable adapter to system interrupts.
 */
static void sktr_disable_interrupts(struct device *dev)
{
	outb(0, dev->base_addr + SIFACL);

	return;
}

/*
 * Set the adapter's interrupt flag. Set system interrupt enable
 * (SINTEN): enable adapter to system interrupts.
 */
static void sktr_enable_interrupts(struct device *dev)
{
	outb(ACL_SINTEN, dev->base_addr + SIFACL);

	return;
}

/*
 * Put command in command queue, try to execute it.
 */
static void sktr_exec_cmd(struct device *dev, unsigned short Command)
{
	struct net_local *tp = (struct net_local *)dev->priv;

	tp->CMDqueue |= Command;
	sktr_chk_outstanding_cmds(dev);

	return;
}

/*
 * Linux always gives 18 byte of source routing information in the frame header.
 * But the length field can indicate shorter length. Then cut header
 * appropriate.
 */
static unsigned char *sktr_fix_srouting(unsigned char *buf, short *FrameLen)
{
	struct trh_hdr *trh = (struct trh_hdr *)buf;
	int len;
        
	if(buf[8] & TR_RII)
	{
		trh->rcf &= ~SWAPB((unsigned short) TR_RCF_LONGEST_FRAME_MASK);
		trh->rcf |= SWAPB((unsigned short) TR_RCF_FRAME4K);
		len = (SWAPB(trh->rcf) & TR_RCF_LEN_MASK) >> 8;
		if(len < 18)
		{
			memcpy(&buf[18-len],buf,sizeof(struct trh_hdr)-18+len);
			*FrameLen -= (18 - len);
		}
		return (&buf[18-len]);
	}

	return (buf);
}

/*
 * Gets skb from system, queues it and checks if it can be sent
 */
static int sktr_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;

	if(dev->tbusy)
	{
		/*
		 * If we get here, some higher level has decided we are broken.
		 * There should really be a "kick me" function call instead.
		 *
		 * Resetting the token ring adapter takes a long time so just
		 * fake transmission time and go on trying. Our own timeout
		 * routine is in sktr_timer_chk()
		 */
		dev->tbusy 	 = 0;
		dev->trans_start = jiffies;
		return (1);
	}

	/*
	 * If some higher layer thinks we've missed an tx-done interrupt we
	 * are passed NULL.
	 */
	if(skb == NULL)
		return (0);

	/*
	 * Block a timer-based transmit from overlapping. This could better be
	 * done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
	 */
	if(test_and_set_bit(0, (void*)&dev->tbusy) != 0)
	{
		printk("%s: Transmitter access conflict.\n", dev->name);
		return (1);
	}

	if(tp->QueueSkb == 0)
		return (1);	/* Return with tbusy set: queue full */

	tp->QueueSkb--;
	skb_queue_tail(&tp->SendSkbQueue, skb);
	sktr_hardware_send_packet(dev, tp);
	if(tp->QueueSkb > 0)
		dev->tbusy = 0;

	return (0);
}

/*
 * Move frames from internal skb queue into adapter tx queue
 */
static void sktr_hardware_send_packet(struct device *dev, struct net_local* tp)
{
	TPL *tpl;
	short length;
	unsigned char *buf, *newbuf;
	struct sk_buff *skb;
	int i;
    
	for(;;)
	{
		/* Try to get a free TPL from the chain.
		 *
		 * NOTE: We *must* always leave one unused TPL in the chain, 
		 * because otherwise the adapter might send frames twice.
		 */
		if(tp->TplFree->NextTPLPtr->BusyFlag)	/* No free TPL */
		{
			printk(KERN_INFO "%s: No free TPL\n", dev->name);
			return;
		}

		/* Send first buffer from queue */
		skb = skb_dequeue(&tp->SendSkbQueue);
		if(skb == NULL)
			return;

		tp->QueueSkb++;
		/* Is buffer reachable for Busmaster-DMA? */
		if(virt_to_bus((void*)(((long) skb->data) + skb->len))
			> ISA_MAX_ADDRESS)
		{
			/* Copy frame to local buffer */
			i 	= tp->TplFree->TPLIndex;
			length 	= skb->len;
			buf 	= tp->LocalTxBuffers[i];
			memcpy(buf, skb->data, length);
			newbuf 	= sktr_fix_srouting(buf, &length);
		}
		else
		{
			/* Send direct from skb->data */
			length = skb->len;
			newbuf = sktr_fix_srouting(skb->data, &length);
		}

		/* Source address in packet? */
		sktr_chk_src_addr(newbuf, dev->dev_addr);

		tp->LastSendTime	= jiffies;
		tpl 			= tp->TplFree;	/* Get the "free" TPL */
		tpl->BusyFlag 		= 1;		/* Mark TPL as busy */
		tp->TplFree 		= tpl->NextTPLPtr;
    
		/* Save the skb for delayed return of skb to system */
		tpl->Skb = skb;
		tpl->FragList[0].DataCount = (unsigned short) SWAPB(length);
		tpl->FragList[0].DataAddr  = htonl(virt_to_bus(newbuf));

		/* Write the data length in the transmit list. */
		tpl->FrameSize 	= (unsigned short) SWAPB(length);
		tpl->MData 	= newbuf;

		/* Transmit the frame and set the status values. */
		sktr_write_tpl_status(tpl, TX_VALID | TX_START_FRAME
					| TX_END_FRAME | TX_PASS_SRC_ADDR
					| TX_FRAME_IRQ);

		/* Let adapter send the frame. */
		sktr_exec_sifcmd(dev, CMD_TX_VALID);
	}

	return;
}

/*
 * Write the given value to the 'Status' field of the specified TPL.
 * NOTE: This function should be used whenever the status of any TPL must be
 * modified by the driver, because the compiler may otherwise change the
 * order of instructions such that writing the TPL status may be executed at
 * an undesireable time. When this function is used, the status is always
 * written when the function is called.
 */
static void sktr_write_tpl_status(TPL *tpl, unsigned int Status)
{
	tpl->Status = Status;
}

static void sktr_chk_src_addr(unsigned char *frame, unsigned char *hw_addr)
{
	unsigned char SRBit;

	if((((unsigned long)frame[8]) & ~0x80) != 0)	/* Compare 4 bytes */
		return;
	if((unsigned short)frame[12] != 0)		/* Compare 2 bytes */
		return;

	SRBit = frame[8] & 0x80;
	memcpy(&frame[8], hw_addr, 6);
	frame[8] |= SRBit;

	return;
}

/*
 * The timer routine: Check if adapter still open and working, reopen if not. 
 */
static void sktr_timer_chk(unsigned long data)
{
	struct device *dev = (struct device*)data;
	struct net_local *tp = (struct net_local*)dev->priv;

	if(tp->HaltInProgress)
		return;

	sktr_chk_outstanding_cmds(dev);
	if(time_before(tp->LastSendTime + SEND_TIMEOUT, jiffies)
		&& (tp->QueueSkb < MAX_TX_QUEUE || tp->TplFree != tp->TplBusy))
	{
		/* Anything to send, but stalled to long */
		tp->LastSendTime = jiffies;
		sktr_exec_cmd(dev, OC_CLOSE);	/* Does reopen automatically */
	}

	tp->timer.expires = jiffies + 2*HZ;
	add_timer(&tp->timer);

	if(tp->AdapterOpenFlag || tp->ReOpenInProgress)
		return;
	tp->ReOpenInProgress = 1;
	sktr_open_adapter(dev);

	return;
}

/*
 * The typical workload of the driver: Handle the network interface interrupts.
 */
static void sktr_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = dev_id;
	struct net_local *tp;
	int ioaddr;
	unsigned short irq_type;

	if(dev == NULL)
	{
		printk("%s: irq %d for unknown device.\n", dev->name, irq);
		return;
	}

	dev->interrupt = 1;

	ioaddr = dev->base_addr;
	tp = (struct net_local *)dev->priv;

	irq_type = inw(ioaddr + SIFSTS);

	while(irq_type & STS_SYSTEM_IRQ)
	{
		irq_type &= STS_IRQ_MASK;

		if(!sktr_chk_ssb(tp, irq_type))
		{
			printk(KERN_INFO "%s: DATA LATE occurred\n", dev->name);
			break;
		}

		switch(irq_type)
		{
			case STS_IRQ_RECEIVE_STATUS:
				sktr_reset_interrupt(dev);
				sktr_rcv_status_irq(dev);
				break;

			case STS_IRQ_TRANSMIT_STATUS:
				/* Check if TRANSMIT.HALT command is complete */
				if(tp->ssb.Parm[0] & COMMAND_COMPLETE)
				{
					tp->TransmitCommandActive = 0;
					tp->TransmitHaltScheduled = 0;

					/* Issue a new transmit command. */
					sktr_exec_cmd(dev, OC_TRANSMIT);
				}

				sktr_reset_interrupt(dev);
				sktr_tx_status_irq(dev);
				break;

			case STS_IRQ_COMMAND_STATUS:
				/* The SSB contains status of last command
				 * other than receive/transmit.
				 */
				sktr_cmd_status_irq(dev);
				break;

			case STS_IRQ_SCB_CLEAR:
				/* The SCB is free for another command. */
				tp->ScbInUse = 0;
				sktr_chk_outstanding_cmds(dev);
				break;

			case STS_IRQ_RING_STATUS:
				sktr_ring_status_irq(dev);
				break;

			case STS_IRQ_ADAPTER_CHECK:
				sktr_chk_irq(dev);
				break;

			default:
				printk(KERN_INFO "Unknown Token Ring IRQ\n");
				break;
		}

		/* Reset system interrupt if not already done. */
		if(irq_type != STS_IRQ_TRANSMIT_STATUS
			&& irq_type != STS_IRQ_RECEIVE_STATUS)
		{
			sktr_reset_interrupt(dev);
		}

		irq_type = inw(ioaddr + SIFSTS);
	}

	dev->interrupt = 0;

	return;
}

/*
 *  Reset the INTERRUPT SYSTEM bit and issue SSB CLEAR command.
 */
static void sktr_reset_interrupt(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;
	SSB *ssb = &tp->ssb;

	/*
	 * [Workaround for "Data Late"]
	 * Set all fields of the SSB to well-defined values so we can
	 * check if the adapter has written the SSB.
	 */

	ssb->STS	= (unsigned short) -1;
	ssb->Parm[0] 	= (unsigned short) -1;
	ssb->Parm[1] 	= (unsigned short) -1;
	ssb->Parm[2] 	= (unsigned short) -1;

	/* Free SSB by issuing SSB_CLEAR command after reading IRQ code
	 * and clear STS_SYSTEM_IRQ bit: enable adapter for further interrupts.
	 */
	sktr_exec_sifcmd(dev, CMD_SSB_CLEAR | CMD_CLEAR_SYSTEM_IRQ);

	return;
}

/*
 * Check if the SSB has actually been written by the adapter.
 */
static unsigned char sktr_chk_ssb(struct net_local *tp, unsigned short IrqType)
{
	SSB *ssb = &tp->ssb;	/* The address of the SSB. */

	/* C 0 1 2 INTERRUPT CODE
	 * - - - - --------------
	 * 1 1 1 1 TRANSMIT STATUS
	 * 1 1 1 1 RECEIVE STATUS
	 * 1 ? ? 0 COMMAND STATUS
	 * 0 0 0 0 SCB CLEAR
	 * 1 1 0 0 RING STATUS
	 * 0 0 0 0 ADAPTER CHECK
	 *
	 * 0 = SSB field not affected by interrupt
	 * 1 = SSB field is affected by interrupt
	 *
	 * C = SSB ADDRESS +0: COMMAND
	 * 0 = SSB ADDRESS +2: STATUS 0
	 * 1 = SSB ADDRESS +4: STATUS 1
	 * 2 = SSB ADDRESS +6: STATUS 2
	 */

	/* Check if this interrupt does use the SSB. */

	if(IrqType != STS_IRQ_TRANSMIT_STATUS
		&& IrqType != STS_IRQ_RECEIVE_STATUS
		&& IrqType != STS_IRQ_COMMAND_STATUS
		&& IrqType != STS_IRQ_RING_STATUS)
	{
		return (1);	/* SSB not involved. */
	}

	/* Note: All fields of the SSB have been set to all ones (-1) after it
	 * has last been used by the software (see DriverIsr()).
	 *
	 * Check if the affected SSB fields are still unchanged.
	 */

	if(ssb->STS == (unsigned short) -1)
		return (0);	/* Command field not yet available. */
	if(IrqType == STS_IRQ_COMMAND_STATUS)
		return (1);	/* Status fields not always affected. */
	if(ssb->Parm[0] == (unsigned short) -1)
		return (0);	/* Status 1 field not yet available. */
	if(IrqType == STS_IRQ_RING_STATUS)
		return (1);	/* Status 2 & 3 fields not affected. */

	/* Note: At this point, the interrupt is either TRANSMIT or RECEIVE. */
	if(ssb->Parm[1] == (unsigned short) -1)
		return (0);	/* Status 2 field not yet available. */
	if(ssb->Parm[2] == (unsigned short) -1)
		return (0);	/* Status 3 field not yet available. */

	return (1);	/* All SSB fields have been written by the adapter. */
}

/*
 * Evaluates the command results status in the SSB status field.
 */
static void sktr_cmd_status_irq(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;
	unsigned short ssb_cmd, ssb_parm_0;
	unsigned short ssb_parm_1;
	char *open_err = "Open error -";
	char *code_err = "Open code -";

	/* Copy the ssb values to local variables */
	ssb_cmd    = tp->ssb.STS;
	ssb_parm_0 = tp->ssb.Parm[0];
	ssb_parm_1 = tp->ssb.Parm[1];

	if(ssb_cmd == OPEN)
	{
		tp->Sleeping = 0;
		if(!tp->ReOpenInProgress)
	    		wake_up_interruptible(&tp->wait_for_tok_int);

		tp->OpenCommandIssued = 0;
		tp->ScbInUse = 0;

		if((ssb_parm_0 & 0x00FF) == GOOD_COMPLETION)
		{
			/* Success, the adapter is open. */
			tp->LobeWireFaultLogged	= 0;
			tp->AdapterOpenFlag 	= 1;
			tp->AdapterVirtOpenFlag = 1;
			tp->TransmitCommandActive = 0;
			sktr_exec_cmd(dev, OC_TRANSMIT);
			sktr_exec_cmd(dev, OC_RECEIVE);

			if(tp->ReOpenInProgress)
				tp->ReOpenInProgress = 0;

			return;
		}
		else 	/* The adapter did not open. */
		{
	    		if(ssb_parm_0 & NODE_ADDR_ERROR)
				printk(KERN_INFO "%s: Node address error\n",
					dev->name);
	    		if(ssb_parm_0 & LIST_SIZE_ERROR)
				printk(KERN_INFO "%s: List size error\n",
					dev->name);
	    		if(ssb_parm_0 & BUF_SIZE_ERROR)
				printk(KERN_INFO "%s: Buffer size error\n",
					dev->name);
	    		if(ssb_parm_0 & TX_BUF_COUNT_ERROR)
				printk(KERN_INFO "%s: Tx buffer count error\n",
					dev->name);
	    		if(ssb_parm_0 & INVALID_OPEN_OPTION)
				printk(KERN_INFO "%s: Invalid open option\n",
					dev->name);
	    		if(ssb_parm_0 & OPEN_ERROR)
			{
				/* Show the open phase. */
				switch(ssb_parm_0 & OPEN_PHASES_MASK)
				{
					case LOBE_MEDIA_TEST:
						if(!tp->LobeWireFaultLogged)
						{
							tp->LobeWireFaultLogged = 1;
							printk(KERN_INFO "%s: %s Lobe wire fault (check cable !).\n", dev->name, open_err);
		    				}
						tp->ReOpenInProgress	= 1;
						tp->AdapterOpenFlag 	= 0;
						tp->AdapterVirtOpenFlag = 1;
						sktr_open_adapter(dev);
						return;

					case PHYSICAL_INSERTION:
						printk(KERN_INFO "%s: %s Physical insertion.\n", dev->name, open_err);
						break;

					case ADDRESS_VERIFICATION:
						printk(KERN_INFO "%s: %s Address verification.\n", dev->name, open_err);
						break;

					case PARTICIPATION_IN_RING_POLL:
						printk(KERN_INFO "%s: %s Participation in ring poll.\n", dev->name, open_err);
						break;

					case REQUEST_INITIALISATION:
						printk(KERN_INFO "%s: %s Request initialisation.\n", dev->name, open_err);
						break;

					case FULLDUPLEX_CHECK:
						printk(KERN_INFO "%s: %s Full duplex check.\n", dev->name, open_err);
						break;

					default:
						printk(KERN_INFO "%s: %s Unknown open phase\n", dev->name, open_err);
						break;
				}

				/* Show the open errors. */
				switch(ssb_parm_0 & OPEN_ERROR_CODES_MASK)
				{
					case OPEN_FUNCTION_FAILURE:
						printk(KERN_INFO "%s: %s OPEN_FUNCTION_FAILURE", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_FUNCTION_FAILURE;
						break;

					case OPEN_SIGNAL_LOSS:
						printk(KERN_INFO "%s: %s OPEN_SIGNAL_LOSS\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_SIGNAL_LOSS;
						break;

					case OPEN_TIMEOUT:
						printk(KERN_INFO "%s: %s OPEN_TIMEOUT\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_TIMEOUT;
						break;

					case OPEN_RING_FAILURE:
						printk(KERN_INFO "%s: %s OPEN_RING_FAILURE\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_RING_FAILURE;
						break;

					case OPEN_RING_BEACONING:
						printk(KERN_INFO "%s: %s OPEN_RING_BEACONING\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_RING_BEACONING;
						break;

					case OPEN_DUPLICATE_NODEADDR:
						printk(KERN_INFO "%s: %s OPEN_DUPLICATE_NODEADDR\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_DUPLICATE_NODEADDR;
						break;

					case OPEN_REQUEST_INIT:
						printk(KERN_INFO "%s: %s OPEN_REQUEST_INIT\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_REQUEST_INIT;
						break;

					case OPEN_REMOVE_RECEIVED:
						printk(KERN_INFO "%s: %s OPEN_REMOVE_RECEIVED", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_REMOVE_RECEIVED;
						break;

					case OPEN_FULLDUPLEX_SET:
						printk(KERN_INFO "%s: %s OPEN_FULLDUPLEX_SET\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_FULLDUPLEX_SET;
						break;

					default:
						printk(KERN_INFO "%s: %s Unknown open err code", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_FUNCTION_FAILURE;
						break;
				}
			}

			tp->AdapterOpenFlag 	= 0;
			tp->AdapterVirtOpenFlag = 0;

			return;
		}
	}
	else
	{
		if(ssb_cmd != READ_ERROR_LOG)
			return;

		/* Add values from the error log table to the MAC
		 * statistics counters and update the errorlogtable
		 * memory.
		 */
		tp->MacStat.line_errors += tp->errorlogtable.Line_Error;
		tp->MacStat.burst_errors += tp->errorlogtable.Burst_Error;
		tp->MacStat.A_C_errors += tp->errorlogtable.ARI_FCI_Error;
		tp->MacStat.lost_frames += tp->errorlogtable.Lost_Frame_Error;
		tp->MacStat.recv_congest_count += tp->errorlogtable.Rx_Congest_Error;
		tp->MacStat.rx_errors += tp->errorlogtable.Rx_Congest_Error;
		tp->MacStat.frame_copied_errors += tp->errorlogtable.Frame_Copied_Error;
		tp->MacStat.token_errors += tp->errorlogtable.Token_Error;
		tp->MacStat.dummy1 += tp->errorlogtable.DMA_Bus_Error;
		tp->MacStat.dummy1 += tp->errorlogtable.DMA_Parity_Error;
		tp->MacStat.abort_delimiters += tp->errorlogtable.AbortDelimeters;
		tp->MacStat.frequency_errors += tp->errorlogtable.Frequency_Error;
		tp->MacStat.internal_errors += tp->errorlogtable.Internal_Error;
	}

	return;
}

/*
 * The inverse routine to sktr_open().
 */
static int sktr_close(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;

	dev->tbusy = 1;
	dev->start = 0;

	del_timer(&tp->timer);

	/* Flush the Tx and disable Rx here. */

	tp->HaltInProgress 	= 1;
	sktr_exec_cmd(dev, OC_CLOSE);
	tp->timer.expires	= jiffies + 1*HZ;
	tp->timer.function 	= sktr_timer_end_wait;
	tp->timer.data 		= (unsigned long)dev;
	add_timer(&tp->timer);

	sktr_enable_interrupts(dev);

	tp->Sleeping = 1;
	interruptible_sleep_on(&tp->wait_for_tok_int);
	tp->TransmitCommandActive = 0;
    
	del_timer(&tp->timer);
	sktr_disable_interrupts(dev);
   
	if(dev->dma > 0) 
	{
		unsigned long flags=claim_dma_lock();
		disable_dma(dev->dma);
		release_dma_lock(flags);
	}
	
	outw(0xFF00, dev->base_addr + SIFCMD);
	if(dev->dma > 0)
		outb(0xff, dev->base_addr + POSREG);

#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif

	sktr_cancel_tx_queue(tp);

	return (0);
}

/*
 * Get the current statistics. This may be called with the card open
 * or closed.
 */
static struct enet_statistics *sktr_get_stats(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;

	return ((struct enet_statistics *)&tp->MacStat);
}

/*
 * Set or clear the multicast filter for this adapter.
 */
static void sktr_set_multicast_list(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;
	unsigned int OpenOptions;

	OpenOptions = tp->ocpl.OPENOptions &
		~(PASS_ADAPTER_MAC_FRAMES
		| PASS_ATTENTION_FRAMES
		| PASS_BEACON_MAC_FRAMES
		| COPY_ALL_MAC_FRAMES
		| COPY_ALL_NON_MAC_FRAMES);

	if(dev->flags & IFF_PROMISC)
		/* Enable promiscuous mode */
		OpenOptions |= COPY_ALL_NON_MAC_FRAMES | COPY_ALL_MAC_FRAMES;
	else
	{
		if(dev->flags & IFF_ALLMULTI)
			/* || dev->mc_count > HW_MAX_ADDRS) */
		{
			/* Disable promiscuous mode, use normal mode. */
		}
		else
		{
			if(dev->mc_count)
			{
				/* Walk the address list, and load the filter */
			}
		}
	}

	tp->ocpl.OPENOptions = OpenOptions;
	sktr_exec_cmd(dev, OC_MODIFY_OPEN_PARMS);

	return;
}

/*
 * Wait for some time (microseconds)
 */
static void sktr_wait(unsigned long time)
{
	long tmp;

	tmp = jiffies + time/(1000000/HZ);
	do {
  		current->state 		= TASK_INTERRUPTIBLE;
		tmp = schedule_timeout(tmp);
	} while(time_after(tmp, jiffies));

	return;
}

/*
 * Write a command value to the SIFCMD register
 */
static void sktr_exec_sifcmd(struct device *dev, unsigned int WriteValue)
{
	int ioaddr = dev->base_addr;
	unsigned short cmd;
	unsigned short SifStsValue;
	unsigned long loop_counter;

	WriteValue = ((WriteValue ^ CMD_SYSTEM_IRQ) | CMD_INTERRUPT_ADAPTER);
	cmd = (unsigned short)WriteValue;
	loop_counter = 0,5 * 800000;
	do {
		SifStsValue = inw(ioaddr + SIFSTS);
	} while((SifStsValue & CMD_INTERRUPT_ADAPTER) && loop_counter--);
	outw(cmd, ioaddr + SIFCMD);

	return;
}

/*
 * Processes adapter hardware reset, halts adapter and downloads firmware,
 * clears the halt bit.
 */
static int sktr_reset_adapter(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;
	unsigned short *fw_ptr = (unsigned short *)&sktr_code;
	unsigned short count, c;
	int ioaddr = dev->base_addr;

	/* Hardware adapter reset */
	outw(ACL_ARESET, ioaddr + SIFACL);
	sktr_wait(40);

	c = inw(ioaddr + SIFACL);
	sktr_wait(20);

	if(dev->dma == 0)	/* For PCI adapters */
	{
		c &= ~(ACL_SPEED4 | ACL_SPEED16);	/* Clear bits */
		if(tp->DataRate == SPEED_4)
			c |= ACL_SPEED4;		/* Set 4Mbps */
		else
			c |= ACL_SPEED16;		/* Set 16Mbps */
	}

	/* In case a command is pending - forget it */
	tp->ScbInUse = 0;

	c &= ~ACL_ARESET;		/* Clear adapter reset bit */
	c |=  ACL_CPHALT;		/* Halt adapter CPU, allow download */
	c &= ~ACL_PSDMAEN;		/* Clear pseudo dma bit */
	outw(c, ioaddr + SIFACL);
	sktr_wait(40);

	/* Download firmware via DIO interface: */
	do {
		/* Download first address part */
		outw(*fw_ptr, ioaddr + SIFADX);
		fw_ptr++;

		/* Download second address part */
		outw(*fw_ptr, ioaddr + SIFADD);
		fw_ptr++;

		if((count = *fw_ptr) != 0)	/* Load loop counter */
		{
			fw_ptr++;	/* Download block data */
			for(; count > 0; count--)
			{
				outw(*fw_ptr, ioaddr + SIFINC);
				fw_ptr++;
			}
		}
		else	/* Stop, if last block downloaded */
		{
			c = inw(ioaddr + SIFACL);
			c &= (~ACL_CPHALT | ACL_SINTEN);

			/* Clear CPHALT and start BUD */
			outw(c, ioaddr + SIFACL);
			return (1);
		}
	} while(count == 0);

	return (-1);
}

/*
 * Starts bring up diagnostics of token ring adapter and evaluates
 * diagnostic results.
 */
static int sktr_bringup_diags(struct device *dev)
{
	int loop_cnt, retry_cnt;
	unsigned short Status;
	int ioaddr = dev->base_addr;

	sktr_wait(HALF_SECOND);
	sktr_exec_sifcmd(dev, EXEC_SOFT_RESET);
	sktr_wait(HALF_SECOND);

	retry_cnt = BUD_MAX_RETRIES;	/* maximal number of retrys */

	do {
		retry_cnt--;
		if(sktr_debug > 3)
			printk(KERN_INFO "BUD-Status: \n");
		loop_cnt = BUD_MAX_LOOPCNT;	/* maximum: three seconds*/
		do {			/* Inspect BUD results */
			loop_cnt--;
			sktr_wait(HALF_SECOND);
			Status = inw(ioaddr + SIFSTS);
			Status &= STS_MASK;

			if(sktr_debug > 3)
				printk(KERN_INFO " %04X \n", Status);
			/* BUD successfully completed */
			if(Status == STS_INITIALIZE)
				return (1);
		/* Unrecoverable hardware error, BUD not completed? */
		} while((loop_cnt > 0) && ((Status & (STS_ERROR | STS_TEST))
			!= (STS_ERROR | STS_TEST)));

		/* Error preventing completion of BUD */
		if(retry_cnt > 0)
		{
			printk(KERN_INFO "%s: Adapter Software Reset.\n", 
				dev->name);
			sktr_exec_sifcmd(dev, EXEC_SOFT_RESET);
			sktr_wait(HALF_SECOND);
		}
	} while(retry_cnt > 0);

	Status = inw(ioaddr + SIFSTS);
	Status &= STS_ERROR_MASK;	/* Hardware error occurred! */

	printk(KERN_INFO "%s: Bring Up Diagnostics Error (%04X) occurred\n",
		dev->name, Status);

	return (-1);
}

/*
 * Copy initialisation data to adapter memory, beginning at address
 * 1:0A00; Starting DMA test and evaluating result bits.
 */
static int sktr_init_adapter(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;

	const unsigned char SCB_Test[6] = {0x00, 0x00, 0xC1, 0xE2, 0xD4, 0x8B};
	const unsigned char SSB_Test[8] = {0xFF, 0xFF, 0xD1, 0xD7,
						0xC5, 0xD9, 0xC3, 0xD4};
	void *ptr = (void *)&tp->ipb;
	unsigned short *ipb_ptr = (unsigned short *)ptr;
	unsigned char *cb_ptr = (unsigned char *) &tp->scb;
	unsigned char *sb_ptr = (unsigned char *) &tp->ssb;
	unsigned short Status;
	int i, loop_cnt, retry_cnt;
	int ioaddr = dev->base_addr;

	/* Normalize: byte order low/high, word order high/low! (only IPB!) */
	tp->ipb.SCB_Addr = SWAPW(virt_to_bus(&tp->scb));
	tp->ipb.SSB_Addr = SWAPW(virt_to_bus(&tp->ssb));

	/* Maximum: three initialization retries */
	retry_cnt = INIT_MAX_RETRIES;

	do {
		retry_cnt--;

		/* Transfer initialization block */
		outw(0x0001, ioaddr + SIFADX);

		/* To address 0001:0A00 of adapter RAM */
		outw(0x0A00, ioaddr + SIFADD);

		/* Write 11 words to adapter RAM */
		for(i = 0; i < 11; i++)
			outw(ipb_ptr[i], ioaddr + SIFINC);

		/* Execute SCB adapter command */
		sktr_exec_sifcmd(dev, CMD_EXECUTE);

		loop_cnt = INIT_MAX_LOOPCNT;	/* Maximum: 11 seconds */

		/* While remaining retries, no error and not completed */
		do {
			Status = 0;
			loop_cnt--;
			sktr_wait(HALF_SECOND);

			/* Mask interesting status bits */
			Status = inw(ioaddr + SIFSTS);
			Status &= STS_MASK;
		} while(((Status &(STS_INITIALIZE | STS_ERROR | STS_TEST)) != 0)
			&& ((Status & STS_ERROR) == 0) && (loop_cnt != 0));

		if((Status & (STS_INITIALIZE | STS_ERROR | STS_TEST)) == 0)
		{
			/* Initialization completed without error */
			i = 0;
			do {	/* Test if contents of SCB is valid */
				if(SCB_Test[i] != *(cb_ptr + i))
					/* DMA data error: wrong data in SCB */
					return (-1);
				i++;
			} while(i < 6);

			i = 0;
			do {	/* Test if contents of SSB is valid */
				if(SSB_Test[i] != *(sb_ptr + i))
					/* DMA data error: wrong data in SSB */
					return (-1);
				i++;
			} while (i < 8);

			return (1);	/* Adapter successfully initialized */
		}
		else
		{
			if((Status & STS_ERROR) != 0)
			{
				/* Initialization error occurred */
				Status = inw(ioaddr + SIFSTS);
				Status &= STS_ERROR_MASK;
				/* ShowInitialisationErrorCode(Status); */
				return (-1); /* Unrecoverable error */
			}
			else
			{
				if(retry_cnt > 0)
				{
					/* Reset adapter and try init again */
					sktr_exec_sifcmd(dev, EXEC_SOFT_RESET);
					sktr_wait(HALF_SECOND);
				}
			}
		}
	} while(retry_cnt > 0);

	return (-1);
}

/*
 * Check for outstanding commands in command queue and tries to execute
 * command immediately. Corresponding command flag in command queue is cleared.
 */
static void sktr_chk_outstanding_cmds(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;
	unsigned long Addr = 0;
	unsigned char i = 0;

	if(tp->CMDqueue == 0)
		return;		/* No command execution */

	/* If SCB in use: no command */
	if(tp->ScbInUse == 1)
		return;

	/* Check if adapter is opened, avoiding COMMAND_REJECT
	 * interrupt by the adapter!
	 */
	if(tp->AdapterOpenFlag == 0)
	{
		if(tp->CMDqueue & OC_OPEN)
		{
			/* Execute OPEN command	*/
			tp->CMDqueue ^= OC_OPEN;

			/* Copy the 18 bytes of the product ID */
			while((AdapterName[i] != '\0') && (i < PROD_ID_SIZE))
			{
				tp->ProductID[i] = AdapterName[i];
				i++;
			}

			Addr = htonl(virt_to_bus(&tp->ocpl));
			tp->scb.Parm[0] = LOWORD(Addr);
			tp->scb.Parm[1] = HIWORD(Addr);
			tp->scb.CMD = OPEN;
		}
		else
			/* No OPEN command queued, but adapter closed. Note:
			 * We'll try to re-open the adapter in DriverPoll()
			 */
			return;		/* No adapter command issued */
	}
	else
	{
		/* Adapter is open; evaluate command queue: try to execute
		 * outstanding commands (depending on priority!) CLOSE
		 * command queued
		 */
		if(tp->CMDqueue & OC_CLOSE)
		{
			tp->CMDqueue ^= OC_CLOSE;
			tp->AdapterOpenFlag = 0;
			tp->scb.Parm[0] = 0; /* Parm[0], Parm[1] are ignored */
			tp->scb.Parm[1] = 0; /* but should be set to zero! */
			tp->scb.CMD = CLOSE;
			if(!tp->HaltInProgress)
				tp->CMDqueue |= OC_OPEN; /* re-open adapter */
			else
				tp->CMDqueue = 0;	/* no more commands */
		}
		else
		{
			if(tp->CMDqueue & OC_RECEIVE)
			{
				tp->CMDqueue ^= OC_RECEIVE;
				Addr = htonl(virt_to_bus(tp->RplHead));
				tp->scb.Parm[0] = LOWORD(Addr);
				tp->scb.Parm[1] = HIWORD(Addr);
				tp->scb.CMD = RECEIVE;
			}
			else
			{
				if(tp->CMDqueue & OC_TRANSMIT_HALT)
				{
					/* NOTE: TRANSMIT.HALT must be checked 
					 * before TRANSMIT.
					 */
					tp->CMDqueue ^= OC_TRANSMIT_HALT;
					tp->scb.CMD = TRANSMIT_HALT;

					/* Parm[0] and Parm[1] are ignored
					 * but should be set to zero!
					 */
					tp->scb.Parm[0] = 0;
					tp->scb.Parm[1] = 0;
				}
				else
				{
					if(tp->CMDqueue & OC_TRANSMIT)
					{
						/* NOTE: TRANSMIT must be 
						 * checked after TRANSMIT.HALT
						 */
						if(tp->TransmitCommandActive)
						{
							if(!tp->TransmitHaltScheduled)
							{
								tp->TransmitHaltScheduled = 1;
								sktr_exec_cmd(dev, OC_TRANSMIT_HALT) ;
							}
							tp->TransmitCommandActive = 0;
							return;
						}

						tp->CMDqueue ^= OC_TRANSMIT;
						sktr_cancel_tx_queue(tp);
						Addr = htonl(virt_to_bus(tp->TplBusy));
						tp->scb.Parm[0] = LOWORD(Addr);
						tp->scb.Parm[1] = HIWORD(Addr);
						tp->scb.CMD = TRANSMIT;
						tp->TransmitCommandActive = 1;
					}
					else
					{
						if(tp->CMDqueue & OC_MODIFY_OPEN_PARMS)
						{
							tp->CMDqueue ^= OC_MODIFY_OPEN_PARMS;
							tp->scb.Parm[0] = tp->ocpl.OPENOptions; /* new OPEN options*/
							tp->scb.Parm[0] |= ENABLE_FULL_DUPLEX_SELECTION;
							tp->scb.Parm[1] = 0; /* is ignored but should be zero */
							tp->scb.CMD = MODIFY_OPEN_PARMS;
						}
						else
						{
							if(tp->CMDqueue & OC_SET_FUNCT_ADDR)
							{
								tp->CMDqueue ^= OC_SET_FUNCT_ADDR;
								tp->scb.Parm[0] = LOWORD(tp->ocpl.FunctAddr);
								tp->scb.Parm[1] = HIWORD(tp->ocpl.FunctAddr);
								tp->scb.CMD = SET_FUNCT_ADDR;
							}
							else
							{
								if(tp->CMDqueue & OC_SET_GROUP_ADDR)
								{
									tp->CMDqueue ^= OC_SET_GROUP_ADDR;
									tp->scb.Parm[0] = LOWORD(tp->ocpl.GroupAddr);
									tp->scb.Parm[1] = HIWORD(tp->ocpl.GroupAddr);
									tp->scb.CMD = SET_GROUP_ADDR;
								}
								else
								{
									if(tp->CMDqueue & OC_READ_ERROR_LOG)
									{
										tp->CMDqueue ^= OC_READ_ERROR_LOG;
										Addr = htonl(virt_to_bus(&tp->errorlogtable));
										tp->scb.Parm[0] = LOWORD(Addr);
										tp->scb.Parm[1] = HIWORD(Addr);
										tp->scb.CMD = READ_ERROR_LOG;
									}
									else
									{
										printk(KERN_WARNING "CheckForOutstandingCommand: unknown Command\n");
										tp->CMDqueue = 0;
										return;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	tp->ScbInUse = 1;	/* Set semaphore: SCB in use. */

	/* Execute SCB and generate IRQ when done. */
	sktr_exec_sifcmd(dev, CMD_EXECUTE | CMD_SCB_REQUEST);

	return;
}

/*
 * IRQ conditions: signal loss on the ring, transmit or receive of beacon
 * frames (disabled if bit 1 of OPEN option is set); report error MAC
 * frame transmit (disabled if bit 2 of OPEN option is set); open or short
 * cirquit fault on the lobe is detected; remove MAC frame received;
 * error counter overflow (255); opened adapter is the only station in ring.
 * After some of the IRQs the adapter is closed!
 */
static void sktr_ring_status_irq(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;

	tp->CurrentRingStatus = SWAPB(tp->ssb.Parm[0]);

	/* First: fill up statistics */
	if(tp->ssb.Parm[0] & SIGNAL_LOSS)
	{
		printk(KERN_INFO "%s: Signal Loss\n", dev->name);
		tp->MacStat.line_errors++;
	}

	/* Adapter is closed, but initialized */
	if(tp->ssb.Parm[0] & LOBE_WIRE_FAULT)
	{
		printk(KERN_INFO "%s: Lobe Wire Fault, Reopen Adapter\n", 
			dev->name);
		tp->MacStat.line_errors++;
	}

	if(tp->ssb.Parm[0] & RING_RECOVERY)
		printk(KERN_INFO "%s: Ring Recovery\n", dev->name);

	/* Counter overflow: read error log */
	if(tp->ssb.Parm[0] & COUNTER_OVERFLOW)
	{
		printk(KERN_INFO "%s: Counter Overflow\n", dev->name);
		sktr_exec_cmd(dev, OC_READ_ERROR_LOG);
	}

	/* Adapter is closed, but initialized */
	if(tp->ssb.Parm[0] & REMOVE_RECEIVED)
		printk(KERN_INFO "%s: Remove Received, Reopen Adapter\n", 
			dev->name);

	/* Adapter is closed, but initialized */
	if(tp->ssb.Parm[0] & AUTO_REMOVAL_ERROR)
		printk(KERN_INFO "%s: Auto Removal Error, Reopen Adapter\n", 
			dev->name);

	if(tp->ssb.Parm[0] & HARD_ERROR)
		printk(KERN_INFO "%s: Hard Error\n", dev->name);

	if(tp->ssb.Parm[0] & SOFT_ERROR)
		printk(KERN_INFO "%s: Soft Error\n", dev->name);

	if(tp->ssb.Parm[0] & TRANSMIT_BEACON)
		printk(KERN_INFO "%s: Transmit Beacon\n", dev->name);

	if(tp->ssb.Parm[0] & SINGLE_STATION)
		printk(KERN_INFO "%s: Single Station\n", dev->name);

	/* Check if adapter has been closed */
	if(tp->ssb.Parm[0] & ADAPTER_CLOSED)
	{
		printk(KERN_INFO "%s: Adapter closed (Reopening)," 
			"QueueSkb %d, CurrentRingStat %x\n",
			dev->name, tp->QueueSkb, tp->CurrentRingStatus);
		tp->AdapterOpenFlag = 0;
		sktr_open_adapter(dev);
	}

	return;
}

/*
 * Issued if adapter has encountered an unrecoverable hardware
 * or software error.
 */
static void sktr_chk_irq(struct device *dev)
{
	int i;
	unsigned short AdapterCheckBlock[4];
	unsigned short ioaddr = dev->base_addr;
	struct net_local *tp = (struct net_local *)dev->priv;

	tp->AdapterOpenFlag = 0;	/* Adapter closed now */

	/* Page number of adapter memory */
	outw(0x0001, ioaddr + SIFADX);
	/* Address offset */
	outw(CHECKADDR, ioaddr + SIFADR);

	/* Reading 8 byte adapter check block. */
	for(i = 0; i < 4; i++)
		AdapterCheckBlock[i] = inw(ioaddr + SIFINC);

	if(sktr_debug > 3)
	{
		printk("%s: AdapterCheckBlock: ", dev->name);
		for (i = 0; i < 4; i++)
			printk("%04X", AdapterCheckBlock[i]);
		printk("\n");
	}

	switch(AdapterCheckBlock[0])
	{
		case DIO_PARITY:
			printk(KERN_INFO "%s: DIO parity error\n", dev->name);
			break;

		case DMA_READ_ABORT:
			printk(KERN_INFO "%s DMA read operation aborted:\n",
				dev->name);
			switch (AdapterCheckBlock[1])
			{
				case 0:
					printk(KERN_INFO "Timeout\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2],
						AdapterCheckBlock[3]);
					break;

				case 1:
					printk(KERN_INFO "Parity error\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2], 
						AdapterCheckBlock[3]);
					break;

				case 2: 
					printk(KERN_INFO "Bus error\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2], 
						AdapterCheckBlock[3]);
					break;

				default:
					printk(KERN_INFO "Unknown error.\n");
					break;
			}
			break;

		case DMA_WRITE_ABORT:
			printk(KERN_INFO "%s: DMA write operation aborted: \n",
				dev->name);
			switch (AdapterCheckBlock[1])
			{
				case 0: 
					printk(KERN_INFO "Timeout\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2], 
						AdapterCheckBlock[3]);
					break;

				case 1: 
					printk(KERN_INFO "Parity error\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2], 
						AdapterCheckBlock[3]);
					break;

				case 2: 
					printk(KERN_INFO "Bus error\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2], 
						AdapterCheckBlock[3]);
					break;

				default:
					printk(KERN_INFO "Unknown error.\n");
					break;
			}
			break;

		case ILLEGAL_OP_CODE:
			printk("%s: Illegal operation code in firmware\n",
				dev->name);
			/* Parm[0-3]: adapter internal register R13-R15 */
			break;

		case PARITY_ERRORS:
			printk("%s: Adapter internal bus parity error\n",
				dev->name);
			/* Parm[0-3]: adapter internal register R13-R15 */
			break;

		case RAM_DATA_ERROR:
			printk("%s: RAM data error\n", dev->name);
			/* Parm[0-1]: MSW/LSW address of RAM location. */
			break;

		case RAM_PARITY_ERROR:
			printk("%s: RAM parity error\n", dev->name);
			/* Parm[0-1]: MSW/LSW address of RAM location. */
			break;

		case RING_UNDERRUN:
			printk("%s: Internal DMA underrun detected\n",
				dev->name);
			break;

		case INVALID_IRQ:
			printk("%s: Unrecognized interrupt detected\n",
				dev->name);
			/* Parm[0-3]: adapter internal register R13-R15 */
			break;

		case INVALID_ERROR_IRQ:
			printk("%s: Unrecognized error interrupt detected\n",
				dev->name);
			/* Parm[0-3]: adapter internal register R13-R15 */
			break;

		case INVALID_XOP:
			printk("%s: Unrecognized XOP request detected\n",
				dev->name);
			/* Parm[0-3]: adapter internal register R13-R15 */
			break;

		default:
			printk("%s: Unknown status", dev->name);
			break;
	}

	if(sktr_chipset_init(dev) == 1)
	{
		/* Restart of firmware successful */
		tp->AdapterOpenFlag = 1;
	}

	return;
}

/*
 * Internal adapter pointer to RAM data are copied from adapter into
 * host system.
 */
static void sktr_read_ptr(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;
	unsigned short adapterram;

	sktr_read_ram(dev, (unsigned char *)&tp->intptrs.BurnedInAddrPtr,
			ADAPTER_INT_PTRS, 16);
	sktr_read_ram(dev, (unsigned char *)&adapterram,
			(unsigned short)SWAPB(tp->intptrs.AdapterRAMPtr), 2);

	printk(KERN_INFO "%s: Adapter RAM size: %d K\n", 
		dev->name, SWAPB(adapterram));

	return;
}

/*
 * Reads a number of bytes from adapter to system memory.
 */
static void sktr_read_ram(struct device *dev, unsigned char *Data,
				unsigned short Address, int Length)
{
	int i;
	unsigned short old_sifadx, old_sifadr, InWord;
	unsigned short ioaddr = dev->base_addr;

	/* Save the current values */
	old_sifadx = inw(ioaddr + SIFADX);
	old_sifadr = inw(ioaddr + SIFADR);

	/* Page number of adapter memory */
	outw(0x0001, ioaddr + SIFADX);
	/* Address offset in adapter RAM */
	outw(Address, ioaddr + SIFADR);

	/* Copy len byte from adapter memory to system data area. */
	i = 0;
	for(;;)
	{
		InWord = inw(ioaddr + SIFINC);

		*(Data + i) = HIBYTE(InWord);	/* Write first byte */
		if(++i == Length)		/* All is done break */
			break;

		*(Data + i) = LOBYTE(InWord);	/* Write second byte */
		if (++i == Length)		/* All is done break */
			break;
	}

	/* Restore original values */
	outw(old_sifadx, ioaddr + SIFADX);
	outw(old_sifadr, ioaddr + SIFADR);

	return;
}

/*
 * Reads MAC address from adapter ROM.
 */
static void sktr_read_addr(struct device *dev, unsigned char *Address)
{
	int i, In;
	unsigned short ioaddr = dev->base_addr;

	/* Address: 0000:0000 */
	outw(0, ioaddr + SIFADX);
	outw(0, ioaddr + SIFADR);

	/* Read six byte MAC address data */
	for(i = 0; i < 6; i++)
	{
		In = inw(ioaddr + SIFINC);
		*(Address + i) = (unsigned char)(In >> 8);
	}

	return;
}

/*
 * Cancel all queued packets in the transmission queue.
 */
static void sktr_cancel_tx_queue(struct net_local* tp)
{
	TPL *tpl;
	struct sk_buff *skb;

	/*
	 * NOTE: There must not be an active TRANSMIT command pending, when
	 * this function is called.
	 */
	if(tp->TransmitCommandActive)
		return;

	for(;;)
	{
		tpl = tp->TplBusy;
		if(!tpl->BusyFlag)
			break;
		/* "Remove" TPL from busy list. */
		tp->TplBusy = tpl->NextTPLPtr;
		sktr_write_tpl_status(tpl, 0);	/* Clear VALID bit */
		tpl->BusyFlag = 0;		/* "free" TPL */

		printk(KERN_INFO "Cancel tx (%08lXh).\n", (unsigned long)tpl);

		dev_kfree_skb(tpl->Skb);
	}

	for(;;)
	{
		skb = skb_dequeue(&tp->SendSkbQueue);
		if(skb == NULL)
			break;
		tp->QueueSkb++;
		dev_kfree_skb(skb);
	}

	return;
}

/*
 * This function is called whenever a transmit interrupt is generated by the
 * adapter. For a command complete interrupt, it is checked if we have to
 * issue a new transmit command or not.
 */
static void sktr_tx_status_irq(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;
	unsigned char HighByte, HighAc, LowAc;
	TPL *tpl;

	/* NOTE: At this point the SSB from TRANSMIT STATUS is no longer
	 * available, because the CLEAR SSB command has already been issued.
	 *
	 * Process all complete transmissions.
	 */

	for(;;)
	{
		tpl = tp->TplBusy;
		if(!tpl->BusyFlag || (tpl->Status
			& (TX_VALID | TX_FRAME_COMPLETE))
			!= TX_FRAME_COMPLETE)
		{
			break;
		}

		/* "Remove" TPL from busy list. */
		tp->TplBusy = tpl->NextTPLPtr ;

		if(sktr_debug > 3)
			sktr_dump(tpl->MData, SWAPB(tpl->FrameSize));

		/* Check the transmit status field only for directed frames*/
		if(DIRECTED_FRAME(tpl) && (tpl->Status & TX_ERROR) == 0)
		{
			HighByte = GET_TRANSMIT_STATUS_HIGH_BYTE(tpl->Status);
			HighAc   = GET_FRAME_STATUS_HIGH_AC(HighByte);
			LowAc    = GET_FRAME_STATUS_LOW_AC(HighByte);

			if((HighAc != LowAc) || (HighAc == AC_NOT_RECOGNIZED))
			{
				printk(KERN_INFO "%s: (DA=%08lX not recognized)",
					dev->name,
					*(unsigned long *)&tpl->MData[2+2]);
			}
			else
			{
				if(sktr_debug > 3)
					printk("%s: Directed frame tx'd\n", 
						dev->name);
			}
		}
		else
		{
			if(!DIRECTED_FRAME(tpl))
			{
				if(sktr_debug > 3)
					printk("%s: Broadcast frame tx'd\n",
						dev->name);
			}
		}

                tp->MacStat.tx_packets++;
		dev_kfree_skb(tpl->Skb);
		tpl->BusyFlag = 0;	/* "free" TPL */
	}

	dev->tbusy = 0;
	if(tp->QueueSkb < MAX_TX_QUEUE)
		sktr_hardware_send_packet(dev, tp);

	return;
}

/*
 * Called if a frame receive interrupt is generated by the adapter.
 * Check if the frame is valid and indicate it to system.
 */
static void sktr_rcv_status_irq(struct device *dev)
{
	struct net_local *tp = (struct net_local *)dev->priv;
	unsigned char *ReceiveDataPtr;
	struct sk_buff *skb;
	unsigned int Length, Length2;
	RPL *rpl;
	RPL *SaveHead;

	/* NOTE: At this point the SSB from RECEIVE STATUS is no longer
	 * available, because the CLEAR SSB command has already been issued.
	 *
	 * Process all complete receives.
	 */

	for(;;)
	{
		rpl = tp->RplHead;
		if(rpl->Status & RX_VALID)
			break;		/* RPL still in use by adapter */

		/* Forward RPLHead pointer to next list. */
		SaveHead = tp->RplHead;
		tp->RplHead = rpl->NextRPLPtr;

		/* Get the frame size (Byte swap for Intel).
		 * Do this early (see workaround comment below)
		 */
		Length = (unsigned short)SWAPB(rpl->FrameSize);

		/* Check if the Frame_Start, Frame_End and
		 * Frame_Complete bits are set.
		 */
		if((rpl->Status & VALID_SINGLE_BUFFER_FRAME)
			== VALID_SINGLE_BUFFER_FRAME)
		{
			ReceiveDataPtr = rpl->MData;

			/* Workaround for delayed write of FrameSize on ISA
			 * (FrameSize is false but valid-bit is reset)
			 * Frame size is set to zero when the RPL is freed.
			 * Length2 is there because there have also been
			 * cases where the FrameSize was partially written
			 */
			Length2 = (unsigned short)SWAPB(rpl->FrameSize);

			if(Length == 0 || Length != Length2)
			{
				tp->RplHead = SaveHead;
				break;	/* Return to sktr_interrupt */
			}

			/* Drop frames sent by myself */
			if(sktr_chk_frame(dev, rpl->MData))
			{
				printk(KERN_INFO "%s: Received my own frame\n",
					dev->name);
				if(rpl->Skb != NULL)
					dev_kfree_skb(rpl->Skb);
			}
			else
			{
				sktr_update_rcv_stats(tp,ReceiveDataPtr,Length);

				if(sktr_debug > 3)
					printk("%s: Packet Length %04X (%d)\n",
						dev->name, Length, Length);

				/* Indicate the received frame to system the
				 * adapter does the Source-Routing padding for 
				 * us. See: OpenOptions in sktr_init_opb()
				 */
				skb = rpl->Skb;
				if(rpl->SkbStat == SKB_UNAVAILABLE)
				{
					/* Try again to allocate skb */
					skb = dev_alloc_skb(tp->MaxPacketSize);
					if(skb == NULL)
					{
						/* Update Stats ?? */
					}
					else
					{
						skb->dev	= dev;
						skb_put(skb, tp->MaxPacketSize);
						rpl->SkbStat 	= SKB_DATA_COPY;
						ReceiveDataPtr 	= rpl->MData;
					}
				}

				if(rpl->SkbStat == SKB_DATA_COPY
					|| rpl->SkbStat == SKB_DMA_DIRECT)
				{
					if(rpl->SkbStat == SKB_DATA_COPY)
					{
						memmove(skb->data, ReceiveDataPtr, Length);
					}

					/* Deliver frame to system */
					rpl->Skb = NULL;
					skb_trim(skb,Length);
					skb->protocol = tr_type_trans(skb,dev);
					netif_rx(skb);
				}
			}
		}
		else	/* Invalid frame */
		{
			if(rpl->Skb != NULL)
				dev_kfree_skb(rpl->Skb);

			/* Skip list. */
			if(rpl->Status & RX_START_FRAME)
				/* Frame start bit is set -> overflow. */
				tp->MacStat.rx_errors++;
		}

		/* Allocate new skb for rpl */
		rpl->Skb = dev_alloc_skb(tp->MaxPacketSize);

		/* skb == NULL ? then use local buffer */
		if(rpl->Skb == NULL)
		{
			rpl->SkbStat = SKB_UNAVAILABLE;
			rpl->FragList[0].DataAddr = htonl(virt_to_bus(tp->LocalRxBuffers[rpl->RPLIndex]));
			rpl->MData = tp->LocalRxBuffers[rpl->RPLIndex];
		}
		else	/* skb != NULL */
		{
			rpl->Skb->dev = dev;
			skb_put(rpl->Skb, tp->MaxPacketSize);

			/* Data unreachable for DMA ? then use local buffer */
			if(virt_to_bus(rpl->Skb->data) + tp->MaxPacketSize
				> ISA_MAX_ADDRESS)
			{
				rpl->SkbStat = SKB_DATA_COPY;
				rpl->FragList[0].DataAddr = htonl(virt_to_bus(tp->LocalRxBuffers[rpl->RPLIndex]));
				rpl->MData = tp->LocalRxBuffers[rpl->RPLIndex];
			}
			else
			{
				/* DMA directly in skb->data */
				rpl->SkbStat = SKB_DMA_DIRECT;
				rpl->FragList[0].DataAddr = htonl(virt_to_bus(rpl->Skb->data));
				rpl->MData = rpl->Skb->data;
			}
		}

		rpl->FragList[0].DataCount = SWAPB(tp->MaxPacketSize);
		rpl->FrameSize = 0;

		/* Pass the last RPL back to the adapter */
		tp->RplTail->FrameSize = 0;

		/* Reset the CSTAT field in the list. */
		sktr_write_rpl_status(tp->RplTail, RX_VALID | RX_FRAME_IRQ);

		/* Current RPL becomes last one in list. */
		tp->RplTail = tp->RplTail->NextRPLPtr;

		/* Inform adapter about RPL valid. */
		sktr_exec_sifcmd(dev, CMD_RX_VALID);
	}

	return;
}

/*
 * This function should be used whenever the status of any RPL must be
 * modified by the driver, because the compiler may otherwise change the
 * order of instructions such that writing the RPL status may be executed
 * at an undesireable time. When this function is used, the status is
 * always written when the function is called.
 */
static void sktr_write_rpl_status(RPL *rpl, unsigned int Status)
{
	rpl->Status = Status;

	return;
}

/*
 * The function updates the statistic counters in mac->MacStat.
 * It differtiates between directed and broadcast/multicast ( ==functional)
 * frames.
 */
static void sktr_update_rcv_stats(struct net_local *tp, unsigned char DataPtr[],
					unsigned int Length)
{
	tp->MacStat.rx_packets++;

	/* Test functional bit */
	if(DataPtr[2] & GROUP_BIT)
		tp->MacStat.multicast++;

	return;
}

/*
 * Check if it is a frame of myself. Compare source address with my current
 * address in reverse direction, and mask out the TR_RII.
 */
static unsigned char sktr_chk_frame(struct device *dev, unsigned char *Addr)
{
	int i;

	for(i = 5; i > 0; i--)
	{
		if(Addr[8 + i] != dev->dev_addr[i])
			return (0);
	}

	/* Mask out RIF bit. */
	if((Addr[8] & ~TR_RII) != (unsigned char)(dev->dev_addr[0]))
		return (0);

	return (1);  /* It is my frame. */
}

/*
 * Dump Packet (data)
 */
static void sktr_dump(unsigned char *Data, int length)
{
        int i, j;

        for (i = 0, j = 0; i < length / 8; i++, j += 8)
        {
		printk(KERN_DEBUG "%02x %02x %02x %02x %02x %02x %02x %02x\n",
			Data[j+0],Data[j+1],Data[j+2],Data[j+3],
                        Data[j+4],Data[j+5],Data[j+6],Data[j+7]);
        }

        return;
}

#ifdef MODULE

static struct device* dev_sktr[SKTR_MAX_ADAPTERS];
static int io[SKTR_MAX_ADAPTERS]	= { 0, 0 };
static int irq[SKTR_MAX_ADAPTERS] 	= { 0, 0 };
static int mem[SKTR_MAX_ADAPTERS] 	= { 0, 0 };

MODULE_PARM(io,  "1-" __MODULE_STRING(SKTR_MAX_ADAPTERS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(SKTR_MAX_ADAPTERS) "i");
MODULE_PARM(mem, "1-" __MODULE_STRING(SKTR_MAX_ADAPTERS) "i");

int init_module(void)
{
	int i;

	for(i = 0; i < SKTR_MAX_ADAPTERS; i++)
	{
                irq[i] = 0;
                mem[i] = 0;
                dev_sktr[i] = NULL;
                dev_sktr[i] = init_trdev(dev_sktr[i], 0);
                if(dev_sktr[i] == NULL)
                        return (-ENOMEM);

		dev_sktr[i]->base_addr = io[i];
                dev_sktr[i]->irq       = irq[i];
                dev_sktr[i]->mem_start = mem[i];
                dev_sktr[i]->init      = &sktr_probe;

                if(register_trdev(dev_sktr[i]) != 0)
		{
                        kfree_s(dev_sktr[i], sizeof(struct device));
                        dev_sktr[i] = NULL;
                        if(i == 0)
			{
                                printk("sktr: register_trdev() returned non-zero.\n");
                                return (-EIO);
                        }
			else
                                return (0);
                }
        }

        return (0);
}

void cleanup_module(void)
{
	int i;

        for(i = 0; i < SKTR_MAX_ADAPTERS; i++)
	{
		if(dev_sktr[i])
		{
			unregister_trdev(dev_sktr[i]);
			release_region(dev_sktr[i]->base_addr, SKTR_IO_EXTENT);
			if(dev_sktr[i]->irq)
				free_irq(dev_sktr[i]->irq, dev_sktr[i]);
			if(dev_sktr[i]->dma > 0)
				free_dma(dev_sktr[i]->dma);
			if(dev_sktr[i]->priv)
				kfree_s(dev_sktr[i]->priv, sizeof(struct net_local));
			kfree_s(dev_sktr[i], sizeof(struct device));
			dev_sktr[i] = NULL;
                }
	}
}
#endif /* MODULE */
