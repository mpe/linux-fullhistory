#define RCS_ID "$Id: scc.c,v 1.41 1995/12/17 22:36:40 jreuter Exp jreuter $"

#define BANNER "Z8530 SCC driver version 2.01.dl1bke (alpha) by DL1BKE\n"

/*

   ********************************************************************
   *   SCC.C - Linux driver for Z8530 based HDLC cards for AX.25      *
   ********************************************************************


   ********************************************************************

	Copyright (c) 1993, 1995 Joerg Reuter DL1BKE

	portions (c) 1993 Guido ten Dolle PE1NNZ

   ********************************************************************
   
   The driver and the programs in the archive are UNDER CONSTRUCTION.
   The code is likely to fail, and so your kernel could --- even 
   a whole network. 

   This driver is intended for Amateur Radio use. If you are running it
   for commercial purposes, please drop me a note. I am nosy...

   ...BUT:
 
   ! You  m u s t  recognize the appropriate legislations of your country !
   ! before you connect a radio to the SCC board and start to transmit or !
   ! receive. The GPL allows you to use the  d r i v e r,  NOT the RADIO! !

   For non-Amateur-Radio use please note that you might need a special
   allowance/licence from the designer of the SCC Board and/or the
   MODEM. 

   This program is free software; you can redistribute it and/or modify 
   it under the terms of the (modified) GNU General Public License 
   delivered with the Linux kernel source.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should find a copy of the GNU General Public License in 
   /usr/src/linux/COPYING; 
   
   ******************************************************************** 


   ...If you find any portions of the code that are copyrighted to you,
   and you don't want to see in here, please send me a private (!) 
   message to my internet site. I will change it as soon as possible. 
   Please don't flame to the tcp-group or anywhere else. Thanks!

   Joerg Reuter	ampr-net: dl1bke@db0pra.ampr.org
		AX-25   : DL1BKE @ DB0ACH.#NRW.DEU.EU
		Internet: jreuter@lykos.tng.oche.de
		
		
   Incomplete history of z8530drv:
   -------------------------------

   940913	- started to write the driver, rescued most of my own
		  code (and Hans Alblas' memory buffer pool concept) from 
		  an earlier project "sccdrv" which was initiated by 
		  Guido ten Dolle. Not much of the old driver survived, 
		  though. The first version I put my hands on was sccdrv1.3
		  from August 1993. The memory buffer pool concept
		  appeared in an unauthorized sccdrv version (1.5) from
		  August 1994.

   950131	- changed copyright notice to GPL without limitations.
   
     .
     .
     .
      
   950922	- using kernel timer chain
   
   951002	- splitting timer routine, adding support for throttle/
   		  unthrottle calls, removed typo in kiss decoder,
   		  corrected termios settings.
   		  
   951011	- at last found one (the?) problem which caused the 
   		  driver to mess up buffers on heavy load pe1ayx was
   		  complaining about. Quite simple to reproduce:
   		  set a 9k6 port into audio loopback, add a route
   		  to 44.99.99.99 on that route and do a 
   		  ping -f 44.99.99.99
   		  
   951013	- it still does not survive a flood ping...
   
   951107	- axattach w/o "-s" option (or setting an invalid 
   		  baudrate) does not produce "division by zero" faults 
   		  anymore.
   		  
   951114	- rewrote memory management, took first steps to allow
		  compilation as a module. Well, it looks like a whole
		  new driver now. BTW: It  d o e s  survive the flood
		  ping at last...
		  
   951116	- scc_config.h is gone -- the driver will be initialized
   		  through ioctl-commands. Use sccinit.c for this purpose.
   		  
   951117	- Well --- what should I say: You can compile it as a
   		  module now. And I solved the problem with slip.c...
   		  
   951120	- most ioctl() routines may be called w/o suser()
   		  permissions, check if you've set the permissions of
   		  /dev/scc* right! NOT 0666, you know it's evil ;-)
   		  
   951217	- found silly bug in init_channel(), some bugfixes
   		  for the "standalone" module


   Thanks to:
   ----------
   
   PE1CHL Rob	- for a lot of good ideas from his SCC driver for DOS
   PE1NNZ Guido - for his port of the original driver to Linux
   KA9Q   Phil  - from whom we stole the mbuf-structure
   PA3AYX Hans  - for some useful changes
   DL8MBT Flori - for support
   DG0FT  Rene  - for the BayCom USCC support
   PA3AOU Harry - for ESCC testing, information supply and support
   
   PE1KOX Rob, DG1RTF Thomas, ON5QK Roland, G4XYW Andy, Linus,
   EI9GL Paul,
   
   and all who sent me bug reports and ideas... 
   
   
   NB -- if you find errors, change something, please let me know
      	 first before you distribute it... And please don't touch
   	 the version number. Just replace my callsign in
   	 "v2.01.dl1bke" with your own. Just to avoid confusion...
   	 
   If you want to add your modification to the linux distribution
   please (!) contact me first.
   	  
   Jörg Reuter DL1BKE
  
*/

/* ----------------------------------------------------------------------- */

#define DEBUG_BUFFERS	/* keep defined unless it is really stable... */

#undef  SCC_DELAY 	/* perhaps a 486DX2 is a *bit* too fast */
#undef  SCC_LDELAY 5	/* slow it even a bit more down */
#undef  DONT_CHECK	/* don't look if the SCCs you specified are available */

#define MAXSCC          4       /* number of max. supported chips */
#define RXBUFFERS       8       /* default number of RX buffers */
#define TXBUFFERS       8       /* default number of TX buffers */
#define BUFSIZE         384     /* must not exceed 4096-sizeof(mbuf) */
#define TPS             25      /* scc_tx_timer():  Ticks Per Second */

#define DEFAULT_CLOCK	4915200 /* default pclock if nothing is specified */

/* ----------------------------------------------------------------------- */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/tqueue.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/termios.h>
#include <linux/serial.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/scc.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/bitops.h>

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <linux/kernel.h>

#ifdef MODULE
int init_module(void);
void cleanup_module(void);
#endif

#ifndef Z8530_MAJOR
#define Z8530_MAJOR 34
#endif

int scc_init(void);

static struct mbuf * scc_enqueue_buffer(struct mbuf **queue, struct mbuf * buffer);
static struct mbuf * scc_dequeue_buffer(struct mbuf **queue);
static void alloc_buffer_pool(struct scc_channel *scc);
static void free_buffer_pool(struct scc_channel *scc);
static struct mbuf * scc_get_buffer(struct scc_channel *scc, char type);

int scc_open(struct tty_struct *tty, struct file *filp);
static void scc_close(struct tty_struct *tty, struct file *filp);
int scc_write(struct tty_struct *tty, int from_user, const unsigned char *buf, int count);
static void scc_put_char(struct tty_struct *tty, unsigned char ch);
static void scc_flush_chars(struct tty_struct *tty);
static int scc_write_room(struct tty_struct *tty);
static int scc_chars_in_buffer(struct tty_struct *tty);
static void scc_flush_buffer(struct tty_struct *tty);
static int scc_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg);
static void scc_set_termios(struct tty_struct *tty, struct termios *old_termios);
static void scc_set_ldisc(struct tty_struct *tty);
static void scc_throttle(struct tty_struct *tty);
static void scc_unthrottle(struct tty_struct *tty);
static void scc_start(struct tty_struct *tty);
static void scc_stop(struct tty_struct *tty);

static void z8530_init(void);

static void scc_change_speed(struct scc_channel *scc);
static void kiss_encode(struct scc_channel *scc);

static void init_channel(struct scc_channel *scc);
static void scc_key_trx (struct scc_channel *scc, char tx);
static void scc_txint(register struct scc_channel *scc);
static void scc_exint(register struct scc_channel *scc);
static void scc_rxint(register struct scc_channel *scc);
static void scc_spint(register struct scc_channel *scc);
static void scc_isr(int irq, void *dev_id, struct pt_regs *regs);
static void scc_tx_timer(unsigned long);
static void scc_rx_timer(unsigned long);
static void scc_init_timer(struct scc_channel *scc);

/* from serial.c */

static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 0 };


struct tty_driver scc_driver;
static int scc_refcount;
static struct tty_struct *scc_table[2*MAXSCC];
static struct termios scc_termios[2 * MAXSCC];
static struct termios scc_termios_locked[2 * MAXSCC];

struct irqflags { unsigned char used : 1; } Ivec[16];
	
struct scc_channel SCC_Info[2 * MAXSCC];         /* information per channel */
io_port SCC_ctrl[2 * MAXSCC];			 /* Control ports */

unsigned char Random = 0;		/* random number for p-persist */
unsigned char Driver_Initialized = 0;
int Nchips = 0;
io_port Vector_Latch = 0;

struct semaphore scc_sem = MUTEX;
unsigned char scc_wbuf[BUFSIZE];

static struct termios scc_std_termios;

/* ******************************************************************** */
/* *			Port Access Functions			      * */
/* ******************************************************************** */

static inline unsigned char
InReg(register io_port port, register unsigned char reg)
{
#ifdef SCC_LDELAY
	register unsigned char r;
	Outb(port, reg);
	udelay(SCC_LDELAY);
	r=Inb(port);
	udelay(SCC_LDELAY);
	return r;
#else
	Outb(port, reg);
	return Inb(port);
#endif
}

static inline void
OutReg(register io_port port, register unsigned char reg, register unsigned char val)
{
#ifdef SCC_LDELAY
	Outb(port, reg); udelay(SCC_LDELAY);
	Outb(port, val); udelay(SCC_LDELAY);
#else
	Outb(port, reg);
	Outb(port, val);
#endif
}

static inline void
wr(register struct scc_channel *scc, register unsigned char reg, register unsigned char val)
{
	OutReg(scc->ctrl, reg, (scc->wreg[reg] = val));
}

static inline void
or(register struct scc_channel *scc, register unsigned char reg, register unsigned char val)
{
	OutReg(scc->ctrl, reg, (scc->wreg[reg] |= val));
}

static inline void
cl(register struct scc_channel *scc, register unsigned char reg, register unsigned char val)
{
	OutReg(scc->ctrl, reg, (scc->wreg[reg] &= ~val));
}

/* ******************************************************************** */
/* * 			Memory Buffer Management			*/
/* ******************************************************************** */

/*
 * The new buffer scheme uses a ring chain of buffers. This has the
 * advantage to access both, first and last element of the list, very
 * fast. It has the disadvantage to mess things up double if something
 * is wrong.
 *
 * Every scc channel has its own buffer pool now with an adjustable
 * number of buffers for transmit and receive buffers. The buffer
 * size remains the same for each AX.25 frame, but is (as a semi-
 * undocumented feature) adjustable within a range of 512 and 4096
 * to benefit experiments with higher frame lengths. If you need
 * larger frames... Well, you'd better choose a whole new concept
 * for that, like a DMA capable I/O card and a block device driver...
 *
 */

