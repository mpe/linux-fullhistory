/*
 * slip.c	This module implements the SLIP protocol for kernel-based
 *		devices like TTY.  It interfaces between a raw TTY, and the
 *		kernel's INET protocol layers (via DDI).
 *
 * Version:	@(#)slip.c	0.7.6	05/25/93
 *
 * Authors:	Laurence Culhane, <loz@holmes.demon.co.uk>
 *		Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/termios.h>
#include <linux/sockios.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/in.h>
#include "inet.h"
#include "dev.h"
#include "eth.h"
#include "timer.h"
#include "ip.h"
#include "route.h"
#include "protocol.h"
#include "tcp.h"
#include "skbuff.h"
#include "sock.h"
#include "arp.h"
#include "slip.h"


#define	SLIP_VERSION	"0.7.5"


/* Define some IP layer stuff.  Not all systems have it. */
#ifdef SL_DUMP
#   define	IP_VERSION	4	/* version# of our IP software	*/
#   define	IPF_F_OFFSET	0x1fff	/* Offset field			*/
#   define	IPF_DF		0x4000	/* Don't fragment flag		*/
#   define	IPF_MF		0x2000	/* More Fragments flag		*/
#   define	IP_OF_COPIED	0x80	/* Copied-on-fragmentation flag	*/
#   define	IP_OF_CLASS	0x60	/* Option class			*/
#   define	IP_OF_NUMBER	0x1f	/* Option number		*/
#endif


static struct slip	sl_ctrl[SL_NRUNIT];
static struct tty_ldisc	sl_ldisc;
static int		already = 0;


/* Dump the contents of an IP datagram. */
static void
ip_dump(unsigned char *ptr, int len)
{
#ifdef SL_DUMP
  struct iphdr *ip;
  int dlen, doff;

  if (inet_debug != DBG_SLIP) return;

  ip = (struct iphdr *) ptr;
  dlen = ntohs(ip->tot_len);
  doff = ((ntohs(ip->frag_off) & IPF_F_OFFSET) << 3);

  printk("\r*****\n");
  printk("SLIP: %s->", in_ntoa(ip->saddr));
  printk("%s\n", in_ntoa(ip->daddr));
  printk(" len %u ihl %u ver %u ttl %u prot %u",
	dlen, ip->ihl, ip->version, ip->ttl, ip->protocol);

  if (ip->tos != 0) printk(" tos %u", ip->tos);
  if (doff != 0 || (ntohs(ip->frag_off) & IPF_MF))
	printk(" id %u offs %u", ntohs(ip->id), doff);

  if (ntohs(ip->frag_off) & IPF_DF) printk(" DF");
  if (ntohs(ip->frag_off) & IPF_MF) printk(" MF");
  printk("\n*****\n");
#endif
}


/* Initialize a SLIP control block for use. */
static void
sl_initialize(struct slip *sl, struct device *dev)
{
  sl->inuse		= 0;
  sl->sending		= 0;
  sl->escape		= 0;

  sl->line		= dev->base_addr;
  sl->tty		= NULL;
  sl->dev		= dev;

  /* Clear all pointers. */
  sl->rbuff		= NULL;
  sl->xbuff		= NULL;

  sl->rhead		= NULL;
  sl->rend		= NULL;
  dev->rmem_end		= (unsigned long) NULL;
  dev->rmem_start	= (unsigned long) NULL;
  dev->mem_end		= (unsigned long) NULL;
  dev->mem_start	= (unsigned long) NULL;
}


/* Find a SLIP channel from its `tty' link. */
static struct slip *
sl_find(struct tty_struct *tty)
{
  struct slip *sl;
  int i;

  if (tty == NULL) return(NULL);
  for (i = 0; i < SL_NRUNIT; i++) {
	sl = &sl_ctrl[i];
	if (sl->tty == tty) return(sl);
  }
  return(NULL);
}


/* Find a free SLIP channel, and link in this `tty' line. */
static inline struct slip *
sl_alloc(void)
{
  unsigned long flags;
  struct slip *sl;
  int i;

  for (i = 0; i < SL_NRUNIT; i++) {
	sl = &sl_ctrl[i];
	if (sl->inuse == 0) {
		__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
		sl->inuse = 1;
		sl->tty = NULL;
		__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
		return(sl);
	}
  }
  return(NULL);
}


