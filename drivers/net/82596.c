/* 82596.c: A generic 82596 ethernet driver for linux. */
/*
   Based on Apricot.c
   Written 1994 by Mark Evans.
   This driver is for the Apricot 82596 bus-master interface

   Modularised 12/94 Mark Evans


   Modified to support the 82596 ethernet chips on 680x0 VME boards.
   by Richard Hirst <richard@sleepie.demon.co.uk>
   Renamed to be 82596.c

   *** Untested on Apricot hardware, and may require some hacking
   *** to make it work.  The old 82596.c reported hasn't worked
   *** since 1.3.xx anyway.  I have been unable to find any users
   *** of Apricot hardware to test this on.

   Most of my modifications relate to the braindead big-endian
   implementation by Intel.  When the i596 is operating in
   'big-endian' mode, it thinks a 32 bit value of 0x12345678
   should be stored as 0x56781234.  This is a real pain, when
   you have linked lists which are shared by the 680x0 and the
   i596.

   Driver skeleton
   Written 1993 by Donald Becker.
   Copyright 1993 United States Government as represented by the Director,
   National Security Agency. This software may only be used and distributed
   according to the terms of the GNU Public License as modified by SRC,
   incorporated herein by reference.

   The author may be reached as becker@super.org or
   C/O Supercomputing Research Ctr., 17100 Science Dr., Bowie MD 20715

 */

static const char *version = "82596.c:v1.0 15/07/98\n";

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>

#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/pgtable.h>	/*?? */

#ifdef CONFIG_MVME16x_NET
#include <asm/mvme16xhw.h>
#endif
#ifdef CONFIG_BVME6000_NET
#include <asm/bvme6000hw.h>
#endif

/*
 * Define various macros for Channel Attention, word swapping etc., dependent
 * on architecture.  MVME and BVME are 680x0 based, otherwise it is Intel.
 */

#ifdef __mc68000__
#define WSWAPrfd(x)  ((struct i596_rfd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPrbd(x)  ((struct i596_rbd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPiscp(x) ((struct i596_iscp *)(((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPscb(x)  ((struct i596_scb *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPcmd(x)  ((struct i596_cmd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPtbd(x)  ((struct i596_tbd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPchar(x) ((char *)            (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define ISCP_BUSY	0x00010000
#define MACH_IS_APRICOT	0
#else
#define WSWAPrfd(x)	x
#define WSWAPiscp(x)	((struct i596_iscp *)(x))
#define WSWAPscb(x)	((struct i596_scb *)(x))
#define WSWAPcmd(x)	x
#define WSWAPtbd(x)	x
#define WSWAPchar(x)	x
#define ISCP_BUSY	0x0001
#define MACH_IS_APRICOT	1
#endif

/*
 * The MPU_PORT command allows direct access to the 82596. With PORT access
 * the following commands are available (p5-18). The 32-bit port command
 * must be word-swapped with the most significant word written first.
 * This only applies to VME boards.
 */
#define PORT_RESET		0x00	/* reset 82596 */
#define PORT_SELFTEST		0x01	/* selftest */
#define PORT_ALTSCP		0x02	/* alternate SCB address */
#define PORT_ALTDUMP		0x03	/* Alternate DUMP address */

#ifndef HAVE_PORTRESERVE
#define check_region(addr, size)	0
#define request_region(addr, size,name)	do ; while(0)
#endif

#ifndef HAVE_ALLOC_SKB
#define alloc_skb(size, priority) (struct sk_buff *) kmalloc(size,priority)
#define kfree_skbmem(buff, size) kfree_s(buff,size)
#endif

#define APRICOT_DEBUG 2

#ifdef APRICOT_DEBUG
int i596_debug = APRICOT_DEBUG;
#else
int i596_debug = 1;
#endif

#define I596_TOTAL_SIZE 17

#define I596_NULL -1

#define CMD_EOL		0x8000	/* The last command of the list, stop. */
#define CMD_SUSP	0x4000	/* Suspend after doing cmd. */
#define CMD_INTR	0x2000	/* Interrupt after doing cmd. */

#define CMD_FLEX	0x0008	/* Enable flexible memory model */

enum commands {
	CmdNOp = 0, CmdSASetup = 1, CmdConfigure = 2, CmdMulticastList = 3,
	CmdTx = 4, CmdTDR = 5, CmdDump = 6, CmdDiagnose = 7
};

#define STAT_C		0x8000	/* Set to 0 after execution */
#define STAT_B		0x4000	/* Command being executed */
#define STAT_OK		0x2000	/* Command executed ok */
#define STAT_A		0x1000	/* Command aborted */

#define	 CUC_START	0x0100
#define	 CUC_RESUME	0x0200
#define	 CUC_SUSPEND    0x0300
#define	 CUC_ABORT	0x0400
#define	 RX_START	0x0010
#define	 RX_RESUME	0x0020
#define	 RX_SUSPEND	0x0030
#define	 RX_ABORT	0x0040

struct i596_reg {
	unsigned short porthi;
	unsigned short portlo;
	unsigned long ca;
};

struct i596_cmd {
	unsigned short status;
	unsigned short command;
	struct i596_cmd *next;
};

#define EOF		0x8000
#define SIZE_MASK	0x3fff

struct i596_tbd {
	unsigned short size;
	unsigned short pad;
	struct i596_tbd *next;
	char *data;
};

