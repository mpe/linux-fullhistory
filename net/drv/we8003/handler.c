/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		WE - A simple WD8003, WD8013 and SMC Elite-16 driver.
 *
 * Version:	$Id: handler.c,v 1.0.0 1993/02/15 00:00:00 waltje Exp $
 *
 * Authors:	Original taken from the 386BSD operating system.
 *		Ross Biro, <bir7@leland.Stanford.Edu>
 *		Bob Harris, <rth@sparta.com>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <errno.h>
#include <linux/fcntl.h>
#include <netinet/in.h>
#include <linux/interrupt.h>
#include "dp8390.h"
#include "we8003.h"


static unsigned char interrupt_mask;

static struct enet_statistics stats;	/* Statistics collection */
static unsigned char max_pages;		/* Board memory/256 */
static unsigned char wd_debug = 0;	/* turns on/off debug messages */
static unsigned char dconfig = WD_DCONFIG;	/* default data configuration */
static int tx_aborted = 0;			/* Empties tx bit bucket */

static void wd_trs (struct device *);

static  int
max(int a, int b)
{
  if (a>b) return (a);
  return (b);
}

static  void
wd_start(struct device *dev)
{
  unsigned char cmd;
  interrupt_mask=RECV_MASK|TRANS_MASK;
  cli();
  cmd = inb_p(WD_COMM);
  cmd &= ~(CSTOP|CPAGE);
  cmd |= CSTART;
  outb_p(cmd, WD_COMM);
  outb_p(interrupt_mask,WD_IMR);
  sti();
  dev->start = 1;
}

int
wd8003_open(struct device *dev)
{
  unsigned char cmd;
  int i;
  /* we probably don't want to be interrupted here. */
  cli();
  /* This section of code is mostly copied from the bsd driver which is
     mostly copied from somewhere else. */
  /* The somewhere else is probably the cmwymr(sp?) dos packet driver */

  cmd=inb_p(WD_COMM);
  cmd|=CSTOP;
  cmd &= ~(CSTART|CPAGE);
  outb_p(cmd, WD_COMM);
  outb_p(0, WD_IMR);
  sti();
  outb_p( dconfig,WD_DCR);
  /*Zero the remote byte count. */
  outb_p(0, WD_RBY0);
  outb_p(0, WD_RBY1);
  outb_p(WD_MCONFIG,WD_RCC);
  outb_p(WD_TCONFIG,WD_TRC);
  outb_p(0,WD_TRPG);		 /* Set the transmit page start = 0 */
  outb_p( max_pages,WD_PSTOP); /* (read) page stop = top of board memory */
  outb_p(WD_TXBS,WD_PSTRT);	/* (read) page start = cur = bnd = top of tx memory */
  outb_p(WD_TXBS,WD_BNDR);
  /* clear interrupt status. */
  outb_p(0xff,WD_ISR);
  /* we don't want no stinking interrupts. */
  outb_p(0 ,WD_IMR);
  cmd|=1<<CPAGE_SHIFT;
  outb_p(cmd,WD_COMM);
  /* set the ether address. */
  for (i=0; i < ETHER_ADDR_LEN; i++)
    {
      outb_p(dev->dev_addr[i],WD_PAR0+i);
    }
  /* National recommends setting the boundry < current page register */
  outb_p(WD_TXBS+1,WD_CUR);	/* Set the current page = page start + 1 */
  /* set the multicast address. */
  for (i=0; i < ETHER_ADDR_LEN; i++)
    {
      outb_p(dev->broadcast[i],WD_MAR0+i);
    }

  cmd&=~(CPAGE|CRDMA);
  cmd|= 4<<CRDMA_SHIFT;
  outb_p(cmd, WD_COMM);
  outb_p(WD_RCONFIG,WD_RCC);
  wd_start(dev); 
  return (0);
}

/* This routine just calls the ether rcv_int. */
static  int
wdget(volatile struct wd_ring *ring, struct device *dev)
{
  unsigned char *fptr;
  long len;
  fptr = (unsigned char *)(ring +1);
  /* some people have bugs in their hardware which let
     ring->count be 0.  It shouldn't happen, but we
     should check for it. */
  len = ring->count-4;
  if (len < 56)
    printk ("we.c: Hardware problem, runt packet. ring->count = %d\n",
	    ring->count);
  return (dev_rint(fptr, len, 0, dev));
}

