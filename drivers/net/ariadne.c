/*
 *  Amiga Linux/m68k Ariadne Ethernet Driver
 *
 *  © Copyright 1995 by Geert Uytterhoeven
 *		       (Geert.Uytterhoeven@cs.kuleuven.ac.be)
 *			Peter De Schrijver
 *		       (Peter.DeSchrijver@linux.cc.kuleuven.ac.be)
 *
 *  ---------------------------------------------------------------------------
 *
 *  This program is based on
 *
 *	lance.c:	An AMD LANCE ethernet driver for linux.
 *			Written 1993-94 by Donald Becker.
 *
 *	Am79C960:	PCnet(tm)-ISA Single-Chip Ethernet Controller
 *			Advanced Micro Devices
 *			Publication #16907, Rev. B, Amendment/0, May 1994
 *
 *	MC68230:	Parallel Interface/Timer (PI/T)
 *			Motorola Semiconductors, December, 1983
 *
 *  ---------------------------------------------------------------------------
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of the Linux
 *  distribution for more details.
 *
 *  ---------------------------------------------------------------------------
 *
 *  The Ariadne is a Zorro-II board made by Village Tronic. It contains:
 *
 *	- an Am79C960 PCnet-ISA Single-Chip Ethernet Controller with both
 *	  10BASE-2 (thin coax) and 10BASE-T (UTP) connectors
 *
 *	- an MC68230 Parallel Interface/Timer configured as 2 parallel ports
 */

#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/init.h>

#include <asm/bitops.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <linux/zorro.h>
#include <asm/io.h>
#include <asm/irq.h>

#include "ariadne.h"


#ifdef ARIADNE_DEBUG
int ariadne_debug = ARIADNE_DEBUG;
#else
int ariadne_debug = 1;
#endif


    /*
     *	Macros to Fix Endianness problems
     */

				/* Swap the Bytes in a WORD */
#define swapw(x)	(((x>>8)&0x00ff)|((x<<8)&0xff00))
				/* Get the Low BYTE in a WORD */
#define lowb(x)		(x&0xff)
				/* Get the Swapped High WORD in a LONG */
#define swhighw(x)	((((x)>>8)&0xff00)|(((x)>>24)&0x00ff))
				/* Get the Swapped Low WORD in a LONG */
#define swloww(x)	((((x)<<8)&0xff00)|(((x)>>8)&0x00ff))


    /*
     *	Transmit/Receive Ring Definitions
     */

#define TX_RING_SIZE	5
#define RX_RING_SIZE	16

#define PKT_BUF_SIZE	1520


    /*
     *	Private Device Data
     */

struct ariadne_private {
    struct AriadneBoard *board;
    struct TDRE *tx_ring[TX_RING_SIZE];
    struct RDRE *rx_ring[RX_RING_SIZE];
    u_short *tx_buff[TX_RING_SIZE];
    u_short *rx_buff[RX_RING_SIZE];
    int cur_tx, cur_rx;			/* The next free ring entry */
    int dirty_tx;			/* The ring entries to be free()ed. */
    struct net_device_stats stats;
    char tx_full;
    unsigned long lock;
    unsigned int key;
};


    /*
     *	Structure Created in the Ariadne's RAM Buffer
     */

struct lancedata {
    struct TDRE tx_ring[TX_RING_SIZE];
    struct RDRE rx_ring[RX_RING_SIZE];
    u_short tx_buff[TX_RING_SIZE][PKT_BUF_SIZE/sizeof(u_short)];
    u_short rx_buff[RX_RING_SIZE][PKT_BUF_SIZE/sizeof(u_short)];
};


static int ariadne_open(struct device *dev);
static void ariadne_init_ring(struct device *dev);
static int ariadne_start_xmit(struct sk_buff *skb, struct device *dev);
static int ariadne_rx(struct device *dev);
static void ariadne_interrupt(int irq, void *data, struct pt_regs *fp);
static int ariadne_close(struct device *dev);
static struct net_device_stats *ariadne_get_stats(struct device *dev);
#ifdef HAVE_MULTICAST
static void set_multicast_list(struct device *dev);
#endif