struct tx_cmd {
	struct i596_cmd cmd;
	struct i596_tbd *tbd;
	unsigned short size;
	unsigned short pad;
	struct sk_buff *skb;	/* So we can free it after tx */
};

struct i596_rfd {
	unsigned short stat;
	unsigned short cmd;
	struct i596_rfd *next;
	long rbd;
	unsigned short count;
	unsigned short size;
	char data[1532];
};

#define RX_RING_SIZE 16

struct i596_scb {
	unsigned short status;
	unsigned short command;
	struct i596_cmd *cmd;
	struct i596_rfd *rfd;
	unsigned long crc_err;
	unsigned long align_err;
	unsigned long resource_err;
	unsigned long over_err;
	unsigned long rcvdt_err;
	unsigned long short_err;
	unsigned short t_on;
	unsigned short t_off;
};

struct i596_iscp {
	unsigned long stat;
	struct i596_scb *scb;
};

struct i596_scp {
	unsigned long sysbus;
	unsigned long pad;
	struct i596_iscp *iscp;
};

struct i596_private {
	volatile struct i596_scp scp;
	volatile struct i596_iscp iscp;
	volatile struct i596_scb scb;
	struct i596_cmd set_add;
	char eth_addr[8];
	struct i596_cmd set_conf;
	char i596_config[16];
	struct i596_cmd tdr;
	unsigned long stat;
	int last_restart __attribute__((aligned(4)));
	struct i596_rfd *rx_tail;
	struct i596_cmd *cmd_tail;
	struct i596_cmd *cmd_head;
	int cmd_backlog;
	unsigned long last_cmd;
	struct net_device_stats stats;
};

char init_setup[] =
{
	0x8E,			/* length, prefetch on */
	0xC8,			/* fifo to 8, monitor off */
#ifdef CONFIG_VME
	0xc0,			/* don't save bad frames */
#else
	0x80,			/* don't save bad frames */
#endif
	0x2E,			/* No source address insertion, 8 byte preamble */
	0x00,			/* priority and backoff defaults */
	0x60,			/* interframe spacing */
	0x00,			/* slot time LSB */
	0xf2,			/* slot time and retries */
	0x00,			/* promiscuous mode */
	0x00,			/* collision detect */
	0x40,			/* minimum frame length */
	0xff,
	0x00,
	0x7f /*  *multi IA */ };

static int i596_open(struct device *dev);
static int i596_start_xmit(struct sk_buff *skb, struct device *dev);
static void i596_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int i596_close(struct device *dev);
static struct net_device_stats *i596_get_stats(struct device *dev);
static void i596_add_cmd(struct device *dev, struct i596_cmd *cmd);
static void print_eth(char *);
static void set_multicast_list(struct device *dev);

static int ticks_limit = 25;
static int max_cmd_backlog = 16;


static inline void CA(struct device *dev)
{
#ifdef CONFIG_MVME16x_NET
	if (MACH_IS_MVME16x) {
		((struct i596_reg *) dev->base_addr)->ca = 1;
	}
#endif
#ifdef CONFIG_BVME6000_NET
	if (MACH_IS_BVME6000) {
		volatile u32 i = *(volatile u32 *) (dev->base_addr);
	}
#endif
#ifdef CONFIG_APRICOT_i596
	if (MACH_IS_APRICOT) {
		outw(0, (short) (dev->base_addr) + 4);
	}
#endif
}


static inline void MPU_PORT(struct device *dev, int c, volatile void *x)
{
#ifdef CONFIG_MVME16x_NET
	if (MACH_IS_MVME16x) {
		struct i596_reg *p = (struct i596_reg *) (dev->base_addr);
		p->porthi = ((c) | (u32) (x)) & 0xffff;
		p->portlo = ((c) | (u32) (x)) >> 16;
	}
#endif
#ifdef CONFIG_BVME6000_NET
	if (MACH_IS_BVME6000) {
		u32 v = (u32) (c) | (u32) (x);
		v = ((u32) (v) << 16) | ((u32) (v) >> 16);
		*(volatile u32 *) dev->base_addr = v;
		udelay(1);
		*(volatile u32 *) dev->base_addr = v;
	}
#endif
}


#if defined(CONFIG_MVME16x_NET) || defined(CONFIG_BVME6000_NET)
static void i596_error(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = dev_id;
	struct i596_cmd *cmd;

	struct i596_private *lp = (struct i596_private *) dev->priv;
	printk("i596_error: lp = 0x%08x\n", (u32) lp);
	printk("scp at %08x, .sysbus = %08x, .iscp = %08x\n",
	       (u32) & lp->scp, (u32) lp->scp.sysbus, (u32) lp->scp.iscp);
	printk("iscp at %08x, .stat = %08x, .scb = %08x\n",
	       (u32) & lp->iscp, (u32) lp->iscp.stat, (u32) lp->iscp.scb);
	printk("scb at %08x, .status = %04x, .command = %04x\n",
	       (u32) & lp->scb, lp->scb.status, lp->scb.command);
	printk("   .cmd = %08x, .rfd = %08x\n", (u32) lp->scb.cmd,
	       (u32) lp->scb.rfd);
	cmd = WSWAPcmd(lp->scb.cmd);
	while (cmd && (u32) cmd < 0x1000000) {
		printk("cmd at %08x, .status = %04x, .command = %04x, .next = %08x\n",
		  (u32) cmd, cmd->status, cmd->command, (u32) cmd->next);
		cmd = WSWAPcmd(cmd->next);
	}
	while (1);
}
#endif

