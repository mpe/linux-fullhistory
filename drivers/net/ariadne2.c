/*
 *  Amiga Linux/m68k Ariadne II Ethernet Driver
 *
 *  (C) Copyright 1998 by some Elitist 680x0 Users(TM)
 *
 *  ---------------------------------------------------------------------------
 *
 *  This program is based on all the other NE2000 drivers for Linux
 *
 *  ---------------------------------------------------------------------------
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of the Linux
 *  distribution for more details.
 *
 *  ---------------------------------------------------------------------------
 *
 *  The Ariadne II is a Zorro-II board made by Village Tronic. It contains a
 *  Realtek RTL8019AS Ethernet Controller.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/zorro.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>

#include "8390.h"


#define ARIADNE2_BASE		0x0300
#define ARIADNE2_BOOTROM	0xc000


#define NE_BASE		(dev->base_addr)
#define NE_CMD		(0x00*2)
#define NE_DATAPORT	(0x10*2)	/* NatSemi-defined port window offset. */
#define NE_RESET	(0x1f*2)	/* Issue a read to reset, a write to clear. */
#define NE_IO_EXTENT	(0x20*2)

#define NE_EN0_ISR	(0x07*2)
#define NE_EN0_DCFG	(0x0e*2)

#define NE_EN0_RSARLO	(0x08*2)
#define NE_EN0_RSARHI	(0x09*2)
#define NE_EN0_RCNTLO	(0x0a*2)
#define NE_EN0_RXCR	(0x0c*2)
#define NE_EN0_TXCR	(0x0d*2)
#define NE_EN0_RCNTHI	(0x0b*2)
#define NE_EN0_IMR	(0x0f*2)

#define NESM_START_PG	0x40	/* First page of TX buffer */
#define NESM_STOP_PG	0x80	/* Last page +1 of RX ring */


#define WORDSWAP(a)	((((a)>>8)&0xff) | ((a)<<8))

int ariadne2_probe(struct device *dev);
static int ariadne2_init(struct device *dev, unsigned int key,
			 unsigned long board);

static int ariadne2_open(struct device *dev);
static int ariadne2_close(struct device *dev);

static void ariadne2_reset_8390(struct device *dev);
static void ariadne2_get_8390_hdr(struct device *dev,
				  struct e8390_pkt_hdr *hdr, int ring_page);
static void ariadne2_block_input(struct device *dev, int count,
				 struct sk_buff *skb, int ring_offset);
static void ariadne2_block_output(struct device *dev, const int count,
				  const unsigned char *buf,
				  const int start_page);


__initfunc(int ariadne2_probe(struct device *dev))
{
    unsigned int key;
    const struct ConfigDev *cd;
    u_long board;
    int err;

    if ((key = zorro_find(ZORRO_PROD_VILLAGE_TRONIC_ARIADNE2, 0, 0))) {
	cd = zorro_get_board(key);
	if ((board = (u_long)cd->cd_BoardAddr)) {
	    if ((err = ariadne2_init(dev, key, ZTWO_VADDR(board))))
		return err;
	    zorro_config_board(key, 0);
	    return 0;
	}
    }
    return -ENODEV;
}

