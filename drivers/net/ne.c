/* ne.c: A general non-shared-memory NS8390 ethernet driver for linux. */
/*
    Written 1992-94 by Donald Becker.

    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.

    This software may be used and distributed according to the terms
    of the GNU Public License, incorporated herein by reference.

    The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
    Center of Excellence in Space Data and Information Sciences
        Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

    This driver should work with many programmed-I/O 8390-based ethernet
    boards.  Currently it supports the NE1000, NE2000, many clones,
    and some Cabletron products.

    13/04/95 -- Change in philosophy. We now monitor ENISR_RDC for
    handshaking the Tx PIO xfers. If we don't get a RDC within a
    reasonable period of time, we know the 8390 has gone south, and we
    kick the board before it locks the system. Also use set_bit() to
    create atomic locks on the PIO xfers, and added some defines
    that the end user can play with to save memory.	-- Paul Gortmaker

*/

/* Routines for the NatSemi-based designs (NE[12]000). */

static char *version =
    "ne.c:v1.10 9/23/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <asm/system.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include "8390.h"

/* Some defines that people can play with if so inclined. */

/* Do we support clones that don't adhere to 14,15 of the SAprom ? */
#define CONFIG_NE_BAD_CLONES

/* Do we perform extra sanity checks on stuff ? */
/* #define CONFIG_NE_SANITY */

/* Do we implement the read before write bugfix ? */
/* #define CONFIG_NE_RW_BUGFIX */

/* ---- No user-serviceable parts below ---- */

extern struct device *init_etherdev(struct device *dev, int sizeof_private,
				    unsigned long *mem_startp);


/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int netcard_portlist[] =
{ 0x300, 0x280, 0x320, 0x340, 0x360, 0};

#ifdef CONFIG_NE_BAD_CLONES
/* A list of bad clones that we none-the-less recognize. */
static struct { char *name8, *name16; unsigned char SAprefix[4];}
bad_clone_list[] = {
    {"DE100", "DE200", {0x00, 0xDE, 0x01,}},
    {"DE120", "DE220", {0x00, 0x80, 0xc8,}},
    {"DFI1000", "DFI2000", {'D', 'F', 'I',}}, /* Original, eh?  */
    {"EtherNext UTP8", "EtherNext UTP16", {0x00, 0x00, 0x79}},
    {"NE1000","NE2000-invalid", {0x00, 0x00, 0xd8}}, /* Ancient real NE1000. */
    {"NN1000", "NN2000",  {0x08, 0x03, 0x08}}, /* Outlaw no-name clone. */
    {"4-DIM8","4-DIM16", {0x00,0x00,0x4d,}},  /* Outlaw 4-Dimension cards. */
    {0,}
};
#endif

#define NE_BASE	 (dev->base_addr)
#define NE_CMD	 	0x00
#define NE_DATAPORT	0x10	/* NatSemi-defined port window offset. */
#define NE_RESET	0x1f	/* Issue a read to reset, a write to clear. */
#define NE_IO_EXTENT	0x20

#define NE1SM_START_PG	0x20	/* First page of TX buffer */
#define NE1SM_STOP_PG 	0x40	/* Last page +1 of RX ring */
#define NESM_START_PG	0x40	/* First page of TX buffer */
#define NESM_STOP_PG	0x80	/* Last page +1 of RX ring */

#define NE_RDC_TIMEOUT	0x02	/* Max wait in jiffies for Tx RDC */

int ne_probe(struct device *dev);
static int ne_probe1(struct device *dev, int ioaddr);

static void ne_reset_8390(struct device *dev);
static int ne_block_input(struct device *dev, int count,
			  char *buf, int ring_offset);
static void ne_block_output(struct device *dev, const int count,
		const unsigned char *buf, const int start_page);


