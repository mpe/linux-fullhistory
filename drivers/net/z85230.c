/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	(c) Copyright 1998 Building Number Three Ltd
 *
 *	Development of this driver was funded by Equiinet Ltd
 *			http://www.equiinet.com
 *
 *	ChangeLog:
 *
 *	Asynchronous mode dropped for 2.2. For 2.3 we will attempt the
 *	unification of all the Z85x30 asynchronous drivers for real.
 *
 *	To Do:
 *	
 *	Finish DMA mode support.
 *
 *	Performance
 *
 *	Z85230:
 *	Non DMA you want a 486DX50 or better to do 64Kbits. 9600 baud
 *	X.25 is not unrealistic on all machines. DMA mode can in theory
 *	handle T1/E1 quite nicely.
 *
 *	Z85C30:
 *	64K will take DMA, 9600 baud X.25 should be ok.
 *
 *	Z8530:
 *	Synchronous mode without DMA is unlikely to pass about 2400 baud.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <asm/dma.h>
#include <asm/io.h>
#define RT_LOCK
#define RT_UNLOCK
#include <asm/spinlock.h>

#include "z85230.h"


static spinlock_t z8530_buffer_lock = SPIN_LOCK_UNLOCKED;

/*
 *	Provided port access methods. The Comtrol SV11 requires no delays
 *	between accesses and uses PC I/O. Some drivers may need a 5uS delay
 */

extern __inline__ int z8530_read_port(int p)
{
	u8 r=inb(Z8530_PORT_OF(p));
	if(p&Z8530_PORT_SLEEP)	/* gcc should figure this out efficiently ! */
		udelay(5);
	return r;
}

extern __inline__ void z8530_write_port(int p, u8 d)
{
	outb(d,Z8530_PORT_OF(p));
	if(p&Z8530_PORT_SLEEP)
		udelay(5);
}



static void z8530_rx_done(struct z8530_channel *c);
static void z8530_tx_done(struct z8530_channel *c);


/*
 *	Port accesses
 */
 
extern inline u8 read_zsreg(struct z8530_channel *c, u8 reg)
{
	u8 r;
	unsigned long flags;
	save_flags(flags);
	cli();
	if(reg)
		z8530_write_port(c->ctrlio, reg);
	r=z8530_read_port(c->ctrlio);
	restore_flags(flags);
	return r;
}

extern inline u8 read_zsdata(struct z8530_channel *c)
{
	u8 r;
	r=z8530_read_port(c->dataio);
	return r;
}

extern inline void write_zsreg(struct z8530_channel *c, u8 reg, u8 val)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	if(reg)
		z8530_write_port(c->ctrlio, reg);
	z8530_write_port(c->ctrlio, val);
	restore_flags(flags);
}

extern inline void write_zsctrl(struct z8530_channel *c, u8 val)
{
	z8530_write_port(c->ctrlio, val);
}

extern inline void write_zsdata(struct z8530_channel *c, u8 val)
{
	z8530_write_port(c->dataio, val);
}

/*
 *	Register loading parameters for a dead port
 */
 
u8 z8530_dead_port[]=
{
	255
};

EXPORT_SYMBOL(z8530_dead_port);

/*
 *	Register loading parameters for currently supported circuit types
 */


/*
 *	Data clocked by telco end. This is the correct data for the UK
 *	"kilostream" service, and most other similar services.
 */
 
u8 z8530_hdlc_kilostream[]=
{
	4,	SYNC_ENAB|SDLC|X1CLK,
	2,	0,	/* No vector */
	1,	0,
	3,	ENT_HM|RxCRC_ENAB|Rx8,
	5,	TxCRC_ENAB|RTS|TxENAB|Tx8|DTR,
	9,	0,		/* Disable interrupts */
	6,	0xFF,
	7,	FLAG,
	10,	ABUNDER|NRZ|CRCPS,/*MARKIDLE ??*/
	11,	TCTRxCP,
	14,	DISDPLL,
	15,	DCDIE|SYNCIE|CTSIE|TxUIE|BRKIE,
	1,	EXT_INT_ENAB|TxINT_ENAB|INT_ALL_Rx,
	9,	NV|MIE|NORESET,
	255
};

EXPORT_SYMBOL(z8530_hdlc_kilostream);

/*
 *	As above but for enhanced chips.
 */

u8 z8530_hdlc_kilostream_85230[]=
{
	4,	SYNC_ENAB|SDLC|X1CLK,
	2,	0,	/* No vector */
	1,	0,
	3,	ENT_HM|RxCRC_ENAB|Rx8,
	5,	TxCRC_ENAB|RTS|TxENAB|Tx8|DTR,
	9,	0,		/* Disable interrupts */
	6,	0xFF,
	7,	FLAG,
	10,	ABUNDER|NRZ|CRCPS,	/* MARKIDLE?? */
	11,	TCTRxCP,
	14,	DISDPLL,
	15,	DCDIE|SYNCIE|CTSIE|TxUIE|BRKIE,
	1,	EXT_INT_ENAB|TxINT_ENAB|INT_ALL_Rx,
	9,	NV|MIE|NORESET,
	23,	3,		/* Extended mode AUTO TX and EOM*/
	31,	3,		/* Extended mode AUTO TX and EOM*/
	
	255
};