static void memcpyw(u_short *dest, u_short *src, int len)
{
    while (len >= 2) {
	*(dest++) = *(src++);
	len -= 2;
    }
    if (len == 1)
	*dest = (*(u_char *)src)<<8;
}


__initfunc(int ariadne_probe(struct device *dev))
{
    unsigned int key;
    const struct ConfigDev *cd;
    u_long board;
    struct ariadne_private *priv;

    /* Ethernet is part 0, Parallel is part 1 */
    if ((key = zorro_find(ZORRO_PROD_VILLAGE_TRONIC_ARIADNE, 0, 0))) {
	cd = zorro_get_board(key);
	if ((board = (u_long)cd->cd_BoardAddr)) {
	    dev->dev_addr[0] = 0x00;
	    dev->dev_addr[1] = 0x60;
	    dev->dev_addr[2] = 0x30;
	    dev->dev_addr[3] = (cd->cd_Rom.er_SerialNumber>>16)&0xff;
	    dev->dev_addr[4] = (cd->cd_Rom.er_SerialNumber>>8)&0xff;
	    dev->dev_addr[5] = cd->cd_Rom.er_SerialNumber&0xff;
	    printk("%s: Ariadne at 0x%08lx, Ethernet Address "
		   "%02x:%02x:%02x:%02x:%02x:%02x\n", dev->name, board,
		   dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		   dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	    init_etherdev(dev, 0);

	    dev->priv = kmalloc(sizeof(struct ariadne_private), GFP_KERNEL);
	    priv = (struct ariadne_private *)dev->priv;
	    memset(priv, 0, sizeof(struct ariadne_private));

	    priv->board = (struct AriadneBoard *)ZTWO_VADDR(board);
	    priv->key = key;

	    dev->open = &ariadne_open;
	    dev->stop = &ariadne_close;
	    dev->hard_start_xmit = &ariadne_start_xmit;
	    dev->get_stats = &ariadne_get_stats;
	    dev->set_multicast_list = &set_multicast_list;

	    zorro_config_board(key, 0);
	    return(0);
	}
    }
    return(ENODEV);
}


static int ariadne_open(struct device *dev)
{
    struct ariadne_private *priv = (struct ariadne_private *)dev->priv;
    struct AriadneBoard *board = priv->board;
    struct lancedata *lancedata;
    u_short in;
    u_long version;

    /* Reset the LANCE */
    in = board->Lance.Reset;

    /* Stop the LANCE */
    board->Lance.RAP = CSR0;	/* PCnet-ISA Controller Status */
    board->Lance.RDP = STOP;

    /* Check the LANCE version */
    board->Lance.RAP = CSR88;	/* Chip ID */
    version = swapw(board->Lance.RDP);
    board->Lance.RAP = CSR89;	/* Chip ID */
    version |= swapw(board->Lance.RDP)<<16;
    if ((version & 0x00000fff) != 0x00000003) {
	printk("ariadne_open: Couldn't find AMD Ethernet Chip\n");
	return(-EAGAIN);
    }
    if ((version & 0x0ffff000) != 0x00003000) {
	printk("ariadne_open: Couldn't find Am79C960 (Wrong part number = %ld)\n",
	       (version & 0x0ffff000)>>12);
	return(-EAGAIN);
    }
#if 0
    printk("ariadne_open: Am79C960 (PCnet-ISA) Revision %ld\n",
	   (version & 0xf0000000)>>28);
#endif

    ariadne_init_ring(dev);

    /* Miscellaneous Stuff */
    board->Lance.RAP = CSR3;	/* Interrupt Masks and Deferral Control */
    board->Lance.RDP = 0x0000;
    board->Lance.RAP = CSR4;	/* Test and Features Control */
    board->Lance.RDP = DPOLL|APAD_XMT|MFCOM|RCVCCOM|TXSTRTM|JABM;

    /* Set the Multicast Table */
    board->Lance.RAP = CSR8;	/* Logical Address Filter, LADRF[15:0] */
    board->Lance.RDP = 0x0000;
    board->Lance.RAP = CSR9;	/* Logical Address Filter, LADRF[31:16] */
    board->Lance.RDP = 0x0000;
    board->Lance.RAP = CSR10;	/* Logical Address Filter, LADRF[47:32] */
    board->Lance.RDP = 0x0000;
    board->Lance.RAP = CSR11;	/* Logical Address Filter, LADRF[63:48] */
    board->Lance.RDP = 0x0000;

    /* Set the Ethernet Hardware Address */
    board->Lance.RAP = CSR12;	/* Physical Address Register, PADR[15:0] */
    board->Lance.RDP = ((u_short *)&dev->dev_addr[0])[0];
    board->Lance.RAP = CSR13;	/* Physical Address Register, PADR[31:16] */
    board->Lance.RDP = ((u_short *)&dev->dev_addr[0])[1];
    board->Lance.RAP = CSR14;	/* Physical Address Register, PADR[47:32] */
    board->Lance.RDP = ((u_short *)&dev->dev_addr[0])[2];

    /* Set the Init Block Mode */
    board->Lance.RAP = CSR15;	/* Mode Register */
    board->Lance.RDP = 0x0000;

    lancedata = (struct lancedata *)offsetof(struct AriadneBoard, RAM);

    /* Set the Transmit Descriptor Ring Pointer */
    board->Lance.RAP = CSR30;	/* Base Address of Transmit Ring */
    board->Lance.RDP = swloww((u_long)&lancedata->tx_ring);
    board->Lance.RAP = CSR31;	/* Base Address of transmit Ring */
    board->Lance.RDP = swhighw((u_long)&lancedata->tx_ring);

    /* Set the Receive Descriptor Ring Pointer */
    board->Lance.RAP = CSR24;	/* Base Address of Receive Ring */
    board->Lance.RDP = swloww((u_long)&lancedata->rx_ring);
    board->Lance.RAP = CSR25;	/* Base Address of Receive Ring */
    board->Lance.RDP = swhighw((u_long)&lancedata->rx_ring);

    /* Set the Number of RX and TX Ring Entries */
    board->Lance.RAP = CSR76;	/* Receive Ring Length */
    board->Lance.RDP = swapw(((u_short)-RX_RING_SIZE));
    board->Lance.RAP = CSR78;	/* Transmit Ring Length */
    board->Lance.RDP = swapw(((u_short)-TX_RING_SIZE));

    /* Enable Media Interface Port Auto Select (10BASE-2/10BASE-T) */
    board->Lance.RAP = ISACSR2;	/* Miscellaneous Configuration */
    board->Lance.IDP = ASEL;

    /* LED Control */
    board->Lance.RAP = ISACSR5;	/* LED1 Status */
    board->Lance.IDP = PSE|XMTE;
    board->Lance.RAP = ISACSR6;	/* LED2 Status */
    board->Lance.IDP = PSE|COLE;
    board->Lance.RAP = ISACSR7;	/* LED3 Status */
    board->Lance.IDP = PSE|RCVE;

    dev->tbusy = 0;
    dev->interrupt = 0;
    dev->start = 1;

    if (request_irq(IRQ_AMIGA_PORTS, ariadne_interrupt, 0,
                    "Ariadne Ethernet", dev))
	return(-EAGAIN);

    board->Lance.RAP = CSR0;	/* PCnet-ISA Controller Status */
    board->Lance.RDP = INEA|STRT;

    MOD_INC_USE_COUNT;

    return(0);
}


static void ariadne_init_ring(struct device *dev)
{
    struct ariadne_private *priv = (struct ariadne_private *)dev->priv;
    struct AriadneBoard *board = priv->board;
    struct lancedata *lancedata;	/* LANCE point of view */
    struct lancedata *alancedata;	/* Amiga point of view */
    int i;

    priv->lock = 0, priv->tx_full = 0;
    priv->cur_rx = priv->cur_tx = 0;
    priv->dirty_tx = 0;

    lancedata = (struct lancedata *)offsetof(struct AriadneBoard, RAM);
    alancedata = (struct lancedata *)board->RAM;

    /* Set up TX Ring */
    for (i = 0; i < TX_RING_SIZE; i++) {
	alancedata->tx_ring[i].TMD0 = swloww((u_long)lancedata->tx_buff[i]);
	alancedata->tx_ring[i].TMD1 = swhighw((u_long)lancedata->tx_buff[i])|TF_STP|TF_ENP;
	alancedata->tx_ring[i].TMD2 = swapw((u_short)-PKT_BUF_SIZE);
	alancedata->tx_ring[i].TMD3 = 0;
	priv->tx_ring[i] = &alancedata->tx_ring[i];
	priv->tx_buff[i] = alancedata->tx_buff[i];
#if 0
	printk("TX Entry %2d @ 0x%08x (LANCE 0x%08x), Buf @ 0x%08x (LANCE 0x%08x)\n",
	       i, (int)&alancedata->tx_ring[i], (int)&lancedata->tx_ring[i],
	       (int)alancedata->tx_buff[i], (int)lancedata->tx_buff[i]);
#endif
    }

    /* Set up RX Ring */
    for (i = 0; i < RX_RING_SIZE; i++) {
	alancedata->rx_ring[i].RMD0 = swloww((u_long)lancedata->rx_buff[i]);
	alancedata->rx_ring[i].RMD1 = swhighw((u_long)lancedata->rx_buff[i])|RF_OWN;
	alancedata->rx_ring[i].RMD2 = swapw((u_short)-PKT_BUF_SIZE);
	alancedata->rx_ring[i].RMD3 = 0x0000;
	priv->rx_ring[i] = &alancedata->rx_ring[i];
	priv->rx_buff[i] = alancedata->rx_buff[i];
#if 0
	printk("RX Entry %2d @ 0x%08x (LANCE 0x%08x), Buf @ 0x%08x (LANCE 0x%08x)\n",
	       i, (int)&alancedata->rx_ring[i], (int)&lancedata->rx_ring[i],
	       (int)alancedata->rx_buff[i], (int)lancedata->rx_buff[i]);
#endif
    }
}


static int ariadne_close(struct device *dev)
{
    struct ariadne_private *priv = (struct ariadne_private *)dev->priv;
    struct AriadneBoard *board = priv->board;

    dev->start = 0;
    dev->tbusy = 1;

    board->Lance.RAP = CSR112;	/* Missed Frame Count */
    priv->stats.rx_missed_errors = swapw(board->Lance.RDP);
    board->Lance.RAP = CSR0;	/* PCnet-ISA Controller Status */

    if (ariadne_debug > 1) {
	printk("%s: Shutting down ethercard, status was %2.2x.\n", dev->name,
	       board->Lance.RDP);
	printk("%s: %lu packets missed\n", dev->name,
	       priv->stats.rx_missed_errors);
    }

    /* We stop the LANCE here -- it occasionally polls memory if we don't. */
    board->Lance.RDP = STOP;

    free_irq(IRQ_AMIGA_PORTS, dev);

    MOD_DEC_USE_COUNT;

    return(0);
}


static void ariadne_interrupt(int irq, void *data, struct pt_regs *fp)
{
    struct device *dev = (struct device *)data;
    struct ariadne_private *priv;
    struct AriadneBoard *board;
    int csr0, boguscnt = 10;

    if (dev == NULL) {
	printk("ariadne_interrupt(): irq for unknown device.\n");
	return;
    }

    priv = (struct ariadne_private *)dev->priv;
    board = priv->board;

    board->Lance.RAP = CSR0;	/* PCnet-ISA Controller Status */

    if (!(board->Lance.RDP & INTR))	/* Check if any interrupt has been
	return;				   generated by the board. */

    if (dev->interrupt)
	printk("%s: Re-entering the interrupt handler.\n", dev->name);

    dev->interrupt = 1;

    while ((csr0 = board->Lance.RDP) & (ERR|RINT|TINT) && --boguscnt >= 0) {
	/* Acknowledge all of the current interrupt sources ASAP. */
	board->Lance.RDP = csr0 & ~(INEA|TDMD|STOP|STRT|INIT);

#if 0
	if (ariadne_debug > 5) {
	    printk("%s: interrupt  csr0=%#2.2x new csr=%#2.2x.", dev->name,
		   csr0, board->Lance.RDP);
	    printk("[");
	    if (csr0 & INTR)
		printk(" INTR");
	    if (csr0 & INEA)
		printk(" INEA");
	    if (csr0 & RXON)
		printk(" RXON");
	    if (csr0 & TXON)
		printk(" TXON");
	    if (csr0 & TDMD)
		printk(" TDMD");
	    if (csr0 & STOP)
		printk(" STOP");
	    if (csr0 & STRT)
		printk(" STRT");
	    if (csr0 & INIT)
		printk(" INIT");
	    if (csr0 & ERR)
		printk(" ERR");
	    if (csr0 & BABL)
		printk(" BABL");
	    if (csr0 & CERR)
		printk(" CERR");
	    if (csr0 & MISS)
		printk(" MISS");
	    if (csr0 & MERR)
		printk(" MERR");
	    if (csr0 & RINT)
		printk(" RINT");
	    if (csr0 & TINT)
		printk(" TINT");
	    if (csr0 & IDON)
		printk(" IDON");
	    printk(" ]\n");
	}
#endif

	if (csr0 & RINT)	/* Rx interrupt */
	    ariadne_rx(dev);

	if (csr0 & TINT) {	/* Tx-done interrupt */
	    int dirty_tx = priv->dirty_tx;

	    while (dirty_tx < priv->cur_tx) {
		int entry = dirty_tx % TX_RING_SIZE;
		int status = lowb(priv->tx_ring[entry]->TMD1);

		if (status & TF_OWN)
		    break;	/* It still hasn't been Txed */

		priv->tx_ring[entry]->TMD1 &= 0xff00;

		if (status & TF_ERR) {
		    /* There was an major error, log it. */
		    int err_status = priv->tx_ring[entry]->TMD3;
		    priv->stats.tx_errors++;
		    if (err_status & EF_RTRY)
			priv->stats.tx_aborted_errors++;
		    if (err_status & EF_LCAR)
			priv->stats.tx_carrier_errors++;
		    if (err_status & EF_LCOL)
			priv->stats.tx_window_errors++;
		    if (err_status & EF_UFLO) {
			/* Ackk!  On FIFO errors the Tx unit is turned off! */
			priv->stats.tx_fifo_errors++;
			/* Remove this verbosity later! */
			printk("%s: Tx FIFO error! Status %4.4x.\n", dev->name,
			       csr0);
			/* Restart the chip. */
			board->Lance.RDP = STRT;
		    }
		} else {
		    if (status & (TF_MORE|TF_ONE))
			priv->stats.collisions++;
		    priv->stats.tx_packets++;
		}
		dirty_tx++;
	    }

#ifndef final_version
	    if (priv->cur_tx - dirty_tx >= TX_RING_SIZE) {
		printk("out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
		       dirty_tx, priv->cur_tx, priv->tx_full);
		dirty_tx += TX_RING_SIZE;
	    }
#endif

	    if (priv->tx_full && dev->tbusy &&
		dirty_tx > priv->cur_tx - TX_RING_SIZE + 2) {
		/* The ring is no longer full, clear tbusy. */
		priv->tx_full = 0;
		dev->tbusy = 0;
		mark_bh(NET_BH);
	    }

	    priv->dirty_tx = dirty_tx;
	}

	/* Log misc errors. */
	if (csr0 & BABL)
	    priv->stats.tx_errors++;	/* Tx babble. */
	if (csr0 & MISS)
	    priv->stats.rx_errors++;	/* Missed a Rx frame. */
	if (csr0 & MERR) {
	    printk("%s: Bus master arbitration failure, status %4.4x.\n",
		   dev->name, csr0);
	    /* Restart the chip. */
	    board->Lance.RDP = STRT;
	}
    }

    /* Clear any other interrupt, and set interrupt enable. */
    board->Lance.RAP = CSR0;	/* PCnet-ISA Controller Status */
    board->Lance.RDP = INEA|BABL|CERR|MISS|MERR|IDON;

#if 0
    if (ariadne_debug > 4)
	printk("%s: exiting interrupt, csr%d=%#4.4x.\n", dev->name,
	       board->Lance.RAP, board->Lance.RDP);
#endif

    dev->interrupt = 0;
    return;
}


static int ariadne_start_xmit(struct sk_buff *skb, struct device *dev)
{
    struct ariadne_private *priv = (struct ariadne_private *)dev->priv;
    struct AriadneBoard *board = priv->board;
    int entry;
    unsigned long flags;

    /* Transmitter timeout, serious problems. */
    if (dev->tbusy) {
	int tickssofar = jiffies - dev->trans_start;
	if (tickssofar < 20)
	    return(1);
	board->Lance.RAP = CSR0;	/* PCnet-ISA Controller Status */
	printk("%s: transmit timed out, status %4.4x, resetting.\n", dev->name,
	       board->Lance.RDP);
	board->Lance.RDP = STOP;
	priv->stats.tx_errors++;
#ifndef final_version
	{
	    int i;
	    printk(" Ring data dump: dirty_tx %d cur_tx %d%s cur_rx %d.",
		   priv->dirty_tx, priv->cur_tx, priv->tx_full ? " (full)" : "",
		   priv->cur_rx);
	    for (i = 0 ; i < RX_RING_SIZE; i++)
		printk("%s %08x %04x %04x", i & 0x3 ? "" : "\n ",
		       (swapw((priv->rx_ring[i]->RMD1))<<16)|swapw(priv->rx_ring[i]->RMD0),
		       swapw(-priv->rx_ring[i]->RMD2), swapw(priv->rx_ring[i]->RMD3));
	    for (i = 0 ; i < TX_RING_SIZE; i++)
		printk("%s %08x %04x %04x", i & 0x3 ? "" : "\n ",
		       (swapw((priv->tx_ring[i]->TMD1))<<16)|swapw(priv->tx_ring[i]->TMD0),
		       swapw(-priv->tx_ring[i]->TMD2), priv->tx_ring[i]->TMD3);
	    printk("\n");
	}
#endif
	ariadne_init_ring(dev);
	board->Lance.RDP = INEA|STRT;

	dev->tbusy = 0;
	dev->trans_start = jiffies;

	return(0);
    }

#if 0
    if (ariadne_debug > 3) {
	board->Lance.RAP = CSR0;	/* PCnet-ISA Controller Status */
	printk("%s: ariadne_start_xmit() called, csr0 %4.4x.\n", dev->name,
	       board->Lance.RDP);
	board->Lance.RDP = 0x0000;
    }
#endif

    /* Block a timer-based transmit from overlapping.  This could better be
	done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
    if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
	printk("%s: Transmitter access conflict.\n", dev->name);
	return(1);
    }

    if (test_and_set_bit(0, (void*)&priv->lock) != 0) {
	if (ariadne_debug > 0)
	    printk("%s: tx queue lock!.\n", dev->name);
	/* don't clear dev->tbusy flag. */
	return(1);
    }

    /* Fill in a Tx ring entry */

#if 0
    printk("TX pkt type 0x%04x from ", ((u_short *)skb->data)[6]);
    {
	int i;
	u_char *ptr = &((u_char *)skb->data)[6];
	for (i = 0; i < 6; i++)
	    printk("%02x", ptr[i]);
    }
    printk(" to ");
    {
	int i;
	u_char *ptr = (u_char *)skb->data;
	for (i = 0; i < 6; i++)
	    printk("%02x", ptr[i]);
    }
    printk(" data 0x%08x len %d\n", (int)skb->data, (int)skb->len);
#endif

    save_flags(flags);
    cli();

    entry = priv->cur_tx % TX_RING_SIZE;

    /* Caution: the write order is important here, set the base address with
		the "ownership" bits last. */

    priv->tx_ring[entry]->TMD2 = swapw((u_short)-skb->len);
    priv->tx_ring[entry]->TMD3 = 0x0000;
    memcpyw(priv->tx_buff[entry], (u_short *)skb->data, skb->len);

#if 0
    {
	int i, len;

	len = skb->len > 64 ? 64 : skb->len;
	len >>= 1;
	for (i = 0; i < len; i += 8) {
	    int j;
	    printk("%04x:", i);
	    for (j = 0; (j < 8) && ((i+j) < len); j++) {
		if (!(j & 1))
		    printk(" ");
		printk("%04x", priv->tx_buff[entry][i+j]);
	    }
	    printk("\n");
	}
    }
#endif

    priv->tx_ring[entry]->TMD1 = (priv->tx_ring[entry]->TMD1&0xff00)|TF_OWN|TF_STP|TF_ENP;

    dev_kfree_skb(skb);

    priv->cur_tx++;
    if ((priv->cur_tx >= TX_RING_SIZE) && (priv->dirty_tx >= TX_RING_SIZE)) {

#if 0
	printk("*** Subtracting TX_RING_SIZE from cur_tx (%d) and dirty_tx (%d)\n",
	       priv->cur_tx, priv->dirty_tx);
#endif

	priv->cur_tx -= TX_RING_SIZE;
	priv->dirty_tx -= TX_RING_SIZE;
    }

    /* Trigger an immediate send poll. */
    board->Lance.RAP = CSR0;	/* PCnet-ISA Controller Status */
    board->Lance.RDP = INEA|TDMD;

    dev->trans_start = jiffies;

    priv->lock = 0;
    if (lowb(priv->tx_ring[(entry+1) % TX_RING_SIZE]->TMD1) == 0)
	dev->tbusy = 0;
    else
	priv->tx_full = 1;
    restore_flags(flags);

    return(0);
}


