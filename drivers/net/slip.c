/*
 * slip.c	This module implements the SLIP protocol for kernel-based
 *		devices like TTY.  It interfaces between a raw TTY, and the
 *		kernel's INET protocol layers (via DDI).
 *
 * Version:	@(#)slip.c	0.7.6	05/25/93
 *
 * Authors:	Laurence Culhane, <loz@holmes.demon.co.uk>
 *		Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *
 * Fixes:
 *		Alan Cox	: 	Sanity checks and avoid tx overruns.
 *					Has a new sl->mtu field.
 *		Alan Cox	: 	Found cause of overrun. ifconfig sl0 mtu upwards.
 *					Driver now spots this and grows/shrinks its buffers(hack!).
 *					Memory leak if you run out of memory setting up a slip driver fixed.
 *		Matt Dillon	:	Printable slip (borrowed from NET2E)
 *	Pauline Middelink	:	Slip driver fixes.
 *		Alan Cox	:	Honours the old SL_COMPRESSED flag
 *		Alan Cox	:	KISS AX.25 and AXUI IP support
 *		Michael Riepe	:	Automatic CSLIP recognition added
 *		Charles Hedrick :	CSLIP header length problem fix.
 *		Alan Cox	:	Corrected non-IP cases of the above.
 *		Alan Cox	:	Now uses hardware type as per FvK.
 *		Alan Cox	:	Default to 192.168.0.0 (RFC 1597)
 *		A.N.Kuznetsov	:	dev_tint() recursion fix.
 *	Dmitry Gorodchanin	:	SLIP memory leaks
 *		Alan Cox	:	Oops - fix AX.25 buffer lengths
 *
 *
 *	FIXME:	This driver still makes some IP'ish assumptions. It should build cleanly KISS TNC only without
 *	CONFIG_INET defined.
 */
 
#include <asm/segment.h>
#include <asm/system.h>

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#ifdef CONFIG_AX25
#include "ax25.h"
#endif
#include <linux/etherdevice.h>
#ifdef CONFIG_INET
#include "ip.h"
#include "route.h"
#include "protocol.h"
#include "tcp.h"
#endif
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include "sock.h"
#include "slip.h"
#ifdef CONFIG_INET
#include "slhc.h"
#endif

#define	SLIP_VERSION	"0.7.5-NET3.014-NEWTTY"


static struct slip	sl_ctrl[SL_NRUNIT];
static struct tty_ldisc	sl_ldisc;
static int		already = 0;

/* Initialize a SLIP control block for use. */
static void
sl_initialize(struct slip *sl, struct device *dev)
{
  sl->magic		= SLIP_MAGIC;
  sl->inuse		= 0;
  sl->sending		= 0;
  sl->escape		= 0;
  sl->flags		= 0;
#ifdef SL_ADAPTIVE
  sl->mode		= SL_MODE_ADAPTIVE;	/* automatic CSLIP recognition */
#else
#ifdef SL_COMPRESSED
  sl->mode		= SL_MODE_CSLIP | SL_MODE_ADAPTIVE;	/* Default */
#else
  sl->mode		= SL_MODE_SLIP;		/* Default for non compressors */
#endif
#endif  

  sl->line		= dev->base_addr;
  sl->tty		= NULL;
  sl->dev		= dev;
  sl->slcomp		= NULL;

  /* Clear all pointers. */
  sl->rbuff		= NULL;
  sl->xbuff		= NULL;
  sl->cbuff		= NULL;

  sl->rhead		= NULL;
  sl->rend		= NULL;
  dev->rmem_end		= (unsigned long) NULL;
  dev->rmem_start	= (unsigned long) NULL;
  dev->mem_end		= (unsigned long) NULL;
  dev->mem_start	= (unsigned long) NULL;
  dev->type		= ARPHRD_SLIP + sl->mode;
  if(dev->type == 260)	/* KISS */
  	dev->type=ARPHRD_AX25;
}

