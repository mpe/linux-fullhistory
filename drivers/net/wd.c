/* wd.c: A WD80x3 ethernet driver for linux. */
/*
	Written 1993-94 by Donald Becker.

	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	This is a driver for WD8003 and WD8013 "compatible" ethercards.

	Thanks to Russ Nelson (nelson@crnwyr.com) for loaning me a WD8013.
*/

static char *version =
	"wd.c:v1.10 9/23/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <asm/io.h>
#include <asm/system.h>

#include <linux/netdevice.h>
#include "8390.h"
extern struct device *init_etherdev(struct device *dev, int sizeof_private,
									unsigned long *mem_startp);

/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int wd_portlist[] =
{0x300, 0x280, 0x380, 0x240, 0};

int wd_probe(struct device *dev);
int wd_probe1(struct device *dev, int ioaddr);

static int wd_open(struct device *dev);
static void wd_reset_8390(struct device *dev);
static int wd_block_input(struct device *dev, int count,
						  char *buf, int ring_offset);
static void wd_block_output(struct device *dev, int count,
							const unsigned char *buf, const start_page);
static int wd_close_card(struct device *dev);


#define WD_START_PG		0x00	/* First page of TX buffer */
#define WD03_STOP_PG	0x20	/* Last page +1 of RX ring */
#define WD13_STOP_PG	0x40	/* Last page +1 of RX ring */

#define WD_CMDREG		0		/* Offset to ASIC command register. */
#define	 WD_RESET		0x80	/* Board reset, in WD_CMDREG. */
#define	 WD_MEMENB		0x40	/* Enable the shared memory. */
#define WD_CMDREG5		5		/* Offset to 16-bit-only ASIC register 5. */
#define	 ISA16			0x80	/* Enable 16 bit access from the ISA bus. */
#define	 NIC16			0x40	/* Enable 16 bit access from the 8390. */
#define WD_NIC_OFFSET	16		/* Offset to the 8390 from the base_addr. */
#define WD_IO_EXTENT	32


/*	Probe for the WD8003 and WD8013.  These cards have the station
	address PROM at I/O ports <base>+8 to <base>+13, with a checksum
	following. A Soundblaster can have the same checksum as an WDethercard,
	so we have an extra exclusionary check for it.

	The wd_probe1() routine initializes the card and fills the
	station address field. */

#ifdef HAVE_DEVLIST
struct netdev_entry wd_drv =
{"wd", wd_probe1, WD_IO_EXTENT, wd_portlist};
#else

int wd_probe(struct device *dev)
{
	int i;
	int base_addr = dev ? dev->base_addr : 0;

	if (base_addr > 0x1ff)		/* Check a single specified location. */
		return wd_probe1(dev, base_addr);
	else if (base_addr != 0)	/* Don't probe at all. */
		return ENXIO;

	for (i = 0; wd_portlist[i]; i++) {
		int ioaddr = wd_portlist[i];
		if (check_region(ioaddr, WD_IO_EXTENT))
			continue;
		if (wd_probe1(dev, ioaddr) == 0)
			return 0;
	}

	return ENODEV;
}
#endif