/* ------ Management of buffer queues ------ */


/* Insert a buffer at the "end" of the chain */

static struct mbuf * 
scc_enqueue_buffer(struct mbuf **queue, struct mbuf * buffer)
{
	unsigned long flags;
	struct mbuf * anchor;
	
	save_flags(flags); cli();		/* do not disturb! */

#ifdef DEBUG_BUFFERS			
	if (queue == NULLBUFP)			/* called with illegal parameters, notify the user */

	{
		printk("z8530drv: Pointer to queue anchor is NULL pointer [enq]\n");
		restore_flags(flags);
		return NULLBUF;
	}
	
	if (buffer == NULLBUF)
	{
		printk("z8530drv: can't enqueue a NULL pointer\n");
		restore_flags(flags);
		return NULLBUF;
	}
#endif

	anchor = *queue;			/* the anchor is the "start" of the chain */

	if (anchor == NULLBUF)			/* found an empty list */
	{
		*queue = buffer;		/* new anchor */
		buffer->next = buffer->prev = NULLBUF;
	} else	
	if (anchor->prev == NULLBUF)		/* list has one member only */
	{
#ifdef DEBUG_BUFFERS
		if (anchor->prev != NULLBUF)	/* oops?! */
			printk("weird --- anchor->prev is NULL but not anchor->next [enq]\n");
#endif			
		anchor->next = anchor->prev = buffer;
		buffer->next = buffer->prev = anchor;
	} else
#ifdef DEBUG_BUFFERS	
	if (anchor->next == NULLBUF)		/* this has to be an error. Okay, make the best out of it */
	{
		printk("z8530drv: weird --- anchor->next is NULL but not anchor->prev [enq]\n");
		anchor->next = anchor->prev = buffer;
		buffer->next = buffer->prev = anchor;
	} else
#endif
	{					/* every other case */
		buffer->prev = anchor->prev;	/* self explaining, isn't it? */
		buffer->next = anchor;
		anchor->prev->next = buffer;
		anchor->prev = buffer;
	}
	
	restore_flags(flags);
	return *queue;				/* return "start" of chain */
}



/* Remove a buffer from the "start" of the chain an return it */

static struct mbuf * 
scc_dequeue_buffer(struct mbuf **queue)
{
	unsigned long flags;
	struct mbuf *buffer;
	
	save_flags(flags); cli();

#ifdef DEBUG_BUFFERS			
	if (queue == NULLBUFP)			/* called with illegal parameter */

	{
		printk("z8530drv: Pointer to queue anchor is NULL pointer [deq]\n");
		restore_flags(flags);
		return NULLBUF;
	}
#endif

	buffer = *queue;			/* head of the chain */
	
	if (buffer != NULLBUF)			/* not an empty list? */
	{
		if (buffer->prev != NULLBUF)	/* not last buffer? */
		{
#ifdef DEBUG_BUFFERS
			if (buffer->next == NULLBUF)
			{			/* what?! */
				printk("z8530drv: weird --- buffer->next is NULL but not buffer->prev [deq]\n");
			} else
#endif
			if (buffer->prev->next == buffer->prev->prev)
			{			/* only one buffer remaining... */
				buffer->next->prev = NULLBUF;
				buffer->next->next = NULLBUF;
			} else {		/* the one remaining situation... */
				buffer->next->prev = buffer->prev;
				buffer->prev->next = buffer->next;
			}
		} 
#ifdef DEBUG_BUFFERS
		else if (buffer->next != NULLBUF)
			printk("z8530drv: weird --- buffer->prev is NULL but not buffer->next [deq]\n");
#endif
		*queue = buffer->next;		/* new head of chain */
		
		buffer->next = NULLBUF;		/* for security only... */
		buffer->prev = NULLBUF;
		buffer->rw_ptr = buffer->data;
	} 
	
	restore_flags(flags);
	return buffer;				/* give it away... */
}

/* ------ buffer pool management ------ */


/* allocate buffer pool	for a channel */

static void 
alloc_buffer_pool(struct scc_channel *scc)
{
	int k;
	struct mbuf * bptr;
	int  buflen;
	
	buflen = sizeof(struct mbuf) + scc->stat.bufsize;
	
	if (scc->stat.bufsize <  336) scc->stat.bufsize = 336;
	if (buflen > 4096)
	{
		scc->stat.bufsize = 4096-sizeof(struct mbuf);
		buflen = 4096;
	}
	
	if (scc->stat.rxbuffers < 4)  scc->stat.rxbuffers = 4;
	if (scc->stat.txbuffers < 4)  scc->stat.txbuffers = 4;
	

	
	/* allocate receive buffers for this channel */
	
	for (k = 0; k < scc->stat.rxbuffers ; k++)
	{
		/* allocate memory for the struct and the buffer */
		
		bptr = (struct mbuf *) kmalloc(buflen, GFP_ATOMIC);
		
		/* should not happen, but who knows? */
		
		if (bptr == NULLBUF)
		{
			printk("z8530drv: %s: can't allocate memory for rx buffer pool", kdevname(scc->tty->device));
			break;
		}
		
		/* clear memory */
		
		memset(bptr, 0, buflen);
		
		/* initialize structure */
		
		bptr->rw_ptr = bptr->data;
		
		/* and append the buffer to the pool */
		
		scc_enqueue_buffer(&scc->rx_buffer_pool, bptr);
	}
	
	/* now do the same for the transmit buffers */
	
	for (k = 0; k < scc->stat.txbuffers ; k++)
	{
		bptr = (struct mbuf *) kmalloc(buflen, GFP_ATOMIC);
		
		if (bptr == NULLBUF)
		{
			printk("z8530drv: %s: can't allocate memory for tx buffer pool", kdevname(scc->tty->device));
			break;
		}
		
		memset(bptr, 0, buflen);
		
		bptr->rw_ptr = bptr->data;
		
		scc_enqueue_buffer(&scc->tx_buffer_pool, bptr);
	}
}


/* remove buffer pool */

static void 
free_buffer_pool(struct scc_channel *scc)
{
	struct mbuf * bptr;
	unsigned long flags;
	int cnt;
	
	/* this one is a bit tricky and probably dangerous. */
		
	save_flags(flags); cli();
	
	/* esp. to free the buffers currently in use by ISR */
	
	bptr = scc->rx_bp;
	if (bptr != NULLBUF)
	{
		scc->rx_bp = NULLBUF;
		scc_enqueue_buffer(&scc->rx_buffer_pool, bptr);
	}

	bptr = scc->tx_bp;	
	if (bptr != NULLBUF)
	{
		scc->tx_bp = NULLBUF;
		scc_enqueue_buffer(&scc->tx_buffer_pool, bptr);
	}
	
	bptr = scc->kiss_decode_bp;
	if (bptr != NULLBUF)
	{
		scc->kiss_decode_bp = NULLBUF;
		scc_enqueue_buffer(&scc->tx_buffer_pool, bptr);
	}
	
	bptr = scc->kiss_encode_bp;
	if (bptr != NULLBUF)
	{
		scc->kiss_encode_bp = NULLBUF;
		scc_enqueue_buffer(&scc->rx_buffer_pool, bptr);
	}
	
	restore_flags(flags);
	
	
	while (scc->rx_queue != NULLBUF)
	{
		bptr = scc_dequeue_buffer(&scc->rx_queue);
		scc_enqueue_buffer(&scc->rx_buffer_pool, bptr);
	}
	
	while (scc->tx_queue != NULLBUF)
	{
		bptr = scc_dequeue_buffer(&scc->tx_queue);
		scc_enqueue_buffer(&scc->tx_buffer_pool, bptr);
	}
	
	/* you want to know why we move every buffer back to the
	   buffer pool? Well, good question... ;-) 
	 */
	
	cnt = 0;
	
	/* Probably because we just want a central position in the
	   code were we actually call free()?
	 */
	
	while (scc->rx_buffer_pool != NULLBUF)
	{
		bptr = scc_dequeue_buffer(&scc->rx_buffer_pool);
		
		if (bptr != NULLBUF)
		{
			cnt++;
			kfree(bptr);
		}
	}

	if (cnt < scc->stat.rxbuffers)		/* hmm... hmm... :-( */
		printk("z8530drv: oops, deallocated only %d of %d rx buffers\n", cnt, scc->stat.rxbuffers);
	if (cnt > scc->stat.rxbuffers)		/* WHAT?!! */
		printk("z8530drv: oops, deallocated %d instead of %d rx buffers. Very strange.\n", cnt, scc->stat.rxbuffers);
		
	cnt = 0;
	
	while (scc->tx_buffer_pool != NULLBUF)
	{
		bptr = scc_dequeue_buffer(&scc->tx_buffer_pool);
		
		if (bptr != NULLBUF)
		{
			cnt++;
			kfree(bptr);
		}
	}

	if (cnt < scc->stat.txbuffers)
		printk("z8530drv: oops, deallocated only %d of %d tx buffers\n", cnt, scc->stat.txbuffers);
	if (cnt > scc->stat.txbuffers)
		printk("z8530drv: oops, deallocated %d instead of %d tx buffers. Very strange.\n", cnt, scc->stat.txbuffers);
}
		
		
/* ------ rx/tx buffer management ------ */

/* 
   get a fresh buffer from the pool if possible; if not: get one from
   the queue. We will remove the oldest frame from the queue and hope
   it was a good idea... ;-)
   
 */

static struct mbuf * 
scc_get_buffer(struct scc_channel *scc, char type)
{
	struct mbuf * bptr;
	
	if (type == BT_TRANSMIT)
	{
		bptr = scc_dequeue_buffer(&scc->tx_buffer_pool);
		
		/* no free buffers in the pool anymore? */
		
		if (bptr == NULLBUF)
		{
			printk("z8530drv: scc_get_buffer(%s): tx buffer pool empty\n", kdevname(scc->tty->device));
			
			/* use the oldest from the queue instead */
			
			bptr = scc_dequeue_buffer(&scc->tx_queue);
			
			/* this should never, ever happen... */
			
			if (bptr == NULLBUF)
				printk("z8530drv: scc_get_buffer(): panic - even no buffer found in tx queue\n");
		}
	} else {
		bptr = scc_dequeue_buffer(&scc->rx_buffer_pool);
		
		if (bptr == NULLBUF)
		{
			printk("z8530drv: scc_get_buffer(%s): rx buffer pool empty\n", kdevname(scc->tty->device));
			
			bptr = scc_dequeue_buffer(&scc->rx_queue);
			
			if (bptr == NULLBUF)
				printk("z8530drv: scc_get_buffer(): panic - even no buffer found in rx queue\n");
		}	
	}
	
	if (bptr != NULLBUF)
	{
		bptr->rw_ptr = bptr->data;
		bptr->cnt = 0;
	}

	return bptr;
}


