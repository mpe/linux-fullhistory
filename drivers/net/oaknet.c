/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: oaknet.c
 *
 *    Description:
 *      Driver for the National Semiconductor DP83902AV Ethernet controller
 *      on-board the IBM PowerPC "Oak" evaluation board. Adapted from the
 *      various other 8390 drivers written by Donald Becker and Paul Gortmaker.
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <asm/board.h>
#include <asm/io.h>

#include "8390.h"


/* Preprocessor Defines */

#if !defined(TRUE) || TRUE != 1
#define	TRUE	1
#endif

#if !defined(FALSE) || FALSE != 0
#define	FALSE	0
#endif

#define	OAKNET_CMD     		0x00
#define OAKNET_DATA		0x10	/* NS-defined port window offset. */
#define OAKNET_RESET		0x1f	/* A read resets, a write clears. */

#define	OAKNET_START_PG		0x20	/* First page of TX buffer */
#define	OAKNET_STOP_PG		0x40	/* Last pagge +1 of RX ring */

#define	OAKNET_BASE		(dev->base_addr)

#define	OAKNET_WAIT		(2 * HZ / 100)	/* 20 ms */


/* Global Variables */

#if defined(MODULE)
static struct net_device *oaknet_devs;
#endif


/* Function Prototypes */

static int	 oaknet_open(struct net_device *dev);
static int	 oaknet_close(struct net_device *dev);

static void	 oaknet_reset_8390(struct net_device *dev);
static void	 oaknet_get_8390_hdr(struct net_device *dev,
				     struct e8390_pkt_hdr *hdr, int ring_page);
static void	 oaknet_block_input(struct net_device *dev, int count,
				    struct sk_buff *skb, int ring_offset);
static void	 oaknet_block_output(struct net_device *dev, int count,
				     const unsigned char *buf, int start_page);

static void	 oaknet_dma_error(struct net_device *dev, const char *name);


/*
 * int oaknet_init()
 *
 * Description:
 *   This routine performs all the necessary platform-specific initiali-
 *   zation and set-up for the IBM "Oak" evaluation board's National
 *   Semiconductor DP83902AV "ST-NIC" Ethernet controller.
 *
 * Input(s):
 *   N/A
 *
 * Output(s):
 *   N/A
 *
 * Returns:
 *   0 if OK, otherwise system error number on error.
 *
 */