static int ariadne_rx(struct device *dev)
{
    struct ariadne_private *priv = (struct ariadne_private *)dev->priv;
    int entry = priv->cur_rx % RX_RING_SIZE;
    int i;

    /* If we own the next entry, it's a new packet. Send it up. */
    while (!(lowb(priv->rx_ring[entry]->RMD1) & RF_OWN)) {
	int status = lowb(priv->rx_ring[entry]->RMD1);

	if (status != (RF_STP|RF_ENP)) {	/* There was an error. */
	    /* There is a tricky error noted by John Murphy,
		<murf@perftech.com> to Russ Nelson: Even with full-sized
		buffers it's possible for a jabber packet to use two
		buffers, with only the last correctly noting the error. */
	    if (status & RF_ENP)
		/* Only count a general error at the end of a packet.*/
		priv->stats.rx_errors++;
	    if (status & RF_FRAM)
		priv->stats.rx_frame_errors++;
	    if (status & RF_OFLO)
		priv->stats.rx_over_errors++;
	    if (status & RF_CRC)
		priv->stats.rx_crc_errors++;
	    if (status & RF_BUFF)
		priv->stats.rx_fifo_errors++;
	    priv->rx_ring[entry]->RMD1 &= 0xff00|RF_STP|RF_ENP;
	} else {
	    /* Malloc up new buffer, compatible with net-3. */
	    short pkt_len = swapw(priv->rx_ring[entry]->RMD3);
	    struct sk_buff *skb;

	    skb = dev_alloc_skb(pkt_len+2);
	    if (skb == NULL) {
		printk("%s: Memory squeeze, deferring packet.\n", dev->name);
		for (i = 0; i < RX_RING_SIZE; i++)
		    if (lowb(priv->rx_ring[(entry+i) % RX_RING_SIZE]->RMD1) & RF_OWN)
			break;

		if (i > RX_RING_SIZE-2) {
		    priv->stats.rx_dropped++;
		    priv->rx_ring[entry]->RMD1 |= RF_OWN;
		    priv->cur_rx++;
		}
		break;
	    }


	    skb->dev = dev;
	    skb_reserve(skb,2);		/* 16 byte align */
	    skb_put(skb,pkt_len);	/* Make room */
	    eth_copy_and_sum(skb, (char *)priv->rx_buff[entry], pkt_len,0);
	    skb->protocol=eth_type_trans(skb,dev);
#if 0
	    printk("RX pkt type 0x%04x from ", ((u_short *)skb->data)[6]);
	    {
		int i;
		u_char *ptr = &((u_char *)skb->data)[6];
		for (i = 0; i < 6; i++)
		    printk("%02x", ptr[i]);
	    }
	    printk(" to ");
	    {
		int i;
		u_char *ptr = (u_char *)skb->data;
		for (i = 0; i < 6; i++)
		    printk("%02x", ptr[i]);
	    }
	    printk(" data 0x%08x len %d\n", (int)skb->data, (int)skb->len);
#endif

	    netif_rx(skb);
	    priv->stats.rx_packets++;
	}

	priv->rx_ring[entry]->RMD1 |= RF_OWN;
	entry = (++priv->cur_rx) % RX_RING_SIZE;
    }

    priv->cur_rx = priv->cur_rx % RX_RING_SIZE;

    /* We should check that at least two ring entries are free.	 If not,
       we should free one and mark stats->rx_dropped++. */

    return(0);
}