static inline int init_rx_bufs(struct device *dev, int num)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	int i;
	struct i596_rfd *rfd;

	lp->scb.rfd = (struct i596_rfd *) I596_NULL;

	if (i596_debug > 1)
		printk("%s: init_rx_bufs %d.\n", dev->name, num);

	for (i = 0; i < num; i++) {
		if (!(rfd = (struct i596_rfd *) kmalloc(sizeof(struct i596_rfd), GFP_KERNEL)))
			 break;

		rfd->stat = 0x0000;
		rfd->rbd = I596_NULL;
		rfd->count = 0;
		rfd->size = 1532;
		if (i == 0) {
			rfd->cmd = CMD_EOL;
			lp->rx_tail = rfd;
		} else
			rfd->cmd = 0x0000;

		rfd->next = lp->scb.rfd;
		lp->scb.rfd = WSWAPrfd(rfd);
	}

	if (i != 0)
		lp->rx_tail->next = lp->scb.rfd;

	return (i);
}

static inline void remove_rx_bufs(struct device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	struct i596_rfd *rfd = WSWAPrfd(lp->scb.rfd);

	lp->rx_tail->next = (struct i596_rfd *) I596_NULL;

	do {
		lp->scb.rfd = rfd->next;
		kfree(rfd);
		rfd = WSWAPrfd(lp->scb.rfd);
	}
	while (rfd != lp->rx_tail);
}

static inline void init_i596_mem(struct device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
#if !defined(CONFIG_MVME16x_NET) && !defined(CONFIG_BVME6000_NET)
	short ioaddr = dev->base_addr;
#endif
	int boguscnt = 100000;
	unsigned long flags;

#if defined(CONFIG_MVME16x_NET) || defined(CONFIG_BVME6000_NET)
#ifdef CONFIG_MVME16x_NET
	if (MACH_IS_MVME16x) {
		volatile unsigned char *pcc2 = (unsigned char *) 0xfff42000;

		/* Disable all ints for now */
		pcc2[0x28] = 1;
		pcc2[0x2a] = 0x40;
		pcc2[0x2b] = 0x40;	/* Set snooping bits now! */
	}
#endif
#ifdef CONFIG_BVME6000_NET
	if (MACH_IS_BVME6000) {
		volatile unsigned char *ethirq = (unsigned char *) BVME_ETHIRQ_REG;

		*ethirq = 1;
	}
#endif

	MPU_PORT(dev, PORT_RESET, 0);

	udelay(100);		/* Wait 100us - seems to help */

	/* change the scp address */

	MPU_PORT(dev, PORT_ALTSCP, &lp->scp);

#else

	/* change the scp address */
	outw(0, ioaddr);
	outw(0, ioaddr);
	outb(4, ioaddr + 0xf);
	outw(((((int) &lp->scp) & 0xffff) | 2), ioaddr);
	outw((((int) &lp->scp) >> 16) & 0xffff, ioaddr);
#endif

	lp->last_cmd = jiffies;

#ifdef CONFIG_MVME16x_NET
	if (MACH_IS_MVME16x)
		lp->scp.sysbus = 0x00000054;
#endif
#ifdef CONFIG_BVME6000_NET
	if (MACH_IS_BVME6000)
		lp->scp.sysbus = 0x0000004c;
#endif
#ifdef CONFIG_APRICOT_i596
	if (MACH_IS_APRICOT)
		lp->scp.sysbus = 0x00440000;
#endif

	lp->scp.iscp = WSWAPiscp(&(lp->iscp));
	lp->iscp.scb = WSWAPscb(&(lp->scb));
	lp->iscp.stat = ISCP_BUSY;
	lp->cmd_backlog = 0;

	lp->cmd_head = lp->scb.cmd = (struct i596_cmd *) I596_NULL;

	if (i596_debug > 1)
		printk("%s: starting i82596.\n", dev->name);

#if !defined(CONFIG_MVME16x_NET) && !defined(CONFIG_BVME6000_NET)
	(void) inb(ioaddr + 0x10);
	outb(4, ioaddr + 0xf);
#endif
	CA(dev);

	while (lp->iscp.stat)
		if (--boguscnt == 0) {
			printk("%s: i82596 initialization timed out with status %4.4x, cmd %4.4x.\n",
			     dev->name, lp->scb.status, lp->scb.command);
			break;
		}
	lp->scb.command = 0;

#ifdef CONFIG_MVME16x_NET
	if (MACH_IS_MVME16x) {
		volatile unsigned char *pcc2 = (unsigned char *) 0xfff42000;

		/* Enable ints, etc. now */
		pcc2[0x2a] = 0x08;
		pcc2[0x2a] = 0x55;	/* Edge sensitive */
		pcc2[0x2b] = 0x55;
	}
#endif
#ifdef CONFIG_BVME6000_NET
	if (MACH_IS_BVME6000) {
		volatile unsigned char *ethirq = (unsigned char *) BVME_ETHIRQ_REG;

		*ethirq = 3;
	}
#endif

	memcpy(lp->i596_config, init_setup, 14);
	lp->set_conf.command = CmdConfigure;
	i596_add_cmd(dev, &lp->set_conf);

	memcpy(lp->eth_addr, dev->dev_addr, 6);
	lp->set_add.command = CmdSASetup;
	i596_add_cmd(dev, &lp->set_add);

	lp->tdr.command = CmdTDR;
	i596_add_cmd(dev, &lp->tdr);

	boguscnt = 200000;

	save_flags(flags);
	cli();

	while (lp->scb.command)
		if (--boguscnt == 0) {
			printk("%s: receive unit start timed out with status %4.4x, cmd %4.4x.\n",
			     dev->name, lp->scb.status, lp->scb.command);
			break;
		}
	lp->scb.command = RX_START;
	CA(dev);

	restore_flags(flags);

	boguscnt = 2000;
	while (lp->scb.command)
		if (--boguscnt == 0) {
			printk("i82596 init timed out with status %4.4x, cmd %4.4x.\n",
			       lp->scb.status, lp->scb.command);
			break;
		}
	return;
}