EXPORT_SYMBOL(z8530_hdlc_kilostream_85230);

/*
 *	Flush the FIFO
 */
 
static void z8530_flush_fifo(struct z8530_channel *c)
{
	read_zsreg(c, R1);
	read_zsreg(c, R1);
	read_zsreg(c, R1);
	read_zsreg(c, R1);
	if(c->dev->type==Z85230)
	{
		read_zsreg(c, R1);
		read_zsreg(c, R1);
		read_zsreg(c, R1);
		read_zsreg(c, R1);
	}
}	

/* Sets or clears DTR/RTS on the requested line */

static void z8530_rtsdtr(struct z8530_channel *c, int set)
{
	if (set)
		c->regs[5] |= (RTS | DTR);
	else
		c->regs[5] &= ~(RTS | DTR);
	write_zsreg(c, R5, c->regs[5]);
}

/*
 *	Receive handler. This is much like the async one but not quite the
 *	same or as complex
 *
 *	Note: Its intended that this handler can easily be separated from
 *	the main code to run realtime. That'll be needed for some machines
 *	(eg to ever clock 64kbits on a sparc ;)).
 *
 *	The RT_LOCK macros don't do anything now. Keep the code covered
 *	by them as short as possible in all circumstances - clocks cost
 *	baud. The interrupt handler is assumed to be atomic w.r.t. to
 *	other code - this is true in the RT case too.
 *
 *	We only cover the sync cases for this. If you want 2Mbit async
 *	do it yourself but consider medical assistance first.
 *
 *	This non DMA synchronous mode is portable code.
 */
 
static void z8530_rx(struct z8530_channel *c)
{
	u8 ch,stat;
	 
	while(1)
	{
		/* FIFO empty ? */
		if(!(read_zsreg(c, R0)&1))
			break;
		ch=read_zsdata(c);
		stat=read_zsreg(c, R1);
	
		/*
		 *	Overrun ?
		 */
		if(c->count < c->max)
		{
			*c->dptr++=ch;
			c->count++;
		}

		if(stat&END_FR)
		{
		
			/*
			 *	Error ?
			 */
			if(stat&(Rx_OVR|CRC_ERR))
			{
				/* Rewind the buffer and return */
				if(c->skb)
					c->dptr=c->skb->data;
				c->count=0;
				if(stat&Rx_OVR)
				{
					printk(KERN_WARNING "%s: overrun\n", c->dev->name);
					c->rx_overrun++;
				}
				if(stat&CRC_ERR)
				{
					c->rx_crc_err++;
					/* printk("crc error\n"); */
				}
				/* Shove the frame upstream */
			}
			else
			{
				z8530_rx_done(c);
				write_zsctrl(c, RES_Rx_CRC);
			}
		}
	}
	/*
	 *	Clear irq
	 */
	write_zsctrl(c, ERR_RES);
	write_zsctrl(c, RES_H_IUS);
}


/*
 *	Z8530 transmit interrupt handler
 */
 
static void z8530_tx(struct z8530_channel *c)
{
	while(c->txcount)
	{
		/* FIFO full ? */
		if(!(read_zsreg(c, R0)&4))
			break;
		c->txcount--;
		/*
		 *	Shovel out the byte
		 */
		write_zsreg(c, R8, *c->tx_ptr++);
		write_zsctrl(c, RES_H_IUS);
		/* We are about to underflow */
		if(c->txcount==0)
		{
			write_zsctrl(c, RES_EOM_L);
			write_zsreg(c, R10, c->regs[10]&~ABUNDER);
		}
		return;
	}
	
	/*
	 *	End of frame TX - fire another one
	 */
	 
	write_zsctrl(c, RES_Tx_P);

	z8530_tx_done(c);	 
/*	write_zsreg(c, R8, *c->tx_ptr++); */
	write_zsctrl(c, RES_H_IUS);
}

static void z8530_status(struct z8530_channel *chan)
{
	u8 status=read_zsreg(chan, R0);
	u8 altered=chan->status^status;
	
	chan->status=status;
	
	if(status&TxEOM)
	{
/*		printk("%s: Tx underrun.\n", chan->dev->name); */
		write_zsctrl(chan, ERR_RES);
		z8530_tx_done(chan);
	}
		
	if(altered&DCD)
	{
		if(status&DCD)
		{
			printk(KERN_INFO "%s: DCD raised\n", chan->dev->name);
			write_zsreg(chan, R3, chan->regs[3]|RxENABLE);
		}
		else
		{
			printk(KERN_INFO "%s: DCD lost\n", chan->dev->name);
			write_zsreg(chan, R3, chan->regs[3]&~RxENABLE);
			z8530_flush_fifo(chan);
		}
		
	}	
	write_zsctrl(chan, RES_EXT_INT);
	write_zsctrl(chan, RES_H_IUS);
}