/*  Probe for various non-shared-memory ethercards.

   NEx000-clone boards have a Station Address PROM (SAPROM) in the packet
   buffer memory space.  NE2000 clones have 0x57,0x57 in bytes 0x0e,0x0f of
   the SAPROM, while other supposed NE2000 clones must be detected by their
   SA prefix.

   Reading the SAPROM from a word-wide card with the 8390 set in byte-wide
   mode results in doubled values, which can be detected and compensated for.

   The probe is also responsible for initializing the card and filling
   in the 'dev' and 'ei_status' structures.

   We use the minimum memory size for some ethercard product lines, iff we can't
   distinguish models.  You can increase the packet buffer size by setting
   PACKETBUF_MEMSIZE.  Reported Cabletron packet buffer locations are:
	E1010   starts at 0x100 and ends at 0x2000.
	E1010-x starts at 0x100 and ends at 0x8000. ("-x" means "more memory")
	E2010	 starts at 0x100 and ends at 0x4000.
	E2010-x starts at 0x100 and ends at 0xffff.  */

#ifdef HAVE_DEVLIST
struct netdev_entry netcard_drv =
{"ne", ne_probe1, NE_IO_EXTENT, netcard_portlist};
#else

int ne_probe(struct device *dev)
{
    int i;
    int base_addr = dev ? dev->base_addr : 0;

    if (base_addr > 0x1ff)	/* Check a single specified location. */
	return ne_probe1(dev, base_addr);
    else if (base_addr != 0)	/* Don't probe at all. */
	return ENXIO;

    for (i = 0; netcard_portlist[i]; i++) {
	int ioaddr = netcard_portlist[i];
	if (check_region(ioaddr, NE_IO_EXTENT))
	    continue;
	if (ne_probe1(dev, ioaddr) == 0)
	    return 0;
    }

    return ENODEV;
}
#endif