int
wd8003_start_xmit(struct sk_buff *skb, struct device *dev)
{
  unsigned char cmd;
  int len;

  cli();
  if (dev->tbusy)
    {
       /* put in a time out. */
       if (jiffies - dev->trans_start < 30)
	 {
	   return (1);
	 }

       printk ("wd8003 transmit timed out. \n");
    }
  dev->tbusy = 1;

  if (skb == NULL)
    {
      sti();
      wd_trs(dev);
      return (0);
    }

  /* this should check to see if it's been killed. */
  if (skb->dev != dev)
    {
      sti();
      return (0);
    }


  if (!skb->arp)
    {
      if ( dev->rebuild_header (skb+1, dev)) 
	{
	  cli();
	  if (skb->dev == dev)
	    {
	      arp_queue (skb);
	    }
	   cli (); /* arp_queue turns them back on. */
 	  dev->tbusy = 0;
	   sti();
	   return (0);
	}
    }

  memcpy ((unsigned char *)dev->mem_start, skb+1, skb->len);

  len = skb->len;

  /* now we need to set up the card info. */
  dev->trans_start = jiffies;
  len=max(len, ETHER_MIN_LEN); /* actually we should zero out
				  the extra memory. */
/*  printk ("start_xmit len - %d\n", len);*/

  cmd=inb_p(WD_COMM);
  cmd &= ~CPAGE;
  outb_p(cmd, WD_COMM);

  interrupt_mask |= TRANS_MASK;
  if (!(dev->interrupt))
    outb (interrupt_mask, WD_IMR);

  outb_p(len&0xff,WD_TB0);
  outb_p(len>>8,WD_TB1);
  cmd |= CTRANS;
  outb_p(cmd,WD_COMM);
  sti();
  
  if (skb->free)
    {
	    kfree_skb (skb, FREE_WRITE);
    }

  return (0);
}

/* tell the card about the new boundary. */

static  void
wd_put_bnd(unsigned char bnd, struct device *dev )
{

	unsigned char cmd;

	/* Ensure page 0 selected */
	cmd = inb_p( CR );
	if (cmd & 0x40) {
		outb_p(cmd & (~CPAGE1), WD_COMM);	/* select page 0 */
		outb_p(bnd, WD_BNDR);
		outb_p(cmd | CPAGE1, WD_COMM);	/* reselect page 1 */
	} else {
		outb_p(bnd, WD_BNDR);
	}
}

static  unsigned char
wd_get_bnd( struct device *dev )
{

	unsigned char   cmd, bnd;

	/* Ensure page 0 selected */
	cmd = inb_p(WD_COMM);
	if (cmd & 0x40) {
		outb_p(cmd & (~CPAGE1), WD_COMM);	/* select page 0 */
		bnd = inb_p(WD_BNDR);
		outb_p(cmd | CPAGE1, WD_COMM);	/* reselect page 1 */
		return (bnd);
	} else {
		return (inb_p(WD_BNDR));
	}
}

static  unsigned char
wd_get_cur( struct device *dev )
{

	unsigned char   cmd, cur;

	/* Ensure page 1 selected */
	cmd = inb_p(WD_COMM);
	if (cmd & 0x40) {
		return (inb_p(WD_CUR));
	} else {
		outb_p(cmd | CPAGE1, WD_COMM);	/* select page 1 */
		cur = inb_p(WD_CUR);
		outb_p(cmd & (~CPAGE1), WD_COMM);	/* reselect page 0 */
		return (cur);
	}
}

/* This routine handles the packet recieved interrupt. */
/* Debug routines slow things down, but reveal bugs... */
/* Modified Boundry Page Register to follow Current Page */

