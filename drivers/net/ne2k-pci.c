/* ne2k-pci.c: A NE2000 clone on PCI bus driver for Linux. */
/*
	A Linux device driver for PCI NE2000 clones.

	Authorship and other copyrights:
	1992-1998 by Donald Becker, NE2000 core and various modifications.
	1995-1998 by Paul Gortmaker, core modifications and PCI support.

	Copyright 1993 assigned to the United States Government as represented
	by the Director, National Security Agency.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
	Center of Excellence in Space Data and Information Sciences
	Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	People are making PCI ne2000 clones! Oh the horror, the horror...

	Issues remaining:
	No full-duplex support.
*/

/* Our copyright info must remain in the binary. */
static const char *version =
"ne2k-pci.c:v0.99L 2/7/98 D. Becker/P. Gortmaker http://cesdis.gsfc.nasa.gov/linux/drivers/ne2k-pci.html\n";

#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "8390.h"

/* Set statically or when loading the driver module. */
static int debug = 1;

/* Some defines that people can play with if so inclined. */

/* Use 32 bit data-movement operations instead of 16 bit. */
#define USE_LONGIO

/* Do we implement the read before write bugfix ? */
/* #define NE_RW_BUGFIX */

/* Do we have a non std. amount of memory? (in units of 256 byte pages) */
/* #define PACKETBUF_MEMSIZE	0x40 */

static struct {
	unsigned short vendor, dev_id;
	char *name;
}
pci_clone_list[] __initdata = {
	{0x10ec, 0x8029, "RealTek RTL-8029"},
	{0x1050, 0x0940, "Winbond 89C940"},
	{0x11f6, 0x1401, "Compex RL2000"},
	{0x8e2e, 0x3000, "KTI ET32P2"},
	{0x4a14, 0x5000, "NetVin NV5000SC"},
	{0x1106, 0x0926, "Via 82C926"},
	{0x10bd, 0x0e34, "SureCom NE34"},
	{0x1050, 0x5a5a, "Winbond"},
	{0,}
};

/* ---- No user-serviceable parts below ---- */

#define NE_BASE	 (dev->base_addr)
#define NE_CMD	 	0x00
#define NE_DATAPORT	0x10	/* NatSemi-defined port window offset. */
#define NE_RESET	0x1f	/* Issue a read to reset, a write to clear. */
#define NE_IO_EXTENT	0x20

#define NESM_START_PG	0x40	/* First page of TX buffer */
#define NESM_STOP_PG	0x80	/* Last page +1 of RX ring */

int ne2k_pci_probe(struct device *dev);
static struct device *ne2k_pci_probe1(struct device *dev, int ioaddr, int irq);

static int ne2k_pci_open(struct device *dev);
static int ne2k_pci_close(struct device *dev);

static void ne2k_pci_reset_8390(struct device *dev);
static void ne2k_pci_get_8390_hdr(struct device *dev, struct e8390_pkt_hdr *hdr,
			  int ring_page);
static void ne2k_pci_block_input(struct device *dev, int count,
			  struct sk_buff *skb, int ring_offset);
static void ne2k_pci_block_output(struct device *dev, const int count,
		const unsigned char *buf, const int start_page);



/* No room in the standard 8390 structure for extra info we need. */
struct ne2k_pci_card {
	struct ne2k_pci_card *next;
	struct device *dev;
	struct pci_dev *pci_dev;
};
/* A list of all installed devices, for removing the driver module. */
static struct ne2k_pci_card *ne2k_card_list = NULL;

#ifdef MODULE

int
init_module(void)
{
	int retval;

	/* We must emit version information. */
	if (debug)
		printk(KERN_INFO "%s", version);

	retval = ne2k_pci_probe(0);

	if (retval) {
		printk(KERN_NOTICE "ne2k-pci.c: no (useable) cards found, driver NOT installed.\n");
		return retval;
	}
	lock_8390_module();
	return 0;
}

void
cleanup_module(void)
{
	struct device *dev;
	struct ne2k_pci_card *this_card;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (ne2k_card_list) {
		dev = ne2k_card_list->dev;
		unregister_netdev(dev);
		release_region(dev->base_addr, NE_IO_EXTENT);
		kfree(dev);
		this_card = ne2k_card_list;
		ne2k_card_list = ne2k_card_list->next;
		kfree(this_card);
	}
	unlock_8390_module();
}

