/* ac3200.c: A driver for the Ansel Communications EISA ethernet adaptor. */
/*
	Written 1993, 1994 by Donald Becker.
	Copyright 1993 United States Government as represented by the Director,
	National Security Agency.  This software may only be used and distributed
	according to the terms of the GNU Public License as modified by SRC,
	incorporated herein by reference.

	The author may be reached as becker@cesdis.gsfc.nasa.gov, or
    C/O Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	This is driver for the Ansel Communications Model 3200 EISA Ethernet LAN
	Adapter.  The programming information is from the users manual, as related
	by glee@ardnassak.math.clemson.edu.
  */

static char *version =
	"ac3200.c:v1.01 7/1/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include "8390.h"

/* Offsets from the base address. */
#define AC_NIC_BASE		0x00
#define AC_SA_PROM		0x16			/* The station address PROM. */
#define  AC_ADDR0		 0x00			/* Prefix station address values. */
#define  AC_ADDR1		 0x40			/* !!!!These are just guesses!!!! */
#define  AC_ADDR2		 0x90
#define AC_ID_PORT		0xC80
#define AC_EISA_ID		 0x0110d305
#define AC_RESET_PORT	0xC84
#define  AC_RESET		 0x00
#define  AC_ENABLE		 0x01
#define AC_CONFIG		0xC90	/* The configuration port. */

/* Decoding of the configuration register. */
static unsigned char config2irqmap[8] = {15, 12, 11, 10, 9, 7, 5, 3};
static int addrmap[8] =
{0xFF0000, 0xFE0000, 0xFD0000, 0xFFF0000, 0xFFE0000, 0xFFC0000,  0xD0000, 0 };
static char *port_name[4] = { "10baseT", "invalid", "AUI", "10base2"};

#define config2irq(configval)	config2irqmap[((configval) >> 3) & 7]
#define config2mem(configval)	addrmap[(configval) & 7]
#define config2name(configval)	port_name[((configval) >> 6) & 3]

/* First and last 8390 pages. */
#define AC_START_PG		0x00	/* First page of 8390 TX buffer */
#define AC_STOP_PG		0x80	/* Last page +1 of the 8390 RX ring */

int ac3200_probe(struct device *dev);
static int ac_probe1(int ioaddr, struct device *dev);

static int ac_open(struct device *dev);
static void ac_reset_8390(struct device *dev);
static int ac_block_input(struct device *dev, int count,
						  char *buf, int ring_offset);
static void ac_block_output(struct device *dev, const int count,
							const unsigned char *buf, const int start_page);
static int ac_close_card(struct device *dev);


/*	Probe for the AC3200.

	The AC3200 can be identified by either the EISA configuration registers,
	or the unique value in the station address PROM.
	*/

int ac3200_probe(struct device *dev)
{
	unsigned short ioaddr = dev->base_addr;

	if (ioaddr > 0x1ff)		/* Check a single specified location. */
		return ac_probe1(ioaddr, dev);
	else if (ioaddr > 0)		/* Don't probe at all. */
		return ENXIO;

	/* If you have a pre-pl15 machine you should delete this line. */
	if ( ! EISA_bus)
		return ENXIO;

	for (ioaddr = 0x1000; ioaddr < 0x9000; ioaddr += 0x1000)
		if (ac_probe1(ioaddr, dev) == 0)
			return 0;

	return ENODEV;
}