static inline int i596_rx(struct device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	struct i596_rfd *rfd;
	int frames = 0;

	if (i596_debug > 3)
		printk("i596_rx()\n");

	rfd = WSWAPrfd(lp->scb.rfd);	/* Reference next frame descriptor to check */

	while ((rfd->stat) & STAT_C) {	/* Loop while we have complete frames */
		if (i596_debug > 2)
			print_eth(rfd->data);

		if ((rfd->stat) & STAT_OK) {
			/* a good frame */
			int pkt_len = rfd->count & 0x3fff;
			struct sk_buff *skb = dev_alloc_skb(pkt_len);

			frames++;

			if (skb == NULL) {
				printk("%s: i596_rx Memory squeeze, dropping packet.\n", dev->name);
				lp->stats.rx_dropped++;
			} else {
				skb->dev = dev;
				memcpy(skb_put(skb, pkt_len), rfd->data, pkt_len);
				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);
				lp->stats.rx_packets++;
				lp->stats.rx_bytes += pkt_len;
			}
		} else {
			lp->stats.rx_errors++;
			if ((rfd->stat) & 0x0001)
				lp->stats.collisions++;
			if ((rfd->stat) & 0x0080)
				lp->stats.rx_length_errors++;
			if ((rfd->stat) & 0x0100)
				lp->stats.rx_over_errors++;
			if ((rfd->stat) & 0x0200)
				lp->stats.rx_fifo_errors++;
			if ((rfd->stat) & 0x0400)
				lp->stats.rx_frame_errors++;
			if ((rfd->stat) & 0x0800)
				lp->stats.rx_crc_errors++;
			if ((rfd->stat) & 0x1000)
				lp->stats.rx_length_errors++;
		}

		/* Clear the buffer descriptor count and EOF + F flags */

		rfd->stat = 0;
		rfd->count = 0;
		rfd->cmd = CMD_EOL;
		lp->rx_tail->cmd = 0;
		lp->rx_tail = rfd;
		lp->scb.rfd = rfd->next;
		rfd = WSWAPrfd(lp->scb.rfd);	/* Next frame descriptor to check */
	}

	if (i596_debug > 3)
		printk("frames %d\n", frames);

	return 0;
}

static inline void i596_cleanup_cmd(struct i596_private *lp)
{
	struct i596_cmd *ptr;
	int boguscnt = 1000;

	if (i596_debug > 4)
		printk("i596_cleanup_cmd\n");

	while (lp->cmd_head != (struct i596_cmd *) I596_NULL) {
		ptr = lp->cmd_head;

		lp->cmd_head = WSWAPcmd(lp->cmd_head->next);
		lp->cmd_backlog--;

		switch ((ptr->command) & 0x7) {
		case CmdTx:
			{
				struct tx_cmd *tx_cmd = (struct tx_cmd *) ptr;
				struct sk_buff *skb = tx_cmd->skb;

				dev_kfree_skb(skb);

				lp->stats.tx_errors++;
				lp->stats.tx_aborted_errors++;

				ptr->next = (struct i596_cmd *) I596_NULL;
				kfree(tx_cmd);
				break;
			}
		case CmdMulticastList:
			{
				ptr->next = (struct i596_cmd *) I596_NULL;
				kfree(ptr);
				break;
			}
		default:
			ptr->next = (struct i596_cmd *) I596_NULL;
		}
	}

	while (lp->scb.command)
		if (--boguscnt == 0) {
			printk("i596_cleanup_cmd timed out with status %4.4x, cmd %4.4x.\n",
			       lp->scb.status, lp->scb.command);
			break;
		}
	lp->scb.cmd = WSWAPcmd(lp->cmd_head);
}

static inline void i596_reset(struct device *dev, struct i596_private *lp, int ioaddr)
{
	int boguscnt = 1000;
	unsigned long flags;

	if (i596_debug > 1)
		printk("i596_reset\n");

	save_flags(flags);
	cli();

	while (lp->scb.command)
		if (--boguscnt == 0) {
			printk("i596_reset timed out with status %4.4x, cmd %4.4x.\n",
			       lp->scb.status, lp->scb.command);
			break;
		}
	dev->start = 0;
	dev->tbusy = 1;

	lp->scb.command = CUC_ABORT | RX_ABORT;
	CA(dev);

	/* wait for shutdown */
	boguscnt = 4000;

	while (lp->scb.command)
		if (--boguscnt == 0) {
			printk("i596_reset 2 timed out with status %4.4x, cmd %4.4x.\n",
			       lp->scb.status, lp->scb.command);
			break;
		}
	restore_flags(flags);

	i596_cleanup_cmd(lp);
	i596_rx(dev);

	dev->start = 1;
	dev->tbusy = 0;
	dev->interrupt = 0;
	init_i596_mem(dev);
}