int
oaknet_init(void)
{
	register int i;
	int reg0, regd;
	struct net_device *dev = NULL;
	unsigned long ioaddr = OAKNET_IO_BASE;
	const char *name = "National DP83902AV";
	bd_t *bip = (bd_t *)__res;

	/* Quick register check to see if the device is really there. */

	if ((reg0 = inb_p(ioaddr)) == 0xFF)
		return (ENODEV);

	/*
	 * That worked. Now a more thorough check, using the multicast
	 * address registers, that the device is definitely out there
	 * and semi-functional.
	 */

	outb_p(E8390_NODMA + E8390_PAGE1 + E8390_STOP, ioaddr + E8390_CMD);
	regd = inb_p(ioaddr + 0x0D);
	outb_p(0xFF, ioaddr + 0x0D);
	outb_p(E8390_NODMA + E8390_PAGE0, ioaddr + E8390_CMD);
	inb_p(ioaddr + EN0_COUNTER0);

	/* It's no good. Fix things back up and leave. */

	if (inb_p(ioaddr + EN0_COUNTER0) != 0) {
		outb_p(reg0, ioaddr);
		outb_p(regd, ioaddr + 0x0D);
		dev->base_addr = 0;

		return (ENODEV);
	}

	/*
	 * We're dependent on the 8390 generic driver module, make
	 * sure its symbols are loaded.
	 */

	if (load_8390_module("oaknet.c"))
		return (-ENOSYS);

	/*
	 * We're not using the old-style probing API, so we have to allocate
	 * our own device structure.
	 */

	dev = init_etherdev(0, 0);
#if defined(MODULE)
	oaknet_devs = dev;
#endif

	/*
	 * This controller is on an embedded board, so the base address
	 * and interrupt assignments are pre-assigned and unchageable.
	 */

	dev->base_addr = OAKNET_IO_BASE;
	dev->irq = OAKNET_INT;

	/* Allocate 8390-specific device-private area and fields. */

	if (ethdev_init(dev)) {
		printk(" unable to get memory for dev->priv.\n");
		return (-ENOMEM);
	}

	/*
	 * Just to be safe, reset the card as we cannot really* be sure
	 * what state it was last left in.
	 */

	oaknet_reset_8390(dev);

	/*
	 * Disable all chip interrupts for now and ACK all pending
	 * interrupts.
	 */

	outb_p(0x0, ioaddr + EN0_IMR);
	outb_p(0xFF, ioaddr + EN0_ISR);

	/* Attempt to get the interrupt line */

	if (request_irq(dev->irq, ei_interrupt, 0, name, dev)) {
		printk("%s: unable to request interrupt %d.\n",
		       dev->name, dev->irq);
		kfree(dev->priv);
		dev->priv = NULL;
		return (EAGAIN);
	}

	request_region(dev->base_addr, OAKNET_IO_SIZE, name);

	/* Tell the world about what and where we've found. */

	printk("%s: %s at", dev->name, name);
	for (i = 0; i < ETHER_ADDR_LEN; ++i) {
		dev->dev_addr[i] = bip->bi_enetaddr[i];
		printk("%c%.2x", (i ? ':' : ' '), dev->dev_addr[i]);
	}
	printk(", found at %#lx, using IRQ %d.\n", dev->base_addr, dev->irq);

	/* Set up some required driver fields and then we're done. */

	ei_status.name		= name;
	ei_status.word16	= FALSE;
	ei_status.tx_start_page	= OAKNET_START_PG;
	ei_status.rx_start_page = OAKNET_START_PG + TX_PAGES;
	ei_status.stop_page	= OAKNET_STOP_PG;

	ei_status.reset_8390	= &oaknet_reset_8390;
	ei_status.block_input	= &oaknet_block_input;
	ei_status.block_output	= &oaknet_block_output;
	ei_status.get_8390_hdr	= &oaknet_get_8390_hdr;

	dev->open = oaknet_open;
	dev->stop = oaknet_close;

	NS8390_init(dev, FALSE);

	return (0);
}

/*
 * static int oaknet_open()
 *
 * Description:
 *   This routine is a modest wrapper around ei_open, the 8390-generic,
 *   driver open routine. This just increments the module usage count
 *   and passes along the status from ei_open.
 *
 * Input(s):
 *  *dev - Pointer to the device structure for this driver.
 *
 * Output(s):
 *  *dev - Pointer to the device structure for this driver, potentially
 *         modified by ei_open.
 *
 * Returns:
 *   0 if OK, otherwise < 0 on error.
 *
 */
static int
oaknet_open(struct net_device *dev)
{
	int status = ei_open(dev);
	MOD_INC_USE_COUNT;
	return (status);
}

/*
 * static int oaknet_close()
 *
 * Description:
 *   This routine is a modest wrapper around ei_close, the 8390-generic,
 *   driver close routine. This just decrements the module usage count
 *   and passes along the status from ei_close.
 *
 * Input(s):
 *  *dev - Pointer to the device structure for this driver.
 *
 * Output(s):
 *  *dev - Pointer to the device structure for this driver, potentially
 *         modified by ei_close.
 *
 * Returns:
 *   0 if OK, otherwise < 0 on error.
 *
 */
static int
oaknet_close(struct net_device *dev)
{
	int status = ei_close(dev);
	MOD_DEC_USE_COUNT;
	return (status);
}

/*
 * static void oaknet_reset_8390()
 *
 * Description:
 *   This routine resets the DP83902 chip.
 *
 * Input(s):
 *  *dev - 
 *
 * Output(s):
 *   N/A
 *
 * Returns:
 *   N/A
 *
 */