struct z8530_irqhandler z8530_sync=
{
	z8530_rx,
	z8530_tx,
	z8530_status
};

EXPORT_SYMBOL(z8530_sync);

/*
 *	Non bus mastering DMA interfaces for the Z8x30 devices. This
 *	is really pretty PC specific.
 */
 
static void z8530_dma_rx(struct z8530_channel *chan)
{
	if(chan->rxdma_on)
	{
		/* Special condition check only */
		u8 status;

		read_zsreg(chan, R7);
		read_zsreg(chan, R6);
		
		status=read_zsreg(chan, R1);
		if(status&END_FR)
		{
			z8530_rx_done(chan);	/* Fire up the next one */
		}		
		write_zsctrl(chan, ERR_RES);
		write_zsctrl(chan, RES_H_IUS);
	}
	else
	{
		/* DMA is off right now, drain the slow way */
		z8530_rx(chan);
	}	
}

static void z8530_dma_tx(struct z8530_channel *chan)
{
	if(!chan->txdma_on)
	{
		z8530_tx(chan);
		return;
	}
	/* This shouldnt occur in DMA mode */
	printk(KERN_ERR "DMA tx ??\n");
	z8530_tx(chan);
}

static void z8530_dma_status(struct z8530_channel *chan)
{
	unsigned long flags;
	u8 status=read_zsreg(chan, R0);
	u8 altered=chan->status^status;
	
	chan->status=status;

	if(chan->txdma_on)
	{
		if(status&TxEOM)
		{
			flags=claim_dma_lock();
			/* Transmit underrun */
			disable_dma(chan->txdma);
			clear_dma_ff(chan->txdma);	
			chan->txdma_on=0;
			release_dma_lock(flags);
			z8530_tx_done(chan);
		}
	}
	if(altered&DCD)
	{
		if(status&DCD)
		{
			printk(KERN_INFO "%s: DCD raised\n", chan->dev->name);
			write_zsreg(chan, R3, chan->regs[3]|RxENABLE);
		}
		else
		{
			printk(KERN_INFO "%s:DCD lost\n", chan->dev->name);
			write_zsreg(chan, R3, chan->regs[3]&~RxENABLE);
			z8530_flush_fifo(chan);
		}
	}	
	write_zsctrl(chan, RES_EXT_INT);
	write_zsctrl(chan, RES_H_IUS);
}

struct z8530_irqhandler z8530_dma_sync=
{
	z8530_dma_rx,
	z8530_dma_tx,
	z8530_dma_status
};

EXPORT_SYMBOL(z8530_dma_sync);

struct z8530_irqhandler z8530_txdma_sync=
{
	z8530_rx,
	z8530_dma_tx,
	z8530_dma_status
};

EXPORT_SYMBOL(z8530_txdma_sync);

/*
 *	Interrupt vectors for a Z8530 that is in 'parked' mode.
 *	For machines with PCI Z85x30 cards, or level triggered interrupts
 *	(eg the MacII) we must clear the interrupt cause or die.
 */


static void z8530_rx_clear(struct z8530_channel *c)
{
	/*
	 *	Data and status bytes
	 */
	u8 stat;

	read_zsdata(c);
	stat=read_zsreg(c, R1);
	
	if(stat&END_FR)
		write_zsctrl(c, RES_Rx_CRC);
	/*
	 *	Clear irq
	 */
	write_zsctrl(c, ERR_RES);
	write_zsctrl(c, RES_H_IUS);
}

static void z8530_tx_clear(struct z8530_channel *c)
{
	write_zsctrl(c, RES_Tx_P);
	write_zsctrl(c, RES_H_IUS);
}

static void z8530_status_clear(struct z8530_channel *chan)
{
	u8 status=read_zsreg(chan, R0);
	if(status&TxEOM)
		write_zsctrl(chan, ERR_RES);
	write_zsctrl(chan, RES_EXT_INT);
	write_zsctrl(chan, RES_H_IUS);
}

struct z8530_irqhandler z8530_nop=
{
	z8530_rx_clear,
	z8530_tx_clear,
	z8530_status_clear
};


EXPORT_SYMBOL(z8530_nop);

/*
 *	A Z85[2]30 device has stuck its hand in the air for attention
 */