static void i596_add_cmd(struct device *dev, struct i596_cmd *cmd)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	int ioaddr = dev->base_addr;
	unsigned long flags;
	int boguscnt = 1000;

	if (i596_debug > 4)
		printk("i596_add_cmd\n");

	cmd->status = 0;
	cmd->command |= (CMD_EOL | CMD_INTR);
	cmd->next = (struct i596_cmd *) I596_NULL;
	save_flags(flags);
	cli();

	/*
	 * RGH  300597:  Looks to me like there could be a race condition
	 * here.  Just because we havn't picked up all the command items
	 * yet, doesn't mean that the 82596 hasn't finished processing
	 * them.  So, we may need to do a CUC_START anyway.
	 * Maybe not.  If it interrupts saying the CU is idle when there
	 * is still something in the cmd queue, the int handler with restart
	 * the CU.
	 */

	if (lp->cmd_head != (struct i596_cmd *) I596_NULL) {
		lp->cmd_tail->next = WSWAPcmd(cmd);
	} else {
		lp->cmd_head = cmd;
		while (lp->scb.command)
			if (--boguscnt == 0) {
				printk("i596_add_cmd timed out with status %4.4x, cmd %4.4x.\n",
				       lp->scb.status, lp->scb.command);
				break;
			}
		lp->scb.cmd = WSWAPcmd(cmd);
		lp->scb.command = CUC_START;
		CA(dev);
	}
	lp->cmd_tail = cmd;
	lp->cmd_backlog++;

	lp->cmd_head = WSWAPcmd(lp->scb.cmd);	/* Is this redundant?  RGH 300597 */
	restore_flags(flags);

	if (lp->cmd_backlog > max_cmd_backlog) {
		unsigned long tickssofar = jiffies - lp->last_cmd;

		if (tickssofar < ticks_limit)
			return;

		printk("%s: command unit timed out, status resetting.\n", dev->name);

		i596_reset(dev, lp, ioaddr);
	}
}

static int i596_open(struct device *dev)
{
	int i;

	if (i596_debug > 1)
		printk("%s: i596_open() irq %d.\n", dev->name, dev->irq);

	if (request_irq(dev->irq, &i596_interrupt, 0, "apricot", dev))
		return -EAGAIN;
#ifdef CONFIG_MVME16x_NET
	if (MACH_IS_MVME16x) {
		if (request_irq(0x56, &i596_error, 0, "apricot_error", dev))
			return -EAGAIN;
	}
#endif
	if ((i = init_rx_bufs(dev, RX_RING_SIZE)) < RX_RING_SIZE)
		printk("%s: only able to allocate %d receive buffers\n", dev->name, i);

	if (i < 4) {
		free_irq(dev->irq, dev);
		return -EAGAIN;
	}
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;
	MOD_INC_USE_COUNT;

	/* Initialize the 82596 memory */
	init_i596_mem(dev);

	return 0;		/* Always succeed */
}

static int i596_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	int ioaddr = dev->base_addr;
	struct tx_cmd *tx_cmd;

	if (i596_debug > 2)
		printk("%s: 82596 start xmit\n", dev->name);

	/* Transmitter timeout, serious problems. */
	if (dev->tbusy) {
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 5)
			return 1;
		printk("%s: transmit timed out, status resetting.\n",
		       dev->name);
		lp->stats.tx_errors++;
		/* Try to restart the adaptor */
		if (lp->last_restart == lp->stats.tx_packets) {
			if (i596_debug > 1)
				printk("Resetting board.\n");

			/* Shutdown and restart */
			i596_reset(dev, lp, ioaddr);
		} else {
			/* Issue a channel attention signal */
			if (i596_debug > 1)
				printk("Kicking board.\n");
			lp->scb.command = CUC_START | RX_START;
			CA(dev);
			lp->last_restart = lp->stats.tx_packets;
		}
		dev->tbusy = 0;
		dev->trans_start = jiffies;
	}
	if (i596_debug > 3)
		printk("%s: i596_start_xmit() called\n", dev->name);

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (test_and_set_bit(0, (void *) &dev->tbusy) != 0)
		printk("%s: Transmitter access conflict.\n", dev->name);
	else {
		short length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
		dev->trans_start = jiffies;

		tx_cmd = (struct tx_cmd *) kmalloc((sizeof(struct tx_cmd) + sizeof(struct i596_tbd)), GFP_ATOMIC);
		if (tx_cmd == NULL) {
			printk("%s: i596_xmit Memory squeeze, dropping packet.\n", dev->name);
			lp->stats.tx_dropped++;

			dev_kfree_skb(skb);
		} else {
			struct i596_tbd *tbd = (struct i596_tbd *) (tx_cmd + 1);
			tx_cmd->tbd = WSWAPtbd(tbd);
			tbd->next = (struct i596_tbd *) I596_NULL;

			tx_cmd->cmd.command = CMD_FLEX | CmdTx;
			tx_cmd->skb = skb;

			tx_cmd->pad = 0;
			tx_cmd->size = 0;
			tbd->pad = 0;
			tbd->size = EOF | length;

			tbd->data = WSWAPchar(skb->data);

			if (i596_debug > 3)
				print_eth(skb->data);
			i596_add_cmd(dev, (struct i596_cmd *) tx_cmd);

			lp->stats.tx_packets++;
			lp->stats.tx_bytes += length;
		}
	}

	dev->tbusy = 0;

	return 0;
}