static void
oaknet_reset_8390(struct net_device *dev)
{
	int base = OAKNET_BASE;
	unsigned long start = jiffies;

	outb(inb(base + OAKNET_RESET), base + OAKNET_RESET);

	ei_status.txing = 0;
	ei_status.dmaing = 0;

	/* This check shouldn't be necessary eventually */

	while ((inb_p(base + EN0_ISR) & ENISR_RESET) == 0) {
		if (jiffies - start > OAKNET_WAIT) {
			printk("%s: reset didn't complete\n", dev->name);
			break;
		}
	}

	outb_p(ENISR_RESET, base + EN0_ISR);	/* ACK reset interrupt */

	return;
}

/*
 * XXX - Document me.
 */
static void
oaknet_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
		    int ring_page)
{
	int base = OAKNET_BASE;

	/*
	 * This should NOT happen. If it does, it is the LAST thing you'll
	 * see.
	 */

	if (ei_status.dmaing) {
		oaknet_dma_error(dev, "oaknet_get_8390_hdr");
		return;
	}

	ei_status.dmaing |= 0x01;
	outb_p(E8390_NODMA + E8390_PAGE0 + E8390_START, base + OAKNET_CMD);
	outb_p(sizeof(struct e8390_pkt_hdr), base + EN0_RCNTLO);
	outb_p(0, base + EN0_RCNTHI);
	outb_p(0, base + EN0_RSARLO);		/* On page boundary */
	outb_p(ring_page, base + EN0_RSARHI);
	outb_p(E8390_RREAD + E8390_START, base + OAKNET_CMD);

	if (ei_status.word16)
		insw(base + OAKNET_DATA, hdr,
		     sizeof(struct e8390_pkt_hdr) >> 1);
	else
		insb(base + OAKNET_DATA, hdr,
		     sizeof(struct e8390_pkt_hdr));

	outb_p(ENISR_RDC, base + EN0_ISR);	/* ACK Remote DMA interrupt */
	ei_status.dmaing &= ~0x01;

	return;
}

/*
 * XXX - Document me.
 */
static void
oaknet_block_input(struct net_device *dev, int count, struct sk_buff *skb,
		   int ring_offset)
{
	int base = OAKNET_BASE;
	char *buf = skb->data;

	/*
	 * This should NOT happen. If it does, it is the LAST thing you'll
	 * see.
	 */

	if (ei_status.dmaing) {
		oaknet_dma_error(dev, "oaknet_block_input");
		return;
	}

	ei_status.dmaing |= 0x01;
	outb_p(E8390_NODMA + E8390_PAGE0 + E8390_START, base + OAKNET_CMD);
	outb_p(count & 0xff, base + EN0_RCNTLO);
	outb_p(count >> 8, base + EN0_RCNTHI);
	outb_p(ring_offset & 0xff, base + EN0_RSARLO);
	outb_p(ring_offset >> 8, base + EN0_RSARHI);
	outb_p(E8390_RREAD + E8390_START, base + OAKNET_CMD);
	if (ei_status.word16) {
		insw(base + OAKNET_DATA, buf, count >> 1);
		if (count & 0x01) {
			buf[count-1] = inb(base + OAKNET_DATA);
		}
	} else {
		insb(base + OAKNET_DATA, buf, count);
	}
	outb_p(ENISR_RDC, base + EN0_ISR);	/* ACK Remote DMA interrupt */
	ei_status.dmaing &= ~0x01;

	return;
}

/*
 * XXX - Document me.
 */