void z8530_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct z8530_dev *dev=dev_id;
	u8 intr;
	static volatile int locker=0;
	int work=0;
	
	if(locker)
	{
		printk(KERN_ERR "IRQ re-enter\n");
		return;
	}
	locker=1;
	
	while(++work<5000)
	{
		struct z8530_irqhandler *irqs=dev->chanA.irqs;
		
		intr = read_zsreg(&dev->chanA, R3);
		if(!(intr & (CHARxIP|CHATxIP|CHAEXT|CHBRxIP|CHBTxIP|CHBEXT)))
			break;
	
		/* This holds the IRQ status. On the 8530 you must read it from chan 
		   A even though it applies to the whole chip */
		
		/* Now walk the chip and see what it is wanting - it may be
		   an IRQ for someone else remember */
		   
		if(intr & (CHARxIP|CHATxIP|CHAEXT))
		{
			if(intr&CHARxIP)
				irqs->rx(&dev->chanA);
			if(intr&CHATxIP)
				irqs->tx(&dev->chanA);
			if(intr&CHAEXT)
				irqs->status(&dev->chanA);
		}

		irqs=dev->chanB.irqs;

		if(intr & (CHBRxIP|CHBTxIP|CHBEXT))
		{
			if(intr&CHBRxIP)
				irqs->rx(&dev->chanB);
			if(intr&CHBTxIP)
				irqs->tx(&dev->chanB);
			if(intr&CHBEXT)
				irqs->status(&dev->chanB);
		}
	}
	if(work==5000)
		printk(KERN_ERR "%s: interrupt jammed - abort(0x%X)!\n", dev->name, intr);
	/* Ok all done */
	locker=0;
}

EXPORT_SYMBOL(z8530_interrupt);

static char reg_init[16]=
{
	0,0,0,0,
	0,0,0,0,
	0,0,0,0,
	0x55,0,0,0
};


int z8530_sync_open(struct device *dev, struct z8530_channel *c)
{
	c->sync = 1;
	c->mtu = dev->mtu+64;
	c->count = 0;
	c->skb = NULL;
	c->skb2 = NULL;
	c->irqs = &z8530_sync;
	/* This loads the double buffer up */
	z8530_rx_done(c);	/* Load the frame ring */
	z8530_rx_done(c);	/* Load the backup frame */
	z8530_rtsdtr(c,1);
	write_zsreg(c, R3, c->regs[R3]|RxENABLE);
	return 0;
}


EXPORT_SYMBOL(z8530_sync_open);

int z8530_sync_close(struct device *dev, struct z8530_channel *c)
{
	u8 chk;
	c->irqs = &z8530_nop;
	c->max = 0;
	c->sync = 0;
	
	chk=read_zsreg(c,R0);
	write_zsreg(c, R3, c->regs[R3]);
	z8530_rtsdtr(c,0);
	return 0;
}

EXPORT_SYMBOL(z8530_sync_close);

int z8530_sync_dma_open(struct device *dev, struct z8530_channel *c)
{
	unsigned long flags;
	
	c->sync = 1;
	c->mtu = dev->mtu+64;
	c->count = 0;
	c->skb = NULL;
	c->skb2 = NULL;
	/*
	 *	Load the DMA interfaces up
	 */
	c->rxdma_on = 0;
	c->txdma_on = 0;
	
	/*
	 *	Allocate the DMA flip buffers
	 */
	 
	c->rx_buf[0]=kmalloc(c->mtu, GFP_KERNEL|GFP_DMA);
	if(c->rx_buf[0]==NULL)
		return -ENOBUFS;
	c->rx_buf[1]=kmalloc(c->mtu, GFP_KERNEL|GFP_DMA);
	if(c->rx_buf[1]==NULL)
	{
		kfree(c->rx_buf[0]);
		c->rx_buf[0]=NULL;
		return -ENOBUFS;
	}
	
	c->tx_dma_buf[0]=kmalloc(c->mtu, GFP_KERNEL|GFP_DMA);
	if(c->tx_dma_buf[0]==NULL)
	{
		kfree(c->rx_buf[0]);
		kfree(c->rx_buf[1]);
		c->rx_buf[0]=NULL;
		return -ENOBUFS;
	}
	c->tx_dma_buf[1]=kmalloc(c->mtu, GFP_KERNEL|GFP_DMA);
	if(c->tx_dma_buf[1]==NULL)
	{
		kfree(c->tx_dma_buf[0]);
		kfree(c->rx_buf[0]);
		kfree(c->rx_buf[1]);
		c->rx_buf[0]=NULL;
		c->rx_buf[1]=NULL;
		c->tx_dma_buf[0]=NULL;
		return -ENOBUFS;
	}
	c->tx_dma_used=0;
	c->dma_tx = 1;
	c->dma_num=0;
	c->dma_ready=1;
	
	/*
	 *	Enable DMA control mode
	 */
	 
	/*
	 *	TX DMA via DIR/REQ
	 */
	 
	c->regs[R14]|= DTRREQ;
	write_zsreg(c, R14, c->regs[R14]);     

	/*
	 *	RX DMA via W/Req
	 */	 

	c->regs[R1]|= WT_FN_RDYFN;
	c->regs[R1]|= WT_RDY_RT;
	c->regs[R1]|= INT_ERR_Rx;
	write_zsreg(c, R1, c->regs[R1]);
	c->regs[R1]|= WT_RDY_ENAB;
	write_zsreg(c, R1, c->regs[R1]);            
	
	/*
	 *	DMA interrupts
	 */
	 
	/*
	 *	Set up the DMA configuration
	 */	
	 
	flags=claim_dma_lock();
	 
	disable_dma(c->rxdma);
	clear_dma_ff(c->rxdma);
	set_dma_mode(c->rxdma, DMA_MODE_READ|0x10);
	set_dma_addr(c->rxdma, virt_to_bus(c->rx_buf[0]));
	set_dma_count(c->rxdma, c->mtu);
	enable_dma(c->rxdma);

	disable_dma(c->txdma);
	clear_dma_ff(c->txdma);
	set_dma_mode(c->txdma, DMA_MODE_WRITE);
	disable_dma(c->txdma);
	
	release_dma_lock(flags);
	
	/*
	 *	Select the DMA interrupt handlers
	 */

	c->rxdma_on = 1;
	c->txdma_on = 1;
	c->tx_dma_used = 1;
	 
	c->irqs = &z8530_dma_sync;
	z8530_rtsdtr(c,1);
	write_zsreg(c, R3, c->regs[R3]|RxENABLE);
	return 0;
}