#endif  /* MODULE */

/*
  NEx000-clone boards have a Station Address (SA) PROM (SAPROM) in the packet
  buffer memory space.  By-the-spec NE2000 clones have 0x57,0x57 in bytes
  0x0e,0x0f of the SAPROM, while other supposed NE2000 clones must be
  detected by their SA prefix.

  Reading the SAPROM from a word-wide card with the 8390 set in byte-wide
  mode results in doubled values, which can be detected and compensated for.

  The probe is also responsible for initializing the card and filling
  in the 'dev' and 'ei_status' structures.
*/

#ifdef HAVE_DEVLIST
struct netdev_entry netcard_drv =
{"ne2k_pci", ne2k_pci_probe1, NE_IO_EXTENT, 0};
#endif

__initfunc (int ne2k_pci_probe(struct device *dev))
{
	struct pci_dev *pdev = NULL;
	int cards_found = 0;
	int i;

	if ( ! pci_present())
		return -ENODEV;

	while ((pdev = pci_find_class(PCI_CLASS_NETWORK_ETHERNET << 8, pdev)) != NULL) {
		u8 pci_irq_line;
		u16 pci_command, new_command;
		u32 pci_ioaddr;

		/* Note: some vendor IDs (RealTek) have non-NE2k cards as well. */
		for (i = 0; pci_clone_list[i].vendor != 0; i++)
			if (pci_clone_list[i].vendor == pdev->vendor
				&& pci_clone_list[i].dev_id == pdev->device)
				break;
		if (pci_clone_list[i].vendor == 0)
			continue;

		pci_ioaddr = pdev->base_address[0] & PCI_BASE_ADDRESS_IO_MASK;
		pci_irq_line = pdev->irq;
		pci_read_config_word(pdev, PCI_COMMAND, &pci_command);

		/* Avoid already found cards from previous calls */
		if (check_region(pci_ioaddr, NE_IO_EXTENT))
			continue;

#ifndef MODULE
		{
			static unsigned version_printed = 0;
			if (version_printed++ == 0)
				printk(KERN_INFO "%s", version);
		}
#endif

		/* Activate the card: fix for brain-damaged Win98 BIOSes. */
		new_command = pci_command | PCI_COMMAND_IO;
		if (pci_command != new_command) {
			printk(KERN_INFO "  The PCI BIOS has not enabled this"
				   " NE2k clone!  Updating PCI command %4.4x->%4.4x.\n",
				   pci_command, new_command);
			pci_write_config_word(pdev, PCI_COMMAND, new_command);
		}

		if (pci_irq_line <= 0 || pci_irq_line >= NR_IRQS)
			printk(KERN_WARNING "  WARNING: The PCI BIOS assigned this PCI NE2k"
				   " card to IRQ %d, which is unlikely to work!.\n"
				   KERN_WARNING " You should use the PCI BIOS setup to assign"
				   " a valid IRQ line.\n", pci_irq_line);

		printk("ne2k-pci.c: PCI NE2000 clone '%s' at I/O %#x, IRQ %d.\n",
			   pci_clone_list[i].name, pci_ioaddr, pci_irq_line);
		dev = ne2k_pci_probe1(dev, pci_ioaddr, pci_irq_line);
		if (dev == 0) {
			/* Should not happen. */
			printk(KERN_ERR "ne2k-pci: Probe of PCI card at %#x failed.\n",
				   pci_ioaddr);
			continue;
		} else {
			struct ne2k_pci_card *ne2k_card =
				kmalloc(sizeof(struct ne2k_pci_card), GFP_KERNEL);
			ne2k_card->next = ne2k_card_list;
			ne2k_card_list = ne2k_card;
			ne2k_card->dev = dev;
			ne2k_card->pci_dev = pdev;
		}
		dev = 0;

		cards_found++;
	}

	return cards_found ? 0 : -ENODEV;
}