static void
oaknet_block_output(struct net_device *dev, int count,
		    const unsigned char *buf, int start_page)
{
	int base = OAKNET_BASE;
	int bug;
	unsigned long start;
	unsigned char lobyte;

	/* Round the count up for word writes. */

	if (ei_status.word16 && (count & 0x1))
		count++;

	/*
	 * This should NOT happen. If it does, it is the LAST thing you'll
	 * see.
	 */

	if (ei_status.dmaing) {
		oaknet_dma_error(dev, "oaknet_block_output");
		return;
	}

	ei_status.dmaing |= 0x01;

	/* Make sure we are in page 0. */

	outb_p(E8390_PAGE0 + E8390_START + E8390_NODMA, base + OAKNET_CMD);

	/*
	 * The 83902 documentation states that the processor needs to
	 * do a "dummy read" before doing the remote write to work
	 * around a chip bug they don't feel like fixing.
	 */

	bug = 0;
	while (1) {
		unsigned int rdhi;
		unsigned int rdlo;

		/* Now the normal output. */
		outb_p(ENISR_RDC, base + EN0_ISR);
		outb_p(count & 0xff, base + EN0_RCNTLO);
		outb_p(count >> 8,   base + EN0_RCNTHI);
		outb_p(0x00, base + EN0_RSARLO);
		outb_p(start_page, base + EN0_RSARHI);

		if (bug++)
			break;

		/* Perform the dummy read */
		rdhi = inb_p(base + EN0_CRDAHI);
		rdlo = inb_p(base + EN0_CRDALO);
		outb_p(E8390_RREAD + E8390_START, base + OAKNET_CMD);

		while (1) {
			unsigned int nrdhi;
			unsigned int nrdlo;
			nrdhi = inb_p(base + EN0_CRDAHI);
			nrdlo = inb_p(base + EN0_CRDALO);
			if ((rdhi != nrdhi) || (rdlo != nrdlo))
				break;
		}
	}

	outb_p(E8390_RWRITE+E8390_START, base + OAKNET_CMD);
	if (ei_status.word16) {
		outsw(OAKNET_BASE + OAKNET_DATA, buf, count >> 1);
	} else {
		outsb(OAKNET_BASE + OAKNET_DATA, buf, count);
	}

	start = jiffies;

	while (((lobyte = inb_p(base + EN0_ISR)) & ENISR_RDC) == 0) {
		if (jiffies - start > OAKNET_WAIT) {
			unsigned char hicnt, locnt;
			hicnt = inb_p(base + EN0_CRDAHI);
			locnt = inb_p(base + EN0_CRDALO);
			printk("%s: timeout waiting for Tx RDC, stat = 0x%x\n",
			       dev->name, lobyte);
			printk("\tstart address 0x%x, current address 0x%x, count %d\n",
			       (start_page << 8), (hicnt << 8) | locnt, count);
			oaknet_reset_8390(dev);
			NS8390_init(dev, TRUE);
			break;
		}
	}
	
	outb_p(ENISR_RDC, base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;

	return;
}

static void
oaknet_dma_error(struct net_device *dev, const char *name)
{
	printk(KERN_EMERG "%s: DMAing conflict in %s."
	       "[DMAstat:%d][irqlock:%d][intr:%ld]\n",
	       dev->name, name, ei_status.dmaing, ei_status.irqlock,
	       dev->interrupt);

	return;
}

#if defined(MODULE)
/*
 * Oak Ethernet module load interface.
 */
int
init_module(void)
{
	int status;

	if (oaknet_devs != NULL)
		return (-EBUSY);

	status = oaknet_init()

	lock_8390_module();

	return (status);
}

/*
 * Oak Ethernet module unload interface.
 */
void
cleanup_module(void)
{
	if (oaknet_devs == NULL)
		return;

	if (oaknet_devs->priv != NULL) {
		int ioaddr = oaknet_devs->base_addr;
		void *priv = oaknet_devs->priv;
		free_irq(oaknet_devs->irq, oaknet_devs);
		release_region(ioaddr, OAKNET_IO_SIZE);
		unregister_netdev(oaknet_dev);
		kfree(priv);
	}

	oaknet_devs = NULL;

	unlock_8390_module();

	return;
}
#endif /* MODULE */