/* Find a free SLIP channel, and link in this `tty' line. */
static inline struct slip *
sl_alloc(void)
{
  unsigned long flags;
  struct slip *sl;
  int i;

  save_flags (flags);
  cli();
  for (i = 0; i < SL_NRUNIT; i++) {
	sl = &sl_ctrl[i];
	if (sl->inuse == 0) {
		sl->inuse = 1;
		sl->tty = NULL;
		restore_flags(flags);
		return(sl);
	}
  }
  restore_flags(flags);
  return(NULL);
}


/* Free a SLIP channel. */
static inline void
sl_free(struct slip *sl)
{
  unsigned long flags;

  if (sl->inuse) {
  	save_flags(flags);
  	cli();
	sl->inuse = 0;
	sl->tty = NULL;
	restore_flags(flags);
  }
}

/* MTU has been changed by the IP layer. Unfortunately we are not told about this, but
   we spot it ourselves and fix things up. We could be in an upcall from the tty
   driver, or in an ip packet queue. */
   
static void sl_changedmtu(struct slip *sl)
{
	struct device *dev=sl->dev;
	unsigned char *tb,*rb,*cb,*tf,*rf,*cf;
	int l;
	int omtu=sl->mtu;

#ifdef CONFIG_AX25
	sl->mtu=dev->mtu+73;
#else
	sl->mtu=dev->mtu;
#endif	
	l=(dev->mtu *2);
/*
 * allow for arrival of larger UDP packets, even if we say not to
 * also fixes a bug in which SunOS sends 512-byte packets even with
 * an MSS of 128
 */
	if (l < (576 * 2))
	  l = 576 * 2;
	
	tb= (unsigned char *) kmalloc(l + 4, GFP_ATOMIC);
	rb= (unsigned char *) kmalloc(l + 4, GFP_ATOMIC);
	cb= (unsigned char *) kmalloc(l + 4, GFP_ATOMIC);
	
	if(tb==NULL || rb==NULL || cb==NULL)
	{
		printk("Unable to grow slip buffers. MTU change cancelled.\n");
		sl->mtu=omtu;
		dev->mtu=omtu;
		if(tb!=NULL)
			kfree(tb);
		if(rb!=NULL)
			kfree(rb);
		if(cb!=NULL)
			kfree(cb);
		return;
	}
	
	cli();
	
	tf=(unsigned char *)sl->dev->mem_start;
	sl->dev->mem_start=(unsigned long)tb;
	sl->dev->mem_end=(unsigned long) (sl->dev->mem_start + l);
	rf=(unsigned char *)sl->dev->rmem_start;
	sl->dev->rmem_start=(unsigned long)rb;
	sl->dev->rmem_end=(unsigned long) (sl->dev->rmem_start + l);
	
	sl->xbuff = (unsigned char *) sl->dev->mem_start;
	sl->rbuff = (unsigned char *) sl->dev->rmem_start;
	sl->rend  = (unsigned char *) sl->dev->rmem_end;
	sl->rhead = sl->rbuff;
	
	cf=sl->cbuff;
	sl->cbuff=cb;
	
	sl->escape=0;
	sl->sending=0;
	sl->rcount=0;

	sti();	
	
	if(rf!=NULL)
		kfree(rf);
	if(tf!=NULL)
		kfree(tf);
	if(cf!=NULL)
		kfree(cf);
}


/* Stuff one byte into a SLIP receiver buffer. */
static inline void
sl_enqueue(struct slip *sl, unsigned char c)
{
  unsigned long flags;

  save_flags(flags);
  cli();
  if (sl->rhead < sl->rend) {
	*sl->rhead = c;
	sl->rhead++;
	sl->rcount++;
  } else sl->roverrun++;
  restore_flags(flags);
}

/* Release 'i' bytes from a SLIP receiver buffer. */
static inline void
sl_dequeue(struct slip *sl, int i)
{
  unsigned long flags;

  save_flags(flags);
  cli();
  if (sl->rhead > sl->rbuff) {
	sl->rhead -= i;
	sl->rcount -= i;
  }
  restore_flags(flags);
}


/* Set the "sending" flag.  This must be atomic, hence the ASM. */
static inline void
sl_lock(struct slip *sl)
{
  unsigned long flags;

  save_flags(flags);
  cli();
  sl->sending = 1;
  sl->dev->tbusy = 1;
  restore_flags(flags);
}


