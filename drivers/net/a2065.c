/*
 * Amiga Linux/68k A2065 Ethernet Driver
 *
 * (C) Copyright 1995 by Geert Uytterhoeven
 *                      (Geert.Uytterhoeven@cs.kuleuven.ac.be)
 *
 * Fixes and tips by:
 *	- Janos Farkas (CHEXUM@sparta.banki.hu)
 *	- Jes Degn Soerensen (jds@kom.auc.dk)
 *
 * ----------------------------------------------------------------------------
 *
 * This program is based on
 *
 *	ariadne.?:	Amiga Linux/68k Ariadne Ethernet Driver
 *			(C) Copyright 1995 by Geert Uytterhoeven,
 *                                            Peter De Schrijver
 *
 *	lance.c:	An AMD LANCE ethernet driver for linux.
 *			Written 1993-94 by Donald Becker.
 *
 *	Am79C960:	PCnet(tm)-ISA Single-Chip Ethernet Controller
 *			Advanced Micro Devices
 *			Publication #16907, Rev. B, Amendment/0, May 1994
 *
 * ----------------------------------------------------------------------------
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 *
 * ----------------------------------------------------------------------------
 *
 * The A2065 is a Zorro-II board made by Commodore/Ameristar. It contains:
 *
 *	- an Am7990 Local Area Network Controller for Ethernet (LANCE) with
 *	  both 10BASE-2 (thin coax) and AUI (DB-15) connectors
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
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <asm/bootinfo.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <asm/zorro.h>

#include "a2065.h"

#ifdef A2065_DEBUG
int a2065_debug = A2065_DEBUG;
#else
int a2065_debug = 1;
#endif


	/*
	 *		Transmit/Receive Ring Definitions
	 */

#define LANCE_LOG_TX_BUFFERS	(2)
#define LANCE_LOG_RX_BUFFERS	(4)

#define TX_RING_SIZE		(1<<LANCE_LOG_TX_BUFFERS)
#define RX_RING_SIZE		(1<<LANCE_LOG_RX_BUFFERS)

#define TX_RING_MOD_MASK	(TX_RING_SIZE-1)
#define RX_RING_MOD_MASK	(RX_RING_SIZE-1)

#define PKT_BUF_SIZE		(1520)


	/*
	 *		Private Device Data
	 */

struct a2065_private {
	struct A2065Board *board;
	struct TDRE *tx_ring[TX_RING_SIZE];
	struct RDRE *rx_ring[RX_RING_SIZE];
	u_char *tx_buff[TX_RING_SIZE];
	u_char *rx_buff[RX_RING_SIZE];
	int cur_tx, cur_rx;		/* The next free ring entry */
	int dirty_tx;			/* The ring entries to be free()ed. */
	struct enet_statistics stats;
	char tx_full;
	unsigned long lock;
	int key;
};


	/*
	 *		Structure Created in the A2065's RAM Buffer
	 */

struct lancedata {
	struct InitBlock init;
	struct TDRE tx_ring[TX_RING_SIZE];
	struct RDRE rx_ring[RX_RING_SIZE];
	u_char tx_buff[TX_RING_SIZE][PKT_BUF_SIZE];
	u_char rx_buff[RX_RING_SIZE][PKT_BUF_SIZE];
};


static int a2065_open(struct device *dev);
static void a2065_init_ring(struct device *dev);
static int a2065_start_xmit(struct sk_buff *skb, struct device *dev);
static int a2065_rx(struct device *dev);
static void a2065_interrupt(int irq, struct pt_regs *fp, void *data);
static int a2065_close(struct device *dev);
static struct enet_statistics *a2065_get_stats(struct device *dev);
static void set_multicast_list(struct device *dev);