static void print_eth(char *add)
{
	int i;

	printk("print_eth(%08x)\n", (unsigned int) add);
	printk("Dest  ");
	for (i = 0; i < 6; i++)
		printk(" %2.2X", (unsigned char) add[i]);
	printk("\n");

	printk("Source");
	for (i = 0; i < 6; i++)
		printk(" %2.2X", (unsigned char) add[i + 6]);
	printk("\n");
	printk("type %2.2X%2.2X\n", (unsigned char) add[12], (unsigned char) add[13]);
}

__initfunc(int i82596_probe(struct device *dev))
{
	int i;
	struct i596_private *lp;
	char eth_addr[6];

#ifdef CONFIG_MVME16x_NET
	if (MACH_IS_MVME16x) {
		static int probed = 0;

		if (mvme16x_config & MVME16x_CONFIG_NO_ETHERNET) {
			printk("Ethernet probe disabled - chip not present\n");
			return ENODEV;
		}
		if (probed)
			return ENODEV;
		probed++;
		memcpy(eth_addr, (void *) 0xfffc1f2c, 6);	/* YUCK! Get addr from NOVRAM */
		dev->base_addr = MVME_I596_BASE;
		dev->irq = (unsigned) MVME16x_IRQ_I596;
	}
#endif
#ifdef CONFIG_BVME6000_NET
	if (MACH_IS_BVME6000) {
		volatile unsigned char *rtc = (unsigned char *) BVME_RTC_BASE;
		unsigned char msr = rtc[3];
		int i;

		rtc[3] |= 0x80;
		for (i = 0; i < 6; i++)
			eth_addr[i] = rtc[i * 4 + 7];	/* Stored in RTC RAM at offset 1 */
		rtc[3] = msr;
		dev->base_addr = BVME_I596_BASE;
		dev->irq = (unsigned) BVME_IRQ_I596;
	}
#endif
#ifdef CONFIG_APRICOT_INTEL
	int checksum = 0;
	int ioaddr = 0x300;

	/* this is easy the ethernet interface can only be at 0x300 */
	/* first check nothing is already registered here */

	if (check_region(ioaddr, I596_TOTAL_SIZE))
		return ENODEV;

	for (i = 0; i < 8; i++) {
		eth_addr[i] = inb(ioaddr + 8 + i);
		checksum += eth_addr[i];
	}

	/* checksum is a multiple of 0x100, got this wrong first time
	   some machines have 0x100, some 0x200. The DOS driver doesn't
	   even bother with the checksum */

	if (checksum % 0x100)
		return ENODEV;

	/* Some other boards trip the checksum.. but then appear as ether
	   address 0. Trap these - AC */

	if (memcmp(eth_addr, "\x00\x00\x49", 3) != 0)
		return ENODEV;

	request_region(ioaddr, I596_TOTAL_SIZE, "i596");

	dev->base_addr = ioaddr;
	dev->irq = 10;
#endif
	ether_setup(dev);
	printk("%s: 82596 at %#3lx,", dev->name, dev->base_addr);

	for (i = 0; i < 6; i++)
		printk(" %2.2X", dev->dev_addr[i] = eth_addr[i]);

	printk(" IRQ %d.\n", dev->irq);

	if (i596_debug > 0)
		printk(version);

	/* The APRICOT-specific entries in the device structure. */
	dev->open = &i596_open;
	dev->stop = &i596_close;
	dev->hard_start_xmit = &i596_start_xmit;
	dev->get_stats = &i596_get_stats;
	dev->set_multicast_list = &set_multicast_list;

	dev->mem_start = (int) kmalloc(sizeof(struct i596_private) + 0x0f, GFP_KERNEL);
	/* align for scp */
	dev->priv = (void *) ((dev->mem_start + 0xf) & 0xfffffff0);

	lp = (struct i596_private *) dev->priv;
	if (i596_debug)
		printk("%s: lp at 0x%08lx, lp->scb at 0x%08lx\n"
		,dev->name, (unsigned long) lp, (unsigned long) &lp->scb);
	memset((void *) lp, 0, sizeof(struct i596_private));
	lp->scb.command = 0;
	lp->scb.cmd = (struct i596_cmd *) I596_NULL;
	lp->scb.rfd = (struct i596_rfd *) I596_NULL;

	return 0;
}

static void i596_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = dev_id;
	struct i596_private *lp;
	short ioaddr;
	int boguscnt = 2000;
	unsigned short status, ack_cmd = 0;

#ifdef CONFIG_BVME6000_NET
	if (MACH_IS_BVME6000) {
		if (*(char *) BVME_LOCAL_IRQ_STAT & BVME_ETHERR) {
			i596_error(BVME_IRQ_I596, NULL, NULL);
			return;
		}
	}
