#include <linux/autoconf.h>	/* fastest method */
#ifdef CONFIG_SCC


#define BANNER "Z8530 SCC driver v1.8.17test.17.7.95 PE1AYX (c)\n"

/* ******************************************************************** */
/* *   SCC.C - Linux driver for Z8530 based HDLC cards for AX.25      *	*/ 
/* ******************************************************************** */

/* ********************************************************************

	(c) Hans Alblas PE1AYX 1993,1994,1995
	          Released for GNU
	portions (c) 1995 by Joerg Reuter DL1BKE
	         (c) 1993 Guido ten Dolle PE1NNZ


   ********************************************************************	*/   

/*


   931100	- started with the projekt pe1ayx
   9406??	- KISS-parameter setting and BayCom USCC support dl1bke
   940613	- fixed memory leak ,main memory fragmentation problem,
                  speeded up the interupt routines ,has now its own
                  memory buffer pool and source cleanup. pe1ayx
   940620	- bug fixed in line disipline change ,local ringbuf
   		  reorganisation ,and some little bugfixes.
   940715	- first release to the public (scc15b.tgz) pe1ayx		                
   950228	- (hopefully) fixed the reason for kernel panics in
                  chk_rcv_queue() [stupid error] dl1bke
   950229       - buffer timeout for vergotten buffer :-( pe1ayx                
   950304       - fixed underrun/zcount handling dl1bke
   950305	- the driver registers port addresses now dl1bke
   950314	- fixed underrun interrupt handling again dl1bke
   950514	- fixed slip tty wakeup (it wil now work again with kernel ax25)pe1ayx
   950703	- rewrote the 8530 init/reset routines pe1ayx
   950712	- rewrote the RXirq + buffering routines pe1ayx
   950716	- It can now handle ax25 rx frame info > 256
   O		- rewrite TX + buffering (there is a little mem leak ,but
                  wil not be dangerous since my buffer timeout routine
                  wil put back vergotten buffers in the queue pe1ayx
   O		- change the tty i/o so that the tty wakeup is handled properly
           	  pe1ayx
   
   Thanks to:
   DL1BKE Joerg - for some good ideas
   PE1CHL Rob	- for a lot of good ideas from his SCC driver for DOS
   PE1NNZ Guido - for his port of the original driver to Linux
   PA3AOU Harry - for ESCC testing, information supply and support
   
   PE1KOX Rob, DG1RTF Thomas, ON5QK Roland, 
   
   and all who sent me bug reports and ideas... 
   
   
      
*/


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
#include <linux/config.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/scc.h>
#include <linux/delay.h>
#include "scc_config.h"

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/bitops.h>

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <linux/kernel.h>


long scc_init(long kmem_start);

int scc_open(struct tty_struct *tty, struct file *filp);
static void scc_close(struct tty_struct *tty, struct file *filp);
int scc_write(struct tty_struct *tty, int from_user, unsigned char *buf, int count);
static void scc_put_char(struct tty_struct *tty, unsigned char ch);
static void scc_flush_chars(struct tty_struct *tty);
static int scc_write_room(struct tty_struct *tty);
static int scc_chars_in_buffer(struct tty_struct *tty);
static void scc_flush_buffer(struct tty_struct *tty);
static int scc_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg);
static void scc_set_termios(struct tty_struct *tty, struct termios *old_termios);
static void scc_throttle(struct tty_struct *tty);
static void scc_unthrottle(struct tty_struct *tty);
static void scc_start(struct tty_struct *tty);
static void scc_stop(struct tty_struct *tty);

static void z8530_init(void);

static void scc_change_speed(struct scc_channel *scc);
static void init_channel(struct scc_channel *scc);
static void scc_key_trx (struct scc_channel *scc, char tx);
static void scc_txint(register struct scc_channel *scc);
static void scc_exint(register struct scc_channel *scc);
static void scc_rxint(register struct scc_channel *scc);
static void scc_spint(register struct scc_channel *scc);
static void scc_isr(int irq, struct pt_regs *regs);
static void scc_timer(void);
static void scc_init_timer(struct scc_channel *scc);

/* from serial.c */

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

static unsigned char *tmp_buf = 0;
static struct semaphore tmp_buf_sem = MUTEX;
static unsigned int start_controle;

static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 0 };


struct tty_driver scc_driver;		    /* new in 1.1.xx */
static int scc_refcount;
static struct tty_struct *scc_table[2*MAXSCC];
static struct termios scc_termios[2 * MAXSCC];
static struct termios scc_termios_locked[2 * MAXSCC];
	
struct scc_channel SCC_Info[2 * MAXSCC];         /* information per channel */

unsigned char Random = 0;		/* random number for p-persist */
unsigned char Driver_Initialized = 0;
static struct sccbuf *sccfreelist[MAX_IBUFS] = {0};
static int allocated_ibufs = 0;

#define SERIAL_TYPE_NORMAL	1


/* ******************************************************************** */
/* *			Port Access Functions			      * */
/* ******************************************************************** */

