/* hp-plus.c: A HP PCLAN/plus ethernet driver for linux. */
/*
	Written 1994 by Donald Becker.

	This driver is for the Hewlett Packard PC LAN (27***) plus ethercards.
	These cards are sold under several model numbers, usually 2724*.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O

	Center of Excellence in Space Data and Information Sciences
		Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	As is often the case, a great deal of credit is owed to Russ Nelson.
	The Crynwr packet driver was my primary source of HP-specific
	programming information.
*/

static char *version =
"hp-plus.c:v1.10 9/24/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include <linux/string.h>		/* Important -- this inlines word moves. */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <asm/system.h>
#include <asm/io.h>


#include "8390.h"
extern struct device *init_etherdev(struct device *dev, int sizeof_private,
									unsigned long *mem_startp);

/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int hpplus_portlist[] =
{0x200, 0x240, 0x280, 0x2C0, 0x300, 0x320, 0x340, 0};

/*
   The HP EtherTwist chip implementation is a fairly routine DP8390
   implementation.  It allows both shared memory and programmed-I/O buffer
   access, using a custom interface for both.  The programmed-I/O mode is
   entirely implemented in the HP EtherTwist chip, bypassing the problem
   ridden built-in 8390 facilities used on NE2000 designs.  The shared
   memory mode is likewise special, with an offset register used to make
   packets appear at the shared memory base.  Both modes use a base and bounds
   page register to hide the Rx ring buffer wrap -- a packet that spans the
   end of physical buffer memory appears continuous to the driver. (c.f. the
   3c503 and Cabletron E2100)

   A special note: the internal buffer of the board is only 8 bits wide.
   This lays several nasty traps for the unaware:
   - the 8390 must be programmed for byte-wide operations
   - all I/O and memory operations must work on whole words (the access
     latches are serially preloaded and have no byte-swapping ability).

   This board is laid out in I/O space much like the earlier HP boards:
   the first 16 locations are for the board registers, and the second 16 are
   for the 8390.  The board is easy to identify, with both a dedicated 16 bit
   ID register and a constant 0x530* value in the upper bits of the paging
   register.
*/

#define HP_ID			0x00	/* ID register, always 0x4850. */
#define HP_PAGING		0x02	/* Registers visible @ 8-f, see PageName. */ 
#define HPP_OPTION		0x04	/* Bitmapped options, see HP_Option.	*/
#define HPP_OUT_ADDR	0x08	/* I/O output location in Perf_Page.	*/
#define HPP_IN_ADDR		0x0A	/* I/O input location in Perf_Page.		*/
#define HP_DATAPORT		0x0c	/* I/O data transfer in Perf_Page.		*/
#define NIC_OFFSET		0x10	/* Offset to the 8390 registers.		*/
#define HP_IO_EXTENT	32

#define HP_START_PG		0x00	/* First page of TX buffer */
#define HP_STOP_PG		0x80	/* Last page +1 of RX ring */

/* The register set selected in HP_PAGING. */
enum PageName {
	Perf_Page = 0,				/* Normal operation. */
	MAC_Page = 1,				/* The ethernet address (+checksum). */
	HW_Page = 2,				/* EEPROM-loaded hardware parameters. */
	LAN_Page = 4,				/* Transceiver selection, testing, etc. */
	ID_Page = 6 }; 

/* The bit definitions for the HPP_OPTION register. */
enum HP_Option {
	NICReset = 1, ChipReset = 2, 	/* Active low, really UNreset. */
	EnableIRQ = 4, FakeIntr = 8, BootROMEnb = 0x10, IOEnb = 0x20,
	MemEnable = 0x40, ZeroWait = 0x80, MemDisable = 0x1000, };

int hpplus_probe(struct device *dev);
int hpp_probe1(struct device *dev, int ioaddr);

static void hpp_reset_8390(struct device *dev);
static int hpp_open(struct device *dev);
static int hpp_close(struct device *dev);
static int hpp_mem_block_input(struct device *dev, int count,
						  char *buf, int ring_offset);
static void hpp_mem_block_output(struct device *dev, int count,
							const unsigned char *buf, const start_page);
static int hpp_io_block_input(struct device *dev, int count,
						  char *buf, int ring_offset);
static void hpp_io_block_output(struct device *dev, int count,
							const unsigned char *buf, const start_page);


/*	Probe a list of addresses for an HP LAN+ adaptor.
	This routine is almost boilerplate. */
#ifdef HAVE_DEVLIST
/* Support for a alternate probe manager, which will eliminate the
   boilerplate below. */
struct netdev_entry hpplus_drv =
{"hpplus", hpp_probe1, HP_IO_EXTENT, hpplus_portlist};
#else