/* Clear the "sending" flag.  This must be atomic, hence the ASM. */
static inline void
sl_unlock(struct slip *sl)
{
  unsigned long flags;

  save_flags(flags);
  cli();
  sl->sending = 0;
  sl->dev->tbusy = 0;
  restore_flags(flags);
}


/* Send one completely decapsulated IP datagram to the IP layer. */
static void
sl_bump(struct slip *sl)
{
  int done;
  unsigned char c;
  unsigned long flags;
  int count;

  count = sl->rcount;
#ifdef CONFIG_INET  
  if (sl->mode & (SL_MODE_ADAPTIVE | SL_MODE_CSLIP)) {
    if ((c = sl->rbuff[0]) & SL_TYPE_COMPRESSED_TCP) {
#if 1
      /* ignore compressed packets when CSLIP is off */
      if (!(sl->mode & SL_MODE_CSLIP)) {
	printk("SLIP: compressed packet ignored\n");
	return;
      }
#endif
      /* make sure we've reserved enough space for uncompress to use */
      save_flags(flags);
      cli();
      if ((sl->rhead + 80) < sl->rend) {
	sl->rhead += 80;
	sl->rcount += 80;
	done = 1;
      } else {
	sl->roverrun++;
	done = 0;
      }
      restore_flags(flags);
      if (! done)  /* not enough space available */
	return;

      count = slhc_uncompress(sl->slcomp, sl->rbuff, count);
      if (count <= 0) {
	sl->errors++;
	return;
      }
    } else if (c >= SL_TYPE_UNCOMPRESSED_TCP) {
      if (!(sl->mode & SL_MODE_CSLIP)) {
	/* turn on header compression */
	sl->mode |= SL_MODE_CSLIP;
	printk("SLIP: header compression turned on\n");
      }
      sl->rbuff[0] &= 0x4f;
      if (slhc_remember(sl->slcomp, sl->rbuff, count) <= 0) {
	sl->errors++;
	return;
      }
    }
  }

#endif
  /* Bump the datagram to the upper layers... */
  do {
	/* clh_dump(sl->rbuff, count); */
	done = dev_rint(sl->rbuff, count, 0, sl->dev);
	if (done == 0 || done == 1) break;
  } while(1);

  sl->rpacket++;
}

/* Encapsulate one IP datagram and stuff into a TTY queue. */
static void
sl_encaps(struct slip *sl, unsigned char *icp, int len)
{
  unsigned char *p;
  int actual, count;

  
#ifdef CONFIG_AX25
  if(sl->mtu != sl->dev->mtu+73)/* Someone has been ifconfigging */
#else
  if(sl->mtu != sl->dev->mtu)   /* Someone has been ifconfigging */  
#endif
  	sl_changedmtu(sl);
  
  if(len>sl->mtu)		/* Sigh, shouldn't occur BUT ... */
  {
  	len=sl->mtu;
  	printk("slip: truncating oversized transmit packet!\n");
  }

  p = icp;
#ifdef CONFIG_INET  
  if(sl->mode & SL_MODE_CSLIP)
	  len = slhc_compress(sl->slcomp, p, len, sl->cbuff, &p, 1);
#endif
  if(sl->mode & SL_MODE_SLIP6)
  	count=slip_esc6(p, (unsigned char *)sl->xbuff,len);
  else
  	count=slip_esc(p, (unsigned char *)sl->xbuff,len);
  sl->spacket++;

  /* Tell TTY to send it on its way. */
  actual = sl->tty->driver.write(sl->tty, 0, sl->xbuff, count);
  if (actual == count) {
	  sl_unlock(sl);
	  mark_bh(NET_BH);
  } else {
	  sl->xhead = sl->xbuff + count;
	  sl->xtail = sl->xbuff + actual;
	  sl->tty->flags |= (1 << TTY_DO_WRITE_WAKEUP);
  }
}

/*
 * Called by the driver when there's room for more data.  If we have
 * more packets to send, we send them here.
 */
