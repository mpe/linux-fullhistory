/* smc-ultra.c: A SMC Ultra ethernet driver for linux. */
/*
    Most of this driver, except for ultramca_probe is nearly
    verbatim from smc-ultra.c by Donald Becker. The rest is
    written and copyright 1996 by David Weis, weisd3458@uni.edu

    This is a driver for the SMC Ultra and SMC EtherEZ ethercards.

    This driver uses the cards in the 8390-compatible, shared memory mode.
    Most of the run-time complexity is handled by the generic code in
    8390.c.  The code in this file is responsible for

    This driver enables the shared memory only when doing the actual data
    transfers to avoid a bug in early version of the card that corrupted
    data transferred by a AHA1542.

    This driver does not support the programmed-I/O data transfer mode of
    the EtherEZ.  That support (if available) is smc-ez.c.  Nor does it
    use the non-8390-compatible "Altego" mode. (No support currently planned.)

    Changelog:

    Paul Gortmaker  : multiple card support for module users.
    David Weis      : Micro Channel-ized it.

*/


#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/system.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "8390.h"
#include "smc-mca.h"
#include <linux/mca.h>

int ultramca_probe(struct device *dev);

static int ultramca_open(struct device *dev);
static void ultramca_reset_8390(struct device *dev);
static void ultramca_get_8390_hdr(struct device *dev,
                                  struct e8390_pkt_hdr *hdr,
                                  int ring_page);
static void ultramca_block_input(struct device *dev, int count,
                                 struct sk_buff *skb,
                                 int ring_offset);
static void ultramca_block_output(struct device *dev, int count,
                                  const unsigned char *buf,
                                  const int start_page);
static int ultramca_close_card(struct device *dev);

#define START_PG        0x00    /* First page of TX buffer */

#define ULTRA_CMDREG 0      /* Offset to ASIC command register. */
#define ULTRA_RESET  0x80   /* Board reset, in ULTRA_CMDREG. */
#define ULTRA_MEMENB 0x40   /* Enable the shared memory. */
#define ULTRA_NIC_OFFSET 16 /* NIC register offset from the base_addr. */
#define ULTRA_IO_EXTENT 32
#define EN0_ERWCNT      0x08  /* Early receive warning count. */