__initfunc (static struct device *ne2k_pci_probe1(struct device *dev, int ioaddr, int irq))
{
	int i;
	unsigned char SA_prom[32];
	const char *name = NULL;
	int start_page, stop_page;
	int reg0 = inb(ioaddr);

	if (reg0 == 0xFF)
		return 0;

	/* Do a preliminary verification that we have a 8390. */
	{
		int regd;
		outb(E8390_NODMA+E8390_PAGE1+E8390_STOP, ioaddr + E8390_CMD);
		regd = inb(ioaddr + 0x0d);
		outb(0xff, ioaddr + 0x0d);
		outb(E8390_NODMA+E8390_PAGE0, ioaddr + E8390_CMD);
		inb(ioaddr + EN0_COUNTER0); /* Clear the counter by reading. */
		if (inb(ioaddr + EN0_COUNTER0) != 0) {
			outb(reg0, ioaddr);
			outb(regd, ioaddr + 0x0d);	/* Restore the old values. */
			return 0;
		}
	}

	/* Reset card. Who knows what dain-bramaged state it was left in. */
	{
		unsigned long reset_start_time = jiffies;

		outb(inb(ioaddr + NE_RESET), ioaddr + NE_RESET);

		/* This looks like a horrible timing loop, but it should never take
		   more than a few cycles.
		*/
		while ((inb(ioaddr + EN0_ISR) & ENISR_RESET) == 0)
			/* Limit wait: '2' avoids jiffy roll-over. */
			if (jiffies - reset_start_time > 2) {
				printk("ne2k-pci: Card failure (no reset ack).\n");
				return 0;
			}
		
		outb(0xff, ioaddr + EN0_ISR);		/* Ack all intr. */
	}

	if (load_8390_module("ne2k-pci.c")) {
		return 0;
	}

	/* Read the 16 bytes of station address PROM.
	   We must first initialize registers, similar to NS8390_init(eifdev, 0).
	   We can't reliably read the SAPROM address without this.
	   (I learned the hard way!). */
	{
		struct {unsigned char value, offset; } program_seq[] = {
			{E8390_NODMA+E8390_PAGE0+E8390_STOP, E8390_CMD}, /* Select page 0*/
			{0x49,	EN0_DCFG},	/* Set word-wide access. */
			{0x00,	EN0_RCNTLO},	/* Clear the count regs. */
			{0x00,	EN0_RCNTHI},
			{0x00,	EN0_IMR},	/* Mask completion irq. */
			{0xFF,	EN0_ISR},
			{E8390_RXOFF, EN0_RXCR},	/* 0x20  Set to monitor */
			{E8390_TXOFF, EN0_TXCR},	/* 0x02  and loopback mode. */
			{32,	EN0_RCNTLO},
			{0x00,	EN0_RCNTHI},
			{0x00,	EN0_RSARLO},	/* DMA starting at 0x0000. */
			{0x00,	EN0_RSARHI},
			{E8390_RREAD+E8390_START, E8390_CMD},
		};
		for (i = 0; i < sizeof(program_seq)/sizeof(program_seq[0]); i++)
			outb(program_seq[i].value, ioaddr + program_seq[i].offset);

	}

#ifdef notdef
	/* Some broken PCI cards don't respect the byte-wide
	   request in program_seq above, and hence don't have doubled up values.
	*/
	for(i = 0; i < 32 /*sizeof(SA_prom)*/; i+=2) {
		SA_prom[i] = inb(ioaddr + NE_DATAPORT);
		SA_prom[i+1] = inb(ioaddr + NE_DATAPORT);
		if (SA_prom[i] != SA_prom[i+1])
			sa_prom_doubled = 0;
	}

	if (sa_prom_doubled)
		for (i = 0; i < 16; i++)
			SA_prom[i] = SA_prom[i+i];
#else
	for(i = 0; i < 32 /*sizeof(SA_prom)*/; i++)
		SA_prom[i] = inb(ioaddr + NE_DATAPORT);

#endif

	/* We always set the 8390 registers for word mode. */
	outb(0x49, ioaddr + EN0_DCFG);
	start_page = NESM_START_PG;
	stop_page = NESM_STOP_PG;

	/* Set up the rest of the parameters. */
	name = "PCI NE2000";

	dev = init_etherdev(dev, 0);

	dev->irq = irq;
	dev->base_addr = ioaddr;

	/* Allocate dev->priv and fill in 8390 specific dev fields. */
	if (ethdev_init(dev)) {
		printk ("%s: unable to get memory for dev->priv.\n", dev->name);
		kfree(dev);
		return 0;
	}

	request_region(ioaddr, NE_IO_EXTENT, dev->name);

	printk("%s: %s found at %#x, IRQ %d, ",
		   dev->name, name, ioaddr, dev->irq);
	for(i = 0; i < 6; i++) {
		printk("%2.2X%s", SA_prom[i], i == 5 ? ".\n": ":");
		dev->dev_addr[i] = SA_prom[i];
	}

	ei_status.name = name;
	ei_status.tx_start_page = start_page;
	ei_status.stop_page = stop_page;
	ei_status.word16 = 1;

	ei_status.rx_start_page = start_page + TX_PAGES;
#ifdef PACKETBUF_MEMSIZE
	/* Allow the packet buffer size to be overridden by know-it-alls. */
	ei_status.stop_page = ei_status.tx_start_page + PACKETBUF_MEMSIZE;
#endif

	ei_status.reset_8390 = &ne2k_pci_reset_8390;
	ei_status.block_input = &ne2k_pci_block_input;
	ei_status.block_output = &ne2k_pci_block_output;
	ei_status.get_8390_hdr = &ne2k_pci_get_8390_hdr;
	dev->open = &ne2k_pci_open;
	dev->stop = &ne2k_pci_close;
	NS8390_init(dev, 0);
	return dev;
}