static inline unsigned char
InReg(register io_port port, register unsigned char reg)
{
#ifdef SCC_LDELAY
	register unsigned char r;
	Outb(port, reg);
	udelay(5);
	r=Inb(port);
	udelay(5);
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
	Outb(port, reg); udelay(5);
	Outb(port, val); udelay(5);
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

/* allocate memory for the interrupt buffer pool */

void scc_alloc_buffer_pool(void)
{
	int i;
	struct sccbuf *sccb;
	struct mbuf   *bp;
	
	start_controle = 0;
	
   	for (i = 0 ; i < MAX_IBUFS ; i++)
   	{
   		sccb = (struct sccbuf *)kmalloc(sizeof(struct sccbuf), GFP_ATOMIC);
   		bp = (struct mbuf *)kmalloc(sizeof(struct mbuf), GFP_ATOMIC);
   		
		if ( !(sccb && bp) )
		{
			allocated_ibufs = --i;
			
			if (allocated_ibufs < 0)
				panic("scc_alloc_buffer_pool() - can't get buffer space");
			else
				printk("Warning: scc_alloc_buffer_pool() - allocated only %i buffers\n",i);
				
			return;
		}
		
		sccfreelist[i] = sccb;
		sccfreelist[i]->bp = bp;
		memset(sccfreelist[i]->bp ,0,sizeof(struct mbuf));
		sccfreelist[i]->inuse = 0;
		sccfreelist[i]->bp->type = 0;
		sccfreelist[i]->bp->refcnt = 0;
		sccfreelist[i]->bp->size = BUFSIZE;
	}
	allocated_ibufs = MAX_IBUFS;
}

unsigned int scc_count_used_buffers(unsigned int * rx, unsigned int * tx)
{
	unsigned int i, used = 0;
	
	if (rx) *rx = 0;
	if (tx) *tx = 0;
	
	for (i = 0 ; i < allocated_ibufs ; i++)
	{
		if (sccfreelist[i]->inuse)
		{
			switch (sccfreelist[i]->bp->type)
			{
				case BT_RECEIVE:
					if (rx) (*rx)++; break;
				case BT_TRANSMIT:
					if (tx) (*tx)++; break;
			}
			
			used++;
		}
	}
	
	return used;
}


/* Allocate mbuf */
struct mbuf *
scc_get_buffer(char type)
{
	int i;
	unsigned long flags;

	save_flags(flags); cli();	/* just to be sure */

	for (i = 0 ; i < allocated_ibufs ; i++)
	{
		if(sccfreelist[i]->inuse == 0)
		{
			sccfreelist[i]->inuse = 1;
			sccfreelist[i]->bp->type = type;
			sccfreelist[i]->bp->next = NULLBUF;
			sccfreelist[i]->bp->anext = NULLBUF;
			sccfreelist[i]->bp->dup = NULLBUF;
			sccfreelist[i]->bp->size = BUFSIZE;
			sccfreelist[i]->bp->refcnt = 1;
			sccfreelist[i]->bp->cnt = 0;
			sccfreelist[i]->bp->in_use = 0;
			sccfreelist[i]->bp->time_out = CURRENT_TIME + 300;
		
			restore_flags(flags);
			return sccfreelist[i]->bp;
		}
	}
	
	printk("\nSCC scc_get_buffer(): buffer pool empty\n");	/* should never happen */
	restore_flags(flags);
	return NULLBUF;
}


/* Decrement the reference pointer in an mbuf. If it goes to zero,
 * free all resources associated with mbuf.
 * Return pointer to next mbuf in packet chain
 */
struct mbuf *
scc_return_buffer(register struct mbuf *bp, char type)
{
	struct mbuf *bpnext;
	int i;
	unsigned long flags;
	
	if(!bp)
		return NULLBUF;
		
	save_flags(flags); cli();
	bpnext = bp->next;
/*=========================================================================*/	
/*==      THIS IS A LITTLE ROUTINE TO FIX THE TX MEM LEAK                ==*/
/*==             UNTIL I HAVE REWRITE THE TX ROUTINES                    ==*/
/*==                 PE1AYX@PI8HRL.AMPR.ORG                              ==*/
/*=========================================================================*/

	 start_controle++;
	 if(start_controle > 100){
	  if(bp->type == BT_TRANSMIT){
	    start_controle = 0;
	    for(i = 0 ; i < allocated_ibufs ; i++)
	    {
	       if(sccfreelist[i]->inuse == 1)
	        if(sccfreelist[i]->bp->type == BT_TRANSMIT)
	         if(sccfreelist[i]->bp->time_out < CURRENT_TIME)
		    {
			sccfreelist[i]->bp->cnt = 0;
			sccfreelist[i]->bp->refcnt = 0;
			sccfreelist[i]->inuse = 0;
		    }
 	     }
           } 
	 }
/*=========================================================================*/
/*==                END OF THAT SILLY STUPID ROUTINE                     ==*/
/*=========================================================================*/	 

	if (bp->dup)
	{
		for(i = 0 ; i < allocated_ibufs ; i++)
		{
			if(sccfreelist[i]->bp == bp->dup)
			{
			      if (sccfreelist[i]->bp->type != type)
			      {
			      	   printk("scc_return_buffer(bp->dup, %i): wrong buffer type %i",
			      	   	  type,sccfreelist[i]->bp->type);
			      }
			      
			      sccfreelist[i]->bp->cnt = 0;
			      sccfreelist[i]->bp->refcnt = 0;
			      sccfreelist[i]->bp->in_use = 0;
			      sccfreelist[i]->inuse = 0;
			      bp->dup = NULLBUF;
			}
		}
	}
	
	/* Decrement reference count. If it has gone to zero, free it. */
	if(--bp->refcnt <= 0)
	{
		for(i = 0 ; i < allocated_ibufs ; i++)
		{
			if(sccfreelist[i]->bp == bp)
			{
				if (sccfreelist[i]->bp->type != type)
				{
					printk("scc_return_buffer(bp, %i): wrong buffer type %i",
						type,sccfreelist[i]->bp->type);
				}				
			     
				sccfreelist[i]->bp->cnt = 0;
				sccfreelist[i]->bp->refcnt = 0;
				sccfreelist[i]->inuse = 0;
				restore_flags(flags);
				return bpnext;
			}
		}
	}
		
	printk("\nscc_return_buffer(): bogus pointer %p\n",bp);
	restore_flags(flags);
	return bpnext;
} 


/* Free packet (a chain of mbufs). Return pointer to next packet on queue,
 * if any
 */
struct mbuf *
scc_free_chain(register struct mbuf *bp, char type)
{
	register struct mbuf *abp;
	unsigned long flags;

	if(!bp) 
		return NULLBUF;
		
	save_flags(flags); cli();	
	
	abp = bp->anext;
	while (bp) bp = scc_return_buffer(bp, type);
		
	restore_flags(flags); cli();
	return abp;
}


/* Append mbuf to end of mbuf chain */
void
scc_append_to_chain(struct mbuf **bph,struct mbuf *bp)
{
	register struct mbuf *p;
	unsigned long flags;

	if(bph == NULLBUFP || bp == NULLBUF)
		return;
	
	save_flags(flags); cli();
	
	if(*bph == NULLBUF)
	{
		/* First one on chain */
		*bph = bp;
	} else {
		for(p = *bph ; p->next != NULLBUF ; p = p->next)
			;
		p->next = bp;
	}
	
	restore_flags(flags);
}


/* Append packet (chain of mbufs) to end of packet queue */
void
scc_enqueue(struct mbuf **queue,struct mbuf *bp)
{
	register struct mbuf *p;
	unsigned long flags;

	if(queue == NULLBUFP || bp == NULLBUF)
		return;
		
	save_flags(flags); cli();
	
	if(*queue == NULLBUF)
	{
		/* List is empty, stick at front */
		*queue = bp;
	} else {
		for(p = *queue ; p->anext != NULLBUF ; p = p->anext)
			;
		p->anext = bp;
	}
	restore_flags(flags);
}


/* ******************************************************************** */
/* *			Interrupt Service Routines		      * */
/* ******************************************************************** */

/* ----> interrupt service routine for the 8530 <---- */

/* it's recommendet to keep this function "inline" ;-) */

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
scc_isr(int irq, struct pt_regs *regs)
{
	register unsigned char vector;	
	register struct scc_channel *scc;
	register io_port q;
	register io_port *p;
	register int k;


	cli();
	
	if (Vector_Latch)
	{
	    	while(1)
    		{ 
			Outb(Vector_Latch, 0);      /* Generate INTACK */
        
			/* Read the vector */
			if((vector=Inb(Vector_Latch)) >= 16 * Nchips)
				break; 
          
			/* Extract channel number and status from vector. */
			/* Isolate channel nummer */
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
		        /* Isolate channel nummer */
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


static inline void prepare_next_txframe(register struct scc_channel *scc)
{
	if ((scc->tbp = scc->sndq))
	{
		scc->sndq = scc->sndq->anext;
		scc->stat.tx_state = TXS_NEWFRAME;

	} else {
		scc->stat.tx_state = TXS_BUSY;
		scc->t_tail = scc->kiss.tailtime;
	}
}


/* Transmitter interrupt handler */
static void
scc_txint(register struct scc_channel *scc)
{
	register struct mbuf *bp;

	scc->stat.txints++;

	bp = scc->tbp;
			
	while (bp && !bp->cnt)      /* find next buffer */
		bp = scc_return_buffer(bp, BT_TRANSMIT);
				
	if (bp == NULLBUF)			/* no more buffers in this frame */
	{
		if (--scc->stat.tx_queued < 0)
			scc->stat.tx_queued = 0;
			
		Outb(scc->ctrl,RES_Tx_P);       /* reset pending int */					
		cl(scc,R10,ABUNDER);            /* frame complete, allow CRC transmit */ 
		prepare_next_txframe(scc);
		
	} else {				/* okay, send byte */
	
		if (scc->stat.tx_state == TXS_NEWFRAME)
		{				/* first byte ? */
			Outb(scc->ctrl, RES_Tx_CRC);	/* reset CRC generator */
			or(scc,R10,ABUNDER);		/* re-install underrun protection */
			Outb(scc->data,bp->data[bp->in_use++]);
							/* send byte */
			if (!scc->enhanced)		/* reset EOM latch */
				Outb(scc->ctrl, RES_EOM_L);
				
			scc->stat.tx_state = TXS_ACTIVE;/* next byte... */
		} else {
			Outb(scc->data,bp->data[bp->in_use++]);
		}
		
		bp->cnt--;                      /* decrease byte count */
		scc->tbp=bp;			/* store buffer address */
	}
}

/* Throw away received mbuf(s) when an error occurred */

static inline void
flush_FIFO(register struct scc_channel *scc)
{
	register int k;
	
	for (k=0; k<3; k++)
		Inb(scc->data);

	if(scc->rxbufcnt > 0)
	{
		scc->stat.rxerrs++;
		scc->rxbufcnt = 0;         /* throw away frame */
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


	/* CTS: use external TxDelay (what's that good for?!) */
	
	if (chg_and_stat & CTS)			/* CTS is now ON */
	{			
		if (!Running(t_txdel) && scc->kiss.txdelay == 0) /* zero TXDELAY = wait for CTS */
			scc->t_txdel = 0;	/* kick it! */		
		
	}
	
	if ((scc->stat.tx_state == TXS_ACTIVE) && (status & TxEOM))
	{
		scc->stat.tx_under++;	  /* oops, an underrun! count 'em */
		Outb(scc->ctrl, RES_Tx_P);
		Outb(scc->ctrl, RES_EXT_INT);	/* reset ext/status interrupts */
		scc->t_maxk = 1;
		scc->tbp = scc_free_chain(scc->tbp, BT_TRANSMIT);
		
		if (--scc->stat.tx_queued < 0) scc->stat.tx_queued = 0;
		or(scc,R10,ABUNDER);
	}
		
	if (status & ZCOUNT)		   /* Oops? */
	{
		scc->stat.tx_under = 9999;  /* errr... yes. */
		Outb(scc->ctrl, RES_Tx_P); /* just to be sure */
		scc->t_maxk = 1;
		scc->tbp = scc_free_chain(scc->tbp, BT_TRANSMIT);
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
	unsigned char ch;

	scc->stat.rxints++;

	if( Running(t_maxk) && !(scc->kiss.fulldup))
	{
		Inb(scc->data);		/* discard char */
		or(scc,R3,ENT_HM);	/* enter hunt mode for next flag */
		return;
	}

	if (scc->rxbufcnt > 2044)		/* no buffer available? */
	{
		Inb(scc->data);		/* discard character */
		or(scc,R3,ENT_HM);      /* enter hunt mode */
		scc->rxbufcnt = 0;	/* throw away frame */
		scc->stat.nospace++;    /* and count this error */
		return;
	}

	if(scc->rxbufcnt == 0) /* make begin of kissframe */
	{
		scc->rxbuf[scc->rxbufcnt++] = FEND;
		if (scc->kiss.not_slip)
			scc->rxbuf[scc->rxbufcnt++] = 0;
	}

	switch( ch = Inb(scc->data) )
	{
		case FEND:
			scc->rxbuf[scc->rxbufcnt++] = FESC;
			scc->rxbuf[scc->rxbufcnt++] = TFEND;
			break;
		case FESC:
			scc->rxbuf[scc->rxbufcnt++] = FESC;
			scc->rxbuf[scc->rxbufcnt++] = TFESC;
			break;
		default:
			scc->rxbuf[scc->rxbufcnt++] = ch;
	}

}


/* Receive Special Condition interrupt handler */
static void
scc_spint(register struct scc_channel *scc)
{
	register unsigned char status;
	int i;
	unsigned char	*cp;
	char		*fp;
	int		count;


	scc->stat.spints++;

	status = InReg(scc->ctrl,R1);		/* read Special Receive Condition status */
	
	Inb(scc->data);				/* read byte */

	if(status & Rx_OVR)			/* RX_OVerRrun? */
	{
		scc->stat.rx_over++;
		or(scc,R3,ENT_HM);              /* enter hunt mode */
		scc->rxbufcnt = 0;              /* rewind the buffer */
	}
	
	if(status & END_FR && scc->rxbufcnt != 0)	/* END of FRame */
	{
		if (!(status & CRC_ERR) && (status & 0xe) == RES8 && scc->rxbufcnt > 0)
		{
		   scc->rxbufcnt--;	/*strip the CRC */
  		   scc->rxbuf[scc->rxbufcnt++] = FEND;

		   for(i = 0 ; i < scc->rxbufcnt ; i++)
			{
			if((scc->tty->flip.count + 1) < TTY_FLIPBUF_SIZE)
				tty_insert_flip_char(scc->tty, scc->rxbuf[i], 0);
			else    {
				if (scc->tty->flip.buf_num) {
					cp = scc->tty->flip.char_buf + TTY_FLIPBUF_SIZE;
					fp = scc->tty->flip.flag_buf + TTY_FLIPBUF_SIZE;
					scc->tty->flip.buf_num = 0;
					scc->tty->flip.char_buf_ptr = scc->tty->flip.char_buf;
					scc->tty->flip.flag_buf_ptr = scc->tty->flip.flag_buf;
				} else {
					cp = scc->tty->flip.char_buf;
					fp = scc->tty->flip.flag_buf;
					scc->tty->flip.buf_num = 1;
					scc->tty->flip.char_buf_ptr = scc->tty->flip.char_buf + TTY_FLIPBUF_SIZE;
					scc->tty->flip.flag_buf_ptr = scc->tty->flip.flag_buf + TTY_FLIPBUF_SIZE;
					}
				count = scc->tty->flip.count;
				scc->tty->flip.count = 0;
				scc->tty->ldisc.receive_buf(scc->tty, cp, fp, count);
				tty_insert_flip_char(scc->tty, scc->rxbuf[i], 0);
				}
			}

		if (scc->tty->flip.buf_num) {
			cp = scc->tty->flip.char_buf + TTY_FLIPBUF_SIZE;
			fp = scc->tty->flip.flag_buf + TTY_FLIPBUF_SIZE;
			scc->tty->flip.buf_num = 0;
			scc->tty->flip.char_buf_ptr = scc->tty->flip.char_buf;
			scc->tty->flip.flag_buf_ptr = scc->tty->flip.flag_buf;
		} else {
			cp = scc->tty->flip.char_buf;
			fp = scc->tty->flip.flag_buf;
			scc->tty->flip.buf_num = 1;
			scc->tty->flip.char_buf_ptr = scc->tty->flip.char_buf + TTY_FLIPBUF_SIZE;
			scc->tty->flip.flag_buf_ptr = scc->tty->flip.flag_buf + TTY_FLIPBUF_SIZE;
			}
		count = scc->tty->flip.count;
		scc->tty->flip.count = 0;
		scc->tty->ldisc.receive_buf(scc->tty, cp, fp, count);
		scc->stat.rxframes++;

		scc->rxbufcnt = 0;
			
		} else {
			scc->rxbufcnt = 0;		/* frame is not good */
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
	set_brg(scc, (unsigned) (Clock / (scc->modem.speed * 64)) - 2);
}


/* ----> initialize a SCC channel <---- */

static inline void init_brg(register struct scc_channel *scc)
{
	wr(scc, R14, BRSRC);				/* BRG source = PCLK */
	OutReg(scc->ctrl, R14, SSBR|scc->wreg[R14]);	/* DPLL source = BRG */
	OutReg(scc->ctrl, R14, SNRZI|scc->wreg[R14]);	/* DPLL NRZI mode */
}

static void
init_channel(register struct scc_channel *scc)
{
	unsigned long flags;

	save_flags(flags); cli();

	wr(scc,R1,0);			/* no W/REQ operation */
	wr(scc,R3,Rx8|RxCRC_ENAB);	/* RX 8 bits/char, CRC, disabled */	
	wr(scc,R4,X1CLK|SDLC);		/* *1 clock, SDLC mode */
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
			wr(scc, R11, ((Board & BAYCOM)? TRxCDP : TRxCBR) | RCDPLL|TCRTxCP|TRxCOI);
			init_brg(scc);
			break;

		case CLK_EXTERNAL:
			wr(scc, R11, (Board & BAYCOM)? RCTRxCP|TCRTxCP : RCRTxCP|TCTRxCP);
			OutReg(scc->ctrl, R14, DISDPLL);
			break;

	}
	
	/* enable CTS (not for Baycom), ABORT & DCD interrupts */
	wr(scc,R15,((Board & BAYCOM) ? 0 : CTSIE)|BRKIE|DCDIE|TxUIE);

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
	
	Outb(scc->ctrl,RES_EXT_INT);	/* reset ext/status interrupts */
	Outb(scc->ctrl,RES_EXT_INT);	/* must be done twice */
	
	scc->status = InReg(scc->ctrl,R0);	/* read initial status */
	scc->rxbufcnt = 0;
	or(scc,R1,INT_ALL_Rx|TxINT_ENAB|EXT_INT_ENAB); /* enable interrupts */
	or(scc,R9,MIE);			/* master interrupt enable */
			
	restore_flags(flags);
	
	set_speed(scc); 
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
		
	if (Board & PRIMUS)
		Outb(scc->ctrl + 4, Option | (tx? 0x80 : 0));
	
	time_const = (unsigned) (Clock / (scc->modem.speed * (tx? 2:64))) - 2;
	
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
	prepare_next_txframe(scc);
	
	if (scc->stat.tx_state != TXS_BUSY)
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
	 	if (scc->sndq)		/* we had a timeout? */
	 	{
	 		scc->stat.tx_state = TXS_BUSY;
	 		scc->t_dwait = TPS * scc->kiss.mintime; /* try again */
	 	}
	 	
	 	scc->stat.tx_state = TXS_IDLE;
		scc->t_maxk = TIMER_STOPPED;
	 	scc_key_trx(scc, TX_OFF);
	 	return;
	 }
	 
	 if (scc->sndq)			/* maxkeyup expired */ /* ?! */
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

	bp = scc->sndq;
	
	while (bp) bp = scc_free_chain(bp, BT_TRANSMIT);
	
	scc->sndq = NULLBUF;
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
scc_timer(void)
{
	register struct scc_channel *scc;
	register int chan;
	unsigned long flags;

	save_flags(flags); cli();
	
	for (chan = 0; chan < (Nchips * 2); chan++)
	{
		scc = &SCC_Info[chan];
		
		if (scc->tty && scc->init)
		{
			/* KISS-TNC emulation */
			if (Expired(t_dwait)) dw_slot_timeout(scc)	; else
			if (Expired(t_slot))  dw_slot_timeout(scc)	; else
			if (Expired(t_txdel)) txdel_timeout(scc)   	; else
			if (Expired(t_tail))  tail_timeout(scc)	   	;
			
			/* watchdogs */
			
			if (Expired(t_mbusy)) busy_timeout(scc);
			if (Expired(t_maxk))  maxk_idle_timeout(scc);
			if (Expired(t_idle))  maxk_idle_timeout(scc);

		}
	}

	
	timer_table[SCC_TIMER].fn = scc_timer;
	timer_table[SCC_TIMER].expires = jiffies + HZ/TPS;
	timer_active |= 1 << SCC_TIMER; 
	
	restore_flags(flags);
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

	if (scc->sndq1->cnt < 2)
	{
		if (scc->sndq1) 
			scc_free_chain(scc->sndq1, BT_TRANSMIT);
		else
			scc->sndq1 = NULLBUF;
			
		scc->sndq2 = NULLBUF;
		return;
	}
	
	
	
	if (scc->kiss.not_slip)
	{
		kisscmd = scc->sndq1->data[scc->sndq1->in_use++];
		scc->sndq1->cnt--;
	} else {
		kisscmd = 0;
	}

	if (kisscmd & 0xa0)
	{
		if (scc->sndq1->cnt > 2)
			scc->sndq1->cnt -= 2;
		else
		{
			scc_free_chain(scc->sndq1, BT_TRANSMIT);
			scc->sndq2 = NULLBUF;
			return;
		}
	}
	
	
	kisscmd &= 0x1f;
	
		
	if (kisscmd)
	{
		kiss_set_param(scc, kisscmd, scc->sndq1->data[scc->sndq1->in_use]);
		scc->sndq1->cnt=0;
		scc->sndq1->in_use=0;
					
		scc_free_chain(scc->sndq1, BT_TRANSMIT);
		scc->sndq2 = NULLBUF;
		return;
	}
	
	scc_enqueue(&scc->sndq,scc->sndq1); /* scc_enqueue packet */
	scc->stat.txframes++;
	scc->stat.tx_queued++;
	scc->sndq2 = NULLBUF;		/* acquire a new buffer next time */

	save_flags(flags); cli();

	if(scc->stat.tx_state == TXS_IDLE)
	{      				/* when transmitter is idle */
		scc_init_timer(scc);
		scc->stat.tx_state = TXS_BUSY;
		scc->t_dwait = (scc->kiss.fulldup? 0:scc->kiss.waittime);
	}
	
	restore_flags(flags);
}

static void kiss_store_byte(struct scc_channel *scc, unsigned char ch)
{
	if (scc->sndq2 == NULLBUF) return;
	
	if(scc->sndq2->cnt == scc->sndq2->size)		/* buffer full? */
	{
		if((scc->sndq2 = scc_get_buffer(BT_TRANSMIT)) == NULLBUF)
		{
			printk("\nsccdrv: running out of memory\n");
			return;
		}
		scc_append_to_chain(&scc->sndq1,scc->sndq2);         /* add buffer */
	}
	
	scc->sndq2->data[scc->sndq2->cnt++] = ch;
}

static inline int kiss_decode(struct scc_channel *scc, unsigned char ch)
{
	switch (scc->stat.tx_kiss_state) 
	{
		case KISS_IDLE:
			if (ch == FEND)
			{
				if (!(scc->sndq2 = scc->sndq1 = scc_get_buffer(BT_TRANSMIT)))
					return 0;
					
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
				scc_free_chain(scc->sndq1, BT_TRANSMIT);
				scc->sndq2 = NULLBUF;
				scc->stat.txerrs++;
				scc->stat.tx_kiss_state = KISS_IDLE;
			}
			break;
	} /* switch */
	
	return 0;
	
}

/* ******************************************************************* */
/* *		Init channel structures, special HW, etc...	     * */
/* ******************************************************************* */


static void
z8530_init(void)
{
	struct scc_channel *scc;
	int chip;
	unsigned long flags;

	/* reset all scc chips */
	for (chip = 0; chip < Nchips; chip++)
	{
		/* Special SCC cards */

		if(Board & EAGLE)			/* this is an EAGLE card */
			Outb(Special_Port,0x08);	/* enable interrupt on the board */
			
		if(Board & (PC100 | PRIMUS))		/* this is a PC100/EAGLE card */
			Outb(Special_Port,Option);	/* set the MODEM mode (22H normally) */
			
		scc=&SCC_Info[2*chip];
		if (!scc->ctrl) continue;
		
		save_flags(flags); cli();
		
		OutReg(scc->ctrl,R9,FHWRES);	/* force hardware reset */
		OutReg(scc->ctrl,R9,0);		/* end hardware reset */
		OutReg(scc->ctrl,R9,CHRA);	/* reset channel A */
		OutReg(scc->ctrl,R9,CHRB);	/* reset channel B */
		OutReg(scc->ctrl,R1, 0);	/* No Rx irq from channel A */
		scc=&SCC_Info[2*chip+1];
		OutReg(scc->ctrl,R1, 0);	/* No Rx irq from channel B */
		scc=&SCC_Info[2*chip];
		OutReg(scc->ctrl,R2, chip*16);	/* Set Interrupt vector */
 
        	restore_flags(flags);
        }

	if (Ivec == 2) Ivec = 9;
	request_irq(Ivec, scc_isr,   SA_INTERRUPT, "AX.25 SCC");
 
	Driver_Initialized = 1;
}


/* ******************************************************************** */
/* * 	Filesystem Routines: open, close, ioctl, settermios, etc      * */
/* ******************************************************************** */



/* scc_paranoia_check(): warn user if something went wrong		*/

static inline int scc_paranoia_check(struct scc_channel *scc, dev_t device, const char *routine)
{
#ifdef SCC_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for Z8530 SCC struct (%d, %d) in %s\n";
        static const char *badinfo =
                "Warning: Z8530 not found for (%d, %d) in %s\n";
       
	if (!scc->init) 
	{
        	printk(badinfo, MAJOR(device), MINOR(device), routine);
                return 1;
        }
        if (scc->magic != SCC_MAGIC) {
        	printk(badmagic, MAJOR(device), MINOR(device), routine);
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
        if ((chan < 0) || (chan >= (Nchips * 2)))
                return -ENODEV;
 
 	scc = &SCC_Info[chan];
 	
	tty->driver_data = scc;
        tty->termios->c_iflag = IGNBRK | IGNPAR;
        tty->termios->c_cflag = B9600 | CS8 | CLOCAL;

 	tty->termios->c_cflag &= ~CBAUD; 
 	
	if (!Driver_Initialized)
		return 0;
	
	if (scc->magic != SCC_MAGIC)
	{
		printk("ERROR: scc_open(): bad magic number for device (%d, %d)", MAJOR(tty->device), MINOR(tty->device));
		return -ENODEV;
	}		
	
	if(scc->tty != NULL)
	{
		scc->tty_opened++;
		return 0;
	}

 	if(!scc->init) return 0;
 	 	 	 
  	scc->tty = tty;
	init_channel(scc);

	scc->stat.tx_kiss_state = KISS_IDLE;	/* don't change this... */
	scc->stat.rx_kiss_state = KISS_IDLE;	/* ...or this */
	
	scc_init_timer(scc);
	
	timer_table[SCC_TIMER].fn = scc_timer;
	timer_table[SCC_TIMER].expires = 0;	/* now! */
	timer_active |= 1 << SCC_TIMER;

/*====================new pe1ayx====================
planed for the new TX routines

	if (!scc->xmit_buf) {
		scc->xmit_buf = (unsigned char *) get_free_page(GFP_KERNEL);
		if (!scc->xmit_buf)
			return -ENOMEM;
	}

	scc->xmit_cnt = scc->xmit_head = scc->xmit_tail = 0;


====================new pe1ayx end================*/

	if (!tmp_buf) {
		tmp_buf = (unsigned char *) get_free_page(GFP_KERNEL);
		if (!tmp_buf)
			return -ENOMEM;
	}


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
	
	if(scc->tty_opened)
	{
		scc->tty_opened--;
		return;
	}
	
	tty->driver_data = NULLBUF;
	
	save_flags(flags); cli();
	
	Outb(scc->ctrl,0);		/* Make sure pointer is written */
	wr(scc,R1,0);			/* disable interrupts */
	wr(scc,R3,0);
	
	scc->tty = NULL;
	
	restore_flags(flags);
	tty->stopped = 0;		
}




/*
 * change scc_speed
 */
 
static void
scc_change_speed(struct scc_channel * scc)
{
	if (scc->tty == NULL)
		return;
		
	scc->modem.speed = baud_table[scc->tty->termios->c_cflag & CBAUD];
	
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
	int error;

        if (scc->magic != SCC_MAGIC) 
        {
		printk("ERROR: scc_ioctl(): bad magic number for device %d,%d", 
			MAJOR(tty->device), MINOR(tty->device));
			
                return -ENODEV;
        }
	                                                 
	r = NO_SUCH_PARAM;
	
	if (!Driver_Initialized)
	{
		if (cmd == TIOCSCCINI)
		{
			if (!suser())
				return -EPERM;
			
			scc_alloc_buffer_pool();
			z8530_init();
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
			
		put_fs_long(result,(unsigned long *) arg);
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
			value = get_fs_long((unsigned long *) arg);
			
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
		if (!suser())
			return -EPERM;
		if (!arg)
			return -EFAULT;
		
		memcpy_fromfs(scc->tty->termios, (void *) arg, sizeof(struct termios));
		scc_change_speed(scc);
		return 0;
		
		
	case TIOCSCCSTAT:
		error = verify_area(VERIFY_WRITE, (void *) arg,sizeof(struct scc_stat));
		if (error)
			return error;
		
		if (!arg)
			return -EFAULT;
			
		scc->stat.used_buf = scc_count_used_buffers(&scc->stat.rx_alloc, 
							    &scc->stat.tx_alloc);
			
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

		if (!suser())
			return -EPERM;
			
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


/* ----- TERMIOS function ----- */

static void
scc_set_termios(struct tty_struct * tty, struct termios * old_termios)
{
	if (tty->termios->c_cflag == old_termios->c_cflag) 
		return;
	scc_change_speed(tty->driver_data);
}


static inline void check_tx_queue(register struct scc_channel *scc)
{
	register struct mbuf *bp;
	
	if (scc->stat.tx_queued > QUEUE_THRES)
	{
		if (scc->sndq1 == NULLBUF)
		{
			printk("z8530drv: Warning - scc->stat.tx_queued shows overflow"
			       " (%d) but queue is empty\n", scc->stat.tx_queued);
			       
			scc->stat.tx_queued = 0;	/* correct it */
			scc->stat.nospace = 54321;	/* draw attention to it */
			return;
		}
			
		bp = scc->sndq1->anext;	/* don't use the one we currently use */
		
		while (bp && (scc->stat.tx_queued > QUEUE_HYST))
		{
			bp = scc_free_chain(bp, BT_TRANSMIT);
			scc->stat.tx_queued--;
			scc->stat.nospace++;
		}
		
		scc->sndq1->anext = bp;
	}
	
}



/* ----> tx routine: decode KISS data and scc_enqueue it <---- */

/* send raw frame to SCC. used for AX.25 */
int scc_write(struct tty_struct *tty, int from_user, unsigned char *buf, int count)
{
	unsigned long flags;
	static unsigned char *p;
	struct scc_channel * scc = tty->driver_data;
	int cnt, cnt2;

	if (!tty || !tmp_buf)
		return 0;
		
	if (scc_paranoia_check(scc, tty->device, "scc_write"))
		return 0;

	if (scc->kiss.tx_inhibit) return count;
	
	check_tx_queue(scc);

	save_flags(flags);

	cnt2 = count;
	
	while (cnt2)
	{
		cli();
		cnt   = cnt2 > SERIAL_XMIT_SIZE? SERIAL_XMIT_SIZE:cnt2;
		cnt2 -= cnt;
		
		if (from_user){
			down(&tmp_buf_sem);
			memcpy_fromfs(tmp_buf, buf, cnt);
			up(&tmp_buf_sem);
			}
		else
			memcpy(tmp_buf, buf, cnt);
		
		buf += cnt;
			
		p=tmp_buf;
		
		while(cnt--) 
		  if (kiss_decode(scc, *p++))
		  {
		  	scc->stat.nospace++;
		  	restore_flags(flags);
		  	return 0;
		  }
		  	
	} /* while cnt2 */
		
	if ((scc->tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) 
	     && scc->tty->ldisc.write_wakeup)
		(scc->tty->ldisc.write_wakeup)(scc->tty);

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

/* the kernel does NOT use this routine yet... */

static int scc_write_room(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;

	if (scc_paranoia_check(scc, tty->device, "scc_write_room"))
		return 0;
	
	if (scc->stat.tx_alloc >= QUEUE_THRES)
	{
		printk("scc_write_room(): buffer full (ignore)\n");
		return 0;
	}
		
	return BUFSIZE;
}

static int scc_chars_in_buffer(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;

	if (scc && scc->sndq2)
		return scc->sndq2->cnt;
	else
		return 0;
}

static void scc_flush_buffer(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;

	if (scc_paranoia_check(scc, tty->device, "scc_flush_buffer"))
		return;
		
	scc->stat.tx_kiss_state = KISS_IDLE;

}

static void scc_throttle(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;

	if (scc_paranoia_check(scc, tty->device, "scc_throttle"))
		return;
		
		
	/* dummy */
	
}

static void scc_unthrottle(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;

	if (scc_paranoia_check(scc, tty->device, "scc_unthrottle"))
		return;
		
	/* dummy */
	
}

static void scc_start(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;
	
	if (scc_paranoia_check(scc, tty->device, "scc_start"))
		return;
		
	/* dummy */
}
	                                            	

static void scc_stop(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;
	
	if (scc_paranoia_check(scc, tty->device, "scc_stop"))
		return;
		
	/* dummy */
}

void scc_hangup(struct tty_struct *tty)
{
	struct scc_channel *scc = tty->driver_data;
	
	if (scc_paranoia_check(scc, tty->device, "scc_hangup"))
		return;
}


/* ******************************************************************** */
/* * 			Init SCC driver 			      * */
/* ******************************************************************** */

long scc_init (long kmem_start)
{

	int chip;
#ifdef VERBOSE_BOOTMSG
	int chan;
#endif	
	register io_port ctrl;
	long flags;
	
	
        memset(&scc_driver, 0, sizeof(struct tty_driver));
        scc_driver.magic = TTY_DRIVER_MAGIC;
        scc_driver.name = "sc";
        scc_driver.major = TTY_MAJOR;		
        scc_driver.minor_start = 96;
        scc_driver.num = Nchips*2;
        scc_driver.type = TTY_DRIVER_TYPE_SERIAL;
        scc_driver.subtype = SERIAL_TYPE_NORMAL;			/* not needed */
        scc_driver.init_termios = tty_std_termios;
        scc_driver.init_termios.c_iflag = IGNBRK | IGNPAR;
        scc_driver.init_termios.c_cflag = B9600	| CS8 | CLOCAL;
        scc_driver.flags = TTY_DRIVER_REAL_RAW;
        scc_driver.refcount = &scc_refcount;	/* not needed yet */
        scc_driver.table = scc_table;
        scc_driver.termios = (struct termios **) scc_termios;
        scc_driver.termios_locked = (struct termios **) scc_termios_locked;

        scc_driver.open = scc_open;
        scc_driver.close = scc_close;
        scc_driver.write = scc_write;
        scc_driver.put_char = scc_put_char;
        scc_driver.flush_chars = scc_flush_chars;        
	scc_driver.write_room = scc_write_room;
	scc_driver.chars_in_buffer = scc_chars_in_buffer;
	scc_driver.flush_buffer = scc_flush_buffer;
        scc_driver.ioctl = scc_ioctl;
	scc_driver.throttle = scc_throttle;
	scc_driver.unthrottle = scc_unthrottle;
        scc_driver.set_termios = scc_set_termios;
        scc_driver.stop = scc_stop;
        scc_driver.start = scc_start;
        scc_driver.hangup = scc_hangup;
        
        if (tty_register_driver(&scc_driver))
           panic("Couldn't register Z8530 SCC driver\n");
                                
	printk (BANNER);
	
	if (Nchips > MAXSCC) Nchips = MAXSCC;	/* to avoid the "DAU" (duemmster anzunehmender User) */

	/* reset and pre-init all chips in the system */
	
	for (chip = 0; chip < Nchips; chip++)
	{
		memset((char *) &SCC_Info[2*chip  ], 0, sizeof(struct scc_channel));
		memset((char *) &SCC_Info[2*chip+1], 0, sizeof(struct scc_channel));
		
		ctrl = SCC_ctrl[chip * 2];
		if (!ctrl) continue;

		save_flags(flags); cli();	/* because of 2-step accesses */
		

/* Hmm... this may fail on fast systems with cards who don't delay the INTACK */
/* If you are sure you specified the right port addresses and the driver doesn't */
/* recognize the chips, define DONT_CHECK in scc_config.h */

#ifndef DONT_CHECK
		check_region(ctrl, 1);

		Outb(ctrl, 0);
		OutReg(ctrl,R13,0x55);		/* is this chip realy there? */
		
		if (InReg(ctrl,R13) != 0x55 )
		{
			restore_flags(flags);
			continue;
		}
#endif
		
		SCC_Info[2*chip  ].magic    = SCC_MAGIC;
		SCC_Info[2*chip  ].ctrl     = SCC_ctrl[2*chip];
		SCC_Info[2*chip  ].data     = SCC_data[2*chip];
		SCC_Info[2*chip  ].enhanced = SCC_Enhanced[chip];
			
		SCC_Info[2*chip+1].magic    = SCC_MAGIC;
		SCC_Info[2*chip+1].ctrl     = SCC_ctrl[2*chip+1];
		SCC_Info[2*chip+1].data     = SCC_data[2*chip+1];
		SCC_Info[2*chip+1].enhanced = SCC_Enhanced[chip];

 
        	restore_flags(flags);
        }
        
#ifdef VERBOSE_BOOTMSG
	printk("Init Z8530 driver: %u channels, using irq %u\n",Nchips*2,Ivec);
	

	for (chan = 0; chan < Nchips * 2 ; chan++)
	{
		printk("/dev/%s%i: data port = 0x%3.3x  control port = 0x%3.3x -- %s\n",
			scc_driver.name, chan, SCC_data[chan], SCC_ctrl[chan],
			SCC_Info[chan].ctrl? "found"   : "missing");
			
		if (SCC_Info[chan].ctrl == 0) 
		{
			SCC_ctrl[chan] = 0;
		} else {
			request_region(SCC_ctrl[chan], 1, "scc ctrl");
			request_region(SCC_data[chan], 1, "scc data");
		}
	}
#else
	printk("Init Z8530 driver: %u channels\n",Nchips*2);
#endif

	
	return kmem_start;
}
#endif