EXPORT_SYMBOL(z8530_sync_dma_open);

int z8530_sync_dma_close(struct device *dev, struct z8530_channel *c)
{
	u8 chk;
	unsigned long flags;
	
	c->irqs = &z8530_nop;
	c->max = 0;
	c->sync = 0;
	
	/*
	 *	Disable the PC DMA channels
	 */
	
	flags=claim_dma_lock(); 
	disable_dma(c->rxdma);
	clear_dma_ff(c->rxdma);
	
	c->rxdma_on = 0;
	
	disable_dma(c->txdma);
	clear_dma_ff(c->txdma);
	release_dma_lock(flags);
	
	c->txdma_on = 0;
	c->tx_dma_used = 0;

	/*
	 *	Disable DMA control mode
	 */
	 
	c->regs[R1]&= ~WT_RDY_ENAB;
	write_zsreg(c, R1, c->regs[R1]);            
	c->regs[R1]&= ~(WT_RDY_RT|WT_FN_RDYFN|INT_ERR_Rx);
	c->regs[R1]|= INT_ALL_Rx;
	write_zsreg(c, R1, c->regs[R1]);
	c->regs[R14]&= ~DTRREQ;
	write_zsreg(c, R14, c->regs[R14]);   
	
	if(c->rx_buf[0])
	{
		kfree(c->rx_buf[0]);
		c->rx_buf[0]=NULL;
	}
	if(c->rx_buf[1])
	{
		kfree(c->rx_buf[1]);
		c->rx_buf[1]=NULL;
	}
	if(c->tx_dma_buf[0])
	{
		kfree(c->tx_dma_buf[0]);
		c->tx_dma_buf[0]=NULL;
	}
	if(c->tx_dma_buf[1])
	{
		kfree(c->tx_dma_buf[1]);
		c->tx_dma_buf[1]=NULL;
	}
	chk=read_zsreg(c,R0);
	write_zsreg(c, R3, c->regs[R3]);
	z8530_rtsdtr(c,0);
	return 0;
}

EXPORT_SYMBOL(z8530_sync_dma_close);

int z8530_sync_txdma_open(struct device *dev, struct z8530_channel *c)
{
	printk("Opening sync interface for TX-DMA\n");
	c->sync = 1;
	c->mtu = dev->mtu+64;
	c->count = 0;
	c->skb = NULL;
	c->skb2 = NULL;
	
	/*
	 *	Load the PIO receive ring
	 */

	z8530_rx_done(c);
	z8530_rx_done(c);

 	/*
	 *	Load the DMA interfaces up
	 */

	c->rxdma_on = 0;
	c->txdma_on = 0;
	
	c->tx_dma_buf[0]=kmalloc(c->mtu, GFP_KERNEL|GFP_DMA);
	if(c->tx_dma_buf[0]==NULL)
	{
		kfree(c->rx_buf[0]);
		kfree(c->rx_buf[1]);
		c->rx_buf[0]=NULL;
		return -ENOBUFS;
	}
	c->tx_dma_buf[1]=kmalloc(c->mtu, GFP_KERNEL|GFP_DMA);
	if(c->tx_dma_buf[1]==NULL)
	{
		kfree(c->tx_dma_buf[0]);
		kfree(c->rx_buf[0]);
		kfree(c->rx_buf[1]);
		c->rx_buf[0]=NULL;
		c->rx_buf[1]=NULL;
		c->tx_dma_buf[0]=NULL;
		return -ENOBUFS;
	}
	c->tx_dma_used=0;
	c->dma_num=0;
	c->dma_ready=1;
	c->dma_tx = 1;

 	/*
	 *	Enable DMA control mode
	 */

 	/*
	 *	TX DMA via DIR/REQ
 	 */
	c->regs[R14]|= DTRREQ;
	write_zsreg(c, R14, c->regs[R14]);     
	
	/*
	 *	Set up the DMA configuration
	 */	
	 
	disable_dma(c->txdma);
	clear_dma_ff(c->txdma);
	set_dma_mode(c->txdma, DMA_MODE_WRITE);
	disable_dma(c->txdma);
	
	/*
	 *	Select the DMA interrupt handlers
	 */

	c->rxdma_on = 0;
	c->txdma_on = 1;
	c->tx_dma_used = 1;
	 
	c->irqs = &z8530_txdma_sync;
	printk("Loading RX\n");
	z8530_rtsdtr(c,1);
	printk("Rx interrupts ON\n");	
	write_zsreg(c, R3, c->regs[R3]|RxENABLE);
	return 0;
}