int wd_probe1(struct device *dev, int ioaddr)
{
	int i;
	int checksum = 0;
	int ancient = 0;			/* An old card without config registers. */
	int word16 = 0;				/* 0 = 8 bit, 1 = 16 bit */
	char *model_name;

	for (i = 0; i < 8; i++)
		checksum += inb(ioaddr + 8 + i);
	if (inb(ioaddr + 8) == 0xff 	/* Extra check to avoid soundcard. */
		|| inb(ioaddr + 9) == 0xff
		|| (checksum & 0xff) != 0xFF)
		return ENODEV;

	if (dev == NULL)
		dev = init_etherdev(0, sizeof(struct ei_device), 0);

	printk("%s: WD80x3 at %#3x, ", dev->name, ioaddr);
	for (i = 0; i < 6; i++)
		printk(" %2.2X", dev->dev_addr[i] = inb(ioaddr + 8 + i));

	/* The following PureData probe code was contributed by
	   Mike Jagdis <jaggy@purplet.demon.co.uk>. Puredata does software
	   configuration differently from others so we have to check for them.
	   This detects an 8 bit, 16 bit or dumb (Toshiba, jumpered) card.
	   */
	if (inb(ioaddr+0) == 'P' && inb(ioaddr+1) == 'D') {
		unsigned char reg5 = inb(ioaddr+5);

		switch (inb(ioaddr+2)) {
		case 0x03: word16 = 0; model_name = "PDI8023-8";	break;
		case 0x05: word16 = 0; model_name = "PDUC8023";	break;
		case 0x0a: word16 = 1; model_name = "PDI8023-16"; break;
			/* Either 0x01 (dumb) or they've released a new version. */
		default:	 word16 = 0; model_name = "PDI8023";	break;
		}
		dev->mem_start = ((reg5 & 0x1c) + 0xc0) << 12;
		dev->irq = (reg5 & 0xe0) == 0xe0 ? 10 : (reg5 >> 5) + 1;
	} else {								/* End of PureData probe */
		/* This method of checking for a 16-bit board is borrowed from the
		   we.c driver.  A simpler method is just to look in ASIC reg. 0x03.
		   I'm comparing the two method in alpha test to make certain they
		   return the same result. */
		/* Check for the old 8 bit board - it has register 0/8 aliasing.
		   Do NOT check i>=6 here -- it hangs the old 8003 boards! */
		for (i = 0; i < 6; i++)
			if (inb(ioaddr+i) != inb(ioaddr+8+i))
				break;
		if (i >= 6) {
			ancient = 1;
			model_name = "WD8003-old";
			word16 = 0;
		} else {
			int tmp = inb(ioaddr+1); /* fiddle with 16bit bit */
			outb( tmp ^ 0x01, ioaddr+1 ); /* attempt to clear 16bit bit */
			if (((inb( ioaddr+1) & 0x01) == 0x01) /* A 16 bit card */
				&& (tmp & 0x01) == 0x01	) {				/* In a 16 slot. */
				int asic_reg5 = inb(ioaddr+WD_CMDREG5);
				/* Magic to set ASIC to word-wide mode. */
				outb( NIC16 | (asic_reg5&0x1f), ioaddr+WD_CMDREG5);
				outb(tmp, ioaddr+1);
				model_name = "WD8013";
				word16 = 1;		/* We have a 16bit board here! */
			} else {
				model_name = "WD8003";
				word16 = 0;
			}
			outb(tmp, ioaddr+1);			/* Restore original reg1 value. */
		}
#ifndef final_version
		if ( !ancient && (inb(ioaddr+1) & 0x01) != (word16 & 0x01))
			printk("\nWD80?3: Bus width conflict, %d (probe) != %d (reg report).",
				   word16 ? 16 : 8, (inb(ioaddr+1) & 0x01) ? 16 : 8);
#endif
	}

#if defined(WD_SHMEM) && WD_SHMEM > 0x80000
	/* Allow a compile-time override.	 */
	dev->mem_start = WD_SHMEM;
#else
	if (dev->mem_start == 0) {
		/* Sanity and old 8003 check */
		int reg0 = inb(ioaddr);
		if (reg0 == 0xff || reg0 == 0) {
			/* Future plan: this could check a few likely locations first. */
			dev->mem_start = 0xd0000;
			printk(" assigning address %#lx", dev->mem_start);
		} else {
			int high_addr_bits = inb(ioaddr+WD_CMDREG5) & 0x1f;
			/* Some boards don't have the register 5 -- it returns 0xff. */
			if (high_addr_bits == 0x1f || word16 == 0)
				high_addr_bits = 0x01;
			dev->mem_start = ((reg0&0x3f) << 13) + (high_addr_bits << 19);
		}
	}
#endif

	/* The 8390 isn't at the base address -- the ASIC regs are there! */
	dev->base_addr = ioaddr+WD_NIC_OFFSET;

	if (dev->irq < 2) {
		int irqmap[] = {9,3,5,7,10,11,15,4};
		int reg1 = inb(ioaddr+1);
		int reg4 = inb(ioaddr+4);
		if (ancient || reg1 == 0xff) {	/* Ack!! No way to read the IRQ! */
			short nic_addr = ioaddr+WD_NIC_OFFSET;

			/* We have an old-style ethercard that doesn't report its IRQ
			   line.  Do autoirq to find the IRQ line. Note that this IS NOT
			   a reliable way to trigger an interrupt. */
			outb_p(E8390_NODMA + E8390_STOP, nic_addr);
			outb(0x00, nic_addr+EN0_IMR);	/* Disable all intrs. */
			autoirq_setup(0);
			outb_p(0xff, nic_addr + EN0_IMR);	/* Enable all interrupts. */
			outb_p(0x00, nic_addr + EN0_RCNTLO);
			outb_p(0x00, nic_addr + EN0_RCNTHI);
			outb(E8390_RREAD+E8390_START, nic_addr); /* Trigger it... */
			dev->irq = autoirq_report(2);
			outb_p(0x00, nic_addr+EN0_IMR);	/* Mask all intrs. again. */

			if (ei_debug > 2)
				printk(" autoirq is %d", dev->irq);
			if (dev->irq < 2)
				dev->irq = word16 ? 10 : 5;
		} else
			dev->irq = irqmap[((reg4 >> 5) & 0x03) + (reg1 & 0x04)];
	} else if (dev->irq == 2)		/* Fixup bogosity: IRQ2 is really IRQ9 */
		dev->irq = 9;

	/* Snarf the interrupt now.  There's no point in waiting since we cannot
	   share and the board will usually be enabled. */
	if (request_irq(dev->irq, ei_interrupt, 0, "wd")) {
		printk (" unable to get IRQ %d.\n", dev->irq);
		return EAGAIN;
	}

	/* OK, were are certain this is going to work.  Setup the device. */
	request_region(ioaddr, WD_IO_EXTENT,"wd");
	ethdev_init(dev);

	ei_status.name = model_name;
	ei_status.word16 = word16;
	ei_status.tx_start_page = WD_START_PG;
	ei_status.rx_start_page = WD_START_PG + TX_PAGES;
	ei_status.stop_page = word16 ? WD13_STOP_PG : WD03_STOP_PG;

	/* Don't map in the shared memory until the board is actually opened. */
	dev->rmem_start = dev->mem_start + TX_PAGES*256;
	dev->mem_end = dev->rmem_end
		= dev->mem_start + (ei_status.stop_page - WD_START_PG)*256;

	printk(" %s, IRQ %d, shared memory at %#lx-%#lx.\n",
		   model_name, dev->irq, dev->mem_start, dev->mem_end-1);
	if (ei_debug > 0)
		printk(version);

	ei_status.reset_8390 = &wd_reset_8390;
	ei_status.block_input = &wd_block_input;
	ei_status.block_output = &wd_block_output;
	dev->open = &wd_open;
	dev->stop = &wd_close_card;
	NS8390_init(dev, 0);

#if 1
	/* Enable interrupt generation on softconfig cards -- M.U */
	/* .. but possibly potentially unsafe - Donald */
	if (inb(ioaddr+14) & 0x20)
		outb(inb(ioaddr+4)|0x80, ioaddr+4);
#endif

	return 0;
}