__initfunc(int ultramca_probe(struct device *dev))
{
	unsigned short ioaddr;
	unsigned char reg4, num_pages;
	char slot;
	unsigned char pos2, pos3, pos4, pos5;
	int i;

	/* Look for two flavors of SMC Elite/A (3013EP/A) -jeh- */
	if(( (slot=mca_find_adapter(0x61c8,0)) != MCA_NOTFOUND) ||
            ((slot=mca_find_adapter(0xefd5,0)) != MCA_NOTFOUND) )

	{
#ifndef MODULE
		mca_set_adapter_name( slot, "SMC Elite/A (8013EP/A)" );
#endif
	}
	else if( (slot=mca_find_adapter(0x61c9,0)) != MCA_NOTFOUND)
	{
#ifndef MODULE
		mca_set_adapter_name( slot, "SMC Elite10T/A (8013WP/A)" );
#endif
	}
	else
		return -ENODEV;

	pos2 = mca_read_stored_pos(slot, 2);     /* IO range */
	pos3 = mca_read_stored_pos(slot, 3);     /* shared mem */
	pos4 = mca_read_stored_pos(slot, 4);     /* bios base */
	pos5 = mca_read_stored_pos(slot, 5);     /* irq and media */

	dev->base_addr = ioaddr = addr_table[pos2 >> 4].base_addr;
	dev->irq = irq_table[(pos5 & ~IRQ_MASK) >> 2].irq;

	dev->mem_start = 0;
	num_pages = 40;
	for (i = 0; i < 15; i++)
	{
		if (mem_table[i].mem_index == (pos3 & ~MEM_MASK))
		{
			dev->mem_start = mem_table[i].mem_start;
			num_pages = mem_table[i].num_pages;
		}
	}

	if (dev->mem_start == 0)      /* sanity check, shouldn't happen */
		return -ENODEV;

	reg4 = inb(ioaddr + 4) & 0x7f;
	outb(reg4, ioaddr + 4);

	if (load_8390_module("wd.c"))
		return -ENOSYS;

	printk("%s: SMC Ultra MCA at %#3x,", dev->name, ioaddr);

	for (i = 0; i < 6; i++)
		printk(" %2.2X", dev->dev_addr[i] = inb(ioaddr + 8 + i));

	/*
	 *	Switch from the station address to the alternate register set and
	 *	read the useful registers there.
	 */

	outb(0x80 | reg4, ioaddr + 4);

	/*
	 *	Enable FINE16 mode to avoid BIOS ROM width mismatches @ reboot.
	 */

	outb(0x80 | inb(ioaddr + 0x0c), ioaddr + 0x0c);

	/*
	 *	Switch back to the station address register set so that the MS-DOS driver
	 *	can find the card after a warm boot.
	 */

	outb(reg4, ioaddr + 4);

	/*
	 *	Allocate dev->priv and fill in 8390 specific dev fields.
	 */

	if (ethdev_init(dev))
	{
		printk (", no memory for dev->priv.\n");
		return -ENOMEM;
	}

	/*
	 *	OK, we are certain this is going to work.  Setup the device.
	 */

	request_region(ioaddr, ULTRA_IO_EXTENT, "smc-mca");

	/*
	 *	The 8390 isn't at the base address, so fake the offset
	 */

	dev->base_addr = ioaddr+ULTRA_NIC_OFFSET;

	ei_status.name = "SMC Ultra MCA";
	ei_status.word16 = 1;
	ei_status.tx_start_page = START_PG;
	ei_status.rx_start_page = START_PG + TX_PAGES;
	ei_status.stop_page = num_pages;

	dev->rmem_start = dev->mem_start + TX_PAGES*256;
	dev->mem_end = dev->rmem_end =
	dev->mem_start + (ei_status.stop_page - START_PG)*256;

	printk(", IRQ %d memory %#lx-%#lx.\n", dev->irq, dev->mem_start, dev->mem_end-1);

	ei_status.reset_8390 = &ultramca_reset_8390;
	ei_status.block_input = &ultramca_block_input;
	ei_status.block_output = &ultramca_block_output;
	ei_status.get_8390_hdr = &ultramca_get_8390_hdr;
	dev->open = &ultramca_open;
	dev->stop = &ultramca_close_card;
	NS8390_init(dev, 0);

	return 0;
}

static int ultramca_open(struct device *dev)
{
	int ioaddr = dev->base_addr - ULTRA_NIC_OFFSET; /* ASIC addr */

	if (request_irq(dev->irq, ei_interrupt, 0, ei_status.name, dev))
	return -EAGAIN;

	outb(ULTRA_MEMENB, ioaddr); /* Enable memory */
	outb(0x80, ioaddr + 5);     /* ??? */
	outb(0x01, ioaddr + 6);     /* Enable interrupts and memory. */
	outb(0x04, ioaddr + 5);     /* ??? */

	/*
	 *	Set the early receive warning level in window 0 high enough not
	 *	to receive ERW interrupts.
	 */

	/*
	 *	outb_p(E8390_NODMA+E8390_PAGE0, dev->base_addr);
	 *	outb(0xff, dev->base_addr + EN0_ERWCNT);
	 */

	ei_open(dev);
	MOD_INC_USE_COUNT;
	return 0;
}