/* Free a SLIP channel. */
static inline void
sl_free(struct slip *sl)
{
  unsigned long flags;

  if (sl->inuse) {
	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
	sl->inuse = 0;
	sl->tty = NULL;
	__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
  }
}


/* Stuff one byte into a SLIP receiver buffer. */
static inline void
sl_enqueue(struct slip *sl, unsigned char c)
{
  unsigned long flags;

  __asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
  if (sl->rhead < sl->rend) {
	*sl->rhead = c;
	sl->rhead++;
	sl->rcount++;
  } else sl->roverrun++;
  __asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}


/* Release 'i' bytes from a SLIP receiver buffer. */
static inline void
sl_dequeue(struct slip *sl, int i)
{
  unsigned long flags;

  __asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
  if (sl->rhead > sl->rbuff) {
	sl->rhead -= i;
	sl->rcount -= i;
  }
  __asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}


/* Set the "sending" flag.  This must be atomic, hence the ASM. */
static inline void
sl_lock(struct slip *sl)
{
  unsigned long flags;

  __asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
  sl->sending = 1;
  sl->dev->tbusy = 1;
  __asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}


/* Clear the "sending" flag.  This must be atomic, hence the ASM. */
static inline void
sl_unlock(struct slip *sl)
{
  unsigned long flags;

  __asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
  sl->sending = 0;
  sl->dev->tbusy = 0;
  __asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}


/* Send one completely decapsulated IP datagram to the IP layer. */
static void
sl_bump(struct slip *sl)
{
  int done;

  DPRINTF((DBG_SLIP, "<< \"%s\" recv:\r\n", sl->dev->name));
  ip_dump(sl->rbuff, sl->rcount);

  /* Bump the datagram to the upper layers... */
  do {
	DPRINTF((DBG_SLIP, "SLIP: packet is %d at 0x%X\n",
					sl->rcount, sl->rbuff));
	done = dev_rint(sl->rbuff, sl->rcount, 0, sl->dev);
	if (done == 0 || done == 1) break;
  } while(1);
  sl->rpacket++;
}


/* TTY finished sending a datagram, so clean up. */
static void
sl_next(struct slip *sl)
{
  DPRINTF((DBG_SLIP, "SLIP: sl_next(0x%X) called!\n", sl));
  sl_unlock(sl);
}


/* Encapsulate one IP datagram and stuff into a TTY queue. */
static void
sl_encaps(struct slip *sl, unsigned char *p, int len)
{
  unsigned char *bp;
  unsigned char c;
  int count;

  DPRINTF((DBG_SLIP, "SLIP: sl_encaps(0x%X, %d) called\n", p, len));
  DPRINTF((DBG_SLIP, ">> \"%s\" sent:\r\n", sl->dev->name));
  ip_dump(p, len);

  /*
   * Send an initial END character to flush out any
   * data that may have accumulated in the receiver
   * due to line noise.
   */
  bp = (unsigned char *) sl->xbuff;
  *bp++ = END;
  count = 1;

  /*
   * For each byte in the packet, send the appropriate
   * character sequence, according to the SLIP protocol.
   */
  while(len-- > 0) {
	c = *p++;
	switch((c & 0377)) {
		case END:
			*bp++ = ESC;
			*bp++ = ESC_END;
			count += 2;
                       	break;
		case ESC:
			*bp++ = ESC;
			*bp++ = ESC_ESC;
			count += 2;
                       	break;
		default:
			*bp++ = c;
			count++;
	}
  }
  *bp++ = END;
  count++;
  sl->spacket++;
  bp = (unsigned char *) sl->xbuff;

  /* Tell TTY to send it on its way. */
  DPRINTF((DBG_SLIP, "SLIP: kicking TTY for %d bytes at 0x%X\n", count, bp));
  if (tty_write_data(sl->tty, (char *) bp, count,
	     (void (*)(void *))sl_next, (void *) sl) == 0) {
	DPRINTF((DBG_SLIP, "SLIP: TTY already done with %d bytes!\n", count));
	sl_next(sl);
  }
}