static int
wd_open(struct device *dev)
{
  int ioaddr = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */

  /* Map in the shared memory. Always set register 0 last to remain
	 compatible with very old boards. */
  ei_status.reg0 = ((dev->mem_start>>13) & 0x3f) | WD_MEMENB;
  ei_status.reg5 = ((dev->mem_start>>19) & 0x1f) | NIC16;

  if (ei_status.word16)
	  outb(ei_status.reg5, ioaddr+WD_CMDREG5);
  outb(ei_status.reg0, ioaddr); /* WD_CMDREG */

  return ei_open(dev);
}

static void
wd_reset_8390(struct device *dev)
{
	int wd_cmd_port = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */

	outb(WD_RESET, wd_cmd_port);
	if (ei_debug > 1) printk("resetting the WD80x3 t=%lu...", jiffies);
	ei_status.txing = 0;

	/* Set up the ASIC registers, just in case something changed them. */
	outb((((dev->mem_start>>13) & 0x3f)|WD_MEMENB), wd_cmd_port);
	if (ei_status.word16)
		outb(NIC16 | ((dev->mem_start>>19) & 0x1f), wd_cmd_port+WD_CMDREG5);

	if (ei_debug > 1) printk("reset done\n");
	return;
}

/* Block input and output are easy on shared memory ethercards, and trivial
   on the Western digital card where there is no choice of how to do it.
   The only complications are that the ring buffer wraps, and need to map
   switch between 8- and 16-bit modes. */

static int
wd_block_input(struct device *dev, int count, char *buf, int ring_offset)
{
	int wd_cmdreg = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */
	long xfer_start = dev->mem_start + ring_offset - (WD_START_PG<<8);

	/* We'll always get a 4 byte header read followed by a packet read, so
	   we enable 16 bit mode before the header, and disable after the body. */
	if (count == 4) {
		if (ei_status.word16)
			outb(ISA16 | ei_status.reg5, wd_cmdreg+WD_CMDREG5);
		((int*)buf)[0] = ((int*)xfer_start)[0];
		return 0;
	}

	if (xfer_start + count > dev->rmem_end) {
		/* We must wrap the input move. */
		int semi_count = dev->rmem_end - xfer_start;
		memcpy(buf, (char *)xfer_start, semi_count);
		count -= semi_count;
		memcpy(buf + semi_count, (char *)dev->rmem_start, count);
	} else
		memcpy(buf, (char *)xfer_start, count);

	/* Turn off 16 bit access so that reboot works.	 ISA brain-damage */
	if (ei_status.word16)
		outb(ei_status.reg5, wd_cmdreg+WD_CMDREG5);

	return 0;
}

static void
wd_block_output(struct device *dev, int count, const unsigned char *buf,
				int start_page)
{
	int wd_cmdreg = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */
	long shmem = dev->mem_start + ((start_page - WD_START_PG)<<8);


	if (ei_status.word16) {
		/* Turn on and off 16 bit access so that reboot works. */
		outb(ISA16 | ei_status.reg5, wd_cmdreg+WD_CMDREG5);
		memcpy((char *)shmem, buf, count);
		outb(ei_status.reg5, wd_cmdreg+WD_CMDREG5);
	} else
		memcpy((char *)shmem, buf, count);
}


static int
wd_close_card(struct device *dev)
{
	int wd_cmdreg = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */

	if (ei_debug > 1)
		printk("%s: Shutting down ethercard.\n", dev->name);
	NS8390_init(dev, 0);
	dev->start = 0;

	/* Change from 16-bit to 8-bit shared memory so reboot works. */
	outb(ei_status.reg5, wd_cmdreg + WD_CMDREG5 );

	/* And disable the shared memory. */
	outb(ei_status.reg0 & ~WD_MEMENB, wd_cmdreg);

	return 0;
}


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c wd.c"
 *  version-control: t
 *  tab-width: 4
 *  kept-new-versions: 5
 * End:
 */
