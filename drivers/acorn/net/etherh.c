/*
 * linux/drivers/net/etherh.c
 *
 * NS8390 ANT etherh specific driver
 *  For Acorn machines
 *
 * Thanks to I-Cubed for information on their cards.
 *
 * By Russell King.
 *
 * Changelog:
 *  08-12-1996	RMK	1.00	Created
 *		RMK	1.03	Added support for EtherLan500 cards
 *  23-11-1997	RMK	1.04	Added media autodetection
 *  16-04-1998	RMK	1.05	Improved media autodetection
 *
 * Insmod Module Parameters
 * ------------------------
 *   io=<io_base>
 *   irq=<irqno>
 *   xcvr=<0|1> 0 = 10bT, 1=10b2 (Lan600/600A only)
 */

#include <linux/module.h>
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
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/ecard.h>
#include <asm/io.h>
#include <asm/irq.h>

#include "../../net/8390.h"

#define NET_DEBUG  0
#define DEBUG_INIT 2

static unsigned int net_debug = NET_DEBUG;
static const card_ids etherh_cids[] = {
	{ MANU_I3, PROD_I3_ETHERLAN500 },
	{ MANU_I3, PROD_I3_ETHERLAN600 },
	{ MANU_I3, PROD_I3_ETHERLAN600A },
	{ 0xffff, 0xffff }
};

static char *version = "etherh [500/600/600A] ethernet driver (c) 1998 R.M.King v1.05\n";

#define ETHERH500_DATAPORT	0x200	/* MEMC */
#define ETHERH500_NS8390	0x000	/* MEMC */
#define ETHERH500_CTRLPORT	0x200	/* IOC  */

#define ETHERH600_DATAPORT	16	/* MEMC */
#define ETHERH600_NS8390	0x200	/* MEMC */
#define ETHERH600_CTRLPORT	0x080	/* MEMC */

#define ETHERH_CP_IE		1
#define ETHERH_CP_IF		2
#define ETHERH_CP_HEARTBEAT	2

#define ETHERH_TX_START_PAGE	1
#define ETHERH_STOP_PAGE	0x7f

/* --------------------------------------------------------------------------- */

/*
 * Read the ethernet address string from the on board rom.
 * This is an ascii string...
 */
static int
etherh_addr(char *addr, struct expansion_card *ec)
{
	struct in_chunk_dir cd;
	char *s;
	
	if (ecard_readchunk(&cd, ec, 0xf5, 0) && (s = strchr(cd.d.string, '('))) {
		int i;
		for (i = 0; i < 6; i++) {
			addr[i] = simple_strtoul(s + 1, &s, 0x10);
			if (*s != (i == 5? ')' : ':'))
				break;
		}
		if (i == 6)
			return 0;
	}
	return ENODEV;
}

static void
etherh_setif(struct device *dev)
{
	unsigned long addr;
	unsigned long flags;

	save_flags_cli(flags);

	/* set the interface type */
	switch (dev->mem_end) {
	case PROD_I3_ETHERLAN600:
	case PROD_I3_ETHERLAN600A:
		addr = dev->base_addr + EN0_RCNTHI;

		if (ei_status.interface_num)	/* 10b2 */
			outb((inb(addr) & 0xf8) | 1, addr);
		else				/* 10bT */
			outb((inb(addr) & 0xf8), addr);
		break;

	case PROD_I3_ETHERLAN500:
		addr = dev->rmem_start;

		if (ei_status.interface_num)	/* 10b2 */
			outb(inb(addr) & ~ETHERH_CP_IF, addr);
		else				/* 10bT */
			outb(inb(addr) | ETHERH_CP_IF, addr);
		break;

	default:
		break;
	}

	restore_flags(flags);
}

static int
etherh_getifstat(struct device *dev)
{
	int stat;

	switch (dev->mem_end) {
	case PROD_I3_ETHERLAN600:
	case PROD_I3_ETHERLAN600A:
		if (ei_status.interface_num)	/* 10b2 */
			stat = 1;
		else				/* 10bT */
			stat = inb(dev->base_addr+EN0_RCNTHI) & 4;
		break;

	case PROD_I3_ETHERLAN500:
		if (ei_status.interface_num)	/* 10b2 */
			stat = 1;
		else				/* 10bT */
			stat = inb(dev->rmem_start) & ETHERH_CP_HEARTBEAT;
		break;

	default:
		stat = 0;
		break;
	}

	return stat != 0;
}

/*
 * Reset the 8390 (hard reset)
 */