static  void
wd_rcv( struct device *dev )
{
   
   unsigned char   pkt;	/* Next packet page start */
   unsigned char   bnd;	/* Last packet page end */
   unsigned char   cur;	/* Future packet page start */
   unsigned char   cmd;	/* Command register save */
   volatile struct wd_ring *ring;
   int		   done=0;
   
   /* Calculate next packet location */
   cur = wd_get_cur( dev );
   bnd = wd_get_bnd( dev );
   if( (pkt = bnd + 1) == max_pages )
     pkt = WD_TXBS;
   
   while( done != 1)
     {
	if (pkt != cur)
	  {

	     /* Position pointer to packet in card ring buffer */
	     ring = (volatile struct wd_ring *) (dev->mem_start + (pkt << 8));
	     
	     /* Ensure a valid packet */
	     if( ring->status & 1 )
	       { 
		  /* Too small and too big packets are
		     filtered by the board */
		  if( wd_debug )
		    printk("\nwd8013 - wdget: bnd = %d, pkt = %d, "
			   "cur = %d, status = %d, len = %d, next = %d",
			   bnd, pkt, cur, ring->status, ring->count,
			   ring->next);
		  
		  stats.rx_packets++; /* count all receives */
		  done = wdget( ring, dev ); /* get the packet */
		  
		  /* Calculate next packet location */
		  pkt = ring->next;
		  
		  /* Compute new boundry - tell the chip */
		  if( (bnd = pkt - 1) < WD_TXBS )
		    bnd = max_pages - 1;
		  wd_put_bnd(bnd, dev);
		  
		  /* update our copy of cur. */
		  cur = wd_get_cur(dev);
	       }
	     else 
	       {	/* Bad packet in ring buffer -
			   should not happen due to hardware filtering */
		  printk("wd8013 - bad packet: len = %d, status = x%x, "
			 "bnd = %d, pkt = %d, cur = %d\n"
			 "trashing receive buffer!",
			 ring->count, ring->status, bnd, pkt,
			 cur);
		  /* Reset bnd = cur-1 */
		  if( ( bnd = wd_get_cur( dev ) - 1 ) < WD_TXBS )
		    bnd = max_pages - 1;
		  wd_put_bnd( bnd, dev );
		  break; /* return */
	       }
	     
	  }
	else
	  {
	     done = dev_rint(NULL, 0,0, dev);
	  }
     }
   
   /* reset to page 0 */
   cmd = inb_p(WD_COMM);
   if (cmd & 0x40)
     {
	outb_p(cmd & ~(CPAGE1), WD_COMM);	/* select page 0 */
     }
}


/* Handle the "receiver overrun" interrupt. */
static  void
wd_rx_over( struct device *dev )
{
  unsigned char cmd, dummy;
  register int io;

  /*
   * Nothing actually has been overwritten;
   * the chip has stopped at the boundry but
   * we must get it going again - according
   * to National Semiconductor.
   */
  printk("wd_rx_over\n");
  cmd = inb_p( CR ); /* get current command register */
  cmd = (cmd&~(STA|PS0|PS1))|STOP; /* toggle start and stop bits, select page 0 */
  outb_p( cmd, CR );
  dummy = inb_p( RBCR0 );	/* required to detect reset status */
  dummy = inb_p( RBCR1 );
  wd_rcv( dev );	/* clear out received packets */
	
	if( inb_p( ISR ) & PRX )
		outb_p( PRX, ISR ); /* acknowledge RX interrupt */
	while( ( inb_p( ISR ) & RST ) == 0 ); /* wait for reset to be completed */
	outb_p( RST, ISR ); /* acknowledge RST interrupt */
	outb_p( (cmd&~STOP)|STA, CR ); /* Start NIC */	
	outb_p(	WD_TCONFIG, TCR ); /* resume normal mode */
}

/*
 * This get's the transmit interrupts. It assumes command page 0 is set, and
 * returns with command page 0 set.
 */