static void slip_write_wakeup(struct tty_struct *tty)
{
	register int count, answer;
	struct slip *sl = (struct slip *) tty->disc_data;

	/* First make sure we're connected. */
	if (!sl || sl->magic != SLIP_MAGIC) {
		return;
	}

	if (!sl->xtail)
		return;

	cli();
	if (sl->flags & SLF_XMIT_BUSY) {
		sti();
		return;
	}
	sl->flags |= SLF_XMIT_BUSY;
	sti();
	
	count = sl->xhead - sl->xtail;

	answer = tty->driver.write(tty, 0, sl->xtail, count);
	if (answer == count) {
		sl->xtail = 0;
		tty->flags &= ~TTY_DO_WRITE_WAKEUP;

		sl_unlock(sl);
		mark_bh(NET_BH);
	} else {
		sl->xtail += answer;
	}
	sl->flags &= ~SLF_XMIT_BUSY;
}

/*static void sl_hex_dump(unsigned char *x,int l)
{
	int n=0;
	printk("sl_xmit: (%d bytes)\n",l);
	while(l)
	{
		printk("%2X ",(int)*x++);
		l--;
		n++;
		if(n%32==0)
			printk("\n");
	}
	if(n%32)
		printk("\n");
}*/

/* Encapsulate an IP datagram and kick it into a TTY queue. */
static int
sl_xmit(struct sk_buff *skb, struct device *dev)
{
  struct tty_struct *tty;
  struct slip *sl;
  int size;

  /* Find the correct SLIP channel to use. */
  sl = &sl_ctrl[dev->base_addr];
  tty = sl->tty;

  /*
   * If we are busy already- too bad.  We ought to be able
   * to queue things at this point, to allow for a little
   * frame buffer.  Oh well...
   */
  if (sl->sending) {
	sl->sbusy++;
	return(1);
  }

  /* We were not, so we are now... :-) */
  if (skb != NULL) {
	sl_lock(sl);
	
	size=skb->len;
	sl_encaps(sl, skb->data, size);
	dev_kfree_skb(skb, FREE_WRITE);
  }
  return(0);
}


/* Return the frame type ID.  This is normally IP but maybe be AX.25. */
static unsigned short
sl_type_trans (struct sk_buff *skb, struct device *dev)
{
#ifdef CONFIG_AX25
	struct slip *sl=&sl_ctrl[dev->base_addr];
	if(sl->mode&SL_MODE_AX25)
		return htons(ETH_P_AX25);
#endif
  return htons(ETH_P_IP);
}


/* Fill in the MAC-level header. Not used by SLIP. */
static int
sl_header(unsigned char *buff, struct device *dev, unsigned short type,
	  void *daddr, void *saddr, unsigned len, struct sk_buff *skb)
{
#ifdef CONFIG_AX25
#ifdef CONFIG_INET
  struct slip *sl=&sl_ctrl[dev->base_addr];
  if((sl->mode&SL_MODE_AX25) && type!=htons(ETH_P_AX25))
  	return ax25_encapsulate(buff,dev,type,daddr,saddr,len,skb);
#endif  
#endif

  return(0);
}


/* Rebuild the MAC-level header.  Not used by SLIP. */
static int
sl_rebuild_header(void *buff, struct device *dev, unsigned long raddr,
		struct sk_buff *skb)
{
#ifdef CONFIG_AX25
#ifdef CONFIG_INET
  struct slip *sl=&sl_ctrl[dev->base_addr];
  
  if(sl->mode&SL_MODE_AX25)
  	return ax25_rebuild_header(buff,dev,raddr, skb);
#endif  	
#endif  
  return(0);
}