/* ******************************************************************** */
/* *			Interrupt Service Routines		      * */
/* ******************************************************************** */

/* ----> interrupt service routine for the 8530 <---- */

/* it's recommended to keep this function "inline" ;-) */

static inline void
scc_isr_dispatch(register struct scc_channel *scc, register int vector)
{
	switch (vector & VECTOR_MASK)
	{
		case TXINT: scc_txint(scc); break;
		case EXINT: scc_exint(scc); break;
		case RXINT: scc_rxint(scc); break;
		case SPINT: scc_spint(scc); break;
		default	  : printk("scc_isr(): unknown interrupt status (addr %4.4x, state %2.2x)\n",scc->ctrl,vector);
	}
}





/* If the card has a latch for the interrupt vector (like the PA0HZP card)
   use it to get the number of the chip that generated the int.
   If not: poll all defined chips.
 */

static void
scc_isr(int irq, void *dev_id, struct pt_regs *regs)
{
	register unsigned char vector;	
	register struct scc_channel *scc;
	register io_port q;
	register io_port *p;
	register int k;


	cli();
	
	if (Vector_Latch)
	{
	    	while(1)			   /* forever...? */
    		{ 
			Outb(Vector_Latch, 0);      /* Generate INTACK */
        
			/* Read the vector */
			if((vector=Inb(Vector_Latch)) >= 16 * Nchips) break; 
						/* ...not forever! */
          
			/* Extract channel number and status from vector. */
			/* Isolate channel number */
			/* Call handler */
	        
			if (vector & 0x01) break; 
        	 
		        scc=&SCC_Info[(((vector>>1)&0x7c)^0x04) >> 2];
        
			if (!scc->tty) break;

			scc_isr_dispatch(scc, vector);
			
			Outb(scc->ctrl,0x38);              /* Reset Highest IUS" opcode to WR0 */
		}  
	    	
		sti();	
		return;
	}
	
	
	/* Find the SCC generating the interrupt by polling all attached SCCs
	 * reading RR3A (the interrupt pending register)
	 */

	k = 0;
	p = SCC_ctrl;
	
	while (k++ < Nchips)
	{
		if (!(q=*p++)) break;
		
		Outb(q,3);
		
		if (Inb(q))
		{
			if (!(q=*p++)) break;
			
			Outb(q,2);
			vector=Inb(q);                  /* Read the vector */
        
		        /* Extract channel number and status from vector. */
		        /* Isolate channel number */
		        /* Call handler */
        

			if (vector & 1) break; 
        
		        scc = &SCC_Info[(((vector >> 1) & 0x7c) ^ 0x04) >> 2];
        
		        if (!scc->tty) break;

			/* Isolate status info from vector, call handler */
			
			scc_isr_dispatch(scc, vector);

			k = 0;
			p = SCC_ctrl;

	     	} else p++;
	}    
	
	sti();
}



/* ----> four different interrupt handlers for Tx, Rx, changing of	*/
/*       DCD/CTS and Rx/Tx errors					*/


/* Transmitter interrupt handler */
static void
scc_txint(register struct scc_channel *scc)
{
	register struct mbuf *bp;

	scc->stat.txints++;

	bp = scc->tx_bp;
	
	if (bp == NULLBUF)
	{
		do 
		{
			if (bp != NULLBUF)
				scc_enqueue_buffer(&scc->tx_buffer_pool, bp);

			bp = scc_dequeue_buffer(&scc->tx_queue);
			
			if (bp == NULLBUF)
			{
				scc->stat.tx_state = TXS_BUSY;
				scc->t_tail = scc->kiss.tailtime;
				
				Outb(scc->ctrl, RES_Tx_P);	/* clear int */
				return;
			}
			
			if ( scc->kiss.not_slip && (bp->cnt > 0) )
			{
				bp->rw_ptr++;
				bp->cnt--;
			}
			
		} while (bp->cnt < 1);
		
		
		Outb(scc->ctrl, RES_Tx_CRC);	/* reset CRC generator */
		or(scc,R10,ABUNDER);		/* re-install underrun protection */
		Outb(scc->data,*bp->rw_ptr);	/* send byte */
		if (!scc->enhanced)		/* reset EOM latch */
			Outb(scc->ctrl, RES_EOM_L);
		
		scc->tx_bp = bp;
		scc->stat.tx_state = TXS_ACTIVE; /* next byte... */
	} else
	if (bp->cnt <= 0)
	{
		if (--scc->stat.tx_queued < 0) scc->stat.tx_queued = 0;
		
		Outb(scc->ctrl, RES_Tx_P);	/* reset pending int */
		cl(scc, R10, ABUNDER);		/* send CRC */
		scc_enqueue_buffer(&scc->tx_buffer_pool, bp);
		scc->tx_bp = NULLBUF;
		scc->stat.tx_state = TXS_NEWFRAME; /* next frame... */
		return;
	} else {
		Outb(scc->data,*bp->rw_ptr);		
	}
	
	bp->rw_ptr++;			/* increment pointer */
	bp->cnt--;                      /* decrease byte count */
}

static inline void
flush_FIFO(register struct scc_channel *scc)
{
	register int k;
	
	for (k=0; k<3; k++)
		Inb(scc->data);
		
	if(scc->rx_bp != NULLBUF)	/* did we receive something? */
	{
		scc->stat.rxerrs++;  /* then count it as an error */
		scc_enqueue_buffer(&scc->rx_buffer_pool, scc->rx_bp);
		
		scc->rx_bp = NULLBUF;
	}
}


/* External/Status interrupt handler */
static void
scc_exint(register struct scc_channel *scc)
{
	register unsigned char status,changes,chg_and_stat;

	scc->stat.exints++;

	status = InReg(scc->ctrl,R0);
	changes = status ^ scc->status;
	chg_and_stat = changes & status;
	
	/* ABORT: generated whenever DCD drops while receiving */

	if (chg_and_stat & BRK_ABRT)		/* Received an ABORT */
		flush_FIFO(scc);
			
		
	/* DCD: on = start to receive packet, off = ABORT condition */
	/* (a successfully received packet generates a special condition int) */
	
	if(changes & DCD)                       /* DCD input changed state */
	{
		if(status & DCD)                /* DCD is now ON */
		{
			if (scc->modem.clocksrc != CLK_EXTERNAL)
				OutReg(scc->ctrl,R14,SEARCH|scc->wreg[R14]); /* DPLL: enter search mode */
				
			or(scc,R3,ENT_HM|RxENABLE); /* enable the receiver, hunt mode */
		} else {                        /* DCD is now OFF */
			cl(scc,R3,ENT_HM|RxENABLE); /* disable the receiver */
			flush_FIFO(scc);
		}
	}

#ifdef notdef
	/* CTS: use external TxDelay (what's that good for?!) */
	
	if (chg_and_stat & CTS)			/* CTS is now ON */
	{			
		if (!Running(t_txdel) && scc->kiss.txdelay == 0) /* zero TXDELAY = wait for CTS */
			scc->t_txdel = 0;	/* kick it! */		
		
	}
#endif
	
	if ( (status & TxEOM) && (scc->stat.tx_state == TXS_ACTIVE) )
	{
		scc->stat.tx_under++;	  /* oops, an underrun! count 'em */
		Outb(scc->ctrl, RES_Tx_P);
		Outb(scc->ctrl, RES_EXT_INT);	/* reset ext/status interrupts */
		scc->t_maxk = 1;
		
		if (scc->tx_bp != NULLBUF)
		{
			scc_enqueue_buffer(&scc->tx_buffer_pool, scc->tx_bp);
			scc->tx_bp = NULLBUF;
		}
		
		if (--scc->stat.tx_queued < 0) scc->stat.tx_queued = 0;
		or(scc,R10,ABUNDER);
	}
		
	if (status & ZCOUNT)		   /* Oops? */
	{
		scc->stat.tx_under = 9999;  /* errr... yes. */
		Outb(scc->ctrl, RES_Tx_P); /* just to be sure */
		scc->t_maxk = 1;
		
		if (scc->tx_bp != NULLBUF)
		{
			scc_enqueue_buffer(&scc->tx_buffer_pool, scc->tx_bp);
			scc->tx_bp = NULLBUF;
		}

		if (--scc->stat.tx_queued < 0) scc->stat.tx_queued = 0;
		scc->kiss.tx_inhibit = 1;	/* don't try it again! */
	}
		
	
	scc->status = status;
	Outb(scc->ctrl,RES_EXT_INT);
}


/* Receiver interrupt handler */
static void
scc_rxint(register struct scc_channel *scc)
{
	register struct mbuf *bp;

	scc->stat.rxints++;

	if( Running(t_maxk) && !(scc->kiss.fulldup))
	{
		Inb(scc->data);		/* discard char */
		or(scc,R3,ENT_HM);	/* enter hunt mode for next flag */
		return;
	}

	bp = scc->rx_bp;
	
	if (bp == NULLBUF)
	{
		bp = scc_get_buffer(scc, BT_RECEIVE);
		if (bp == NULLBUF)
		{
			printk("scc_rxint(): panic --- cannot get a buffer\n");
			Inb(scc->data);
			or(scc, R3, ENT_HM);
			scc->stat.nospace++;
			return;
		}
		
		scc->rx_bp = bp;
	}
	
	if (bp->cnt > scc->stat.bufsize)
	{
#ifdef notdef
		printk("scc_rxint(): oops, received huge frame...\n");
#endif
		scc_enqueue_buffer(&scc->rx_buffer_pool, bp);
		scc->rx_bp = NULLBUF;
		Inb(scc->data);
		or(scc, R3, ENT_HM);
		return;
	}
		
	/* now, we have a buffer. read character and store it */
	*bp->rw_ptr = Inb(scc->data);
	bp->rw_ptr++;
	bp->cnt++;
}

/* kick rx_timer (try to send received frame or part of it ASAP) */

/* of course we could define a "bottom half" routine to do the job,
   but since its structures are saved in an array instead of a linked
   list we would get in trouble if it clashes with another driver or
   when we try to modularize the driver. IMHO we are fast enough
   with a timer routine called on the next timer-INT... Your opinions?
 */

static inline void
kick_rx_timer(register struct scc_channel *scc)
{
	if (scc->rx_t.next)
		del_timer(&(scc->rx_t));
		
	scc->rx_t.expires = jiffies + 1;
	scc->rx_t.function = scc_rx_timer;
	scc->rx_t.data = (unsigned long) scc;
	add_timer(&scc->rx_t);
}