static void ultramca_reset_8390(struct device *dev)
{
	int ioaddr = dev->base_addr - ULTRA_NIC_OFFSET; /* ASIC addr */

	outb(ULTRA_RESET, ioaddr);
	if (ei_debug > 1) printk("resetting Ultra, t=%ld...", jiffies);
	ei_status.txing = 0;

	outb(0x80, ioaddr + 5);     /* ??? */
	outb(0x01, ioaddr + 6);     /* Enable interrupts and memory. */

	if (ei_debug > 1)
		printk("reset done\n");
	return;
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void ultramca_get_8390_hdr(struct device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	unsigned long hdr_start = dev->mem_start + ((ring_page - START_PG)<<8);

#ifdef notdef
	/* Officially this is what we are doing, but the readl() is faster */
	memcpy_fromio(hdr, hdr_start, sizeof(struct e8390_pkt_hdr));
#else
	((unsigned int*)hdr)[0] = readl(hdr_start);
#endif
}

/* Block input and output are easy on shared memory ethercards, the only
   complication is when the ring buffer wraps. */

static void ultramca_block_input(struct device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	unsigned long xfer_start = dev->mem_start + ring_offset - (START_PG<<8);

	if (xfer_start + count > dev->rmem_end)
	{
        	/* We must wrap the input move. */
		int semi_count = dev->rmem_end - xfer_start;
		memcpy_fromio(skb->data, xfer_start, semi_count);
		count -= semi_count;
		memcpy_fromio(skb->data + semi_count, dev->rmem_start, count);
	}
	else
	{
		/* Packet is in one chunk -- we can copy + cksum. */
		eth_io_copy_and_sum(skb, xfer_start, count, 0);
	}

}

static void ultramca_block_output(struct device *dev, int count, const unsigned char *buf,
                int start_page)
{
	unsigned long shmem = dev->mem_start + ((start_page - START_PG)<<8);

	memcpy_toio(shmem, buf, count);
}

static int ultramca_close_card(struct device *dev)
{
	int ioaddr = dev->base_addr - ULTRA_NIC_OFFSET; /* ASIC addr */

	dev->start = 0;
	dev->tbusy = 1;

	if (ei_debug > 1)
		printk("%s: Shutting down ethercard.\n", dev->name);

	outb(0x00, ioaddr + 6);     /* Disable interrupts. */
	free_irq(dev->irq, dev);

	NS8390_init(dev, 0);
	/* We should someday disable shared memory and change to 8-bit mode
       "just in case"... */

	MOD_DEC_USE_COUNT;

	return 0;
}


#ifdef MODULE
#undef MODULE        /* don't want to bother now! */

#define MAX_ULTRAMCA_CARDS  4   /* Max number of Ultra cards per module */
#define NAMELEN     8   /* # of chars for storing dev->name */
static char namelist[NAMELEN * MAX_ULTRAMCA_CARDS] = { 0, };

static struct device dev_ultra[MAX_ULTRAMCA_CARDS] =
{
	{
		NULL,       /* assign a chunk of namelist[] below */
	        0, 0, 0, 0,
	        0, 0,
	        0, 0, 0, NULL, NULL
	},
};

static int io[MAX_ULTRAMCA_CARDS] = { 0, };
static int irq[MAX_ULTRAMCA_CARDS]  = { 0, };

MODULE_PARM(io, "1-" __MODULE_STRING(MAX_ULTRAMCA_CARDS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(MAX_ULTRAMCA_CARDS) "i");

/* This is set up so that only a single autoprobe takes place per call.
ISA device autoprobes on a running machine are not recommended. */

int init_module(void)
{
	int this_dev, found = 0;

	for (this_dev = 0; this_dev < MAX_ULTRAMCA_CARDS; this_dev++)
	{
		struct device *dev = &dev_ultra[this_dev];
		dev->name = namelist+(NAMELEN*this_dev);
		dev->irq = irq[this_dev];
		dev->base_addr = io[this_dev];
		dev->init = ultramca_probe;
		if (io[this_dev] == 0)
		{
			if (this_dev != 0)
				break; /* only autoprobe 1st one */
			printk(KERN_NOTICE "smc-mca.c: Presently autoprobing (not recommended) for a single card.\n");
		}
		if (register_netdev(dev) != 0)
		{
			printk(KERN_WARNING "smc-mca.c: No SMC Ultra card found (i/o = 0x%x).\n", io[this_dev]);
			if (found != 0) {	/* Got at least one. */
				lock_8390_module();
				return 0;
			}
			return -ENXIO;
		}
		found++;
	}
	lock_8390_module();
	return 0;
}

void cleanup_module(void)
{
	int this_dev;

	for (this_dev = 0; this_dev < MAX_ULTRAMCA_CARDS; this_dev++)
	{
		struct device *dev = &dev_ultra[this_dev];
        	if (dev->priv != NULL)
        	{
			void *priv = dev->priv;
			/* NB: ultra_close_card() does free_irq */
			int ioaddr = dev->base_addr - ULTRA_NIC_OFFSET;
			release_region(ioaddr, ULTRA_IO_EXTENT);
			unregister_netdev(dev);
			kfree(priv);
		}
	}
	unlock_8390_module();
}
#endif /* MODULE */


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -Wall -O6 -I/usr/src/linux/net/inet -c smc-mca.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  c-indent-level: 8
 *  tab-width: 8
 * End:
 */