#endif
	if (dev == NULL) {
		printk("i596_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}
	if (i596_debug > 3)
		printk("%s: i596_interrupt(): irq %d\n", dev->name, irq);

	if (dev->interrupt)
		printk("%s: Re-entering the interrupt handler.\n", dev->name);

	dev->interrupt = 1;

	ioaddr = dev->base_addr;

	lp = (struct i596_private *) dev->priv;

	while (lp->scb.command)
		if (--boguscnt == 0) {
			printk("%s: i596 interrupt, timeout status %4.4x command %4.4x.\n", dev->name, lp->scb.status, lp->scb.command);
			break;
		}
	status = lp->scb.status;

	if (i596_debug > 4)
		printk("%s: i596 interrupt, status %4.4x.\n", dev->name, status);

	ack_cmd = status & 0xf000;

	if ((status & 0x8000) || (status & 0x2000)) {
		struct i596_cmd *ptr;

		if ((i596_debug > 4) && (status & 0x8000))
			printk("%s: i596 interrupt completed command.\n", dev->name);
		if ((i596_debug > 4) && (status & 0x2000))
			printk("%s: i596 interrupt command unit inactive %x.\n", dev->name, status & 0x0700);

		while ((lp->cmd_head != (struct i596_cmd *) I596_NULL) && (lp->cmd_head->status & STAT_C)) {
			ptr = lp->cmd_head;

			if (i596_debug > 2)
				printk("cmd_head->status = %04x, ->command = %04x\n",
				       lp->cmd_head->status, lp->cmd_head->command);
			lp->cmd_head = WSWAPcmd(lp->cmd_head->next);
			lp->cmd_backlog--;

			switch ((ptr->command) & 0x7) {
			case CmdTx:
				{
					struct tx_cmd *tx_cmd = (struct tx_cmd *) ptr;
					struct sk_buff *skb = tx_cmd->skb;

					if ((ptr->status) & STAT_OK) {
						if (i596_debug > 2)
							print_eth(skb->data);
					} else {
						lp->stats.tx_errors++;
						if ((ptr->status) & 0x0020)
							lp->stats.collisions++;
						if (!((ptr->status) & 0x0040))
							lp->stats.tx_heartbeat_errors++;
						if ((ptr->status) & 0x0400)
							lp->stats.tx_carrier_errors++;
						if ((ptr->status) & 0x0800)
							lp->stats.collisions++;
						if ((ptr->status) & 0x1000)
							lp->stats.tx_aborted_errors++;
					}

					dev_kfree_skb(skb);

					ptr->next = (struct i596_cmd *) I596_NULL;
					kfree(tx_cmd);
					break;
				}
			case CmdMulticastList:
				{
					ptr->next = (struct i596_cmd *) I596_NULL;
					kfree(ptr);
					break;
				}
			case CmdTDR:
				{
					unsigned long status = *((unsigned long *) (ptr + 1));

					if (status & 0x8000) {
						if (i596_debug > 3)
							printk("%s: link ok.\n", dev->name);
					} else {
						if (status & 0x4000)
							printk("%s: Transceiver problem.\n", dev->name);
						if (status & 0x2000)
							printk("%s: Termination problem.\n", dev->name);
						if (status & 0x1000)
							printk("%s: Short circuit.\n", dev->name);

						if (i596_debug > 1)
							printk("%s: Time %ld.\n", dev->name, status & 0x07ff);
					}
					break;
				}
			case CmdConfigure:
				{
					ptr->next = (struct i596_cmd *) I596_NULL;
					/* Zap command so set_multicast_list() knows it is free */
					ptr->command = 0;
					break;
				}
			default:
				ptr->next = (struct i596_cmd *) I596_NULL;
			}
			lp->last_cmd = jiffies;
		}

		ptr = lp->cmd_head;
		while ((ptr != (struct i596_cmd *) I596_NULL) && (ptr != lp->cmd_tail)) {
			ptr->command &= 0x1fff;
			ptr = WSWAPcmd(ptr->next);
		}

		if ((lp->cmd_head != (struct i596_cmd *) I596_NULL) && (dev->start))
			ack_cmd |= CUC_START;
		lp->scb.cmd = WSWAPcmd(lp->cmd_head);
	}
	if ((status & 0x1000) || (status & 0x4000)) {
		if ((i596_debug > 4) && (status & 0x4000))
			printk("%s: i596 interrupt received a frame.\n", dev->name);
		/* Only RX_START if stopped - RGH 07-07-96 */
		if (status & 0x1000) {
			if (dev->start)
				ack_cmd |= RX_START;
			if (i596_debug > 1)
				printk("%s: i596 interrupt receive unit inactive %x.\n", dev->name, status & 0x00f0);
		}
		i596_rx(dev);
	}
	/* acknowledge the interrupt */

/*      COMMENTED OUT <<<<<
   if ((lp->scb.cmd != (struct i596_cmd *) I596_NULL) && (dev->start))
   ack_cmd |= CUC_START;
 */
	boguscnt = 1000;
	while (lp->scb.command)
		if (--boguscnt == 0) {
			printk("%s: i596 interrupt, timeout status %4.4x command %4.4x.\n", dev->name, lp->scb.status, lp->scb.command);
			break;
		}
	lp->scb.command = ack_cmd;

#ifdef CONFIG_MVME16x_NET
	if (MACH_IS_MVME16x) {
		/* Ack the interrupt */

		volatile unsigned char *pcc2 = (unsigned char *) 0xfff42000;

		pcc2[0x2a] |= 0x08;
	}
#endif
#ifdef CONFIG_BVME6000_NET
	if (MACH_IS_BVME6000) {
		volatile unsigned char *ethirq = (unsigned char *) BVME_ETHIRQ_REG;

		*ethirq = 1;
		*ethirq = 3;
	}
#endif
#ifdef CONFIG_APRICOT_INTEL
	(void) inb(ioaddr + 0x10);
	outb(4, ioaddr + 0xf);
#endif
	CA(dev);

	if (i596_debug > 4)
		printk("%s: exiting interrupt.\n", dev->name);

	dev->interrupt = 0;
	return;
}

static int i596_close(struct device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	int boguscnt = 2000;
	unsigned long flags;

	dev->start = 0;
	dev->tbusy = 1;

	if (i596_debug > 1)
		printk("%s: Shutting down ethercard, status was %4.4x.\n",
		       dev->name, lp->scb.status);

	save_flags(flags);
	cli();

	while (lp->scb.command)
		if (--boguscnt == 0) {
			printk("%s: close1 timed out with status %4.4x, cmd %4.4x.\n",
			     dev->name, lp->scb.status, lp->scb.command);
			break;
		}
	lp->scb.command = CUC_ABORT | RX_ABORT;
	CA(dev);

	boguscnt = 2000;

	while (lp->scb.command)
		if (--boguscnt == 0) {
			printk("%s: close2 timed out with status %4.4x, cmd %4.4x.\n",
			     dev->name, lp->scb.status, lp->scb.command);
			break;
		}
	restore_flags(flags);

	i596_cleanup_cmd(lp);

#ifdef CONFIG_MVME16x_NET
	if (MACH_IS_MVME16x) {
		volatile unsigned char *pcc2 = (unsigned char *) 0xfff42000;

		/* Disable all ints */
		pcc2[0x28] = 1;
		pcc2[0x2a] = 0x40;
		pcc2[0x2b] = 0x40;	/* Set snooping bits now! */
	}
#endif
#ifdef CONFIG_BVME6000_NET
	if (MACH_IS_BVME6000) {
		volatile unsigned char *ethirq = (unsigned char *) BVME_ETHIRQ_REG;

		*ethirq = 1;
	}
#endif

	free_irq(dev->irq, dev);
	remove_rx_bufs(dev);
	MOD_DEC_USE_COUNT;

	return 0;
}

static struct net_device_stats *
 i596_get_stats(struct device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;

	return &lp->stats;
}

/*
 *    Set or clear the multicast filter for this adaptor.
 */

static void set_multicast_list(struct device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	struct i596_cmd *cmd;
	int config = 0;

	if (i596_debug > 1)
		printk("%s: set multicast list, %d entries, promisc %s, allmulti %s\n", dev->name, dev->mc_count, dev->flags & IFF_PROMISC ? "ON" : "OFF", dev->flags & IFF_ALLMULTI ? "ON" : "OFF");

	if ((dev->flags & IFF_PROMISC) && !(lp->i596_config[8] & 0x01)) {
		lp->i596_config[8] |= 0x01;
		config = 1;
	}
	if (!(dev->flags & IFF_PROMISC) && (lp->i596_config[8] & 0x01)) {
		lp->i596_config[8] &= ~0x01;
		config = 1;
	}
	if ((dev->flags & IFF_ALLMULTI) && (lp->i596_config[11] & 0x20)) {
		lp->i596_config[11] &= ~0x20;
		config = 1;
	}
	if (!(dev->flags & IFF_ALLMULTI) && !(lp->i596_config[11] & 0x20)) {
		lp->i596_config[11] |= 0x20;
		config = 1;
	}
	if (config) {
		if (lp->set_conf.command)
			printk("%s: config change request already queued\n",
			       dev->name);
		else {
			lp->set_conf.command = CmdConfigure;
			i596_add_cmd(dev, &lp->set_conf);
		}
	}
	if (dev->mc_count > 0) {
		struct dev_mc_list *dmi;
		unsigned char *cp;
		cmd = (struct i596_cmd *) kmalloc(sizeof(struct i596_cmd) + 2 + dev->mc_count * 6, GFP_ATOMIC);
		if (cmd == NULL) {
			printk("%s: set_multicast Memory squeeze.\n", dev->name);
			return;
		}
		cmd->command = CmdMulticastList;
		*((unsigned short *) (cmd + 1)) = dev->mc_count * 6;
		cp = ((unsigned char *) (cmd + 1)) + 2;
		for (dmi = dev->mc_list; dmi != NULL; dmi = dmi->next) {
			memcpy(cp, dmi->dmi_addr, 6);
			if (i596_debug > 1)
				printk("%s: Adding address %02x:%02x:%02x:%02x:%02x:%02x\n", dev->name, *(cp + 0), *(cp + 1), *(cp + 2), *(cp + 3), *(cp + 4), *(cp + 5));
			cp += 6;
		}
		if (i596_debug > 2)
			print_eth(((char *) (cmd + 1)) + 2);
		i596_add_cmd(dev, cmd);
	}
}

#ifdef HAVE_DEVLIST
static unsigned int i596_portlist[] __initdata =
{0x300, 0};
struct netdev_entry i596_drv =
{"apricot", i82596_probe, I596_TOTAL_SIZE, apricot_portlist};
#endif

#ifdef MODULE
static char devicename[9] =
{0,};
static struct device dev_apricot =
{
	devicename,		/* device name inserted by /linux/drivers/net/net_init.c */
	0, 0, 0, 0,
	0x300, 10,
	0, 0, 0, NULL, i82596_probe};

static int io = 0x300;
static int irq = 10;
MODULE_PARM(irq, "i");

int init_module(void)
{
	dev_apricot.base_addr = io;
	dev_apricot.irq = irq;
	if (register_netdev(&dev_apricot) != 0)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	unregister_netdev(&dev_apricot);
	kfree((void *) dev_apricot.mem_start);
	dev_apricot.priv = NULL;

	/* If we don't do this, we can't re-insmod it later. */
	release_region(dev_apricot.base_addr, I596_TOTAL_SIZE);
}

#endif				/* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c 82596.c"
 * End:
 */