int hp_plus_probe(struct device *dev)
{
	int i;
	int base_addr = dev ? dev->base_addr : 0;

	if (base_addr > 0x1ff)		/* Check a single specified location. */
		return hpp_probe1(dev, base_addr);
	else if (base_addr != 0)	/* Don't probe at all. */
		return ENXIO;

	for (i = 0; hpplus_portlist[i]; i++) {
		int ioaddr = hpplus_portlist[i];
		if (check_region(ioaddr, HP_IO_EXTENT))
			continue;
		if (hpp_probe1(dev, ioaddr) == 0)
			return 0;
	}

	return ENODEV;
}
#endif

/* Do the interesting part of the probe at a single address. */
int hpp_probe1(struct device *dev, int ioaddr)
{
	int i;
	unsigned char checksum = 0;
	char *name = "HP-PC-LAN+";
	int mem_start;

	/* Check for the HP+ signature, 50 48 0x 53. */
	if (inw(ioaddr + HP_ID) != 0x4850
		|| (inw(ioaddr + HP_PAGING) & 0xfff0) != 0x5300)
		return ENODEV;

    if (dev == NULL)
		dev = init_etherdev(0, sizeof(struct ei_device), 0);

	printk("%s: %s at %#3x,", dev->name, name, ioaddr);

	/* Retrieve and checksum the station address. */
	outw(MAC_Page, ioaddr + HP_PAGING);

	for(i = 0; i < ETHER_ADDR_LEN; i++) {
		unsigned char inval = inb(ioaddr + 8 + i);
		dev->dev_addr[i] = inval;
		checksum += inval;
		printk(" %2.2x", inval);
	}
	checksum += inb(ioaddr + 14);

	if (checksum != 0xff) {
		printk(" bad checksum %2.2x.\n", checksum);
		return ENODEV;
	} else {
		/* Point at the Software Configuration Flags. */
		outw(ID_Page, ioaddr + HP_PAGING);
		printk(" ID %4.4x", inw(ioaddr + 12));
	}

	/* Grab the region so we can find another board if something fails. */
	request_region(ioaddr, HP_IO_EXTENT,"hp-plus");

	/* Read the IRQ line. */
	outw(HW_Page, ioaddr + HP_PAGING);
	{
		int irq = inb(ioaddr + 13) & 0x0f;
		int option = inw(ioaddr + HPP_OPTION);

		dev->irq = irq;
		if (option & MemEnable) {
			mem_start = inw(ioaddr + 9) << 8;
			printk(", IRQ %d, memory address %#x.\n", irq, mem_start);
		} else {
			mem_start = 0;
			printk(", IRQ %d, programmed-I/O mode.\n", irq);
		}
	}

	printk( "%s%s", KERN_INFO, version);

	/* Set the wrap registers for string I/O reads.   */
	outw((HP_START_PG + TX_2X_PAGES) | ((HP_STOP_PG - 1) << 8), ioaddr + 14);

	/* Set the base address to point to the NIC, not the "real" base! */
	dev->base_addr = ioaddr + NIC_OFFSET;

	ethdev_init(dev);

    dev->open = &hpp_open;
    dev->stop = &hpp_close;

	ei_status.name = name;
	ei_status.word16 = 0;		/* Agggghhhhh! Debug time: 2 days! */
	ei_status.tx_start_page = HP_START_PG;
	ei_status.rx_start_page = HP_START_PG + TX_2X_PAGES;
	ei_status.stop_page = HP_STOP_PG;

	ei_status.reset_8390 = &hpp_reset_8390;
	ei_status.block_input = &hpp_io_block_input;
	ei_status.block_output = &hpp_io_block_output;

	/* Check if the memory_enable flag is set in the option register. */
	if (mem_start) {
		ei_status.block_input = &hpp_mem_block_input;
		ei_status.block_output = &hpp_mem_block_output;
		dev->mem_start = mem_start;
		dev->rmem_start = dev->mem_start + TX_2X_PAGES*256;
		dev->mem_end = dev->rmem_end
			= dev->mem_start + (HP_STOP_PG - HP_START_PG)*256;
	}

	outw(Perf_Page, ioaddr + HP_PAGING);
	NS8390_init(dev, 0);
	/* Leave the 8390 and HP chip reset. */
	outw(inw(ioaddr + HPP_OPTION) & ~EnableIRQ, ioaddr + HPP_OPTION);

	return 0;
}