static  void
wd_trs( struct device *dev )
{
	unsigned char errors;

	if( wd_debug )
		printk("\nwd_trs() - TX complete, status = x%x", inb_p(TSR));

	if( ( errors = inb_p( TSR ) & PTXOK  ) || tx_aborted ){
		if( (errors&~0x02) == 0 ){
			stats.tx_packets++;
			tx_aborted = 0;
		}
		dev->tbusy = 0;
		mark_bh (INET_BH);
		
#if 0		
		/* attempt to start a new transmission. */
		len = dev_tint( (unsigned char *)dev->mem_start, dev );
		if( len != 0 ){
      			len=max(len, ETHER_MIN_LEN);
			cmd=inb_p(WD_COMM);
			outb_p(len&0xff,WD_TB0);
			outb_p(len>>8,WD_TB1);
			cmd |= CTRANS;
			outb_p(cmd,WD_COMM);
			interrupt_mask |= TRANS_MASK;
		}
		else
		{
			dev->tbusy = 0
			interrupt_mask &= ~TRANS_MASK;
			return;
		}
#endif
      }
	else{ /* TX error occurred! - H/W will reschedule */
		if( errors & CRS ){
			stats.tx_carrier_errors++;
			printk("\nwd8013 - network cable short!");
		}
		if (errors & COL )
			stats.collisions += inb_p( NCR );
		if (errors & CDH )
			stats.tx_heartbeat_errors++;
		if (errors & OWC )
			stats.tx_window_errors++;
	}
}

void
wd8003_interrupt(int reg_ptr)
{
	unsigned char   cmd;
	unsigned char   errors;
	unsigned char   isr;
	struct device *dev;
	struct pt_regs *ptr;
	int irq;
	int count = 0;

	ptr = (struct pt_regs *)reg_ptr;
	irq = -(ptr->orig_eax+2);
	for (dev = dev_base; dev != NULL; dev = dev->next)
	  {
	     if (dev->irq == irq) break;
	  }
	if (dev == NULL) 
	  {
	     printk ("we.c: irq %d for unknown device\n", irq);
	     return;
	  }
	sti(); /* this could take a long time, we should have interrupts on. */

	cmd = inb_p( CR );/* Select page 0 */  
	if( cmd & (PS0|PS1 ) ){
		cmd &= ~(PS0|PS1);
		outb_p(cmd, CR );
	}
	
	if (wd_debug)
		printk("\nwd8013 - interrupt isr = x%x", inb_p( ISR ) );

	dev->interrupt = 1;

	do{ /* find out who called */ 
	  sti();
		/* Check for overrunning receive buffer first */
		if ( ( isr = inb_p( ISR ) ) & OVW ) {	/* Receiver overwrite warning */
			stats.rx_over_errors++;
			if( wd_debug )
				printk("\nwd8013 overrun bnd = %d, cur = %d", wd_get_bnd( dev ), wd_get_cur( dev ) );
			wd_rx_over( dev ); /* performs wd_rcv() as well */
			outb_p( OVW, ISR ); /* acknowledge interrupt */
		} 
		else if ( isr & PRX ) {	/* got a packet. */
			wd_rcv( dev );
			outb_p( PRX, ISR ); /* acknowledge interrupt */
		}
		/* This completes rx processing... whats next */

		if ( inb_p( ISR ) & PTX ) {	/* finished sending a packet. */
			wd_trs( dev );
			outb_p( PTX, ISR ); /* acknowledge interrupt */
		}

		if (inb_p( ISR ) & RXE ) {	/* recieve error */
			stats.rx_errors++; /* general errors */
			errors = inb_p( RSR ); /* detailed errors */
			if (errors & CRC )
				stats.rx_crc_errors++;
			if (errors & FAE )
				stats.rx_frame_errors++;
			if (errors & FO )
				stats.rx_fifo_errors++;
			if (errors & MPA )
				stats.rx_missed_errors++;
			outb_p( RXE, ISR ); /* acknowledge interrupt */
		}

		if (inb_p( ISR ) & TXE ) {	/* transmit aborted! */
			stats.tx_errors++; /* general errors */
			errors = inb_p( TSR ); /* get detailed errors */
			if (errors & ABT ){
				stats.tx_aborted_errors++;
				printk("\nwd8013 - network cable open!");
			}
			if (errors & FU )
			  {
			    stats.tx_fifo_errors++;
			    printk("\nwd8013 - TX FIFO underrun!");
			  }

			/* Cannot do anymore - empty the bit bucket */
			tx_aborted = 1;
			wd_trs( dev );
			tx_aborted = 0;

			outb_p( TXE, ISR ); /* acknowledge interrupt */
		}

		if( inb_p( ISR ) & CNTE ){ /* Tally counters overflowing */
			errors = inb_p( CNTR0 );
			errors = inb_p( CNTR1 );
			errors = inb_p( CNTR2 );
			outb_p( CNTE, ISR ); /* acknowledge interrupt */
		}
		if( inb_p( ISR ) & RST ) /* Reset has been performed */
			outb_p( RST, ISR ); /* acknowledge interrupt */

		if( wd_debug ){
			if( ( isr = inb_p( ISR ) ) != 0 )
				printk("\nwd8013 - ISR not cleared = x%x", isr );
		}
		if( ++count > max_pages + 1 ){
			printk("\nwd8013_interrupt - infinite loop detected, isr = x%x, count = %d", isr, count );
		}
		cli();
	} while( inb_p( ISR ) != 0 );

	dev->interrupt = 0;
}