__initfunc(static int ariadne2_init(struct device *dev, unsigned int key,
				    unsigned long board))
{
    int i;
    unsigned char SA_prom[32];
    const char *name = NULL;
    int start_page, stop_page;
    static u32 ariadne2_offsets[16] = {
	0x00, 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e,
	0x10, 0x12, 0x14, 0x16, 0x18, 0x1a, 0x1c, 0x1e,
    };
    int ioaddr = board+ARIADNE2_BASE*2;

    if (load_8390_module("ariadne2.c"))
	return -ENOSYS;

    /* We should have a "dev" from Space.c or the static module table. */
    if (dev == NULL) {
	printk(KERN_ERR "ariadne2.c: Passed a NULL device.\n");
	dev = init_etherdev(0, 0);
    }

    /* Reset card. Who knows what dain-bramaged state it was left in. */
    {
	unsigned long reset_start_time = jiffies;

	writeb(readb(ioaddr + NE_RESET), ioaddr + NE_RESET);

	while ((readb(ioaddr + NE_EN0_ISR) & ENISR_RESET) == 0)
	    if (jiffies - reset_start_time > 2*HZ/100) {
		printk(" not found (no reset ack).\n");
		return -ENODEV;
	    }

	writeb(0xff, ioaddr + NE_EN0_ISR);		/* Ack all intr. */
    }

    /* Read the 16 bytes of station address PROM.
       We must first initialize registers, similar to NS8390_init(eifdev, 0).
       We can't reliably read the SAPROM address without this.
       (I learned the hard way!). */
    {
	struct {
	    u32 value;
	    u32 offset;
	} program_seq[] = {
	    {E8390_NODMA+E8390_PAGE0+E8390_STOP, NE_CMD}, /* Select page 0*/
	    {0x48,	NE_EN0_DCFG},	/* Set byte-wide (0x48) access. */
	    {0x00,	NE_EN0_RCNTLO},	/* Clear the count regs. */
	    {0x00,	NE_EN0_RCNTHI},
	    {0x00,	NE_EN0_IMR},	/* Mask completion irq. */
	    {0xFF,	NE_EN0_ISR},
	    {E8390_RXOFF, NE_EN0_RXCR},	/* 0x20  Set to monitor */
	    {E8390_TXOFF, NE_EN0_TXCR},	/* 0x02  and loopback mode. */
	    {32,	NE_EN0_RCNTLO},
	    {0x00,	NE_EN0_RCNTHI},
	    {0x00,	NE_EN0_RSARLO},	/* DMA starting at 0x0000. */
	    {0x00,	NE_EN0_RSARHI},
	    {E8390_RREAD+E8390_START, NE_CMD},
	};
	for (i = 0; i < sizeof(program_seq)/sizeof(program_seq[0]); i++) {
	    writeb(program_seq[i].value, ioaddr + program_seq[i].offset);
	}
    }
    for (i = 0; i < 16; i++) {
	SA_prom[i] = readb(ioaddr + NE_DATAPORT);
	(void)readb(ioaddr + NE_DATAPORT);
    }

    /* We must set the 8390 for word mode. */
    writeb(0x49, ioaddr + NE_EN0_DCFG);
    start_page = NESM_START_PG;
    stop_page = NESM_STOP_PG;

    name = "NE2000";

    dev->base_addr = ioaddr;

    /* Install the Interrupt handler */
    if (request_irq(IRQ_AMIGA_PORTS, ei_interrupt, 0, "AriadNE2 Ethernet",
		    dev))
	return -EAGAIN;

    /* Allocate dev->priv and fill in 8390 specific dev fields. */
    if (ethdev_init(dev)) {
	printk("Unable to get memory for dev->priv.\n");
	return -ENOMEM;
    }
    ((struct ei_device *)dev->priv)->priv = key;

    for(i = 0; i < ETHER_ADDR_LEN; i++) {
	printk(" %2.2x", SA_prom[i]);
	dev->dev_addr[i] = SA_prom[i];
    }

    printk("%s: AriadNE2 at 0x%08lx, Ethernet Address "
	   "%02x:%02x:%02x:%02x:%02x:%02x\n", dev->name, board,
	   dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
	   dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

    ei_status.name = name;
    ei_status.tx_start_page = start_page;
    ei_status.stop_page = stop_page;
    ei_status.word16 = 1;

    ei_status.rx_start_page = start_page + TX_PAGES;

    ei_status.reset_8390 = &ariadne2_reset_8390;
    ei_status.block_input = &ariadne2_block_input;
    ei_status.block_output = &ariadne2_block_output;
    ei_status.get_8390_hdr = &ariadne2_get_8390_hdr;
    ei_status.reg_offset = ariadne2_offsets;
    dev->open = &ariadne2_open;
    dev->stop = &ariadne2_close;
    NS8390_init(dev, 0);
    return 0;
}

static int ariadne2_open(struct device *dev)
{
    ei_open(dev);
    MOD_INC_USE_COUNT;
    return 0;
}

static int ariadne2_close(struct device *dev)
{
    if (ei_debug > 1)
	printk("%s: Shutting down ethercard.\n", dev->name);
    ei_close(dev);
    MOD_DEC_USE_COUNT;
    return 0;
}

/* Hard reset the card.  This used to pause for the same period that a
   8390 reset command required, but that shouldn't be necessary. */
static void ariadne2_reset_8390(struct device *dev)
{
    unsigned long reset_start_time = jiffies;

    if (ei_debug > 1)
	printk("resetting the 8390 t=%ld...", jiffies);

    writeb(readb(NE_BASE + NE_RESET), NE_BASE + NE_RESET);

    ei_status.txing = 0;
    ei_status.dmaing = 0;

    /* This check _should_not_ be necessary, omit eventually. */
    while ((readb(NE_BASE+NE_EN0_ISR) & ENISR_RESET) == 0)
	if (jiffies - reset_start_time > 2*HZ/100) {
	    printk("%s: ne_reset_8390() did not complete.\n", dev->name);
	    break;
	}
    writeb(ENISR_RESET, NE_BASE + NE_EN0_ISR);	/* Ack intr. */
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void ariadne2_get_8390_hdr(struct device *dev,
				  struct e8390_pkt_hdr *hdr, int ring_page)
{
    int nic_base = dev->base_addr;
    int cnt;
    short *ptrs;

    /* This *shouldn't* happen. If it does, it's the last thing you'll see */
    if (ei_status.dmaing) {
	printk("%s: DMAing conflict in ne_get_8390_hdr "
	   "[DMAstat:%d][irqlock:%d][intr:%ld].\n", dev->name, ei_status.dmaing,
	   ei_status.irqlock, dev->interrupt);
	return;
    }

    ei_status.dmaing |= 0x01;
    writeb(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
    writeb(ENISR_RDC, nic_base + NE_EN0_ISR);
    writeb(sizeof(struct e8390_pkt_hdr), nic_base + NE_EN0_RCNTLO);
    writeb(0, nic_base + NE_EN0_RCNTHI);
    writeb(0, nic_base + NE_EN0_RSARLO);		/* On page boundary */
    writeb(ring_page, nic_base + NE_EN0_RSARHI);
    writeb(E8390_RREAD+E8390_START, nic_base + NE_CMD);

    ptrs = (short*)hdr;
    for (cnt = 0; cnt < (sizeof(struct e8390_pkt_hdr)>>1); cnt++)
	*ptrs++ = readw(NE_BASE + NE_DATAPORT);

    writeb(ENISR_RDC, nic_base + NE_EN0_ISR);	/* Ack intr. */

    hdr->count = WORDSWAP(hdr->count);

    ei_status.dmaing &= ~0x01;
}

/* Block input and output, similar to the Crynwr packet driver.  If you
   are porting to a new ethercard, look at the packet driver source for hints.
   The NEx000 doesn't share the on-board packet memory -- you have to put
   the packet out through the "remote DMA" dataport using writeb. */

static void ariadne2_block_input(struct device *dev, int count,
				 struct sk_buff *skb, int ring_offset)
{
    int nic_base = dev->base_addr;
    char *buf = skb->data;
    short *ptrs;
    int cnt;

    /* This *shouldn't* happen. If it does, it's the last thing you'll see */
    if (ei_status.dmaing) {
	printk("%s: DMAing conflict in ne_block_input "
	   "[DMAstat:%d][irqlock:%d][intr:%ld].\n",
	   dev->name, ei_status.dmaing, ei_status.irqlock,
	   dev->interrupt);
	return;
    }
    ei_status.dmaing |= 0x01;
    writeb(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
    writeb(ENISR_RDC, nic_base + NE_EN0_ISR);
    writeb(count & 0xff, nic_base + NE_EN0_RCNTLO);
    writeb(count >> 8, nic_base + NE_EN0_RCNTHI);
    writeb(ring_offset & 0xff, nic_base + NE_EN0_RSARLO);
    writeb(ring_offset >> 8, nic_base + NE_EN0_RSARHI);
    writeb(E8390_RREAD+E8390_START, nic_base + NE_CMD);
    ptrs = (short*)buf;
    for (cnt = 0; cnt < (count>>1); cnt++)
	*ptrs++ = readw(NE_BASE + NE_DATAPORT);
    if (count & 0x01)
	buf[count-1] = readb(NE_BASE + NE_DATAPORT);

    writeb(ENISR_RDC, nic_base + NE_EN0_ISR);	/* Ack intr. */
    ei_status.dmaing &= ~0x01;
}

static void ariadne2_block_output(struct device *dev, int count,
				  const unsigned char *buf,
				  const int start_page)
{
    int nic_base = NE_BASE;
    unsigned long dma_start;
    short *ptrs;
    int cnt;

    /* Round the count up for word writes.  Do we need to do this?
       What effect will an odd byte count have on the 8390?
       I should check someday. */
    if (count & 0x01)
	count++;

    /* This *shouldn't* happen. If it does, it's the last thing you'll see */
    if (ei_status.dmaing) {
	printk("%s: DMAing conflict in ne_block_output."
	   "[DMAstat:%d][irqlock:%d][intr:%ld]\n", dev->name, ei_status.dmaing,
	   ei_status.irqlock, dev->interrupt);
	return;
    }
    ei_status.dmaing |= 0x01;
    /* We should already be in page 0, but to be safe... */
    writeb(E8390_PAGE0+E8390_START+E8390_NODMA, nic_base + NE_CMD);

    writeb(ENISR_RDC, nic_base + NE_EN0_ISR);

   /* Now the normal output. */
    writeb(count & 0xff, nic_base + NE_EN0_RCNTLO);
    writeb(count >> 8,   nic_base + NE_EN0_RCNTHI);
    writeb(0x00, nic_base + NE_EN0_RSARLO);
    writeb(start_page, nic_base + NE_EN0_RSARHI);

    writeb(E8390_RWRITE+E8390_START, nic_base + NE_CMD);
    ptrs = (short*)buf;
    for (cnt = 0; cnt < count>>1; cnt++)
	writew(*ptrs++, NE_BASE+NE_DATAPORT);

    dma_start = jiffies;

    while ((readb(NE_BASE + NE_EN0_ISR) & ENISR_RDC) == 0)
	if (jiffies - dma_start > 2*HZ/100) {		/* 20ms */
		printk("%s: timeout waiting for Tx RDC.\n", dev->name);
		ariadne2_reset_8390(dev);
		NS8390_init(dev,1);
		break;
	}

    writeb(ENISR_RDC, nic_base + NE_EN0_ISR);	/* Ack intr. */
    ei_status.dmaing &= ~0x01;
    return;
}

#ifdef MODULE
static char devicename[9] = { 0, };

static struct device ariadne2_dev =
{
    devicename,
    0, 0, 0, 0,
    0, 0,
    0, 0, 0, NULL, ariadne2_probe,
};

int init_module(void)
{
    int err;
    if ((err = register_netdev(&ariadne2_dev))) {
	if (err == -EIO)
	    printk("No AriadNE2 ethernet card found.\n");
	return err;
    }
    lock_8390_module();
    return 0;
}

void cleanup_module(void)
{
    unsigned int key = ((struct ei_device *)ariadne2_dev.priv)->priv;
    free_irq(IRQ_AMIGA_PORTS, &ariadne2_dev);
    unregister_netdev(&ariadne2_dev);
    zorro_unconfig_board(key, 0);
    unlock_8390_module();
}

#endif	/* MODULE */