EXPORT_SYMBOL(z8530_sync_txdma_open);
	
int z8530_sync_txdma_close(struct device *dev, struct z8530_channel *c)
{
	u8 chk;
	c->irqs = &z8530_nop;
	c->max = 0;
	c->sync = 0;
	
	/*
	 *	Disable the PC DMA channels
	 */
	 
	disable_dma(c->txdma);
	clear_dma_ff(c->txdma);
	c->txdma_on = 0;
	c->tx_dma_used = 0;

	/*
	 *	Disable DMA control mode
	 */
	 
	c->regs[R1]&= ~WT_RDY_ENAB;
	write_zsreg(c, R1, c->regs[R1]);            
	c->regs[R1]&= ~(WT_RDY_RT|WT_FN_RDYFN|INT_ERR_Rx);
	c->regs[R1]|= INT_ALL_Rx;
	write_zsreg(c, R1, c->regs[R1]);
	c->regs[R14]&= ~DTRREQ;
	write_zsreg(c, R14, c->regs[R14]);   
	
	if(c->tx_dma_buf[0])
	{
		kfree(c->tx_dma_buf[0]);
		c->tx_dma_buf[0]=NULL;
	}
	if(c->tx_dma_buf[1])
	{
		kfree(c->tx_dma_buf[1]);
		c->tx_dma_buf[1]=NULL;
	}
	chk=read_zsreg(c,R0);
	write_zsreg(c, R3, c->regs[R3]);
	z8530_rtsdtr(c,0);
	return 0;
}


EXPORT_SYMBOL(z8530_sync_txdma_close);

/*
 *	Describe a Z8530 in a standard format. We must pass the I/O as
 *	the port offset isnt predictable. The main reason for this function
 *	is to try and get a common format of report.
 */

static char *z8530_type_name[]={
	"Z8530",
	"Z85C30",
	"Z85230"
};

void z8530_describe(struct z8530_dev *dev, char *mapping, int io)
{
	printk(KERN_INFO "%s: %s found at %s 0x%X, IRQ %d.\n",
		dev->name, 
		z8530_type_name[dev->type],
		mapping,
		io,
		dev->irq);
}

EXPORT_SYMBOL(z8530_describe);

/*
 *	Configure up a Z8530
 */

 
int z8530_init(struct z8530_dev *dev)
{
	/* NOP the interrupt handlers first - we might get a
	   floating IRQ transition when we reset the chip */
	dev->chanA.irqs=&z8530_nop;
	dev->chanB.irqs=&z8530_nop;
	/* Reset the chip */
	write_zsreg(&dev->chanA, R9, 0xC0);
	udelay(100);
	/* Now check its valid */
	write_zsreg(&dev->chanA, R12, 0xAA);
	if(read_zsreg(&dev->chanA, R12)!=0xAA)
		return -ENODEV;
	write_zsreg(&dev->chanA, R12, 0x55);
	if(read_zsreg(&dev->chanA, R12)!=0x55)
		return -ENODEV;
		
	dev->type=Z8530;
	
	/*
	 *	See the application note.
	 */
	 
	write_zsreg(&dev->chanA, R15, 0x01);
	
	/*
	 *	If we can set the low bit of R15 then
	 *	the chip is enhanced.
	 */
	 
	if(read_zsreg(&dev->chanA, R15)==0x01)
	{
		/* This C30 versus 230 detect is from Klaus Kudielka's dmascc */
		/* Put a char in the fifo */
		write_zsreg(&dev->chanA, R8, 0);
		if(read_zsreg(&dev->chanA, R0)&Tx_BUF_EMP)
			dev->type = Z85230;	/* Has a FIFO */
		else
			dev->type = Z85C30;	/* Z85C30, 1 byte FIFO */
	}
		
	/*
	 *	The code assumes R7' and friends are
	 *	off. Use write_zsext() for these and keep
	 *	this bit clear.
	 */
	 
	write_zsreg(&dev->chanA, R15, 0);
		
	/*
	 *	At this point it looks like the chip is behaving
	 */
	 
	memcpy(dev->chanA.regs, reg_init, 16);
	memcpy(dev->chanB.regs, reg_init ,16);
	
	return 0;
}


EXPORT_SYMBOL(z8530_init);

int z8530_shutdown(struct z8530_dev *dev)
{
	/* Reset the chip */
	dev->chanA.irqs=&z8530_nop;
	dev->chanB.irqs=&z8530_nop;
	write_zsreg(&dev->chanA, R9, 0xC0);
	udelay(100);
	return 0;
}

EXPORT_SYMBOL(z8530_shutdown);