/* Encapsulate an IP datagram and kick it into a TTY queue. */
static int
sl_xmit(struct sk_buff *skb, struct device *dev)
{
  struct tty_struct *tty;
  struct slip *sl;

  /* Find the correct SLIP channel to use. */
  sl = &sl_ctrl[dev->base_addr];
  tty = sl->tty;
  DPRINTF((DBG_SLIP, "SLIP: sl_xmit(\"%s\") skb=0x%X busy=%d\n",
				dev->name, skb, sl->sending));

  /*
   * If we are busy already- too bad.  We ought to be able
   * to queue things at this point, to allow for a little
   * frame buffer.  Oh well...
   */
  if (sl->sending) {
	DPRINTF((DBG_SLIP, "SLIP: sl_xmit: BUSY\r\n"));
	sl->sbusy++;
	return(1);
  }

  /* We were not, so we are now... :-) */
  if (skb != NULL) {
	sl_lock(sl);
	sl_encaps(sl, (unsigned char *) (skb + 1), skb->len);
	if (skb->free) kfree_skb(skb, FREE_WRITE);
  }
  return(0);
}


/* Return the frame type ID.  This is always IP. */
static unsigned short
sl_type_trans (struct sk_buff *skb, struct device *dev)
{
  return(NET16(ETH_P_IP));
}


/* Fill in the MAC-level header. Not used by SLIP. */
static int
sl_header(unsigned char *buff, struct device *dev, unsigned short type,
	  unsigned long daddr, unsigned long saddr, unsigned len)
{
  return(0);
}


/* Add an ARP-entry for this device's broadcast address. Not used. */
static void
sl_add_arp(unsigned long addr, struct sk_buff *skb, struct device *dev)
{
}