static int ne_probe1(struct device *dev, int ioaddr)
{
    int i;
    unsigned char SA_prom[32];
    int wordlength = 2;
    char *name = NULL;
    int start_page, stop_page;
    int neX000, ctron;
    int reg0 = inb_p(ioaddr);

    if (reg0 == 0xFF)
	return ENODEV;

    /* Do a preliminary verification that we have a 8390. */
    {	int regd;
	outb_p(E8390_NODMA+E8390_PAGE1+E8390_STOP, ioaddr + E8390_CMD);
	regd = inb_p(ioaddr + 0x0d);
	outb_p(0xff, ioaddr + 0x0d);
	outb_p(E8390_NODMA+E8390_PAGE0, ioaddr + E8390_CMD);
	inb_p(ioaddr + EN0_COUNTER0); /* Clear the counter by reading. */
	if (inb_p(ioaddr + EN0_COUNTER0) != 0) {
	    outb_p(reg0, ioaddr);
	    outb_p(regd, ioaddr + 0x0d);	/* Restore the old values. */
	    return ENODEV;
	}
    }

    printk("NE*000 ethercard probe at %#3x:", ioaddr);

    /* Read the 16 bytes of station address PROM.
       We must first initialize registers, similar to NS8390_init(eifdev, 0).
       We can't reliably read the SAPROM address without this.
       (I learned the hard way!). */
    {
	struct {unsigned char value, offset; } program_seq[] = {
	    {E8390_NODMA+E8390_PAGE0+E8390_STOP, E8390_CMD}, /* Select page 0*/
	    {0x48,	EN0_DCFG},	/* Set byte-wide (0x48) access. */
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
	    outb_p(program_seq[i].value, ioaddr + program_seq[i].offset);

    }
    for(i = 0; i < 32 /*sizeof(SA_prom)*/; i+=2) {
	SA_prom[i] = inb(ioaddr + NE_DATAPORT);
	SA_prom[i+1] = inb(ioaddr + NE_DATAPORT);
	if (SA_prom[i] != SA_prom[i+1])
	    wordlength = 1;
    }

    if (wordlength == 2) {
	/* We must set the 8390 for word mode. */
	outb_p(0x49, ioaddr + EN0_DCFG);
	/* We used to reset the ethercard here, but it doesn't seem
	   to be necessary. */
	/* Un-double the SA_prom values. */
	for (i = 0; i < 16; i++)
	    SA_prom[i] = SA_prom[i+i];
	start_page = NESM_START_PG;
	stop_page = NESM_STOP_PG;
    } else {
	start_page = NE1SM_START_PG;
	stop_page = NE1SM_STOP_PG;
    }

    for(i = 0; i < ETHER_ADDR_LEN; i++) {
	dev->dev_addr[i] = SA_prom[i];
	printk(" %2.2x", SA_prom[i]);
    }

    neX000 = (SA_prom[14] == 0x57  &&  SA_prom[15] == 0x57);
    ctron =  (SA_prom[0] == 0x00 && SA_prom[1] == 0x00 && SA_prom[2] == 0x1d);

    /* Set up the rest of the parameters. */
    if (neX000) {
	name = (wordlength == 2) ? "NE2000" : "NE1000";
    } else if (ctron) {
	name = "Cabletron";
	start_page = 0x01;
	stop_page = (wordlength == 2) ? 0x40 : 0x20;
    } else {
#ifdef CONFIG_NE_BAD_CLONES
	/* Ack!  Well, there might be a *bad* NE*000 clone there.
	   Check for total bogus addresses. */
	for (i = 0; bad_clone_list[i].name8; i++) {
	    if (SA_prom[0] == bad_clone_list[i].SAprefix[0] &&
		SA_prom[1] == bad_clone_list[i].SAprefix[1] &&
		SA_prom[2] == bad_clone_list[i].SAprefix[2]) {
		if (wordlength == 2) {
		    name = bad_clone_list[i].name16;
		} else {
		    name = bad_clone_list[i].name8;
		}
		break;
	    }
	}
	if (bad_clone_list[i].name8 == NULL) {
	    printk(" not found (invalid signature %2.2x %2.2x).\n",
		   SA_prom[14], SA_prom[15]);
	    return ENXIO;
	}
#else
	printk(" not found.\n");
	return ENXIO;
#endif

    }


    if (dev == NULL)
	dev = init_etherdev(0, sizeof(struct ei_device), 0);

    if (dev->irq < 2) {
	autoirq_setup(0);
	outb_p(0x50, ioaddr + EN0_IMR);	/* Enable one interrupt. */
	outb_p(0x00, ioaddr + EN0_RCNTLO);
	outb_p(0x00, ioaddr + EN0_RCNTHI);
	outb_p(E8390_RREAD+E8390_START, ioaddr); /* Trigger it... */
	outb_p(0x00, ioaddr + EN0_IMR); 		/* Mask it again. */
	dev->irq = autoirq_report(0);
	if (ei_debug > 2)
	    printk(" autoirq is %d\n", dev->irq);
    } else if (dev->irq == 2)
	/* Fixup for users that don't know that IRQ 2 is really IRQ 9,
	   or don't know which one to set. */
	dev->irq = 9;
    
    /* Snarf the interrupt now.  There's no point in waiting since we cannot
       share and the board will usually be enabled. */
    {
	int irqval = request_irq (dev->irq, ei_interrupt, 0, wordlength==2 ? "ne2000":"ne1000");
	if (irqval) {
	    printk (" unable to get IRQ %d (irqval=%d).\n", dev->irq, irqval);
	    return EAGAIN;
	}
    }

    dev->base_addr = ioaddr;

    request_region(ioaddr, NE_IO_EXTENT, wordlength==2 ? "ne2000":"ne1000");

    for(i = 0; i < ETHER_ADDR_LEN; i++)
	dev->dev_addr[i] = SA_prom[i];

    ethdev_init(dev);
    printk("\n%s: %s found at %#x, using IRQ %d.\n",
	   dev->name, name, ioaddr, dev->irq);

    if (ei_debug > 0)
	printk(version);

    ei_status.name = name;
    ei_status.tx_start_page = start_page;
    ei_status.stop_page = stop_page;
    ei_status.word16 = (wordlength == 2);

    ei_status.rx_start_page = start_page + TX_PAGES;
#ifdef PACKETBUF_MEMSIZE
    /* Allow the packet buffer size to be overridden by know-it-alls. */
    ei_status.stop_page = ei_status.tx_start_page + PACKETBUF_MEMSIZE;
#endif

    ei_status.reset_8390 = &ne_reset_8390;
    ei_status.block_input = &ne_block_input;
    ei_status.block_output = &ne_block_output;
    NS8390_init(dev, 0);
    return 0;
}

/* Hard reset the card.  This used to pause for the same period that a
   8390 reset command required, but that shouldn't be necessary. */
static void
ne_reset_8390(struct device *dev)
{
    int tmp = inb_p(NE_BASE + NE_RESET);
    int reset_start_time = jiffies;

    if (ei_debug > 1) printk("resetting the 8390 t=%ld...", jiffies);
    ei_status.txing = 0;

    outb_p(tmp, NE_BASE + NE_RESET);
    /* This check _should_not_ be necessary, omit eventually. */
    while ((inb_p(NE_BASE+EN0_ISR) & ENISR_RESET) == 0)
	if (jiffies - reset_start_time > 2) {
	    printk("%s: ne_reset_8390() did not complete.\n", dev->name);
	    break;
	}
}

/* Block input and output, similar to the Crynwr packet driver.  If you
   porting to a new ethercard look at the packet driver source for hints.
   The NEx000 doesn't share it on-board packet memory -- you have to put
   the packet out through the "remote DMA" dataport using outb. */

static int
ne_block_input(struct device *dev, int count, char *buf, int ring_offset)
{
#ifdef CONFIG_NE_SANITY
    int xfer_count = count;
#endif
    int nic_base = dev->base_addr;

    /* This *shouldn't* happen. If it does, it's the last thing you'll see */
    if (set_bit(0,(void*)&ei_status.dmaing)) {
	if (ei_debug > 0)
	    printk("%s: DMAing conflict in ne_block_input "
		   "[DMAstat:%d][irqlock:%d][intr:%d].\n",
		   dev->name, ei_status.dmaing, ei_status.irqlock,
		   dev->interrupt);
	return 0;
    }
    ei_status.dmaing |= 0x02;
    outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
    outb_p(count & 0xff, nic_base + EN0_RCNTLO);
    outb_p(count >> 8, nic_base + EN0_RCNTHI);
    outb_p(ring_offset & 0xff, nic_base + EN0_RSARLO);
    outb_p(ring_offset >> 8, nic_base + EN0_RSARHI);
    outb_p(E8390_RREAD+E8390_START, nic_base + NE_CMD);
    if (ei_status.word16) {
      insw(NE_BASE + NE_DATAPORT,buf,count>>1);
      if (count & 0x01) {
	buf[count-1] = inb(NE_BASE + NE_DATAPORT);
#ifdef CONFIG_NE_SANITY
	xfer_count++;
#endif
      }
    } else {
	insb(NE_BASE + NE_DATAPORT, buf, count);
    }

    /* This was for the ALPHA version only, but enough people have
       been encountering problems so it is still here.  If you see
       this message you either 1) have a slightly incompatible clone
       or 2) have noise/speed problems with your bus. */
#ifdef CONFIG_NE_SANITY
    if (ei_debug > 1) {		/* DMA termination address check... */
	int addr, tries = 20;
	do {
	    /* DON'T check for 'inb_p(EN0_ISR) & ENISR_RDC' here
	       -- it's broken for Rx on some cards! */
	    int high = inb_p(nic_base + EN0_RSARHI);
	    int low = inb_p(nic_base + EN0_RSARLO);
	    addr = (high << 8) + low;
	    if (((ring_offset + xfer_count) & 0xff) == low)
		break;
	} while (--tries > 0);
	if (tries <= 0)
	    printk("%s: RX transfer address mismatch,"
		   "%#4.4x (expected) vs. %#4.4x (actual).\n",
		   dev->name, ring_offset + xfer_count, addr);
    }
#endif
    outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
    ei_status.dmaing &= ~0x03;
    return ring_offset + count;
}

static void
ne_block_output(struct device *dev, int count,
		const unsigned char *buf, const int start_page)
{
#ifdef CONFIG_NE_SANITY
    int retries = 0;
#endif
    int nic_base = NE_BASE;
    unsigned long dma_start;

    /* Round the count up for word writes.  Do we need to do this?
       What effect will an odd byte count have on the 8390?
       I should check someday. */
    if (ei_status.word16 && (count & 0x01))
      count++;

    /* This *shouldn't* happen. If it does, it's the last thing you'll see */
    if (set_bit(0,(void*)&ei_status.dmaing)) {
	if (ei_debug > 0)
	    printk("%s: DMAing conflict in ne_block_output."
		   "[DMAstat:%d][irqlock:%d][intr:%d]\n",
		   dev->name, ei_status.dmaing, ei_status.irqlock,
		   dev->interrupt);
	return;
    }
    ei_status.dmaing |= 0x04;
    /* We should already be in page 0, but to be safe... */
    outb_p(E8390_PAGE0+E8390_START+E8390_NODMA, nic_base + NE_CMD);

#ifdef CONFIG_NE_SANITY
 retry:
#endif

#ifdef CONFIG_NE_RW_BUGFIX 
    /* Handle the read-before-write bug the same way as the
       Crynwr packet driver -- the NatSemi method doesn't work.
       Actually this doesn't always work either, but if you have
       problems with your NEx000 this is better than nothing! */
    outb_p(0x42, nic_base + EN0_RCNTLO);
    outb_p(0x00,   nic_base + EN0_RCNTHI);
    outb_p(0x42, nic_base + EN0_RSARLO);
    outb_p(0x00, nic_base + EN0_RSARHI);
    outb_p(E8390_RREAD+E8390_START, nic_base + NE_CMD);
    /* Make certain that the dummy read has occurred. */
    SLOW_DOWN_IO;
    SLOW_DOWN_IO;
    SLOW_DOWN_IO;
#endif  /* rw_bugfix */

    outb_p(ENISR_RDC, nic_base + EN0_ISR);

   /* Now the normal output. */
    outb_p(count & 0xff, nic_base + EN0_RCNTLO);
    outb_p(count >> 8,   nic_base + EN0_RCNTHI);
    outb_p(0x00, nic_base + EN0_RSARLO);
    outb_p(start_page, nic_base + EN0_RSARHI);

    outb_p(E8390_RWRITE+E8390_START, nic_base + NE_CMD);
    if (ei_status.word16) {
	outsw(NE_BASE + NE_DATAPORT, buf, count>>1);
    } else {
	outsb(NE_BASE + NE_DATAPORT, buf, count);
    }

    dma_start = jiffies;

#ifdef CONFIG_NE_SANITY
    /* This was for the ALPHA version only, but enough people have
       been encountering problems so it is still here. */
    if (ei_debug > 1) {		/* DMA termination address check... */
	int addr, tries = 20;
	do {
	    int high = inb_p(nic_base + EN0_RSARHI);
	    int low = inb_p(nic_base + EN0_RSARLO);
	    addr = (high << 8) + low;
	    if ((start_page << 8) + count == addr)
		break;
	} while (--tries > 0);
	if (tries <= 0) {
	    printk("%s: Tx packet transfer address mismatch,"
		   "%#4.4x (expected) vs. %#4.4x (actual).\n",
		   dev->name, (start_page << 8) + count, addr);
	    if (retries++ == 0)
		goto retry;
	}
    }
#endif

    while ((inb_p(nic_base + EN0_ISR) & ENISR_RDC) == 0)
	if (jiffies - dma_start > NE_RDC_TIMEOUT) {
		printk("%s: timeout waiting for Tx RDC.\n", dev->name);
		ne_reset_8390(dev);
		NS8390_init(dev,1);
		break;
	}

    outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
    ei_status.dmaing &= ~0x05;
    return;
}


/*
 * Local variables:
 *  compile-command: "gcc -DKERNEL -Wall -O6 -fomit-frame-pointer -I/usr/src/linux/net/tcp -c ne.c"
 *  version-control: t
 *  kept-new-versions: 5
 * End:
 */