int a2065_probe(struct device *dev)
{
	int key1, key2;
	struct ConfigDev *cd;
	u_long board;
	u_long sn;
	struct a2065_private *priv;

	if ((key1 = zorro_find(MANUF_COMMODORE, PROD_A2065, 0, 0)) ||
	    (key2 = zorro_find(MANUF_AMERISTAR, PROD_AMERISTAR2065, 0, 0))) {
		cd = zorro_get_board(key1 ? key1 : key2);
		if ((board = (u_long)cd->cd_BoardAddr)) {
			sn = cd->cd_Rom.er_SerialNumber;
			if (key1) {			/* Commodore */
				dev->dev_addr[0] = 0x00;
				dev->dev_addr[1] = 0x80;
				dev->dev_addr[2] = 0x10;
			} else {			/* Ameristar */
				dev->dev_addr[0] = 0x00;
				dev->dev_addr[1] = 0x00;
				dev->dev_addr[2] = 0x9f;
			}
			dev->dev_addr[3] = (sn>>16) & 0xff;
			dev->dev_addr[4] = (sn>>8) & 0xff;
			dev->dev_addr[5] = sn & 0xff;
			printk("%s: A2065 at 0x%08lx, Ethernet Address %02x:%02x:%02x:%02x:%02x:%02x\n",
			       dev->name, board, dev->dev_addr[0],
			       dev->dev_addr[1], dev->dev_addr[2],
			       dev->dev_addr[3], dev->dev_addr[4],
			       dev->dev_addr[5]);

			init_etherdev(dev, 0);

			dev->priv = kmalloc(sizeof(struct
						   a2065_private),
					    GFP_KERNEL);
			priv = (struct a2065_private *)dev->priv;
			memset(priv, 0, sizeof(struct a2065_private));

			priv->board = (struct A2065Board *)ZTWO_VADDR(board);
			priv->key = key1 ? key1 : key2;

			dev->open = &a2065_open;
			dev->stop = &a2065_close;
			dev->hard_start_xmit = &a2065_start_xmit;
			dev->get_stats = &a2065_get_stats;
			dev->set_multicast_list = &set_multicast_list;

			zorro_config_board(key1 ? key1 : key2, 0);
			return(0);
		}
	}
	return(ENODEV);
}


static int a2065_open(struct device *dev)
{
	struct a2065_private *priv = (struct a2065_private *)dev->priv;
	struct A2065Board *board = priv->board;
	struct lancedata *lancedata;		/* LANCE point of view */
	struct lancedata *alancedata;		/* Amiga point of view */

	lancedata = (struct lancedata *)offsetof(struct A2065Board, RAM);
	alancedata = (struct lancedata *)board->RAM;

	/* Stop the LANCE */
	board->Lance.RAP = CSR0;		/* LANCE Controller Status */
	board->Lance.RDP = STOP;

	/* Enable big endian byte ordering */
	board->Lance.RAP = CSR3;		/* CSR3 */
	board->Lance.RDP = BSWP;

	/* Set the Init Block Pointer */
	board->Lance.RAP = CSR1;		/* IADR[15:0] */
	board->Lance.RDP = (u_long)&lancedata->init;
	board->Lance.RAP = CSR2;		/* IADR[23:16] */
	board->Lance.RDP = 0x0000;

	/* Set the Mode */
	alancedata->init.Mode = 0;

	/* Set the Ethernet Hardware Address */
	                    /* Physical Address Register */
	alancedata->init.PADR[0] = dev->dev_addr[1];
	alancedata->init.PADR[1] = dev->dev_addr[0];
	alancedata->init.PADR[2] = dev->dev_addr[3];
	alancedata->init.PADR[3] = dev->dev_addr[2];
	alancedata->init.PADR[4] = dev->dev_addr[5];
	alancedata->init.PADR[5] = dev->dev_addr[4];

	/* Set the Multicast Table */
	                    /* Logical Address Filter, LADRF[31:0] */
	alancedata->init.LADRF[0] = 0x00000000;
	                    /* Logical Address Filter, LADRF[63:32] */	
	alancedata->init.LADRF[1] = 0x00000000;

	/* Set the Receive and Transmit Descriptor Ring Pointers */
	alancedata->init.RDRA = (u_long)&lancedata->rx_ring;
	alancedata->init.RLEN = LANCE_LOG_RX_BUFFERS << 13;
	alancedata->init.TDRA = (u_long)&lancedata->tx_ring;
	alancedata->init.TLEN = LANCE_LOG_TX_BUFFERS << 13;

	/* Initialise the Rings */
	a2065_init_ring(dev);


	/* Install the Interrupt handler */
	if (!add_isr(IRQ_AMIGA_PORTS, a2065_interrupt, 0, dev, "a2065 Ethernet"))
		return(-EAGAIN);

	/* Make the LANCE read the Init Block */
	board->Lance.RAP = CSR0;		/* LANCE Controller Status */
	board->Lance.RDP = INEA|INIT;

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	MOD_INC_USE_COUNT;

	return(0);
}