/* Receive Special Condition interrupt handler */
static void
scc_spint(register struct scc_channel *scc)
{
	register unsigned char status;
	register struct mbuf *bp;

	scc->stat.spints++;

	status = InReg(scc->ctrl,R1);		/* read receiver status */
	
	Inb(scc->data);				/* throw away Rx byte */
	bp = scc->rx_bp;

	if(status & Rx_OVR)			/* receiver overrun */
	{
		scc->stat.rx_over++;             /* count them */
		or(scc,R3,ENT_HM);               /* enter hunt mode for next flag */
		
		if (bp) scc_enqueue_buffer(&scc->rx_buffer_pool, bp);
		scc->rx_bp = NULLBUF;
	}
	
	if(status & END_FR && bp != NULLBUF)	/* end of frame */
	{
		/* CRC okay, frame ends on 8 bit boundary and received something ? */
		
		if (!(status & CRC_ERR) && (status & 0xe) == RES8 && bp->cnt)
		{
			/* ignore last received byte (first of the CRC bytes) */
			bp->cnt--;
			
			scc_enqueue_buffer(&scc->rx_queue, bp);
			scc->rx_bp = NULLBUF;
			scc->stat.rxframes++;
			scc->stat.rx_queued++;
			kick_rx_timer(scc);
		} else {				/* a bad frame */
			scc_enqueue_buffer(&scc->rx_buffer_pool, bp);
			scc->rx_bp = NULLBUF;
			scc->stat.rxerrs++;
		}
	}
	
	Outb(scc->ctrl,ERR_RES);
}


/* ******************************************************************** */
/* *			Init Channel					*/
/* ******************************************************************** */


/* ----> set SCC channel speed <---- */

static inline void set_brg(register struct scc_channel *scc, unsigned int tc)
{
	unsigned long flags;

 	save_flags(flags); cli();      	/* 2-step register accesses... */

	cl(scc,R14,BRENABL);		/* disable baudrate generator */
	wr(scc,R12,tc & 255);		/* brg rate LOW */
	wr(scc,R13,tc >> 8);   		/* brg rate HIGH */
	or(scc,R14,BRENABL);		/* enable baudrate generator */

	restore_flags(flags);
}

static inline void set_speed(register struct scc_channel *scc)
{
	if (scc->modem.speed > 0)	/* paranoia... */
		set_brg(scc, (unsigned) (scc->clock / (scc->modem.speed * 64)) - 2);
}


/* ----> initialize a SCC channel <---- */

static inline void init_brg(register struct scc_channel *scc)
{
	wr(scc, R14, BRSRC);				/* BRG source = PCLK */
	OutReg(scc->ctrl, R14, SSBR|scc->wreg[R14]);	/* DPLL source = BRG */
	OutReg(scc->ctrl, R14, SNRZI|scc->wreg[R14]);	/* DPLL NRZI mode */
}

/*
 * Initialization according to the Z8530 manual (SGS-Thomson's version):
 *
 * 1. Modes and constants
 *
 * WR9	11000000	chip reset
 * WR4	XXXXXXXX	Tx/Rx control, async or sync mode
 * WR1	0XX00X00	select W/REQ (optional)
 * WR2	XXXXXXXX	program interrupt vector
 * WR3	XXXXXXX0	select Rx control
 * WR5	XXXX0XXX	select Tx control
 * WR6	XXXXXXXX	sync character
 * WR7	XXXXXXXX	sync character
 * WR9	000X0XXX	select interrupt control
 * WR10	XXXXXXXX	miscellaneous control (optional)
 * WR11	XXXXXXXX	clock control
 * WR12	XXXXXXXX	time constant lower byte (optional)
 * WR13	XXXXXXXX	time constant upper byte (optional)
 * WR14	XXXXXXX0	miscellaneous control
 * WR14	XXXSSSSS	commands (optional)
 *
 * 2. Enables
 *
 * WR14	000SSSS1	baud rate enable
 * WR3	SSSSSSS1	Rx enable
 * WR5	SSSS1SSS	Tx enable
 * WR0	10000000	reset Tx CRG (optional)
 * WR1	XSS00S00	DMA enable (optional)
 *
 * 3. Interrupt status
 *
 * WR15	XXXXXXXX	enable external/status
 * WR0	00010000	reset external status
 * WR0	00010000	reset external status twice
 * WR1	SSSXXSXX	enable Rx, Tx and Ext/status
 * WR9	000SXSSS	enable master interrupt enable
 *
 * 1 = set to one, 0 = reset to zero
 * X = user defined, S = same as previous init
 *
 *
 * Note that the implementation differs in some points from above scheme.
 *
 */
 
static void
init_channel(register struct scc_channel *scc)
{
	unsigned long flags;
	
	if (scc->rx_t.next) del_timer(&(scc->rx_t));
	if (scc->tx_t.next) del_timer(&(scc->tx_t));

	save_flags(flags); cli();

	wr(scc,R4,X1CLK|SDLC);		/* *1 clock, SDLC mode */
	wr(scc,R1,0);			/* no W/REQ operation */
	wr(scc,R3,Rx8|RxCRC_ENAB);	/* RX 8 bits/char, CRC, disabled */	
	wr(scc,R5,Tx8|DTR|TxCRC_ENAB);	/* TX 8 bits/char, disabled, DTR */
	wr(scc,R6,0);			/* SDLC address zero (not used) */
	wr(scc,R7,FLAG);		/* SDLC flag value */
	wr(scc,R9,VIS);			/* vector includes status */
	wr(scc,R10,(scc->modem.nrz? NRZ : NRZI)|CRCPS|ABUNDER); /* abort on underrun, preset CRC generator, NRZ(I) */
	wr(scc,R14, 0);


/* set clock sources:

   CLK_DPLL: normal halfduplex operation
   
		RxClk: use DPLL
		TxClk: use DPLL
		TRxC mode DPLL output
		
   CLK_EXTERNAL: external clocking (G3RUH or DF9IC modem)
   
  	        BayCom: 		others:
  	        
  	        TxClk = pin RTxC	TxClk = pin TRxC
  	        RxClk = pin TRxC 	RxClk = pin RTxC
  	     

   CLK_DIVIDER:
   		RxClk = use DPLL
   		TxClk = pin RTxC
   		
   		BayCom:			others:
   		pin TRxC = DPLL		pin TRxC = BRG
   		(RxClk * 1)		(RxClk * 32)
*/  

   		
	switch(scc->modem.clocksrc)
	{
		case CLK_DPLL:
			wr(scc, R11, RCDPLL|TCDPLL|TRxCOI|TRxCDP);
			init_brg(scc);
			break;

		case CLK_DIVIDER:
			wr(scc, R11, ((scc->brand & BAYCOM)? TRxCDP : TRxCBR) | RCDPLL|TCRTxCP|TRxCOI);
			init_brg(scc);
			break;

		case CLK_EXTERNAL:
			wr(scc, R11, (scc->brand & BAYCOM)? RCTRxCP|TCRTxCP : RCRTxCP|TCTRxCP);
			OutReg(scc->ctrl, R14, DISDPLL);
			break;

	}
	
	set_speed(scc);			/* set baudrate */
	
	if(scc->enhanced)
	{
		or(scc,R15,SHDLCE|FIFOE);	/* enable FIFO, SDLC/HDLC Enhancements (From now R7 is R7') */
		wr(scc,R7,AUTOEOM);
	}

	if((InReg(scc->ctrl,R0)) & DCD)		/* DCD is now ON */
	{
		if (scc->modem.clocksrc != CLK_EXTERNAL)
			or(scc,R14, SEARCH);
			
		or(scc,R3,ENT_HM|RxENABLE);	/* enable the receiver, hunt mode */
	}
	
	/* enable CTS (not for Baycom), ABORT & DCD interrupts */
	wr(scc,R15,((scc->brand & BAYCOM) ? 0 : CTSIE)|BRKIE|DCDIE|TxUIE);
	
	Outb(scc->ctrl,RES_EXT_INT);	/* reset ext/status interrupts */
	Outb(scc->ctrl,RES_EXT_INT);	/* must be done twice */

	or(scc,R1,INT_ALL_Rx|TxINT_ENAB|EXT_INT_ENAB); /* enable interrupts */
	
	scc->status = InReg(scc->ctrl,R0);	/* read initial status */
	
	or(scc,R9,MIE);			/* master interrupt enable */
	
	scc_init_timer(scc);
			
	restore_flags(flags);
}




/* ******************************************************************** */
/* *			SCC timer functions			      * */
/* ******************************************************************** */


/* ----> scc_key_trx sets the time constant for the baudrate 
         generator and keys the transmitter		     <---- */

static void
scc_key_trx(struct scc_channel *scc, char tx)
{
	unsigned int time_const;

	if (scc->modem.speed < baud_table[1]) 
		scc->modem.speed = 1200;
		
	if (scc->brand & PRIMUS)
		Outb(scc->ctrl + 4, scc->option | (tx? 0x80 : 0));
	
	time_const = (unsigned) (scc->clock / (scc->modem.speed * (tx? 2:64))) - 2;
	
	if (scc->modem.clocksrc == CLK_DPLL)
	{				/* simplex operation */
		if (tx)
		{
			cl(scc,R3,RxENABLE|ENT_HM);	/* then switch off receiver */
			
			set_brg(scc, time_const);	/* reprogram baudrate generator */

			/* DPLL -> Rx clk, BRG -> Tx CLK, TRxC mode output, TRxC = BRG */
			wr(scc, R11, RCDPLL|TCBR|TRxCOI|TRxCBR);
			
			or(scc,R5,RTS|TxENAB);		/* set the RTS line and enable TX */
		} else {
			cl(scc,R5,RTS|TxENAB);
			
			set_brg(scc, time_const);	/* reprogram baudrate generator */
			
			/* DPLL -> Rx clk, DPLL -> Tx CLK, TRxC mode output, TRxC = DPLL */
			wr(scc, R11, RCDPLL|TCDPLL|TRxCOI|TRxCDP);
			
			or(scc,R3,RxENABLE|ENT_HM);
		}
	} else {
		if (tx)
			or(scc,R5,RTS|TxENAB);		/* enable tx */
		else
			cl(scc,R5,RTS|TxENAB);		/* disable tx */
	}
}


/* ----> SCC timer interrupt handler and friends. Will be called every 1/TPS s <---- */

static unsigned char Rand = 17;