static void
etherh_reset(struct device *dev)
{
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, dev->base_addr);

	etherh_setif(dev);
}

/*
 * Write a block of data out to the 8390
 */
static void
etherh_block_output (struct device *dev, int count, const unsigned char *buf, int start_page)
{
	unsigned int addr, dma_addr;
	unsigned long dma_start;
	
	if (ei_status.dmaing) {
		printk ("%s: DMAing conflict in etherh_block_input: "
			" DMAstat %d irqlock %d intr %ld\n", dev->name,
			ei_status.dmaing, ei_status.irqlock, dev->interrupt);
		return;
	}

	ei_status.dmaing |= 1;

	addr = dev->base_addr;
	dma_addr = dev->mem_start;

	count = (count + 1) & ~1;
	outb (E8390_NODMA | E8390_PAGE0 | E8390_START, addr + E8390_CMD);

	outb (0x42, addr + EN0_RCNTLO);
	outb (0x00, addr + EN0_RCNTHI);
	outb (0x42, addr + EN0_RSARLO);
	outb (0x00, addr + EN0_RSARHI);
	outb (E8390_RREAD | E8390_START, addr + E8390_CMD);

	udelay (1);

	outb (ENISR_RDC, addr + EN0_ISR);
	outb (count, addr + EN0_RCNTLO);
	outb (count >> 8, addr + EN0_RCNTHI);
	outb (0, addr + EN0_RSARLO);
	outb (start_page, addr + EN0_RSARHI);
	outb (E8390_RWRITE | E8390_START, addr + E8390_CMD);

	if (ei_status.word16)
		outsw (dma_addr, buf, count >> 1);
#ifdef BIT8
	else
		outsb (dma_addr, buf, count);
#endif

	dma_start = jiffies;

	while ((inb (addr + EN0_ISR) & ENISR_RDC) == 0)
		if (jiffies - dma_start > 2*HZ/100) { /* 20ms */
			printk ("%s: timeout waiting for TX RDC\n", dev->name);
			etherh_reset (dev);
			NS8390_init (dev, 1);
			break;
		}

	outb (ENISR_RDC, addr + EN0_ISR);
	ei_status.dmaing &= ~1;
}

/*
 * Read a block of data from the 8390
 */
static void
etherh_block_input (struct device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	unsigned int addr, dma_addr;
	unsigned char *buf;

	if (ei_status.dmaing) {
		printk ("%s: DMAing conflict in etherh_block_input: "
			" DMAstat %d irqlock %d intr %ld\n", dev->name,
			ei_status.dmaing, ei_status.irqlock, dev->interrupt);
		return;
	}

	ei_status.dmaing |= 1;

	addr = dev->base_addr;
	dma_addr = dev->mem_start;

	buf = skb->data;
	outb (E8390_NODMA | E8390_PAGE0 | E8390_START, addr + E8390_CMD);
	outb (count, addr + EN0_RCNTLO);
	outb (count >> 8, addr + EN0_RCNTHI);
	outb (ring_offset, addr + EN0_RSARLO);
	outb (ring_offset >> 8, addr + EN0_RSARHI);
	outb (E8390_RREAD | E8390_START, addr + E8390_CMD);

	if (ei_status.word16) {
		insw (dma_addr, buf, count >> 1);
		if (count & 1)
			buf[count - 1] = inb (dma_addr);
	}
#ifdef BIT8
	else
		insb (dma_addr, buf, count);
#endif

	outb (ENISR_RDC, addr + EN0_ISR);
	ei_status.dmaing &= ~1;
}

/*
 * Read a header from the 8390
 */
static void
etherh_get_header (struct device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	unsigned int addr, dma_addr;

	if (ei_status.dmaing) {
		printk ("%s: DMAing conflict in etherh_get_header: "
			" DMAstat %d irqlock %d intr %ld\n", dev->name,
			ei_status.dmaing, ei_status.irqlock, dev->interrupt);
		return;
	}

	ei_status.dmaing |= 1;

	addr = dev->base_addr;
	dma_addr = dev->mem_start;

	outb (E8390_NODMA | E8390_PAGE0 | E8390_START, addr + E8390_CMD);
	outb (sizeof (*hdr), addr + EN0_RCNTLO);
	outb (0, addr + EN0_RCNTHI);
	outb (0, addr + EN0_RSARLO);
	outb (ring_page, addr + EN0_RSARHI);
	outb (E8390_RREAD | E8390_START, addr + E8390_CMD);

	if (ei_status.word16)
		insw (dma_addr, hdr, sizeof (*hdr) >> 1);
#ifdef BIT8
	else
		insb (dma_addr, hdr, sizeof (*hdr));
#endif

	outb (ENISR_RDC, addr + EN0_ISR);
	ei_status.dmaing &= ~1;
}