/* Open the low-level part of the SLIP channel. Easy! */
static int
sl_open(struct device *dev)
{
  struct slip *sl;
  unsigned char *p;
  unsigned long l;

  sl = &sl_ctrl[dev->base_addr];
  if (sl->tty == NULL) {
	return(-ENXIO);
  }
  sl->dev = dev;

  /*
   * Allocate the SLIP frame buffers:
   *
   * mem_end	Top of frame buffers
   * mem_start	Start of frame buffers
   * rmem_end	Top of RECV frame buffer
   * rmem_start	Start of RECV frame buffer
   */
  l = (dev->mtu * 2);
/*
 * allow for arrival of larger UDP packets, even if we say not to
 * also fixes a bug in which SunOS sends 512-byte packets even with
 * an MSS of 128
 */
  if (l < (576 * 2))
    l = 576 * 2;

  p = (unsigned char *) kmalloc(l + 4, GFP_KERNEL);
  if (p == NULL) {
	return(-ENOMEM);
  }

#ifdef CONFIG_AX25
  sl->mtu		= dev->mtu+73;
#else    
  sl->mtu		= dev->mtu;
#endif  
  sl->dev->mem_start	= (unsigned long) p;
  sl->dev->mem_end	= (unsigned long) (sl->dev->mem_start + l);

  p = (unsigned char *) kmalloc(l + 4, GFP_KERNEL);
  if (p == NULL) {
	kfree((unsigned char *)sl->dev->mem_start);
	return(-ENOMEM);
  }
  sl->dev->rmem_start	= (unsigned long) p;
  sl->dev->rmem_end	= (unsigned long) (sl->dev->rmem_start + l);

  sl->xbuff		= (unsigned char *) sl->dev->mem_start;
  sl->rbuff		= (unsigned char *) sl->dev->rmem_start;
  sl->rend		= (unsigned char *) sl->dev->rmem_end;
  sl->rhead		= sl->rbuff;

  sl->escape		= 0;
  sl->sending		= 0;
  sl->rcount		= 0;

  p = (unsigned char *) kmalloc(l + 4, GFP_KERNEL);
  if (p == NULL) {
  	kfree((unsigned char *)sl->dev->mem_start);
  	kfree((unsigned char *)sl->dev->rmem_start);
	return(-ENOMEM);
  }
  sl->cbuff		= p;
#ifdef CONFIG_INET
  sl->slcomp = slhc_init(16, 16);
  if (sl->slcomp == NULL) {
  	kfree((unsigned char *)sl->dev->mem_start);
  	kfree((unsigned char *)sl->dev->rmem_start);
  	kfree((unsigned char *)sl->cbuff);
	return(-ENOMEM);
  }
#endif
  dev->flags|=IFF_UP;
  /* Needed because address '0' is special */
  if(dev->pa_addr==0)
  	dev->pa_addr=ntohl(0xC0A80001);
  return(0);
}


/* Close the low-level part of the SLIP channel. Easy! */
static int
sl_close(struct device *dev)
{
  struct slip *sl;

  sl = &sl_ctrl[dev->base_addr];
  if (sl->tty == NULL) {
	return(-EBUSY);
  }
  sl->tty->disc_data = 0;
  sl_free(sl);

  /* Free all SLIP frame buffers. */
  kfree(sl->rbuff);
  kfree(sl->xbuff);
  kfree(sl->cbuff);
#ifdef CONFIG_INET  
  slhc_free(sl->slcomp);
#endif
  sl_initialize(sl, dev);

  return(0);
}

static int slip_receive_room(struct tty_struct *tty)
{
	return 65536;  /* We can handle an infinite amount of data. :-) */
}

/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of SLIP data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing.
 */
static void slip_receive_buf(struct tty_struct *tty, unsigned char *cp,
			       char *fp, int count)
{
	struct slip *sl = (struct slip *) tty->disc_data;
  
	if (!sl || sl->magic != SLIP_MAGIC)
		return;
  
	/*
	 * Argh! mtu change time! - costs us the packet part received
	 * at the change
	 */
#ifdef CONFIG_AX25
	if(sl->mtu!=sl->dev->mtu+73)
#else	 
	if(sl->mtu!=sl->dev->mtu)
#endif	
		sl_changedmtu(sl);
  	
	/* Read the characters out of the buffer */
	while (count--) {
		if (fp && *fp++) {
			sl->flags |= SLF_ERROR;
			cp++;
			continue;
		}
		if (sl->mode & SL_MODE_SLIP6)
			slip_unesc6(sl,*cp++);
		else
			slip_unesc(sl,*cp++);
	}
}

/*
 * Open the high-level part of the SLIP channel.  
 * This function is called by the TTY module when the
 * SLIP line discipline is called for.  Because we are
 * sure the tty line exists, we only have to link it to
 * a free SLIP channel...
 */