static inline int is_grouped(register struct scc_channel *scc)
{
	int k;
	struct scc_channel *scc2;
	unsigned char grp1, grp2;

	grp1 = scc->kiss.group;
	
	for (k = 0; k < (Nchips * 2); k++)
	{
		scc2 = &SCC_Info[k];
		grp2 = scc2->kiss.group;
		
		if (scc2 == scc || !(scc2->tty && grp2)) 
			return 0;
		
		if ((grp1 & 0x3f) == (grp2 & 0x3f))
		{
			if ( (grp1 & TXGROUP) && (scc2->wreg[R5] & RTS) )
				return 1;
			
			if ( (grp1 & RXGROUP) && (scc2->status & DCD) )
				return 1;
		}
	}
	return 0;
}
		

static inline void dw_slot_timeout(register struct scc_channel *scc)
{
	scc->t_dwait = TIMER_STOPPED;
	scc->t_slot = TIMER_STOPPED;
	
	if (!scc->kiss.fulldup)
	{
		Rand = Rand * 17 + 31;
		
		if ( (scc->kiss.softdcd? !(scc->status & SYNC_HUNT):(scc->status & DCD))  || (scc->kiss.persist) < Rand || (scc->kiss.group && is_grouped(scc)) )
		{
			if (scc->t_mbusy == TIMER_STOPPED)
				scc->t_mbusy = TPS * scc->kiss.maxdefer;
				
			scc->t_slot = scc->kiss.slottime;
			return ;
			
		}
	}
	
	if ( !(scc->wreg[R5] & RTS) )
	{
		scc->t_txdel = scc->kiss.txdelay;
		scc_key_trx(scc, TX_ON);
	} else {
		scc->t_txdel = 0;
	}
}




static inline void txdel_timeout(register struct scc_channel *scc)
{
	scc->t_txdel = TIMER_STOPPED;
	
	scc->t_maxk = TPS * scc->kiss.maxkeyup;
	
	if (scc->tx_bp == NULLBUF)
		scc_txint(scc);	
}
	


static inline void tail_timeout(register struct scc_channel *scc)
{
	scc->t_tail = TIMER_STOPPED;
	
	/* when fulldup is 0 or 1, switch off the transmitter.
	 * when frames are still queued (because of transmit time limit),
	 * restart the procedure to get the channel after MINTIME.
	 * when fulldup is 2, the transmitter remains keyed and we
	 * continue sending after waiting for waittime. IDLETIME is an 
	 * idle timeout in this case.
	 */ 
	 
	 if (scc->kiss.fulldup < 2)
	 {
	 	if (scc->tx_bp)		/* we had a timeout? */
	 	{
	 		scc->stat.tx_state = TXS_BUSY;
	 		scc->t_dwait = TPS * scc->kiss.mintime; /* try again */
	 	}
	 	
	 	scc->stat.tx_state = TXS_IDLE;
		scc->t_maxk = TIMER_STOPPED;
	 	scc_key_trx(scc, TX_OFF);
	 	return;
	 }
	 
	 if (scc->tx_bp)			/* maxkeyup expired */ /* ?! */
	 {
	 	scc->stat.tx_state = TXS_BUSY;
	 	scc->t_txdel = TPS * scc->kiss.waittime;
	 }
	 else
	 
	 	scc->t_idle = TPS * scc->kiss.idletime;
}


static inline void busy_timeout(register struct scc_channel *scc)
{
#ifdef	THROW_AWAY_AFTER_BUSY_TIMEOUT
	register struct mbuf *bp;		/* not tested */

	while (bp = scc_dequeue_buffer(&scc->tx_queue))
		scc_enqueue_buffer(&scc->tx_buffer_pool, bp);
		
	scc->tx_queue = NULLBUF;
	scc->stat.tx_state = TXS_IDLE;
	
#else
	scc->t_txdel = scc->kiss.txdelay;	/* brute force ... */
#endif
	scc->t_mbusy = TIMER_STOPPED;
	
}


static inline void maxk_idle_timeout(register struct scc_channel *scc)
{
	scc->t_maxk = TIMER_STOPPED;
	scc->t_idle = TIMER_STOPPED;
	
	scc->stat.tx_state = TXS_BUSY;
	scc->t_tail = scc->kiss.tailtime;
}

static void
scc_tx_timer(unsigned long channel)
{
	register struct scc_channel *scc;
	unsigned long flags;
		

	scc = (struct scc_channel *) channel;
		
	if (scc->tty && scc->init)
	{		
		save_flags(flags); cli();
		
		/* KISS-TNC emulation */
		
		if (Expired(t_dwait)) dw_slot_timeout(scc)	; else
		if (Expired(t_slot))  dw_slot_timeout(scc)	; else
		if (Expired(t_txdel)) txdel_timeout(scc)   	; else
		if (Expired(t_tail))  tail_timeout(scc)	   	;
			
		/* watchdogs */
		
		if (Expired(t_mbusy)) busy_timeout(scc);
		if (Expired(t_maxk))  maxk_idle_timeout(scc);
		if (Expired(t_idle))  maxk_idle_timeout(scc);
			
		restore_flags(flags);
	}
	
	scc->tx_t.expires = jiffies + HZ/TPS;
	add_timer(&scc->tx_t);
}
			

static void
scc_rx_timer(unsigned long channel)
{
	register struct scc_channel *scc;
	
	scc = (struct scc_channel *) channel;
	
	if (scc->rx_queue && scc->throttled)
	{
		scc->rx_t.expires = jiffies + HZ/TPS;
		add_timer(&scc->rx_t);
		return;
	}
	
	kiss_encode(scc);
	
	if (scc->rx_queue && !scc->throttled)
	{

		printk("z8530drv: warning: %s should be throttled\n", 
		       kdevname(scc->tty->device));
			       
		scc->rx_t.expires = jiffies + HZ/TPS;
		add_timer(&scc->rx_t);
	}
}

static void
scc_init_timer(struct scc_channel *scc)
{
	unsigned long flags;
	
	save_flags(flags); cli();
	
	Stop_Timer(t_dwait);
	Stop_Timer(t_slot);
	Stop_Timer(t_txdel);
	Stop_Timer(t_tail);
	Stop_Timer(t_mbusy);
	Stop_Timer(t_maxk);
	Stop_Timer(t_idle);
	
	scc->stat.tx_state = TXS_IDLE;
	
	if (scc->tx_t.next) 
		del_timer(&scc->tx_t);
	
	scc->tx_t.data = (unsigned long) scc;
	scc->tx_t.function = scc_tx_timer;
	scc->tx_t.expires = jiffies + HZ/TPS;
	add_timer(&scc->tx_t);
	
	scc->rx_t.data = (unsigned long) scc;
	scc->rx_t.function = scc_rx_timer;
	
	restore_flags(flags);
}


/* ******************************************************************** */
/* *			KISS interpreter			      * */
/* ******************************************************************** */


/*
 * this will set the "kiss" parameters through kiss itself
 */
 
static void
kiss_set_param(struct scc_channel *scc,char cmd, unsigned int val)
{

#define  VAL val=val*TPS/100
#define SVAL val? val:TIMER_STOPPED

	switch(cmd){
	case PARAM_TXDELAY:
		scc->kiss.txdelay = VAL; break;
	case PARAM_PERSIST:
		scc->kiss.persist = val; break;
	case PARAM_SLOTTIME:
		scc->kiss.slottime = VAL; break;
	case PARAM_TXTAIL:
		scc->kiss.tailtime = VAL; break;
	case PARAM_FULLDUP:
		scc->kiss.fulldup = val; break;
	case PARAM_WAIT:
		scc->kiss.waittime = VAL; break;
	case PARAM_MAXKEY:
		scc->kiss.maxkeyup = SVAL; break;
	case PARAM_MIN:
		scc->kiss.mintime = SVAL; break;
	case PARAM_IDLE:
		scc->kiss.idletime = val; break;
	case PARAM_MAXDEFER:
		scc->kiss.maxdefer = SVAL; break;
	case PARAM_GROUP:
		scc->kiss.group = val;  break;
	case PARAM_TX:
		scc->kiss.tx_inhibit = val;
	case PARAM_SOFTDCD:
		scc->kiss.softdcd = val;
	}
	return;

#undef  VAL
#undef SVAL
}


/* interpret frame: strip CRC and decode KISS */

static void kiss_interpret_frame(struct scc_channel * scc)
{
	unsigned char kisscmd;
	unsigned long flags;
	struct mbuf *bp;

	bp = scc->kiss_decode_bp;
	bp->rw_ptr = bp->data;
	
#ifdef DEBUG_BUFFERS
	if (bp == NULLBUF)
	{
		printk("kiss_interpret_frame(): weird --- nothing to do.\n");
		return;
	}
#endif
	
	if (bp->cnt < 2)
	{
		scc_enqueue_buffer(&scc->tx_buffer_pool, bp);
		scc->kiss_decode_bp = NULLBUF;
		return;
	}
	
	
	
	if (scc->kiss.not_slip)
	{
		kisscmd = *bp->rw_ptr;
		bp->rw_ptr++;
	} else {
		kisscmd = 0;
	}

	if (kisscmd & 0xa0)
	{
		if (bp->cnt > 3) 
			bp->cnt -= 2;
		else
		{
			scc_enqueue_buffer(&scc->tx_buffer_pool, bp);
			scc->kiss_decode_bp = NULLBUF;
			return;
		}
	}
	
	
	kisscmd &= 0x1f;
	
		
	if (kisscmd)
	{
		kiss_set_param(scc, kisscmd, *bp->rw_ptr);
		scc_enqueue_buffer(&scc->tx_buffer_pool, bp);
		scc->kiss_decode_bp = NULLBUF;
		return;
	}

	scc_enqueue_buffer(&scc->tx_queue, bp);	/* enqueue frame */
	
	scc->stat.txframes++;
	scc->stat.tx_queued++;
	scc->kiss_decode_bp = NULLBUF;

	save_flags(flags); cli();

	if(scc->stat.tx_state == TXS_IDLE)
	{      				/* when transmitter is idle */
		scc->stat.tx_state = TXS_BUSY;
		scc->t_dwait = (scc->kiss.fulldup? 0:scc->kiss.waittime);
	}
	
	restore_flags(flags);
}

static inline void kiss_store_byte(struct scc_channel *scc, unsigned char ch)
{
	register struct mbuf *bp = scc->kiss_decode_bp;
	
	if (bp != NULLBUF)
	{
		if (bp->cnt > scc->stat.bufsize)
			printk("kiss_decode(): frame too long\n");
		else
		{
			*bp->rw_ptr = ch;
			bp->rw_ptr++;
			bp->cnt++;
		}
	}
}