/* Rebuild the MAC-level header.  Not used by SLIP. */
static int
sl_rebuild_header(void *buff, struct device *dev)
{
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
	DPRINTF((DBG_SLIP, "SLIP: channel %d not connected!\n", sl->line));
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
  p = (unsigned char *) kmalloc(l + 4, GFP_KERNEL);
  if (p == NULL) {
	DPRINTF((DBG_SLIP, "SLIP: no memory for SLIP XMIT buffer!\n"));
	return(-ENOMEM);
  }
  sl->dev->mem_start	= (unsigned long) p;
  sl->dev->mem_end	= (unsigned long) (sl->dev->mem_start + l);

  p = (unsigned char *) kmalloc(l + 4, GFP_KERNEL);
  if (p == NULL) {
	DPRINTF((DBG_SLIP, "SLIP: no memory for SLIP RECV buffer!\n"));
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

  DPRINTF((DBG_SLIP, "SLIP: channel %d opened.\n", sl->line));
  return(0);
}


/* Close the low-level part of the SLIP channel. Easy! */
static int
sl_close(struct device *dev)
{
  struct slip *sl;

  sl = &sl_ctrl[dev->base_addr];
  if (sl->tty == NULL) {
	DPRINTF((DBG_SLIP, "SLIP: channel %d not connected!\n", sl->line));
	return(-EBUSY);
  }
  sl_free(sl);

  /* Free all SLIP frame buffers. */
  kfree(sl->rbuff);
  kfree(sl->xbuff);
  sl_initialize(sl, dev);

  DPRINTF((DBG_SLIP, "SLIP: channel %d closed.\n", sl->line));
  return(0);
}


/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of SLIP data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing.
 */
static void
slip_recv(struct tty_struct *tty)
{
  unsigned char buff[128];
  unsigned char *p, c;
  struct slip *sl;
  int count;

  DPRINTF((DBG_SLIP, "SLIP: slip_recv(%d) called\n", tty->line));
  if ((sl = sl_find(tty)) == NULL) return;	/* not connected */

  /* Suck the bytes out of the TTY queues. */
  do {
	memset(buff, 0, 128);
	count = tty_read_raw_data(tty, buff, 128);
	if (count <= 0) break;

	p = buff;
	while(count-- > 0) {
		c = *p++;
		switch((c & 0377)) {
			case ESC:
				sl->escape = 1;
				break;
			case ESC_ESC:
				if (sl->escape) sl_enqueue(sl, ESC);
				  else sl_enqueue(sl, c);
				sl->escape = 0;
				break;
			case ESC_END:
				if (sl->escape) sl_enqueue(sl, END);
				  else sl_enqueue(sl, c);
				sl->escape = 0;
				break;
			case END:
				if (sl->rcount > 3) sl_bump(sl);
				sl_dequeue(sl, sl->rcount);
				sl->rcount = 0;
				sl->escape = 0;
				break;
			default:
				sl_enqueue(sl, c);
				sl->escape = 0;
		}
	}
  } while(1);
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
  struct slip *sl;

  /* First make sure we're not already connected. */
  if ((sl = sl_find(tty)) != NULL) {
	DPRINTF((DBG_SLIP, "SLIP: TTY %d already connected to %s !\n",
					tty->line, sl->dev->name));
	return(-EEXIST);
  }

  /* OK.  Find a free SLIP channel to use. */
  if ((sl = sl_alloc()) == NULL) {
	DPRINTF((DBG_SLIP, "SLIP: TTY %d not connected: all channels in use!\n",
						tty->line));
	return(-ENFILE);
  }
  sl->tty = tty;
  tty_read_flush(tty);
  tty_write_flush(tty);

  /* Perform the low-level SLIP initialization. */
  (void) sl_open(sl->dev);
  DPRINTF((DBG_SLIP, "SLIP: TTY %d connected to %s.\n",
				tty->line, sl->dev->name));

  /* Done.  We have linked the TTY line to a channel. */
  return(sl->line);
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
  struct slip *sl;

  /* First make sure we're connected. */
  if ((sl = sl_find(tty)) == NULL) {
	DPRINTF((DBG_SLIP, "SLIP: TTY %d not connected !\n", tty->line));
	return;
  }

  (void) dev_close(sl->dev);
  DPRINTF((DBG_SLIP, "SLIP: TTY %d disconnected from %s.\n",
					tty->line, sl->dev->name));
}


/* Perform I/O control on an active SLIP channel. */
static int
slip_ioctl(struct tty_struct *tty, void *file, int cmd, void *arg)
{
  struct slip *sl;

  /* First make sure we're connected. */
  if ((sl = sl_find(tty)) == NULL) {
	DPRINTF((DBG_SLIP, "SLIP: ioctl: TTY %d not connected !\n", tty->line));
	return(-EINVAL);
  }

  DPRINTF((DBG_SLIP, "SLIP: ioctl(%d, 0x%X, 0x%X)\n", tty->line, cmd, arg));
  switch(cmd) {
	case SIOCGIFNAME:
		verify_area(VERIFY_WRITE, arg, 16);
		memcpy_tofs(arg, sl->dev->name, strlen(sl->dev->name) + 1);
		return(0);
	default:
		return(-EINVAL);
  }
  return(-EINVAL);
}


/* Initialize the SLIP driver.  Called by DDI. */
int
slip_init(struct device *dev)
{
  struct slip *sl;
  int i;

  sl = &sl_ctrl[dev->base_addr];

  if (already++ == 0) {
	printk("SLIP: version %s (%d channels): ",
				SLIP_VERSION, SL_NRUNIT);

	/* Fill in our LDISC request block. */
	sl_ldisc.flags	= 0;
	sl_ldisc.open	= slip_open;
	sl_ldisc.close	= slip_close;
	sl_ldisc.read	= NULL;
	sl_ldisc.write	= NULL;
	sl_ldisc.ioctl	= (int (*)(struct tty_struct *, struct file *,
				   unsigned int, unsigned long)) slip_ioctl;
	sl_ldisc.handler = slip_recv;
	if ((i = tty_register_ldisc(N_SLIP, &sl_ldisc)) == 0) printk("OK\n");
	  else printk("ERROR: %d\n", i);
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
  dev->add_arp		= sl_add_arp;
  dev->type_trans	= sl_type_trans;
  dev->hard_header_len	= 0;
  dev->addr_len		= 0;
  dev->type		= 0;
  dev->queue_xmit	= dev_queue_xmit;
  dev->rebuild_header	= sl_rebuild_header;
  for (i = 0; i < DEV_NUMBUFFS; i++)
		dev->buffs[i] = NULL;

  /* New-style flags. */
  dev->flags		= 0;
  dev->family		= AF_INET;
  dev->pa_addr		= 0;
  dev->pa_brdaddr	= 0;
  dev->pa_mask		= 0;
  dev->pa_alen		= sizeof(unsigned long);

  return(0);
}