static int
slip_open(struct tty_struct *tty)
{
  struct slip *sl = (struct slip *) tty->disc_data;

  /* First make sure we're not already connected. */
  if (sl && sl->magic == SLIP_MAGIC) {
	return(-EEXIST);
  }

  /* OK.  Find a free SLIP channel to use. */
  if ((sl = sl_alloc()) == NULL) {
	return(-ENFILE);
  }
  sl->tty = tty;
  tty->disc_data = sl;
  if (tty->driver.flush_buffer)
	  tty->driver.flush_buffer(tty);
  if (tty->ldisc.flush_buffer)
	  tty->ldisc.flush_buffer(tty);

  /* Perform the low-level SLIP initialization. */
  (void) sl_open(sl->dev);

  /* Done.  We have linked the TTY line to a channel. */
  return(sl->line);
}

 
static struct enet_statistics *
sl_get_stats(struct device *dev)
{
    static struct enet_statistics stats;
    struct slip *sl;
    struct slcompress *comp;

    /* Find the correct SLIP channel to use. */
    sl = &sl_ctrl[dev->base_addr];
    if (! sl)
      return NULL;

    memset(&stats, 0, sizeof(struct enet_statistics));

    stats.rx_packets = sl->rpacket;
    stats.rx_over_errors = sl->roverrun;
    stats.tx_packets = sl->spacket;
    stats.tx_dropped = sl->sbusy;
    stats.rx_errors = sl->errors;
#ifdef CONFIG_INET
    comp = sl->slcomp;
    if (comp) {
      stats.rx_fifo_errors = comp->sls_i_compressed;
      stats.rx_dropped = comp->sls_i_tossed;
      stats.tx_fifo_errors = comp->sls_o_compressed;
      stats.collisions = comp->sls_o_misses;
    }
#endif
    return (&stats);
}

/*
 * Close down a SLIP channel.
 * This means flushing out any pending queues, and then restoring the
 * TTY line discipline to what it was before it got hooked to SLIP
 * (which usually is TTY again).
 */
static void
slip_close(struct tty_struct *tty)
{
  struct slip *sl = (struct slip *) tty->disc_data;

  /* First make sure we're connected. */
  if (!sl || sl->magic != SLIP_MAGIC) {
  	return;
  }

  (void) dev_close(sl->dev);
}


 /************************************************************************
  *			STANDARD SLIP ENCAPSULATION			*
  ************************************************************************
  *
  */
 
 int
 slip_esc(unsigned char *s, unsigned char *d, int len)
 {
     int count = 0;
 
     /*
      * Send an initial END character to flush out any
      * data that may have accumulated in the receiver
      * due to line noise.
      */
 
     d[count++] = END;
 
     /*
      * For each byte in the packet, send the appropriate
      * character sequence, according to the SLIP protocol.
      */
 
     while(len-- > 0) {
     	switch(*s) {
     	case END:
     	    d[count++] = ESC;
     	    d[count++] = ESC_END;
 	    break;
 	case ESC:
     	    d[count++] = ESC;
     	    d[count++] = ESC_ESC;
 	    break;
 	default:
 	    d[count++] = *s;
 	}
 	++s;
     }
     d[count++] = END;
     return(count);
 }
 
 void
 slip_unesc(struct slip *sl, unsigned char s)
 {
     switch(s) {
     case ESC:
	     sl->flags |= SLF_ESCAPE;
	     break;
     case ESC_ESC:
	     if (sl->flags & SLF_ESCAPE)
		     sl_enqueue(sl, ESC);
	     else
		     sl_enqueue(sl, s);
	     sl->flags &= ~SLF_ESCAPE;
	     break;
     case ESC_END:
	     if (sl->flags & SLF_ESCAPE)
		     sl_enqueue(sl, END);
	     else
		     sl_enqueue(sl, s);
	     sl->flags &= ~SLF_ESCAPE;
	     break;
     case END:
	     if (sl->rcount > 2) 
		     sl_bump(sl);
	     sl_dequeue(sl, sl->rcount);
	     sl->rcount = 0;
	     sl->flags &= ~(SLF_ESCAPE | SLF_ERROR);
	     break;
     default:
	     sl_enqueue(sl, s);
	     sl->flags &= ~SLF_ESCAPE;
     }
 }

 
 /************************************************************************
  *			 6 BIT SLIP ENCAPSULATION			*
  ************************************************************************
  *
  */
 
 int
 slip_esc6(unsigned char *s, unsigned char *d, int len)
 {
     int count = 0;
     int i;
     unsigned short v = 0;
     short bits = 0;
 
     /*
      * Send an initial END character to flush out any
      * data that may have accumulated in the receiver
      * due to line noise.
      */
 
     d[count++] = 0x70;
 
     /*
      * Encode the packet into printable ascii characters
      */
 
     for (i = 0; i < len; ++i) {
     	v = (v << 8) | s[i];
     	bits += 8;
     	while (bits >= 6) {
     	    unsigned char c;
 
     	    bits -= 6;
     	    c = 0x30 + ((v >> bits) & 0x3F);
     	    d[count++] = c;
 	}
     }
     if (bits) {
     	unsigned char c;
 
     	c = 0x30 + ((v << (6 - bits)) & 0x3F);
     	d[count++] = c;
     }
     d[count++] = 0x70;
     return(count);
 }
 
 void
 slip_unesc6(struct slip *sl, unsigned char s)
 {
     unsigned char c;
 
     if (s == 0x70) {
	     if (sl->rcount > 8) {	/* XXX must be 2 for compressed slip */
 #ifdef NOTDEF
		     printk("rbuff %02x %02x %02x %02x\n",
			    sl->rbuff[0],
			    sl->rbuff[1],
			    sl->rbuff[2],
			    sl->rbuff[3]
			    );
 #endif
		     sl_bump(sl);
	     }
 	    sl_dequeue(sl, sl->rcount);
 	    sl->rcount = 0;
 	    sl->flags &= ~(SLF_ESCAPE | SLF_ERROR); /* SLF_ESCAPE not used */
 	    sl->xbits = 0;
 	} else if (s >= 0x30 && s < 0x70) {
		sl->xdata = (sl->xdata << 6) | ((s - 0x30) & 0x3F);
		sl->xbits += 6;
		if (sl->xbits >= 8) {
			sl->xbits -= 8;
			c = (unsigned char)(sl->xdata >> sl->xbits);
			sl_enqueue(sl, c);
		}
 	}
 }