static int
hpp_open(struct device *dev)
{
	int ioaddr = dev->base_addr - NIC_OFFSET;
	int option_reg;

	if (request_irq(dev->irq, &ei_interrupt, 0, "hp-plus")) {
	    return -EAGAIN;
	}

	/* Reset the 8390 and HP chip. */
	option_reg = inw(ioaddr + HPP_OPTION);
	outw(option_reg & ~(NICReset + ChipReset), ioaddr + HPP_OPTION);
	SLOW_DOWN_IO; SLOW_DOWN_IO;
	/* Unreset the board and enable interrupts. */
	outw(option_reg | (EnableIRQ + NICReset + ChipReset), ioaddr + HPP_OPTION);

	/* Set the wrap registers for programmed-I/O operation.   */
	outw(HW_Page, ioaddr + HP_PAGING);
	outw((HP_START_PG + TX_2X_PAGES) | ((HP_STOP_PG - 1) << 8), ioaddr + 14);

	/* Select the operational page. */
	outw(Perf_Page, ioaddr + HP_PAGING);

    return ei_open(dev);
}

static int
hpp_close(struct device *dev)
{
	int ioaddr = dev->base_addr - NIC_OFFSET;
	int option_reg = inw(ioaddr + HPP_OPTION);

    free_irq(dev->irq);
    irq2dev_map[dev->irq] = NULL;
    NS8390_init(dev, 0);
	outw((option_reg & ~EnableIRQ) | MemDisable | NICReset | ChipReset,
		 ioaddr + HPP_OPTION);

    return 0;
}

static void
hpp_reset_8390(struct device *dev)
{
	int ioaddr = dev->base_addr - NIC_OFFSET;
	int option_reg = inw(ioaddr + HPP_OPTION);

	if (ei_debug > 1) printk("resetting the 8390 time=%ld...", jiffies);

	outw(option_reg & ~(NICReset + ChipReset), ioaddr + HPP_OPTION);
	/* Pause a few cycles for the hardware reset to take place. */
	SLOW_DOWN_IO;
	SLOW_DOWN_IO;
	ei_status.txing = 0;
	outw(option_reg | (EnableIRQ + NICReset + ChipReset), ioaddr + HPP_OPTION);

	SLOW_DOWN_IO; SLOW_DOWN_IO;


	if ((inb_p(ioaddr+NIC_OFFSET+EN0_ISR) & ENISR_RESET) == 0)
		printk("%s: hp_reset_8390() did not complete.\n", dev->name);

	if (ei_debug > 1) printk("8390 reset done (%ld).", jiffies);
	return;
}

/* Block input and output, similar to the Crynwr packet driver.
   Note that transfer with the EtherTwist+ must be on word boundaries. */

static int
hpp_io_block_input(struct device *dev, int count, char *buf, int ring_offset)
{
	int ioaddr = dev->base_addr - NIC_OFFSET;

	outw(ring_offset, ioaddr + HPP_IN_ADDR);
	insw(ioaddr + HP_DATAPORT, buf, count>>1);
	if (count & 0x01)
        buf[count-1] = inw(ioaddr + HP_DATAPORT);
	return ring_offset + count;
}

static int
hpp_mem_block_input(struct device *dev, int count, char *buf, int ring_offset)
{
	int ioaddr = dev->base_addr - NIC_OFFSET;
	int option_reg = inw(ioaddr + HPP_OPTION);

	outw(ring_offset, ioaddr + HPP_IN_ADDR);

	outw(option_reg & ~(MemDisable + BootROMEnb), ioaddr + HPP_OPTION);
	/* Caution: this relies on 8390.c rounding up allocations! */
	memcpy(buf, (char*)dev->mem_start,  (count + 3) & ~3);
	outw(option_reg, ioaddr + HPP_OPTION);

	return ring_offset + count;
}

/* A special note: we *must* always transfer >=16 bit words.
   It's always safe to round up, so we do. */
static void
hpp_io_block_output(struct device *dev, int count,
					const unsigned char *buf, const start_page)
{
	int ioaddr = dev->base_addr - NIC_OFFSET;
	outw(start_page << 8, ioaddr + HPP_OUT_ADDR);
	outsl(ioaddr + HP_DATAPORT, buf, (count+3)>>2);
	return;
}

static void
hpp_mem_block_output(struct device *dev, int count,
				const unsigned char *buf, const start_page)
{
	int ioaddr = dev->base_addr - NIC_OFFSET;
	int option_reg = inw(ioaddr + HPP_OPTION);

	outw(start_page << 8, ioaddr + HPP_OUT_ADDR);
	outw(option_reg & ~(MemDisable + BootROMEnb), ioaddr + HPP_OPTION);
	memcpy((char *)dev->mem_start, buf, (count + 3) & ~3);
	outw(option_reg, ioaddr + HPP_OPTION);

	return;
}


/*
 * Local variables:
 * compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c hp-plus.c"
 * version-control: t
 * kept-new-versions: 5
 * tab-width: 4
 * c-indent-level: 4
 * End:
 */