static struct net_device_stats *ariadne_get_stats(struct device *dev)
{
    struct ariadne_private *priv = (struct ariadne_private *)dev->priv;
    struct AriadneBoard *board = priv->board;
    short saved_addr;
    unsigned long flags;

    save_flags(flags);
    cli();
    saved_addr = board->Lance.RAP;
    board->Lance.RAP = CSR112;	/* Missed Frame Count */
    priv->stats.rx_missed_errors = swapw(board->Lance.RDP);
    board->Lance.RAP = saved_addr;
    restore_flags(flags);

    return(&priv->stats);
}


/* Set or clear the multicast filter for this adaptor.
    num_addrs == -1	Promiscuous mode, receive all packets
    num_addrs == 0	Normal mode, clear multicast list
    num_addrs > 0	Multicast mode, receive normal and MC packets, and do
			best-effort filtering.
 */
static void set_multicast_list(struct device *dev)
{
    struct ariadne_private *priv = (struct ariadne_private *)dev->priv;
    struct AriadneBoard *board = priv->board;

    /* We take the simple way out and always enable promiscuous mode. */
    board->Lance.RAP = CSR0;	/* PCnet-ISA Controller Status */
    board->Lance.RDP = STOP;	/* Temporarily stop the lance. */

    if (dev->flags & IFF_PROMISC) {
	/* Log any net taps. */
	printk("%s: Promiscuous mode enabled.\n", dev->name);
	board->Lance.RAP = CSR15;	/* Mode Register */
	board->Lance.RDP = PROM;	/* Set promiscuous mode */
    } else {
	short multicast_table[4];
	int num_addrs = dev->mc_count;
	int i;
	/* We don't use the multicast table, but rely on upper-layer filtering. */
	memset(multicast_table, (num_addrs == 0) ? 0 : -1,
	       sizeof(multicast_table));
	for (i = 0; i < 4; i++) {
	    board->Lance.RAP = CSR8+(i<<8);	/* Logical Address Filter */
	    board->Lance.RDP = swapw(multicast_table[i]);
	}
	board->Lance.RAP = CSR15;	/* Mode Register */
	board->Lance.RDP = 0x0000;	/* Unset promiscuous mode */
    }

    board->Lance.RAP = CSR0;		/* PCnet-ISA Controller Status */
    board->Lance.RDP = INEA|STRT|IDON;	/* Resume normal operation. */
}


#ifdef MODULE
static char devicename[9] = { 0, };

static struct device ariadne_dev =
{
    devicename,				/* filled in by register_netdev() */
    0, 0, 0, 0,				/* memory */
    0, 0,				/* base, irq */
    0, 0, 0, NULL, ariadne_probe,
};

int init_module(void)
{
    int err;

    if ((err = register_netdev(&ariadne_dev))) {
	if (err == -EIO)
	    printk("No Ariadne board found. Module not loaded.\n");
	return(err);
    }
    return(0);
}

void cleanup_module(void)
{
    struct ariadne_private *priv = (struct ariadne_private *)ariadne_dev.priv;

    unregister_netdev(&ariadne_dev);
    zorro_unconfig_board(priv->key, 0);
    kfree(priv);
}

#endif /* MODULE */