/*
 * Open/initialize the board.  This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
static int
etherh_open(struct device *dev)
{
	MOD_INC_USE_COUNT;

	if (request_irq(dev->irq, ei_interrupt, 0, "etherh", dev)) {
		MOD_DEC_USE_COUNT;
		return -EAGAIN;
	}

	etherh_reset(dev);
	ei_open(dev);
	return 0;
}

/*
 * The inverse routine to etherh_open().
 */
static int
etherh_close(struct device *dev)
{
	ei_close (dev);
	free_irq (dev->irq, dev);

	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * This is the real probe routine.
 */
static int
etherh_probe1(struct device *dev)
{
	static int version_printed;
	unsigned int addr, i, reg0, tmp;
	const char *dev_type;
	const char *if_type;

	addr = dev->base_addr;

	if (net_debug && version_printed++ == 0)
		printk(version);

	switch (dev->mem_end) {
	case PROD_I3_ETHERLAN500:
		dev_type = "500 ";
		break;
	case PROD_I3_ETHERLAN600:
		dev_type = "600 ";
		break;
	case PROD_I3_ETHERLAN600A:
		dev_type = "600A ";
		break;
	default:
		dev_type = "";
	}

	reg0 = inb (addr);
	if (reg0 == 0xff) {
		if (net_debug & DEBUG_INIT)
			printk ("%s: etherh error: NS8390 command register wrong\n", dev->name);
		return -ENODEV;
	}

 	outb (E8390_NODMA | E8390_PAGE1 | E8390_STOP, addr + E8390_CMD);
	tmp = inb (addr + 13);
	outb (0xff, addr + 13);
	outb (E8390_NODMA | E8390_PAGE0, addr + E8390_CMD);
	inb (addr + EN0_COUNTER0);
	if (inb (addr + EN0_COUNTER0) != 0) {
		if (net_debug & DEBUG_INIT)
			printk ("%s: etherh error: NS8390 not found\n", dev->name);
		outb (reg0, addr);
		outb (tmp, addr + 13);
		return -ENODEV;
	}

	if (ethdev_init (dev))
		return -ENOMEM;

	request_region (addr, 16, "etherh");

	printk("%s: etherh %sfound at %lx, IRQ%d, ether address ",
		dev->name, dev_type, dev->base_addr, dev->irq);

	for (i = 0; i < 6; i++)
		printk (i == 5 ? "%2.2x " : "%2.2x:", dev->dev_addr[i]);

	ei_status.name = "etherh";
	ei_status.word16 = 1;
	ei_status.tx_start_page = ETHERH_TX_START_PAGE;
	ei_status.rx_start_page = ei_status.tx_start_page + TX_PAGES;
	ei_status.stop_page = ETHERH_STOP_PAGE;
	ei_status.reset_8390 = etherh_reset;
	ei_status.block_input = etherh_block_input;
	ei_status.block_output = etherh_block_output;
	ei_status.get_8390_hdr = etherh_get_header;
	dev->open = etherh_open;
	dev->stop = etherh_close;

	/* select 10bT */
	ei_status.interface_num = 0;
	if_type = "10BaseT";
	etherh_setif(dev);
	mdelay(1);
	if (!etherh_getifstat(dev)) {
		if_type = "10Base2";
		ei_status.interface_num = 1;
		etherh_setif(dev);
	}
	if (!etherh_getifstat(dev))
		if_type = "UNKNOWN";

	printk("%s\n", if_type);

	etherh_reset(dev);
	NS8390_init (dev, 0);
	return 0;
}

static void etherh_irq_enable(ecard_t *ec, int irqnr)
{
	unsigned int ctrl_addr = (unsigned int)ec->irq_data;
	outb(inb(ctrl_addr) | ETHERH_CP_IE, ctrl_addr);
}

static void etherh_irq_disable(ecard_t *ec, int irqnr)
{
	unsigned int ctrl_addr = (unsigned int)ec->irq_data;
	outb(inb(ctrl_addr) & ~ETHERH_CP_IE, ctrl_addr);
}

static expansioncard_ops_t etherh_ops = {
	etherh_irq_enable,
	etherh_irq_disable,
	NULL,
	NULL
};

static void etherh_initdev (ecard_t *ec, struct device *dev)
{
	ecard_claim (ec);
	
	dev->irq = ec->irq;
	dev->mem_end = ec->cid.product;

	switch (ec->cid.product) {
	case PROD_I3_ETHERLAN500:
		dev->base_addr = ecard_address (ec, ECARD_MEMC, 0) + ETHERH500_NS8390;
		dev->mem_start = dev->base_addr + ETHERH500_DATAPORT;
		dev->rmem_start = (unsigned long)
		ec->irq_data   = (void *)ecard_address (ec, ECARD_IOC, ECARD_FAST)
				  + ETHERH500_CTRLPORT;
		break;

	case PROD_I3_ETHERLAN600:
	case PROD_I3_ETHERLAN600A:
		dev->base_addr = ecard_address (ec, ECARD_MEMC, 0) + ETHERH600_NS8390;
		dev->mem_start = dev->base_addr + ETHERH600_DATAPORT;
		ec->irq_data = (void *)(dev->base_addr + ETHERH600_CTRLPORT);
		break;

	default:
		printk ("%s: etherh error: unknown card type\n", dev->name);
	}
	ec->ops = &etherh_ops;

	etherh_addr (dev->dev_addr, ec);
}

#ifndef MODULE
int
etherh_probe(struct device *dev)
{
	if (!dev)
		return ENODEV;

	ecard_startfind ();

	if (!dev->base_addr) {
		struct expansion_card *ec;

		if ((ec = ecard_find (0, etherh_cids)) == NULL)
			return ENODEV;

		etherh_initdev (ec, dev);
	}
	return etherh_probe1 (dev);
}
#endif

#ifdef MODULE
#define MAX_ETHERH_CARDS 2

static int io[MAX_ETHERH_CARDS];
static int irq[MAX_ETHERH_CARDS];
static char ethernames[MAX_ETHERH_CARDS][9];
static struct device *my_ethers[MAX_ETHERH_CARDS];
static struct expansion_card *ec[MAX_ETHERH_CARDS];

static int
init_all_cards(void)
{
	struct device *dev = NULL;
	struct expansion_card *boguscards[MAX_ETHERH_CARDS];
	int i, found = 0;

	for (i = 0; i < MAX_ETHERH_CARDS; i++) {
		my_ethers[i] = NULL;
		boguscards[i] = NULL;
		ec[i] = NULL;
		strcpy (ethernames[i], "        ");
	}

	ecard_startfind();

	for (i = 0; i < MAX_ETHERH_CARDS; i++) {
		if (!dev)
			dev = (struct device *)kmalloc (sizeof (struct device), GFP_KERNEL);
		if (dev)
			memset (dev, 0, sizeof (struct device));

		if (!io[i]) {
			if ((ec[i] = ecard_find (0, etherh_cids)) == NULL)
				continue;

			if (!dev)
				return -ENOMEM;

			etherh_initdev (ec[i], dev);
		} else {
			ec[i] = NULL;
			if (!dev)
				return -ENOMEM;
			dev->base_addr = io[i];
			dev->irq = irq[i];
		}

		dev->init = etherh_probe1;
		dev->name = ethernames[i];

		my_ethers[i] = dev;

		if (register_netdev(dev) != 0) {
			printk (KERN_WARNING "No etherh card found at %08lX\n", dev->base_addr);
			if (ec[i]) {
				boguscards[i] = ec[i];
				ec[i] = NULL;
			}
			continue;
		}
		found ++;
		dev = NULL;
	}

	if (dev)
		kfree (dev);

	for (i = 0; i < MAX_ETHERH_CARDS; i++)
		if (boguscards[i]) {
			boguscards[i]->ops = NULL;
			ecard_release (boguscards[i]);
		}

	return found ? 0 : -ENODEV;
}

int
init_module(void)
{
	int ret;

	if (load_8390_module(__FILE__))
		return -ENOSYS;

	lock_8390_module();

	ret = init_all_cards();

	if (ret) {
		unlock_8390_module();
	}

	return ret;
}

void
cleanup_module(void)
{
	int i;
	for (i = 0; i < MAX_ETHERH_CARDS; i++) {
		if (my_ethers[i]) {
			unregister_netdev(my_ethers[i]);
			release_region (my_ethers[i]->base_addr, 16);
			kfree (my_ethers[i]);
			my_ethers[i] = NULL;
		}
		if (ec[i]) {
			ec[i]->ops = NULL;
			ecard_release(ec[i]);
			ec[i] = NULL;
		}
	}
	unlock_8390_module();
}
#endif /* MODULE */