static int ac_probe1(int ioaddr, struct device *dev)
{
	int i;

#ifndef final_version
	printk("AC3200 ethercard probe at %#3x:", ioaddr);

	for(i = 0; i < 6; i++)
		printk(" %02x", inb(ioaddr + AC_SA_PROM + i));
#endif

	/* !!!!The values of AC_ADDRn (see above) should be corrected when we
	   find out the correct station address prefix!!!! */
	if (inb(ioaddr + AC_SA_PROM + 0) != AC_ADDR0
		|| inb(ioaddr + AC_SA_PROM + 1) != AC_ADDR1
		|| inb(ioaddr + AC_SA_PROM + 2) != AC_ADDR2 ) {
#ifndef final_version
		printk(" not found (invalid prefix).\n");
#endif
		return ENODEV;
	}

	/* The correct probe method is to check the EISA ID. */
	for (i = 0; i < 4; i++)
		if (inl(ioaddr + AC_ID_PORT) != AC_EISA_ID) {
			printk("EISA ID mismatch, %8x vs %8x.\n",
				   inl(ioaddr + AC_EISA_ID), AC_EISA_ID); 
			return ENODEV;
		}

	for(i = 0; i < ETHER_ADDR_LEN; i++)
		dev->dev_addr[i] = inb(ioaddr + AC_SA_PROM + i);

#ifndef final_version
	printk("\nAC3200 ethercard configuration register is %#02x,"
		   " EISA ID %02x %02x %02x %02x.\n", inb(ioaddr + AC_CONFIG),
		   inb(ioaddr + AC_ID_PORT + 0), inb(ioaddr + AC_ID_PORT + 1),
		   inb(ioaddr + AC_ID_PORT + 2), inb(ioaddr + AC_ID_PORT + 3));
#endif

	/* Assign and snarf the interrupt now. */
	if (dev->irq == 0)
		dev->irq = config2irq(inb(ioaddr + AC_CONFIG));
	else if (dev->irq == 2)
		dev->irq = 9;

	if (request_irq(dev->irq, ei_interrupt, 0, "ac3200")) {
		printk (" unable to get IRQ %d.\n", dev->irq);
		return 0;
	}

	dev->base_addr = ioaddr;

#ifdef notyet
	if (dev->mem_start)	{		/* Override the value from the board. */
		for (i = 0; i < 7; i++)
			if (addrmap[i] == dev->mem_start)
				break;
		if (i >= 7)
			i = 0;
		outb((inb(ioaddr + AC_CONFIG) & ~7) | i, ioaddr + AC_CONFIG);
	}
#endif

	dev->if_port = inb(ioaddr + AC_CONFIG) >> 6;
	dev->mem_start = config2mem(inb(ioaddr + AC_CONFIG));
	dev->rmem_start = dev->mem_start + TX_PAGES*256;
	dev->mem_end = dev->rmem_end = dev->mem_start
		+ (AC_STOP_PG - AC_START_PG)*256;

	ethdev_init(dev);

	ei_status.name = "AC3200";
	ei_status.tx_start_page = AC_START_PG;
	ei_status.rx_start_page = AC_START_PG + TX_PAGES;
	ei_status.stop_page = AC_STOP_PG;
	ei_status.word16 = 1;

	printk("\n%s: AC3200 at %#x, IRQ %d, %s port, shared memory %#lx-%#lx.\n",
		   dev->name, ioaddr, dev->irq, port_name[dev->if_port],
		   dev->mem_start, dev->mem_end-1);

	if (ei_debug > 0)
		printk(version);

	ei_status.reset_8390 = &ac_reset_8390;
	ei_status.block_input = &ac_block_input;
	ei_status.block_output = &ac_block_output;

	dev->open = &ac_open;
	dev->stop = &ac_close_card;
	NS8390_init(dev, 0);
	return 0;
}

static int ac_open(struct device *dev)
{
#ifdef notyet
	/* Someday we may enable the IRQ and shared memory here. */
	int ioaddr = dev->base_addr;

	if (request_irq(dev->irq, ei_interrupt, 0, "ac3200"))
		return -EAGAIN;
#endif

	return ei_open(dev);
}

static void ac_reset_8390(struct device *dev)
{
	ushort ioaddr = dev->base_addr;

	outb(AC_RESET, ioaddr + AC_RESET_PORT);
	if (ei_debug > 1) printk("resetting AC3200, t=%ld...", jiffies);

	ei_status.txing = 0;
	outb(AC_ENABLE, ioaddr + AC_RESET_PORT);
	if (ei_debug > 1) printk("reset done\n");

	return;
}

/*  Block input and output are easy on shared memory ethercards, the only
	complication is when the ring buffer wraps. */

static int ac_block_input(struct device *dev, int count, char *buf,
						  int ring_offset)
{
	long xfer_start = dev->mem_start + ring_offset - (AC_START_PG<<8);

	if (xfer_start + count > dev->rmem_end) {
		/* We must wrap the input move. */
		int semi_count = dev->rmem_end - xfer_start;
		memcpy(buf, (char*)xfer_start, semi_count);
		count -= semi_count;
		memcpy(buf + semi_count, (char *)dev->rmem_start, count);
		return dev->rmem_start + count;
	}
	memcpy(buf, (char*)xfer_start, count);

	return ring_offset + count;
}

static void ac_block_output(struct device *dev, int count,
							const unsigned char *buf, int start_page)
{
	long shmem = dev->mem_start + ((start_page - AC_START_PG)<<8);

	memcpy((unsigned char *)shmem, buf, count);
}

static int ac_close_card(struct device *dev)
{
	dev->start = 0;
	dev->tbusy = 1;

	if (ei_debug > 1)
		printk("%s: Shutting down ethercard.\n", dev->name);

#ifdef notyet
	/* We should someday disable shared memory and interrupts. */
	outb(0x00, ioaddr + 6);	/* Disable interrupts. */
	free_irq(dev->irq);
	irq2dev_map[dev->irq] = 0;
#endif

	NS8390_init(dev, 0);

	return 0;
}


/*
 * Local variables:
 * compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c ac3200.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 4
 * End:
 */