static void a2065_init_ring(struct device *dev)
{
	struct a2065_private *priv = (struct a2065_private *)dev->priv;
	struct A2065Board *board = priv->board;
	struct lancedata *lancedata;		/* LANCE point of view */
	struct lancedata *alancedata;		/* Amiga point of view */
	int i;

	priv->lock = 0, priv->tx_full = 0;
	priv->cur_rx = priv->cur_tx = 0;
	priv->dirty_tx = 0;

	lancedata = (struct lancedata *)offsetof(struct A2065Board, RAM);
	alancedata = (struct lancedata *)board->RAM;

	/* Set up TX Ring */
	for (i = 0; i < TX_RING_SIZE; i++) {
		alancedata->tx_ring[i].TMD0 = (u_long)lancedata->tx_buff[i];
		alancedata->tx_ring[i].TMD1 = TF_STP|TF_ENP;
		alancedata->tx_ring[i].TMD2 = -PKT_BUF_SIZE;
		alancedata->tx_ring[i].TMD3 = 0x0000;
		priv->tx_ring[i] = &alancedata->tx_ring[i];
		priv->tx_buff[i] = alancedata->tx_buff[i];
#if 0
		printk("TX Entry %2d @ 0x%08x (LANCE 0x%08x), Buf @ 0x%08x (LANCE 0x%08x)\n", i,
		       (int)&alancedata->tx_ring[i],
		       (int)&lancedata->tx_ring[i],
		       (int)alancedata->tx_buff[i],
		       (int)lancedata->tx_buff[i]);
#endif
	}

	/* Set up RX Ring */
	for (i = 0; i < RX_RING_SIZE; i++) {
		alancedata->rx_ring[i].RMD0 = (u_long)lancedata->rx_buff[i];
		alancedata->rx_ring[i].RMD1 = RF_OWN;
		alancedata->rx_ring[i].RMD2 = -PKT_BUF_SIZE;
		alancedata->rx_ring[i].RMD3 = 0x0000;
		priv->rx_ring[i] = &alancedata->rx_ring[i];
		priv->rx_buff[i] = alancedata->rx_buff[i];
#if 0
		printk("RX Entry %2d @ 0x%08x (LANCE 0x%08x), Buf @ 0x%08x (LANCE 0x%08x)\n", i,
		       (int)&alancedata->rx_ring[i],
		       (int)&lancedata->rx_ring[i],
		       (int)alancedata->rx_buff[i],
		       (int)lancedata->rx_buff[i]);
#endif
	}
}


static int a2065_close(struct device *dev)
{
	struct a2065_private *priv = (struct a2065_private *)dev->priv;
	struct A2065Board *board = priv->board;

	dev->start = 0;
	dev->tbusy = 1;

	board->Lance.RAP = CSR0;		/* LANCE Controller Status */

	if (a2065_debug > 1) {
	  printk("%s: Shutting down ethercard, status was %2.2x.\n",
		 dev->name, board->Lance.RDP);
	  printk("%s: %d packets missed\n", dev->name,
		 priv->stats.rx_missed_errors);
	}

	/* We stop the LANCE here - it occasionally polls memory if we don't */
	board->Lance.RDP = STOP;

	remove_isr(IRQ_AMIGA_PORTS, a2065_interrupt, dev);

	MOD_DEC_USE_COUNT;

	return(0);
}