static int
ne2k_pci_open(struct device *dev)
{
	if (request_irq(dev->irq, ei_interrupt, SA_SHIRQ, dev->name, dev))
		return -EAGAIN;
	ei_open(dev);
	MOD_INC_USE_COUNT;
	return 0;
}

static int
ne2k_pci_close(struct device *dev)
{
	ei_close(dev);
	free_irq(dev->irq, dev);
	MOD_DEC_USE_COUNT;
	return 0;
}

/* Hard reset the card.  This used to pause for the same period that a
   8390 reset command required, but that shouldn't be necessary. */
static void
ne2k_pci_reset_8390(struct device *dev)
{
	unsigned long reset_start_time = jiffies;

	if (debug > 1) printk("%s: Resetting the 8390 t=%ld...",
						  dev->name, jiffies);

	outb(inb(NE_BASE + NE_RESET), NE_BASE + NE_RESET);

	ei_status.txing = 0;
	ei_status.dmaing = 0;

	/* This check _should_not_ be necessary, omit eventually. */
	while ((inb(NE_BASE+EN0_ISR) & ENISR_RESET) == 0)
		if (jiffies - reset_start_time > 2) {
			printk("%s: ne2k_pci_reset_8390() did not complete.\n", dev->name);
			break;
		}
	outb(ENISR_RESET, NE_BASE + EN0_ISR);	/* Ack intr. */
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void
ne2k_pci_get_8390_hdr(struct device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{

	int nic_base = dev->base_addr;

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne2k_pci_get_8390_hdr "
			   "[DMAstat:%d][irqlock:%d][intr:%ld].\n",
			   dev->name, ei_status.dmaing, ei_status.irqlock,
			   dev->interrupt);
		return;
	}

	ei_status.dmaing |= 0x01;
	outb(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
	outb(sizeof(struct e8390_pkt_hdr), nic_base + EN0_RCNTLO);
	outb(0, nic_base + EN0_RCNTHI);
	outb(0, nic_base + EN0_RSARLO);		/* On page boundary */
	outb(ring_page, nic_base + EN0_RSARHI);
	outb(E8390_RREAD+E8390_START, nic_base + NE_CMD);

#if defined(USE_LONGIO)
	*(u32*)hdr = inl(NE_BASE + NE_DATAPORT);
#else
	insw(NE_BASE + NE_DATAPORT, hdr, sizeof(struct e8390_pkt_hdr)>>1);
#endif

	outb(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
}

/* Block input and output, similar to the Crynwr packet driver.  If you
   are porting to a new ethercard, look at the packet driver source for hints.
   The NEx000 doesn't share the on-board packet memory -- you have to put
   the packet out through the "remote DMA" dataport using outb. */

static void
ne2k_pci_block_input(struct device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	int nic_base = dev->base_addr;
	char *buf = skb->data;

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne2k_pci_block_input "
			   "[DMAstat:%d][irqlock:%d][intr:%ld].\n",
			   dev->name, ei_status.dmaing, ei_status.irqlock,
			   dev->interrupt);
		return;
	}
	ei_status.dmaing |= 0x01;
	outb(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
	outb(count & 0xff, nic_base + EN0_RCNTLO);
	outb(count >> 8, nic_base + EN0_RCNTHI);
	outb(ring_offset & 0xff, nic_base + EN0_RSARLO);
	outb(ring_offset >> 8, nic_base + EN0_RSARHI);
	outb(E8390_RREAD+E8390_START, nic_base + NE_CMD);

#if defined(USE_LONGIO)
	insl(NE_BASE + NE_DATAPORT, buf, count>>2);
	if (count & 3) {
		buf += count & ~3;
		if (count & 2)
			*((u16*)buf)++ = inw(NE_BASE + NE_DATAPORT);
		if (count & 1)
			*buf = inb(NE_BASE + NE_DATAPORT);
	}
#else
	insw(NE_BASE + NE_DATAPORT,buf,count>>1);
	if (count & 0x01) {
		buf[count-1] = inb(NE_BASE + NE_DATAPORT);
	}
#endif

	outb(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
}

static void
ne2k_pci_block_output(struct device *dev, int count,
		const unsigned char *buf, const int start_page)
{
	int nic_base = NE_BASE;
	unsigned long dma_start;

	/* On little-endian it's always safe to round the count up for
	   word writes. */
	if (count & 0x01)
		count++;

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne2k_pci_block_output."
			   "[DMAstat:%d][irqlock:%d][intr:%ld]\n",
			   dev->name, ei_status.dmaing, ei_status.irqlock,
			   dev->interrupt);
		return;
	}
	ei_status.dmaing |= 0x01;
	/* We should already be in page 0, but to be safe... */
	outb(E8390_PAGE0+E8390_START+E8390_NODMA, nic_base + NE_CMD);

#ifdef NE8390_RW_BUGFIX
	/* Handle the read-before-write bug the same way as the
	   Crynwr packet driver -- the NatSemi method doesn't work.
	   Actually this doesn't always work either, but if you have
	   problems with your NEx000 this is better than nothing! */
	outb(0x42, nic_base + EN0_RCNTLO);
	outb(0x00, nic_base + EN0_RCNTHI);
	outb(0x42, nic_base + EN0_RSARLO);
	outb(0x00, nic_base + EN0_RSARHI);
	outb(E8390_RREAD+E8390_START, nic_base + NE_CMD);
#endif
	outb(ENISR_RDC, nic_base + EN0_ISR);

   /* Now the normal output. */
	outb(count & 0xff, nic_base + EN0_RCNTLO);
	outb(count >> 8,   nic_base + EN0_RCNTHI);
	outb(0x00, nic_base + EN0_RSARLO);
	outb(start_page, nic_base + EN0_RSARHI);
	outb(E8390_RWRITE+E8390_START, nic_base + NE_CMD);
#if defined(USE_LONGIO)
	outsl(NE_BASE + NE_DATAPORT, buf, count>>2);
	if (count & 3) {
		buf += count & ~3;
		if (count & 2)
			outw(*((u16*)buf)++, NE_BASE + NE_DATAPORT);
	}
#else
	outsw(NE_BASE + NE_DATAPORT, buf, count>>1);
#endif

	dma_start = jiffies;

	while ((inb(nic_base + EN0_ISR) & ENISR_RDC) == 0)
		if (jiffies - dma_start > 2) { 			/* Avoid clock roll-over. */
			printk("%s: timeout waiting for Tx RDC.\n", dev->name);
			ne2k_pci_reset_8390(dev);
			NS8390_init(dev,1);
			break;
		}

	outb(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
	return;
}


/*
 * Local variables:
 *  compile-command: "gcc -DMODVERSIONS  -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer -I/usr/src/linux/drivers/net/ -c ne2k-pci.c"
 *  alt-compile-command: "gcc -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer -I/usr/src/linux/drivers/net/ -c ne2k-pci.c"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 *  version-control: t
 *  kept-new-versions: 5
 * End:
 */