/*
 *	Load a Z8530 channel up from the system data
 *	We use +16 to indicate the 'prime' registers
 */

int z8530_channel_load(struct z8530_channel *c, u8 *rtable)
{
	while(*rtable!=255)
	{
		int reg=*rtable++;
		if(reg>0x0F)
			write_zsreg(c, R15, c->regs[15]|1);
		write_zsreg(c, reg&0x0F, *rtable);
		if(reg>0x0F)
			write_zsreg(c, R15, c->regs[15]&~1);
		c->regs[reg]=*rtable++;
	}
	c->rx_function=z8530_null_rx;
	c->skb=NULL;
	c->tx_skb=NULL;
	c->tx_next_skb=NULL;
	c->mtu=1500;
	c->max=0;
	c->count=0;
	c->status=0;	/* Fixme - check DCD now */
	c->sync=1;
	write_zsreg(c, R3, c->regs[R3]|RxENABLE);
	return 0;
}

EXPORT_SYMBOL(z8530_channel_load);


/*
 *	Higher level shovelling - transmit chains
 */

static void z8530_tx_begin(struct z8530_channel *c)
{
	unsigned long flags;
	if(c->tx_skb)
		return;
		
	c->tx_skb=c->tx_next_skb;
	c->tx_next_skb=NULL;
	c->tx_ptr=c->tx_next_ptr;
	
	mark_bh(NET_BH);
	if(c->tx_skb==NULL)
	{
		/* Idle on */
		if(c->txdma)
		{
			flags=claim_dma_lock();
			disable_dma(c->txdma);
			release_dma_lock(flags);
		}
		c->txcount=0;
	}
	else
	{
		c->tx_ptr=c->tx_next_ptr;
		c->txcount=c->tx_skb->len;
		
		
		if(c->dma_tx)
		{
			/*
			 *	FIXME. DMA is broken for the original 8530,
			 *	on the older parts we need to set a flag and
			 *	wait for a further TX interrupt to fire this
			 *	stage off	
			 */
			 
			flags=claim_dma_lock();
			disable_dma(c->txdma);
			clear_dma_ff(c->txdma);
			set_dma_addr(c->txdma, virt_to_bus(c->tx_ptr));
			set_dma_count(c->txdma, c->txcount);
			enable_dma(c->txdma);
			release_dma_lock(flags);
			write_zsreg(c, R5, c->regs[R5]|TxENAB);
		}
		else
		{
			save_flags(flags);
			cli();
			/* ABUNDER off */
			write_zsreg(c, R10, c->regs[10]);
			write_zsctrl(c, RES_Tx_CRC);
			write_zsctrl(c, RES_EOM_L);
	
			while(c->txcount && (read_zsreg(c,R0)&Tx_BUF_EMP))
			{		
				write_zsreg(c, R8, *c->tx_ptr++);
				c->txcount--;
			}
			restore_flags(flags);
		}
	}
}
 
 
static void z8530_tx_done(struct z8530_channel *c)
{
	unsigned long flags;
	struct sk_buff *skb;

	spin_lock_irqsave(&z8530_buffer_lock, flags);
	c->netdevice->tbusy=0;
	/* Can't happen */
	if(c->tx_skb==NULL)
	{
		spin_unlock_irqrestore(&z8530_buffer_lock, flags);
		printk(KERN_WARNING "%s: spurious tx done\n", c->dev->name);
		return;
	}
	skb=c->tx_skb;
	c->tx_skb=NULL;
	z8530_tx_begin(c);
	spin_unlock_irqrestore(&z8530_buffer_lock, flags);
	dev_kfree_skb(skb);
}

/*
 *	Higher level shovelling - receive chains
 */
 
void z8530_null_rx(struct z8530_channel *c, struct sk_buff *skb)
{
	kfree_skb(skb);
}

EXPORT_SYMBOL(z8530_null_rx);