static struct sigaction wd8003_sigaction = 
{
   wd8003_interrupt,
   0,
   0,
   NULL
};


/* Probe for a WD80x3 board. */
static int
we8003_probe(struct ddconf *conf)
{
  unsigned char csum;
  register int io;
  int i;

  io = conf->ioaddr;
  csum = 0;
  for (i = 0; i < 8; i++) {
	csum += inb_p(io + WD_ROM + i);
  }
  if (csum != WD_CHECK) {
	PRINTK (("%s: Warning: board not found at IO=0x%X.\n", io));
	return(-ENODEV);
  }
  return(0);
}


/* Check for a 16-bit ISA controller.  Set a flag if found. */
static void
we8003_8bit(struct ddi *dev)
{
#if !FORCE_8BIT
  unsigned char csum;
  register int io;
  int i;

  io = dev->config.ioaddr;
  for (i = 0; i < 8; i++) {
	if (inb_p(io + EN_SAPROM +i) != inb_p(io + EN_CMD + i)) {
		/* Fiddle with 16-bit bit. */
		csum = inb_p(io + EN_REG1);

		/* Attempt to clear 16-bit bit. */
		outb(csum ^ BUS16, io + EN_REG1);
		if ((csum & BUS16) == (inb_p(io + EN_REG1) & BUS16)) {
			outb_p(LAN16ENABLE|MEMMASK, io + EN_REG5);
			outb(csum, io + EN_REG1);
			dev->flags |= DDI_FBUS16;
			break;
		}
		outb(csum, io + EN_REG1);
	}
  }
#endif
}


/* Setup and clear the on-board memory. */
static void
we8003_cmem(struct ddi *dev)
{
  register int io;

  io = dev->config.ioaddr;
  outb_p(WD_IMEM, io + WD_CTL);		/* mapin the interface memory	*/

  /* FIXME: clear the interface memory here. */
}


/* Fetch and record the interface's hardware address. */
static void
we8003_geth(struct ddi *dev)
{
  register int io;
  register int i;

  io = dev->config.ioaddr;
  for (i = 0; i < ETHER_ADDR_LEN; i++) {
	dev->dev_addr[i] = inb_p(io + WD_ROM + i);
	dev->broadcast[i] = 0xff;
  }
}


/* Initialize the WD8003 hardware. */
int
we8003_conf(struct ddi *dev)
{
  /*
   * Check for any boards present.  We must change this one
   * time to include complete AutoProbe, but for now, we just
   * see if we need to initialize a board at all. - FvK
   */
  if (dev->config.ioaddr == 0) return(0);	/* fake it's OK */
  if (we8003_probe(&dev->config) < 0) return(1);

  /* Check for 16-bit board- it doesn't have register 0/8 aliasing. */
  we8003_8bit(dev);

  /* Set up and clear the shared memory. */
  we8003_cmem(dev);

  /* Fetch and record the hardware address. */
  we8003_geth(dev);

  /* Clear the statistics */
  memset((char *) &stats, 0, sizeof(struct enet_statistics);

  dev->tbusy = 0;
  dev->interrupt = 0;
  if (irqaction (dev->irq, &we8003_sigaction)) {
	printk("%s: unable to get IRQ%d\n", dev->name, dev->irq);
	return(1);
  }
  return(0);
}