static inline int kiss_decode(struct scc_channel *scc, unsigned char ch)
{
	switch (scc->stat.tx_kiss_state) 
	{
		case KISS_IDLE:
			if (ch == FEND)
			{
				scc->kiss_decode_bp = scc_get_buffer(scc, BT_TRANSMIT);
				if (scc->kiss_decode_bp == NULLBUF)
					return 1;

				scc->stat.tx_kiss_state = KISS_DATA;
			} else scc->stat.txerrs++;
			break;
					
		case KISS_DATA:
			if (ch == FESC)
				scc->stat.tx_kiss_state = KISS_ESCAPE;
			else if (ch == FEND)
			{
				kiss_interpret_frame(scc);
				scc->stat.tx_kiss_state = KISS_IDLE;
			}
			else kiss_store_byte(scc, ch);
			break;
					
		case KISS_ESCAPE:
			if (ch == TFEND)
			{
				kiss_store_byte(scc, FEND);
				scc->stat.tx_kiss_state = KISS_DATA;
			}
			else if (ch == TFESC)
			{
				kiss_store_byte(scc, FESC);
				scc->stat.tx_kiss_state = KISS_DATA;
			}
			else
			{
				scc_enqueue_buffer(&scc->tx_buffer_pool, scc->kiss_decode_bp);
				scc->kiss_decode_bp = NULLBUF;
				scc->stat.txerrs++;
				scc->stat.tx_kiss_state = KISS_IDLE;
			}
			break;
	} /* switch */
	
	return 0;
	
}

/* ----> Encode received data and write it to the flip-buffer  <---- */

static void
kiss_encode(register struct scc_channel *scc)
{
	struct mbuf *bp;
	struct tty_struct * tty = scc->tty;
	unsigned char ch;

	bp = scc->kiss_encode_bp;
	
	/* worst case: FEND 0 FESC TFEND -> 4 bytes */
	
	while(tty->flip.count < TTY_FLIPBUF_SIZE-4)
	{
		if (bp == NULLBUF)
		{
			bp = scc_dequeue_buffer(&scc->rx_queue);
			scc->kiss_encode_bp = bp;
			
			if (bp == NULLBUF)
			{
				scc->stat.rx_kiss_state = KISS_IDLE;
				break;
			}
		}
		

		if (bp->cnt <= 0)
		{
			if (--scc->stat.rx_queued < 0)
				scc->stat.rx_queued = 0;
					
			if (scc->stat.rx_kiss_state == KISS_RXFRAME)	/* new packet? */
			{
				tty_insert_flip_char(tty, FEND, 0);  /* send FEND for old frame */
				scc->stat.rx_kiss_state = KISS_IDLE; /* generate FEND for new frame */
			}
			
			scc_enqueue_buffer(&scc->rx_buffer_pool, bp);
			
			bp = scc->kiss_encode_bp = NULLBUF;
			continue;
		}
		

		if (scc->stat.rx_kiss_state == KISS_IDLE)
		{
			tty_insert_flip_char(tty, FEND, 0);
			
			if (scc->kiss.not_slip)
				tty_insert_flip_char(tty, 0, 0);
					
			scc->stat.rx_kiss_state = KISS_RXFRAME;
		}
				
		switch(ch = *bp->rw_ptr)
		{
			case FEND:
				tty_insert_flip_char(tty, FESC, 0);
				tty_insert_flip_char(tty, TFEND, 0);
				break;
			case FESC:
				tty_insert_flip_char(tty, FESC, 0);
				tty_insert_flip_char(tty, TFESC, 0);
				break;
			default:
				tty_insert_flip_char(tty, ch, 0);
		}
			
		bp->rw_ptr++;
		bp->cnt--;
	}
		
 	queue_task(&tty->flip.tqueue, &tq_timer); /* kick it... */
}


/* ******************************************************************* */
/* *		Init channel structures, special HW, etc...	     * */
/* ******************************************************************* */


static void
z8530_init(void)
{
	struct scc_channel *scc;
	int chip, k;
	unsigned long flags;
	char *flag;


	printk("Init Z8530 driver: %u channels, IRQ", Nchips*2);
	
	flag=" ";
	for (k = 0; k < 16; k++)
		if (Ivec[k].used) 
		{
			printk("%s%d", flag, k);
			flag=",";
		}
	printk("\n");
	

	/* reset and pre-init all chips in the system */
	for (chip = 0; chip < Nchips; chip++)
	{
		scc=&SCC_Info[2*chip];
		if (!scc->ctrl) continue;
			
		save_flags(flags); cli();	/* because of 2-step accesses */
		
		/* Special SCC cards */

		if(scc->brand & EAGLE)			/* this is an EAGLE card */
			Outb(scc->special,0x08);	/* enable interrupt on the board */
			
		if(scc->brand & (PC100 | PRIMUS))	/* this is a PC100/EAGLE card */
			Outb(scc->special,scc->option);	/* set the MODEM mode (0x22) */

			
		/* Init SCC */

		/* some general init we can do now */
		
		Outb(scc->ctrl, 0);
		OutReg(scc->ctrl,R9,FHWRES);		/* force hardware reset */
		udelay(100);				/* give it 'a bit' more time than required */
		wr(scc, R2, chip*16);			/* interrupt vector */
		wr(scc, R9, VIS);			/* vector includes status */

        	restore_flags(flags);
        }

 
	Driver_Initialized = 1;
}


/* ******************************************************************** */
/* * 	Filesystem Routines: open, close, ioctl, settermios, etc      * */
/* ******************************************************************** */



/* scc_paranoia_check(): warn user if something went wrong		*/

static inline int scc_paranoia_check(struct scc_channel *scc, kdev_t device,
				     const char *routine)
{
#ifdef SCC_PARANOIA_CHECK

static const char *badmagic = 
	"Warning: bad magic number for Z8530 SCC struct (%s) in %s\n"; 
static const char *badinfo =  
	"Warning: Z8530 not found for (%s) in %s\n";
       
	if (!scc->init) 
	{
       		printk(badinfo, kdevname(device), routine);
		return 1;
	}
	
	if (scc->magic != SCC_MAGIC)
	{
		printk(badmagic, kdevname(device), routine);
		return 1;
	}
#endif

	return 0;
}


/* ----> this one is called whenever you open the device <---- */

int scc_open(struct tty_struct *tty, struct file * filp)
{
	struct scc_channel *scc;
	int chan;
	
	chan = MINOR(tty->device) - tty->driver.minor_start;
	
	if (Driver_Initialized)
	{
		if ( (chan < 0) || (chan >= (Nchips * 2)) )
                	return -ENODEV;
        } else {
        	tty->driver_data = &SCC_Info[0];
        	MOD_INC_USE_COUNT;
        	return 0;
        }
 
 	scc = &SCC_Info[chan];
 	
	tty->driver_data = scc;
 	tty->termios->c_cflag &= ~CBAUD;
 	
	if (scc->magic != SCC_MAGIC)
	{
		printk("ERROR: scc_open(): bad magic number for device (%s)",
		       kdevname(tty->device));
		return -ENODEV;
	}		
	
	MOD_INC_USE_COUNT;
	
	if(scc->tty != NULL)
	{
		scc->tty_opened++;
		return 0;
	}

  	scc->tty = tty;
	alloc_buffer_pool(scc);
	
 	if(!scc->init) return 0;
 	
	scc->throttled = 0;

	scc->stat.tx_kiss_state = KISS_IDLE;	/* don't change this... */
	scc->stat.rx_kiss_state = KISS_IDLE;	/* ...or this */
 
	init_channel(scc);
	return 0;
}


/* ----> and this whenever you close the device <---- */

static void
scc_close(struct tty_struct *tty, struct file * filp)
{
	struct scc_channel *scc = tty->driver_data;
	unsigned long flags;

        if (!scc || (scc->magic != SCC_MAGIC))
                return;
                
        MOD_DEC_USE_COUNT;
	
	if(scc->tty_opened)
	{
		scc->tty_opened--;
		return;
	}
	
	tty->driver_data = NULLBUF;
	
	if (!Driver_Initialized)
		return;
	
	save_flags(flags); cli();
	
	Outb(scc->ctrl,0);		/* Make sure pointer is written */
	wr(scc,R1,0);			/* disable interrupts */
	wr(scc,R3,0);
	
	scc->tty = NULL;
	
	del_timer(&scc->tx_t);
	del_timer(&scc->rx_t);

	free_buffer_pool(scc);
	
	restore_flags(flags);
	
	scc->throttled = 0;
	tty->stopped = 0;
}


/*
 * change scc_speed
 */
 
static void
scc_change_speed(struct scc_channel * scc)
{
	long speed;
	
	if (scc->tty == NULL)
		return;
		

	speed = baud_table[scc->tty->termios->c_cflag & CBAUD];
	
	if (speed > 0) scc->modem.speed = speed;
	
	if (scc->stat.tx_state == 0)	/* only switch baudrate on rx... ;-) */
		set_speed(scc);
}


/* ----> ioctl-routine of the driver <---- */

/* perform ioctl on SCC (sdlc) channel
 * this is used for AX.25 mode, and will set the "kiss" parameters
 */
 
/* TIOCMGET 	- get modem status	arg: (unsigned long *) arg
 * TIOCMBIS 	- set PTT		arg: ---
 * TIOCMBIC 	- reset PTT		arg: ---
 * TIOCMBIC 	- set PTT		arg: ---
 * TIOCSCCINI	- initialize driver	arg: ---
 * TIOCCHANINI	- initialize channel	arg: (struct scc_modem *) arg
 * TIOCGKISS	- get level 1 parameter	arg: (struct ioctl_command *) arg
 * TIOCSKISS	- set level 1 parameter arg: (struct ioctl_command *) arg
 * TIOCSCCSTAT  - get driver status	arg: (struct scc_stat *) arg
 */
 