static void z8530_rx_done(struct z8530_channel *c)
{
	struct sk_buff *skb;
	int ct;
	
	/*
	 *	Is our receive engine in DMA mode
	 */
	 
	if(c->rxdma_on)
	{
		/*
		 *	Save the ready state and the buffer currently
		 *	being used as the DMA target
		 */

		int ready=c->dma_ready;
		unsigned char *rxb=c->rx_buf[c->dma_num];
		unsigned long flags;
		
		/*
		 *	Complete this DMA. Neccessary to find the length
		 */		
		 
		flags=claim_dma_lock();
		
		disable_dma(c->rxdma);
		clear_dma_ff(c->rxdma);
		c->rxdma_on=0;
		ct=c->mtu-get_dma_residue(c->rxdma);
		if(ct<0)
			ct=2;	/* Shit happens.. */
		c->dma_ready=0;
		
		/*
		 *	Normal case: the other slot is free, start the next DMA
		 *	into it immediately.
		 */
		 
		if(ready)
		{
			c->dma_num^=1;
			set_dma_mode(c->rxdma, DMA_MODE_READ|0x10);
			set_dma_addr(c->rxdma, virt_to_bus(c->rx_buf[c->dma_num]));
			set_dma_count(c->rxdma, c->mtu);
			c->rxdma_on = 1;
			enable_dma(c->rxdma);
			/* Stop any frames that we missed the head of 
			   from passing */
			write_zsreg(c, R0, RES_Rx_CRC);
		}
		else
			/* Can't occur as we dont reenable the DMA irq until
			   after the flip is done */
			printk("DMA flip overrun!\n");
			
		release_dma_lock(flags);
		
		/*
		 *	Shove the old buffer into an sk_buff. We can't DMA
		 *	directly into one on a PC - it might be above the 16Mb
		 *	boundary. Optimisation - we could check to see if we
		 *	can avoid the copy. Optimisation 2 - make the memcpy
		 *	a copychecksum.
		 */
		 
		skb=dev_alloc_skb(ct);
		if(skb==NULL)
			printk("%s: Memory squeeze.\n", c->netdevice->name);
		else
		{
			skb_put(skb, ct);
			memcpy(skb->data, rxb, ct);
		}
		c->dma_ready=1;
	}
	else
	{
		RT_LOCK;	
		skb=c->skb;
		
		/*
		 *	The game we play for non DMA is similar. We want to
		 *	get the controller set up for the next packet as fast
		 *	as possible. We potentially only have one byte + the
		 *	fifo length for this. Thus we want to flip to the new
		 *	buffer and then mess around copying and allocating
		 *	things. For the current case it doesn't matter but
		 *	if you build a system where the sync irq isnt blocked
		 *	by the kernel IRQ disable then you need only block the
		 *	sync IRQ for the RT_LOCK area.
		 *	
		 */
		ct=c->count;
		
		c->skb = c->skb2;
		c->count = 0;
		c->max = c->mtu;
		if(c->skb)
		{
			c->dptr = c->skb->data;
			c->max = c->mtu;
		}
		else
		{
			c->count= 0;
			c->max = 0;
		}
		RT_UNLOCK;

		c->skb2 = dev_alloc_skb(c->mtu);
		if(c->skb2==NULL)
			printk(KERN_WARNING "%s: memory squeeze.\n",
				c->netdevice->name);
		else
		{
			skb_put(c->skb2,c->mtu);
		}
	}
	/*
	 *	If we received a frame we must now process it.
	 */
	if(skb)
	{
		skb_trim(skb, ct);
		c->rx_function(c,skb);
	}
	else
		printk("Lost a frame\n");
}

/*
 *	Cannot DMA over a 64K boundary on a PC
 */
 
extern inline int spans_boundary(struct sk_buff *skb)
{
	unsigned long a=(unsigned long)skb->data;
	a^=(a+skb->len);
	if(a&0x00010000)	/* If the 64K bit is different.. */
	{
		printk("spanner\n");
		return 1;
	}
	return 0;
}

/*
 *	Queue a packet for transmission. Because we have rather
 *	hard to hit interrupt latencies for the Z85230 per packet 
 *	even in DMA mode we do the flip to DMA buffer if needed here
 *	not in the IRQ.
 */

int z8530_queue_xmit(struct z8530_channel *c, struct sk_buff *skb)
{
	unsigned long flags;
	if(c->tx_next_skb)
	{
		skb->dev->tbusy=1;
		return 1;
	}
	
	/* PC SPECIFIC - DMA limits */
	
	/*
	 *	If we will DMA the transmit and its gone over the ISA bus
	 *	limit, then copy to the flip buffer
	 */
	 
	if(c->dma_tx && ((unsigned long)(virt_to_bus(skb->data+skb->len))>=16*1024*1024 || spans_boundary(skb)))
	{
		/* 
		 *	Send the flip buffer, and flip the flippy bit.
		 *	We don't care which is used when just so long as
		 *	we never use the same buffer twice in a row. Since
		 *	only one buffer can be going out at a time the other
		 *	has to be safe.
		 */
		c->tx_next_ptr=c->tx_dma_buf[c->tx_dma_used];
		c->tx_dma_used^=1;	/* Flip temp buffer */
		memcpy(c->tx_next_ptr, skb->data, skb->len);
	}
	else
		c->tx_next_ptr=skb->data;	
	RT_LOCK;
	c->tx_next_skb=skb;
	RT_UNLOCK;
	
	spin_lock_irqsave(&z8530_buffer_lock, flags);
	z8530_tx_begin(c);
	spin_unlock_irqrestore(&z8530_buffer_lock, flags);
	return 0;
}

EXPORT_SYMBOL(z8530_queue_xmit);

struct net_device_stats *z8530_get_stats(struct z8530_channel *c)
{
	return &c->stats;
}

EXPORT_SYMBOL(z8530_get_stats);

#ifdef MODULE

/*
 *	Module support
 */
 
int init_module(void)
{
	printk(KERN_INFO "Generic Z85C30/Z85230 interface driver v0.02\n");
	return 0;
}

void cleanup_module(void)
{
}

#endif