static void a2065_interrupt(int irq, struct pt_regs *fp, void *data)
{
	struct device *dev = (struct device *)data;
	struct a2065_private *priv;
	struct A2065Board *board;
	int csr0, boguscnt = 10;

	if (dev == NULL) {
		printk("a2065_interrupt(): irq for unknown device.\n");
		return;
	}

	priv = (struct a2065_private *)dev->priv;
	board = priv->board;

	board->Lance.RAP = CSR0;	/* LANCE Controller Status */

	if (!(board->Lance.RDP & INTR)) /* Check if any interrupt has
					   been generated by the board. */
		return;

	if (dev->interrupt)
		printk("%s: Re-entering the interrupt handler.\n", dev->name);

	dev->interrupt = 1;

	while ((csr0 = board->Lance.RDP) & (ERR|RINT|TINT) && --boguscnt >= 0){
		/* Acknowledge all of the current interrupt sources ASAP. */
		board->Lance.RDP = csr0 & ~(INEA|TDMD|STOP|STRT|INIT);

#if 0
		if (a2065_debug > 5) {
		  printk("%s: interrupt  csr0=%#2.2x new csr=%#2.2x.",
			 dev->name, csr0, board->Lance.RDP);
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

		if (csr0 & RINT)			/* Rx interrupt */
			a2065_rx(dev);

		if (csr0 & TINT) {			/* Tx-done interrupt */
			int dirty_tx = priv->dirty_tx;

			while (dirty_tx < priv->cur_tx) {
				int entry = dirty_tx % TX_RING_SIZE;
				int status =
				  priv->tx_ring[entry]->TMD1 & 0xff00;

				if (status & TF_OWN)
					break;	/* It still hasn't been Txed */

				priv->tx_ring[entry]->TMD1 &= 0x00ff;

				if (status & TF_ERR) {
					/* There was an major error, log it. */
					int err_status =
					  priv->tx_ring[entry]->TMD3;
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
					  printk("%s: Tx FIFO error! Status %4.4x.\n", dev->name, csr0);
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

			if (priv->tx_full && dev->tbusy && dirty_tx >
			    priv->cur_tx - TX_RING_SIZE + 2) {
				/* The ring is no longer full, clear tbusy. */
				priv->tx_full = 0;
				dev->tbusy = 0;
				mark_bh(NET_BH);
			}

			priv->dirty_tx = dirty_tx;
		}

		/* Log misc errors. */
		if (csr0 & BABL)
			priv->stats.tx_errors++;       /* Tx babble. */
		if (csr0 & MISS)
			priv->stats.rx_errors++;       /* Missed a Rx frame. */
		if (csr0 & MERR) {
			printk("%s: Bus master arbitration failure, status %4.4x.\n", dev->name, csr0);
			/* Restart the chip. */
			board->Lance.RDP = STRT;
		}
	}

	/* Clear any other interrupt, and set interrupt enable. */
	board->Lance.RAP = CSR0;		/* LANCE Controller Status */
	board->Lance.RDP = INEA|BABL|CERR|MISS|MERR|IDON;

#if 0
	if (a2065_debug > 4)
		printk("%s: exiting interrupt, csr%d=%#4.4x.\n",
		       dev->name, board->Lance.RAP, board->Lance.RDP);
#endif

	dev->interrupt = 0;
	return;
}


static int a2065_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct a2065_private *priv = (struct a2065_private *)dev->priv;
	struct A2065Board *board = priv->board;
	int entry;

	/* Transmitter timeout, serious problems. */
	if (dev->tbusy) {
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 20)
			return(1);
		board->Lance.RAP = CSR0;	/* LANCE Controller Status */
		printk("%s: transmit timed out, status %4.4x, resetting.\n", dev->name, board->Lance.RDP);
		board->Lance.RDP = STOP;

		/* Enable big endian byte ordering */
		board->Lance.RAP = CSR3;			/* CSR3 */
		board->Lance.RDP = BSWP;

		priv->stats.tx_errors++;
#ifndef final_version
		{
		  int i;
		  printk(" Ring data dump: dirty_tx %d cur_tx %d%s cur_rx %d.",
			 priv->dirty_tx, priv->cur_tx, priv->tx_full ?
			 " (full)" : "", priv->cur_rx);
		  for (i = 0 ; i < RX_RING_SIZE; i++)
		    printk("%s %08x %04x %04x", i & 0x3 ? "" : "\n ",
			   ((priv->rx_ring[i]->RMD1)<<16) |
			   priv->rx_ring[i]->RMD0,
			   -priv->rx_ring[i]->RMD2, priv->rx_ring[i]->RMD3);
		  for (i = 0 ; i < TX_RING_SIZE; i++)
		    printk("%s %08x %04x %04x", i & 0x3 ? "" : "\n ",
			   ((priv->tx_ring[i]->TMD1)<<16) |
			   priv->tx_ring[i]->TMD0,
			   -priv->tx_ring[i]->TMD2, priv->tx_ring[i]->TMD3);
			printk("\n");
		}
#endif
		a2065_init_ring(dev);
		board->Lance.RDP = INEA|INIT;

		dev->tbusy = 0;
		dev->trans_start = jiffies;

		return(0);
	}

	if (skb == NULL) {
		dev_tint(dev);
		return(0);
	}

	if (skb->len <= 0)
		return(0);

#if 0
	if (a2065_debug > 3) {
		board->Lance.RAP = CSR0;	/* LANCE Controller Status */
		printk("%s: a2065_start_xmit() called, csr0 %4.4x.\n",
		       dev->name, board->Lance.RDP);
		board->Lance.RDP = 0x0000;
	}
#endif

	/*
	 * Block a timer-based transmit from overlapping.  This could better be
	 * done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
	 */
	if (set_bit(0, (void*)&dev->tbusy) != 0) {
		printk("%s: Transmitter access conflict.\n", dev->name);
		return(1);
	}

	if (set_bit(0, (void*)&priv->lock) != 0) {
		if (a2065_debug > 0)
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

	entry = priv->cur_tx % TX_RING_SIZE;

	priv->tx_ring[entry]->TMD2 = -(ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN);
	priv->tx_ring[entry]->TMD3 = 0x0000;
	memcpy(priv->tx_buff[entry], skb->data, skb->len);

#if 0
	{
		int i, len;

		len = skb->len > 64 ? 64 : skb->len;
		for (i = 0; i < len; i += 8) {
			int j;
			printk("%02x:", i);
			for (j = 0; (j < 16) && ((i+j) < len); j++) {
				if (!(j & 1))
					printk(" ");
				printk("%02x", priv->tx_buff[entry][i+j]);
			}
			printk("\n");
		}
	}
#endif

	priv->tx_ring[entry]->TMD1 = (priv->tx_ring[entry]->TMD1 &
				      0x00ff)|TF_OWN|TF_STP|TF_ENP;

	dev_kfree_skb(skb, FREE_WRITE);

	priv->cur_tx++;
	if ((priv->cur_tx >= TX_RING_SIZE)&&(priv->dirty_tx >= TX_RING_SIZE)){

#if 0
	  printk("*** Subtracting TX_RING_SIZE from cur_tx (%d) and dirty_tx (%d)\n",
		 priv->cur_tx, priv->dirty_tx);
#endif

		priv->cur_tx -= TX_RING_SIZE;
		priv->dirty_tx -= TX_RING_SIZE;
	}

	/* Trigger an immediate send poll. */
	board->Lance.RAP = CSR0;		/* LANCE Controller Status */
	board->Lance.RDP = INEA|TDMD;

	dev->trans_start = jiffies;

	cli();
	priv->lock = 0;
	if ((priv->tx_ring[(entry+1) % TX_RING_SIZE]->TMD1 & 0xff00) == 0)
		dev->tbusy = 0;
	else
		priv->tx_full = 1;
	sti();

	return(0);
}


static int a2065_rx(struct device *dev)
{
	struct a2065_private *priv = (struct a2065_private *)dev->priv;
	int entry = priv->cur_rx % RX_RING_SIZE;
	int i;

	/* If we own the next entry, it's a new packet. Send it up. */
	while (!(priv->rx_ring[entry]->RMD1 & RF_OWN)) {
		int status = priv->rx_ring[entry]->RMD1 & 0xff00;

		if (status != (RF_STP|RF_ENP)) {      /* There was an error. */
		  /* There is a tricky error noted by John Murphy,
		     <murf@perftech.com> to Russ Nelson: Even with full-sized
		     buffers it's possible for a jabber packet to use two
		     buffers, with only the last correctly noting the error. */
			if (status & RF_ENP)
			  /* Only count a general error at the */
				priv->stats.rx_errors++; /* end of a packet.*/
			if (status & RF_FRAM)
				priv->stats.rx_frame_errors++;
			if (status & RF_OFLO)
				priv->stats.rx_over_errors++;
			if (status & RF_CRC)
				priv->stats.rx_crc_errors++;
			if (status & RF_BUFF)
				priv->stats.rx_fifo_errors++;
			priv->rx_ring[entry]->RMD1 &= 0x00ff|RF_STP|RF_ENP;
		} else {
			/* Malloc up new buffer, compatible with net-3. */
			short pkt_len = priv->rx_ring[entry]->RMD3;
			struct sk_buff *skb;

			if(pkt_len<60)
			{
				printk("%s: Runt packet!\n",dev->name);
				priv->stats.rx_errors++;
			}
			else
			{
				skb = dev_alloc_skb(pkt_len+2);
				if (skb == NULL) {
					printk("%s: Memory squeeze, deferring packet.\n", dev->name);
					for (i = 0; i < RX_RING_SIZE; i++)
						if (priv->rx_ring[(entry+i) % RX_RING_SIZE]->RMD1 & RF_OWN)
							break;

					if (i > RX_RING_SIZE-2) {
						priv->stats.rx_dropped++;
						priv->rx_ring[entry]->RMD1 |= RF_OWN;
						priv->cur_rx++;
					}
					break;
				}
				skb->dev = dev;
				skb_reserve(skb,2);	/* 16 byte align */
				skb_put(skb,pkt_len);	/* Make room */
				eth_copy_and_sum(skb,
						 priv->rx_buff[entry],
						 pkt_len,0);
				skb->protocol=eth_type_trans(skb,dev);
#if 0
				printk("RX pkt type 0x%04x from ", 
				       ((u_short *)skb->data)[6]);
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
				printk(" data 0x%08x len %d\n",
				       (int)skb->data, (int)skb->len);
#endif

				netif_rx(skb);
				priv->stats.rx_packets++;
			}
		}
		priv->rx_ring[entry]->RMD1 |= RF_OWN;
		entry = (++priv->cur_rx) % RX_RING_SIZE;
	}

	priv->cur_rx = priv->cur_rx % RX_RING_SIZE;

	/* We should check that at least two ring entries are free.
	   If not, we should free one and mark stats->rx_dropped++. */

	return(0);
}


static struct enet_statistics *a2065_get_stats(struct device *dev)
{
	struct a2065_private *priv = (struct a2065_private *)dev->priv;

	return(&priv->stats);
}


/* Set or clear the multicast filter for this adaptor.
 */
static void set_multicast_list(struct device *dev)
{
	struct a2065_private *priv = (struct a2065_private *)dev->priv;
	struct A2065Board *board = priv->board;
	struct lancedata *alancedata;	      /* Amiga point of view */
	alancedata = (struct lancedata *)board->RAM;

	/* We take the simple way out and always enable promiscuous mode. */
	board->Lance.RAP = CSR0;	      /* LANCE Controller Status */
	board->Lance.RDP = STOP;	      /* Temporarily stop the lance. */

	/* Enable big endian byte ordering */
	board->Lance.RAP = CSR3;	      /* CSR3 */
	board->Lance.RDP = BSWP;

	if (dev->flags&IFF_PROMISC) {
	  /* Log any net taps. */
	  printk("%s: Promiscuous mode enabled.\n", dev->name);
	  alancedata->init.Mode = PROM;	      /* Set promiscuous mode */
	} else {
	  short multicast_table[4];
	  int num_addrs=dev->mc_count;
	  if(dev->flags&IFF_ALLMULTI)
		num_addrs=1;
	  /*
	   * We don't use the multicast table,
	   * but rely on upper-layer filtering.
	   */
	  memset(multicast_table, (num_addrs == 0) ? 0 : -1,
		 sizeof(multicast_table));
	  alancedata->init.LADRF[0] = multicast_table[0]<<16 |
	    multicast_table[1];
	  alancedata->init.LADRF[1] = multicast_table[2]<<16 |
	    multicast_table[3];
	  alancedata->init.Mode = 0x0000;
	}

	board->Lance.RAP = CSR0;	        /* LANCE Controller Status */
	board->Lance.RDP = INEA|STRT|IDON|INIT;	/* Resume normal operation. */
}


#ifdef MODULE
static char devicename[9] = { 0, };

static struct device a2065_dev =
{
	devicename,			/* filled in by register_netdev() */
	0, 0, 0, 0,			/* memory */
	0, 0,				/* base, irq */
	0, 0, 0, NULL, a2065_probe,
};

int init_module(void)
{
	int err;

	if ((err = register_netdev(&a2065_dev))) {
		if (err == -EIO)
			printk("No A2065 board found. Module not loaded.\n");
		return(err);
	}
	return(0);
}

void cleanup_module(void)
{
	struct a2065_private *priv = (struct a2065_private *)a2065_dev.priv;

	unregister_netdev(&a2065_dev);
	zorro_unconfig_board(priv->key, 0);
	kfree(priv);
}

#endif /* MODULE */