#ifdef CONFIG_AX25

int sl_set_mac_address(struct device *dev, void *addr)
{
	int err=verify_area(VERIFY_READ,addr,7);
	if(err)
		return err;
	memcpy_fromfs(dev->dev_addr,addr,7);	/* addr is an AX.25 shifted ASCII mac address */
	return 0;
}

static int sl_set_dev_mac_address(struct device *dev, void *addr)
{
	memcpy(dev->dev_addr,addr,7);
	return 0;
}
#endif


/* Perform I/O control on an active SLIP channel. */
static int
slip_ioctl(struct tty_struct *tty, void *file, int cmd, void *arg)
{
  struct slip *sl = (struct slip *) tty->disc_data;
  int err;

  /* First make sure we're connected. */
  if (!sl || sl->magic != SLIP_MAGIC) {
	return(-EINVAL);
  }

  switch(cmd) {
	case SIOCGIFNAME:
		err=verify_area(VERIFY_WRITE, arg, 16);
		if(err)
			return -err;
		memcpy_tofs(arg, sl->dev->name, strlen(sl->dev->name) + 1);
		return(0);
	case SIOCGIFENCAP:
		err=verify_area(VERIFY_WRITE,arg,sizeof(long));
		put_fs_long(sl->mode,(long *)arg);
		return(0);
	case SIOCSIFENCAP:
		err=verify_area(VERIFY_READ,arg,sizeof(long));
		sl->mode=get_fs_long((long *)arg);
#ifdef CONFIG_AX25		
		if(sl->mode & SL_MODE_AX25)
		{
			sl->dev->addr_len=7;	/* sizeof an AX.25 addr */
			sl->dev->hard_header_len=17;	/* We don't do digipeaters */
		}
		else
		{
			sl->dev->addr_len=0;	/* No mac addr in slip mode */
			sl->dev->hard_header_len=0;
		}
#endif		
		sl->dev->type=ARPHRD_SLIP+sl->mode;
		if(sl->dev->type==260)
			sl->dev->type=ARPHRD_AX25;
		return(0);
	case SIOCSIFHWADDR:
#ifdef CONFIG_AX25	
		return sl_set_mac_address(sl->dev,arg);
#endif
	/* Allow stty to read, but not set, the serial port */
	case TCGETS:
	case TCGETA:
		return n_tty_ioctl(tty, (struct file *) file, cmd, (unsigned long) arg);
		
	default:
		return -ENOIOCTLCMD;
  }
}