static int
scc_ioctl(struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct scc_channel * scc = tty->driver_data;
	unsigned long flags, r;
	unsigned int result;
	unsigned int value;
	struct ioctl_command kiss_cmd;
	struct scc_mem_config memcfg;
	struct scc_hw_config hwcfg;
	int error, chan;

        if (scc->magic != SCC_MAGIC) 
        {
		printk("ERROR: scc_ioctl(): bad magic number for device %s",
			kdevname(tty->device));
			
                return -ENODEV;
        }
	                                                 
	r = NO_SUCH_PARAM;
	
	if (!Driver_Initialized)
	{
		if (cmd == TIOCSCCCFG)
		{
			int found = 1;
			
			if (!suser()) return -EPERM;
			if (!arg) return -EFAULT;
			
			if (Nchips >= MAXSCC) 
				return -EINVAL;
			
			memcpy_fromfs(&hwcfg, (void *) arg, sizeof(hwcfg));
			
			if (hwcfg.irq == 2) hwcfg.irq = 9;
			
			if (!Ivec[hwcfg.irq].used && hwcfg.irq)
			{
				if (request_irq(hwcfg.irq, scc_isr, SA_INTERRUPT, "AX.25 SCC", NULL))
					printk("z8530drv: Warning --- could not get IRQ %d\n", hwcfg.irq);
				else
					Ivec[hwcfg.irq].used = 1;
			}
			
			if (hwcfg.vector_latch) 
				Vector_Latch = hwcfg.vector_latch;
				
			if (hwcfg.clock == 0)
				hwcfg.clock = DEFAULT_CLOCK;

#ifndef DONT_CHECK
			save_flags(flags); cli();
			
			check_region(scc->ctrl, 1);
			Outb(hwcfg.ctrl_a, 0);
			udelay(5);
			OutReg(hwcfg.ctrl_a,R13,0x55);		/* is this chip really there? */
			udelay(5);
			
			if (InReg(hwcfg.ctrl_a,R13) != 0x55 )
				found = 0;
				
			restore_flags(flags);
#endif

			if (found)
			{
				SCC_Info[2*Nchips  ].ctrl = hwcfg.ctrl_a;
				SCC_Info[2*Nchips  ].data = hwcfg.data_a;
				SCC_Info[2*Nchips+1].ctrl = hwcfg.ctrl_b;
				SCC_Info[2*Nchips+1].data = hwcfg.data_b;
			
				SCC_ctrl[2*Nchips  ] = hwcfg.ctrl_a;
				SCC_ctrl[2*Nchips+1] = hwcfg.ctrl_b;
			}
		
			for (chan = 0; chan < 2; chan++)
			{
				SCC_Info[2*Nchips+chan].special = hwcfg.special;
				SCC_Info[2*Nchips+chan].clock = hwcfg.clock;
				SCC_Info[2*Nchips+chan].brand = hwcfg.brand;
				SCC_Info[2*Nchips+chan].option = hwcfg.option;
				SCC_Info[2*Nchips+chan].enhanced = hwcfg.escc;
			
#ifdef DONT_CHECK
				printk("%s%i: data port = 0x%3.3x  control port = 0x%3.3x\n",
					scc_driver.name, 2*Nchips+chan, 
					SCC_Info[2*Nchips+chan].data, 
					SCC_Info[2*Nchips+chan].ctrl);

#else
				printk("%s%i: data port = 0x%3.3x  control port = 0x%3.3x -- %s\n",
					scc_driver.name, 2*Nchips+chan, 
					chan? hwcfg.data_b : hwcfg.data_a, 
					chan? hwcfg.ctrl_b : hwcfg.ctrl_a,
					found? "found" : "missing");
#endif
				
				if (found)
				{
					request_region(SCC_Info[2*Nchips+chan].ctrl, 1, "scc ctrl");
					request_region(SCC_Info[2*Nchips+chan].data, 1, "scc data");
				}
			}
			
			if (found) Nchips++;
			
			return 0;
		}
		
		if (cmd == TIOCSCCINI)
		{
			if (!suser())
				return -EPERM;
				
			if (Nchips == 0)
				return -EINVAL;
			
			z8530_init();
			
			scc->tty=tty;
			alloc_buffer_pool(scc);
			return 0;
		}
		
		return -EINVAL;	/* confuse the user */
	}
	
	if (!scc->init)
	{
		if (cmd == TIOCCHANINI)
		{
			if (!arg)
				return -EFAULT;
				
			if (!suser())
				return -EPERM;
			
			memcpy_fromfs(&scc->modem, (void *) arg, sizeof(struct scc_modem));
			
			/* default KISS Params */
		
			if (scc->modem.speed < 4800)
			{
				scc->kiss.txdelay = 36*TPS/100;    /* 360 ms */
				scc->kiss.persist = 42;            /* 25% persistence */			/* was 25 */
				scc->kiss.slottime = 16*TPS/100;   /* 160 ms */
				scc->kiss.tailtime = 4;            /* minimal reasonable value */
				scc->kiss.fulldup = 0;             /* CSMA */
				scc->kiss.waittime = 50*TPS/100;   /* 500 ms */
				scc->kiss.maxkeyup = 10;           /* 10 s */
				scc->kiss.mintime = 3;             /* 3 s */
				scc->kiss.idletime = 30;           /* 30 s */
				scc->kiss.maxdefer = 120;	   /* 2 min */
				scc->kiss.not_slip = 1;		   /* KISS mode */
				scc->kiss.softdcd = 0;		   /* hardware dcd */
			} else {
				scc->kiss.txdelay = 10*TPS/100;    /* 100 ms */
				scc->kiss.persist = 64;            /* 25% persistence */			/* was 25 */
				scc->kiss.slottime = 8*TPS/100;    /* 160 ms */
				scc->kiss.tailtime = 1;            /* minimal reasonable value */
				scc->kiss.fulldup = 0;             /* CSMA */
				scc->kiss.waittime = 50*TPS/100;   /* 500 ms */
				scc->kiss.maxkeyup = 7;            /* 7 s */
				scc->kiss.mintime = 3;             /* 3 s */
				scc->kiss.idletime = 30;           /* 30 s */
				scc->kiss.maxdefer = 120;	   /* 2 min */
				scc->kiss.not_slip = 1;		   /* KISS mode */
				scc->kiss.softdcd = 0;		   /* hardware dcd */
			}
			
			scc->init = 1;			
			
			return 0;
		}
		
		return -EINVAL;
	}

	switch(cmd){
	case TCSBRK:
		return 0;
	case TIOCMGET:
		error = verify_area(VERIFY_WRITE, (void *) arg,sizeof(unsigned int *));
		if (error)
			return error;

		save_flags(flags); cli();
		
		result =  ((scc->wreg[R5] & RTS) ? TIOCM_RTS : 0)
			| ((scc->wreg[R5] & DTR) ? TIOCM_DTR : 0)
			| ((InReg(scc->ctrl,R0) & DCD)  ? TIOCM_CAR : 0)
			| ((InReg(scc->ctrl,R0) & CTS)  ? TIOCM_CTS : 0);
		
		restore_flags(flags);
			
		put_user(result,(unsigned int *) arg);
		return 0;
	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		switch (cmd) {
		case TIOCMBIS:
			scc->wreg[R5] |= DTR;
			scc->wreg[R5] |= RTS;
			break;
		case TIOCMBIC:
			scc->wreg[R5] &= ~DTR;
			scc->wreg[R5] &= ~RTS;
			break;
		case TIOCMSET:
			value = get_user((unsigned int *) arg);
			
			if(value & TIOCM_DTR)
				scc->wreg[R5] |= DTR;
			else
				scc->wreg[R5] &= ~DTR;
			if(value & TIOCM_RTS)
				scc->wreg[R5] |= RTS;
			else
				scc->wreg[R5] &= ~RTS;
			break;
		}
		
		save_flags(flags); cli();
		
		if(scc->stat.tx_state == TXS_IDLE && !Running(t_idle))
			maxk_idle_timeout(scc);
			
		restore_flags(flags);
		
		return 0;
		
	case TCGETS:
		error = verify_area(VERIFY_WRITE, (void *) arg, sizeof(struct termios));
		if (error)
			return error;
		if (!arg) 
			return -EFAULT;
			
		memcpy_tofs((void *) arg, scc->tty->termios, sizeof(struct termios));
		return 0;
		
	case TCSETS:
	case TCSETSF:		/* should flush first, but... */
	case TCSETSW:		/* should wait 'till flush, but... */
		if (!arg)
			return -EFAULT;
		
		memcpy_fromfs(scc->tty->termios, (void *) arg, sizeof(struct termios));
		scc_change_speed(scc);
		return 0;
		
	case TIOCCHANMEM:
		if (!arg)
			return -EFAULT;
			
		memcpy_fromfs(&memcfg, (void *) arg, sizeof(struct scc_mem_config));
		
		save_flags(flags); cli();
		
		free_buffer_pool(scc);
		scc->stat.rxbuffers = memcfg.rxbuffers;
		scc->stat.txbuffers = memcfg.txbuffers;
		scc->stat.bufsize   = memcfg.bufsize;
		alloc_buffer_pool(scc);
		
		restore_flags(flags);
		return 0;
		
		
	case TIOCSCCSTAT:
		error = verify_area(VERIFY_WRITE, (void *) arg,sizeof(struct scc_stat));
		if (error)
			return error;
		
		if (!arg)
			return -EFAULT;
			
		memcpy_tofs((void *) arg, &scc->stat, sizeof(struct scc_stat));
		return 0;
		
#define TICKS (100/TPS)
#define CAST(x) (unsigned long)(x)
#define  Val	kiss_cmd.param
#define  VAL	kiss_cmd.param*TPS/100
#define SVAL	kiss_cmd.param? kiss_cmd.param:TIMER_STOPPED

	case TIOCGKISS:
		error = verify_area(VERIFY_WRITE, (void *) arg,sizeof(struct ioctl_command));
		if (error)
			return error;
			
		if (!arg)
			return -EFAULT;
			
		memcpy_fromfs(&kiss_cmd, (void *) arg, sizeof(struct ioctl_command));

		switch (kiss_cmd.command)
		{
			case PARAM_TXDELAY:	r = CAST(scc->kiss.txdelay*TICKS);	break;
			case PARAM_PERSIST:	r = CAST(scc->kiss.persist); 		break;
			case PARAM_SLOTTIME:	r = CAST(scc->kiss.slottime*TICKS);	break;
			case PARAM_TXTAIL:	r = CAST(scc->kiss.tailtime*TICKS);	break;
			case PARAM_FULLDUP:	r = CAST(scc->kiss.fulldup); 		break;
			case PARAM_SOFTDCD:	r = CAST(scc->kiss.softdcd);		break;
			case PARAM_DTR:		r = CAST((scc->wreg[R5] & DTR)? 1:0); break;
			case PARAM_RTS:		r = CAST((scc->wreg[R5] & RTS)? 1:0); break;
			case PARAM_SPEED:	r = CAST(scc->modem.speed);	break;
			case PARAM_GROUP:	r = CAST(scc->kiss.group); 		break;
			case PARAM_IDLE:	r = CAST(scc->kiss.idletime);		break;
			case PARAM_MIN:		r = CAST(scc->kiss.mintime);		break;
			case PARAM_MAXKEY:	r = CAST(scc->kiss.maxkeyup);		break;
			case PARAM_WAIT:	r = CAST(scc->kiss.waittime);		break;
			case PARAM_MAXDEFER:	r = CAST(scc->kiss.maxdefer);		break;
			case PARAM_TX:		r = CAST(scc->kiss.tx_inhibit);	break;
			case PARAM_SLIP:	r = CAST(!scc->kiss.not_slip);		break;
			default:		r = NO_SUCH_PARAM;
		}
		
		kiss_cmd.param = r;
		
		memcpy_tofs((void *) arg, &kiss_cmd, sizeof(struct ioctl_command));
		return 0;
		break;
		
	case TIOCSKISS:
		if (!arg)
			return -EFAULT;

		memcpy_fromfs(&kiss_cmd, (void *) arg, sizeof(struct ioctl_command));
		
		switch (kiss_cmd.command)
		{
			case PARAM_TXDELAY:	scc->kiss.txdelay=VAL;		break;
			case PARAM_PERSIST:	scc->kiss.persist=Val;		break;
			case PARAM_SLOTTIME:	scc->kiss.slottime=VAL;		break;
			case PARAM_TXTAIL:	scc->kiss.tailtime=VAL;		break;
			case PARAM_FULLDUP:	scc->kiss.fulldup=Val;		break;
			case PARAM_SOFTDCD:	scc->kiss.softdcd=Val;		break;
			case PARAM_DTR:		break; /* does someone need this? */
			case PARAM_RTS:		break; /* or this? */
			case PARAM_SPEED:	scc->modem.speed=Val;		break;
			case PARAM_GROUP:	scc->kiss.group=Val;		break;
			case PARAM_IDLE:	scc->kiss.idletime=Val;		break;
			case PARAM_MIN:		scc->kiss.mintime=SVAL;		break;
			case PARAM_MAXKEY:	scc->kiss.maxkeyup=SVAL;	break;
			case PARAM_WAIT:	scc->kiss.waittime=Val;		break;
			case PARAM_MAXDEFER:	scc->kiss.maxdefer=SVAL;	break;
			case PARAM_TX:		scc->kiss.tx_inhibit=Val;	break;
			case PARAM_SLIP:	scc->kiss.not_slip=!Val;	break;
			default:		return -ENOIOCTLCMD;
		}
		
		return 0;
		break;
#undef TICKS
#undef CAST
#undef VAL
#undef SVAL
#undef Val
		
	default:
		return -ENOIOCTLCMD;
    }
}