/* Initialize the SLIP driver.  Called by DDI. */
int
slip_init(struct device *dev)
{
  struct slip *sl;
  int i;
#ifdef CONFIG_AX25  
  static char ax25_bcast[7]={'Q'<<1,'S'<<1,'T'<<1,' '<<1,' '<<1,' '<<1,'0'<<1};
  static char ax25_test[7]={'L'<<1,'I'<<1,'N'<<1,'U'<<1,'X'<<1,' '<<1,'1'<<1};
#endif

  sl = &sl_ctrl[dev->base_addr];

  if (already++ == 0) {
	printk("SLIP: version %s (%d channels)\n",
				SLIP_VERSION, SL_NRUNIT);
#ifdef SL_COMPRESSED
	printk("CSLIP: code copyright 1989 Regents of the University of California\n");
#endif
#ifdef CONFIG_AX25
	printk("AX25: KISS encapsulation enabled\n");
#endif
	/* Fill in our LDISC request block. */
	memset(&sl_ldisc, 0, sizeof(sl_ldisc));
	sl_ldisc.magic	= TTY_LDISC_MAGIC;
	sl_ldisc.flags	= 0;
	sl_ldisc.open	= slip_open;
	sl_ldisc.close	= slip_close;
	sl_ldisc.read	= NULL;
	sl_ldisc.write	= NULL;
	sl_ldisc.ioctl	= (int (*)(struct tty_struct *, struct file *,
				   unsigned int, unsigned long)) slip_ioctl;
	sl_ldisc.select = NULL;
	sl_ldisc.receive_buf = slip_receive_buf;
	sl_ldisc.receive_room = slip_receive_room;
	sl_ldisc.write_wakeup = slip_write_wakeup;
	if ((i = tty_register_ldisc(N_SLIP, &sl_ldisc)) != 0)
		printk("ERROR: %d\n", i);
  }

  /* Set up the "SLIP Control Block". */
  sl_initialize(sl, dev);

  /* Clear all statistics. */
  sl->rcount		= 0;			/* SLIP receiver count	*/
  sl->rpacket		= 0;			/* #frames received	*/
  sl->roverrun		= 0;			/* "overrun" counter	*/
  sl->spacket		= 0;			/* #frames sent out	*/
  sl->sbusy		= 0;			/* "xmit busy" counter	*/
  sl->errors		= 0;			/* not used at present	*/

  /* Finish setting up the DEVICE info. */
  dev->mtu		= SL_MTU;
  dev->hard_start_xmit	= sl_xmit;
  dev->open		= sl_open;
  dev->stop		= sl_close;
  dev->hard_header	= sl_header;
  dev->type_trans	= sl_type_trans;
  dev->get_stats	= sl_get_stats;
#ifdef HAVE_SET_MAC_ADDR
#ifdef CONFIG_AX25
  dev->set_mac_address  = sl_set_dev_mac_address;
#endif
#endif
  dev->hard_header_len	= 0;
  dev->addr_len		= 0;
  dev->type		= 0;
#ifdef CONFIG_AX25  
  memcpy(dev->broadcast,ax25_bcast,7);		/* Only activated in AX.25 mode */
  memcpy(dev->dev_addr,ax25_test,7);		/*    ""      ""       ""    "" */
#endif  
  dev->rebuild_header	= sl_rebuild_header;
  for (i = 0; i < DEV_NUMBUFFS; i++)
	skb_queue_head_init(&dev->buffs[i]);

  /* New-style flags. */
  dev->flags		= 0;
  dev->family		= AF_INET;
  dev->pa_addr		= 0;
  dev->pa_brdaddr	= 0;
  dev->pa_mask		= 0;
  dev->pa_alen		= sizeof(unsigned long);

  return(0);
}