/* ----> tx routine: decode KISS data and scc_enqueue it <---- */

/* send raw frame to SCC. used for AX.25 */
int scc_write(struct tty_struct *tty, int from_user, const unsigned char *buf, int count)
{
	struct scc_channel * scc = tty->driver_data;
	unsigned char *p;
	unsigned long flags;
	int cnt, cnt2;
	
	if (!tty) return count;
	
	if (scc_paranoia_check(scc, tty->device, "scc_write"))
		return 0;

	if (scc->kiss.tx_inhibit) return count;
	
	save_flags(flags); cli();
	
	cnt2 = count;
	
	while (cnt2)
	{
		cnt   = cnt2 > BUFSIZE? BUFSIZE:cnt2;
		cnt2 -= cnt;
		
		if (from_user)
		{
			down(&scc_sem);
			memcpy_fromfs(scc_wbuf, buf, cnt);
			up(&scc_sem);
		}
		else
			memcpy(scc_wbuf, buf, cnt);
		
		/* Strange thing. The timeout of the slip driver is */
		/* very small, thus we'll wake him up now. 	    */

		if (cnt2 == 0)
		{
			wake_up_interruptible(&tty->write_wait);
		
			if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
			    tty->ldisc.write_wakeup)
				(tty->ldisc.write_wakeup)(tty);
		} else 
			buf += cnt;
			
		p=scc_wbuf;
		
		while(cnt--) 
		  if (kiss_decode(scc, *p++))
		  {
		  	scc->stat.nospace++;
		  	restore_flags(flags);
		  	return 0;
		  }		  	
	} /* while cnt2 */

	restore_flags(flags);
		
	return count;
}
				

/* put a single char into the buffer */

static void scc_put_char(struct tty_struct * tty, unsigned char ch)
{
	struct scc_channel *scc = tty->driver_data;
	unsigned char ch2;
	
	if (scc_paranoia_check(scc, tty->device, "scc_put_char"))
		return;
		
	ch2 = ch;
	scc_write(tty, 0, &ch2, 1);	/* that's all */
}

static void scc_flush_chars(struct tty_struct * tty)
{
	struct scc_channel *scc = tty->driver_data;
	
	scc_paranoia_check(scc, tty->device, "scc_flush_chars"); /* just to annoy the user... */
	
	return;	/* no flush needed */
}


static int scc_write_room(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;
	
	if (scc_paranoia_check(scc, tty->device, "scc_write_room"))
		return 0;
	
	return BUFSIZE;
}

static int scc_chars_in_buffer(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;
	
	if (scc && scc->kiss_decode_bp)
		return scc->kiss_decode_bp->cnt;
	else
		return 0;
}

static void scc_flush_buffer(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;

	if (scc_paranoia_check(scc, tty->device, "scc_flush_buffer"))
		return;
	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

static void scc_throttle(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;
	
	if (scc_paranoia_check(scc, tty->device, "scc_throttle"))
		return;

#ifdef DEBUG		
	printk("scc: scc_throttle() called for device %d\n", MINOR(tty->device));
#endif
	scc->throttled = 1;
	
	del_timer(&(scc->rx_t));
	scc->rx_t.expires = jiffies + HZ/TPS;
	add_timer(&scc->rx_t);
}

static void scc_unthrottle(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;
	
	if (scc_paranoia_check(scc, tty->device, "scc_unthrottle"))
		return;
		
#ifdef DEBUG
	printk("scc: scc_unthrottle() called for device %d\n", MINOR(tty->device));
#endif		
	scc->throttled = 0;
	del_timer(&(scc->rx_t));
	scc_tx_timer(scc->rx_t.data);
}

/* experimental, the easiest way to stop output is a fake scc_throttle */

static void scc_start(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;
	
	if (scc_paranoia_check(scc, tty->device, "scc_start"))
		return;
		
	scc_unthrottle(tty);
}

static void scc_stop(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;
	
	if (scc_paranoia_check(scc, tty->device, "scc_stop"))
		return;
		
	scc_throttle(tty);
}

static void
scc_set_termios(struct tty_struct * tty, struct termios * old_termios)
{
	struct scc_channel *scc = tty->driver_data;
	
	if (scc_paranoia_check(scc, tty->device, "scc_set_termios"))
		return;
		
	if (old_termios && (tty->termios->c_cflag == old_termios->c_cflag)) 
		return;
		
	scc_change_speed(scc);
}

static void
scc_set_ldisc(struct tty_struct * tty)
{
	struct scc_channel *scc = tty->driver_data;

	if (scc_paranoia_check(scc, tty->device, "scc_set_ldisc"))
		return;
		
	scc_change_speed(scc);
}


/* ******************************************************************** */
/* * 			Init SCC driver 			      * */
/* ******************************************************************** */

int  scc_init (void)
{
	int chip, chan, k;
	
	memset(&scc_std_termios, 0, sizeof(struct termios));
        memset(&scc_driver, 0, sizeof(struct tty_driver));
        scc_driver.magic = TTY_DRIVER_MAGIC;
        scc_driver.name = "scc";
        scc_driver.major = Z8530_MAJOR;
        scc_driver.minor_start = 0;
        scc_driver.num = MAXSCC*2;
        scc_driver.type = TTY_DRIVER_TYPE_SERIAL;
        scc_driver.subtype = 1;			/* not needed */
        scc_driver.init_termios = scc_std_termios;
        scc_driver.init_termios.c_cflag = B9600  | CREAD | CS8 | HUPCL | CLOCAL;
        scc_driver.init_termios.c_iflag = IGNBRK | IGNPAR;
        scc_driver.flags = TTY_DRIVER_REAL_RAW;
        scc_driver.refcount = &scc_refcount;
        scc_driver.table = scc_table;
        scc_driver.termios = (struct termios **) scc_termios;
        scc_driver.termios_locked = (struct termios **) scc_termios_locked;
        scc_driver.open = scc_open;
        scc_driver.close = scc_close;
        scc_driver.write = scc_write;
        scc_driver.start = scc_start;
        scc_driver.stop = scc_stop;
        
        scc_driver.put_char = scc_put_char;
        scc_driver.flush_chars = scc_flush_chars;        
	scc_driver.write_room = scc_write_room;
	scc_driver.chars_in_buffer = scc_chars_in_buffer;
	scc_driver.flush_buffer = scc_flush_buffer;
	
	scc_driver.throttle = scc_throttle;
	scc_driver.unthrottle = scc_unthrottle;
        
        scc_driver.ioctl = scc_ioctl;
        scc_driver.set_termios = scc_set_termios;
        scc_driver.set_ldisc = scc_set_ldisc;

	printk(BANNER);
        
        if (tty_register_driver(&scc_driver))
        {
		printk("Failed to register Z8530 SCC driver\n");
		return -EIO;
	}
	
	/* pre-init channel information */
	
	for (chip = 0; chip < MAXSCC; chip++)
	{
		memset((char *) &SCC_Info[2*chip  ], 0, sizeof(struct scc_channel));
		memset((char *) &SCC_Info[2*chip+1], 0, sizeof(struct scc_channel));
		
		for (chan = 0; chan < 2; chan++)
		{
			SCC_Info[2*chip+chan].magic    = SCC_MAGIC;
			SCC_Info[2*chip+chan].stat.rxbuffers = RXBUFFERS;
			SCC_Info[2*chip+chan].stat.txbuffers = TXBUFFERS;
			SCC_Info[2*chip+chan].stat.bufsize   = BUFSIZE;
		}
	}
	
	for (k = 0; k < 16; k++) Ivec[k].used = 0;

	return 0;
}

/* ******************************************************************** */
/* *			    Module support 			      * */
/* ******************************************************************** */


#ifdef MODULE
int init_module(void)
{
	int result = 0;
	
	result = scc_init();
	
	if (result == 0)
		printk("Copyright 1993,1995 Joerg Reuter DL1BKE (jreuter@lykos.tng.oche.de)\n");
		
	return result;
}

void cleanup_module(void)
{
	long flags;
	io_port ctrl;
	int k, errno;
	struct scc_channel *scc;
	
	save_flags(flags); cli();
	if ( (errno = tty_unregister_driver(&scc_driver)) )
	{
		printk("Failed to unregister Z8530 SCC driver (%d)", -errno);
		restore_flags(flags);
		return;
	}
	
	for (k = 0; k < Nchips; k++)
		if ( (ctrl = SCC_ctrl[k*2]) )
		{
			Outb(ctrl, 0);
			OutReg(ctrl,R9,FHWRES);	/* force hardware reset */
			udelay(50);
		}
		
	for (k = 0; k < Nchips*2; k++)
	{
		scc = &SCC_Info[k];
		if (scc)
		{
			release_region(scc->ctrl, 1);
			release_region(scc->data, 1);
		}
	}
	
	for (k=0; k < 16 ; k++)
		if (Ivec[k].used) free_irq(k, NULL);
		
	restore_flags(flags);
}
#endif
