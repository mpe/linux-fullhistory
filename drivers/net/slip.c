/*
 * slip.c	This module implements the SLIP protocol for kernel-based
 *		devices like TTY.  It interfaces between a raw TTY, and the
 *		kernel's INET protocol layers (via DDI).
 *
 * Version:	@(#)slip.c	0.8.3	12/24/94
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
 *      Dmitry Gorodchanin      :       Code cleanup. Reduce tty driver
 *                                      buffering from 4096 to 256 bytes.
 *                                      Improving SLIP response time.
 *                                      CONFIG_SLIP_MODE_SLIP6.
 *                                      ifconfig sl? up & down now works correctly.
 *					Modularization.
 *              Alan Cox        :       Oops - fix AX.25 buffer lengths
 *      Dmitry Gorodchanin      :       Even more cleanups. Preserve CSLIP
 *                                      statistics. Include CSLIP code only
 *                                      if it really needed.
 *		Alan Cox	:	Free slhc buffers in the right place.
 *
 *
 *
 *	FIXME:	This driver still makes some IP'ish assumptions. It should build cleanly KISS TNC only without
 *	CONFIG_INET defined.
 *      I hope now it is fixed ;)
 */

#define SL_CHECK_TRANSMIT
#include <linux/config.h>
#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

/* Undef this, if you don't need 6bit encapsulation code in the driver */
#define CONFIG_SLIP_MODE_SLIP6

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#ifdef CONFIG_AX25
#include "ax25.h"
#endif
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include "slip.h"
#ifdef CONFIG_INET
#include <linux/ip.h>
#include <linux/tcp.h>
#include "slhc.h"
#endif

#ifdef MODULE
#define SLIP_VERSION    "0.8.3-NET3.019-NEWTTY-MODULAR"
#else
#define	SLIP_VERSION	"0.8.3-NET3.019-NEWTTY"
#endif


static struct slip	sl_ctrl[SL_NRUNIT];
static struct tty_ldisc	sl_ldisc;
static int		already = 0;

static int slip_esc(unsigned char *p, unsigned char *d, int len);
static void slip_unesc(struct slip *sl, unsigned char c);
#ifdef CONFIG_SLIP_MODE_SLIP6
static int slip_esc6(unsigned char *p, unsigned char *d, int len);
static void slip_unesc6(struct slip *sl, unsigned char c);
#endif


/* Find a free SLIP channel, and link in this `tty' line. */
static inline struct slip *
sl_alloc(void)
{
	struct slip *sl;
	int i;

	for (i = 0; i < SL_NRUNIT; i++) {
		sl = &sl_ctrl[i];
		if (!set_bit(SLF_INUSE, &sl->flags)) {
			return sl;
		}
	}
	return NULL;
}


/* Free a SLIP channel. */
static inline void
sl_free(struct slip *sl)
{
	/* Free all SLIP frame buffers. */
	if (sl->rbuff)  {
		kfree(sl->rbuff);
	}
	sl->rbuff = NULL;
	if (sl->xbuff)  {
		kfree(sl->xbuff);
	}
	sl->xbuff = NULL;
#ifdef SL_INCLUDE_CSLIP
	/* Save CSLIP statistics */
	if (sl->slcomp)  {
		sl->rx_compressed += sl->slcomp->sls_i_compressed;
		sl->rx_dropped    += sl->slcomp->sls_i_tossed;
		sl->tx_compressed += sl->slcomp->sls_o_compressed;
		sl->tx_misses     += sl->slcomp->sls_o_misses;
	}
	if (sl->cbuff)  {
		kfree(sl->cbuff);
	}
	sl->cbuff = NULL;
	if(sl->slcomp)
		slhc_free(sl->slcomp);
	sl->slcomp = NULL;
#endif

	if (!clear_bit(SLF_INUSE, &sl->flags)) {
		printk("%s: sl_free for already free unit.\n", sl->dev->name);
	}
}

/* MTU has been changed by the IP layer. Unfortunately we are not told about this, but
   we spot it ourselves and fix things up. We could be in an upcall from the tty
   driver, or in an ip packet queue. */

static void sl_changedmtu(struct slip *sl)
{
	struct device *dev = sl->dev;
	unsigned char *xbuff, *rbuff, *oxbuff, *orbuff;
#ifdef SL_INCLUDE_CSLIP
	unsigned char *cbuff, *ocbuff;
#endif
	int len;
	unsigned long flags;

	len = dev->mtu * 2;
/*
 * allow for arrival of larger UDP packets, even if we say not to
 * also fixes a bug in which SunOS sends 512-byte packets even with
 * an MSS of 128
 */
	if (len < 576 * 2)  {
		len = 576 * 2;
	}

	xbuff = (unsigned char *) kmalloc (len + 4, GFP_ATOMIC);
	rbuff = (unsigned char *) kmalloc (len + 4, GFP_ATOMIC);
#ifdef SL_INCLUDE_CSLIP
	cbuff = (unsigned char *) kmalloc (len + 4, GFP_ATOMIC);
#endif

#ifdef SL_INCLUDE_CSLIP
	if (xbuff == NULL || rbuff == NULL || cbuff == NULL)  {
#else
	if (xbuff == NULL || rbuff == NULL)  {
#endif
		printk("%s: unable to grow slip buffers, MTU change cancelled.\n",
		       sl->dev->name);
		dev->mtu = sl->mtu;
		if (xbuff != NULL)  {
			kfree(xbuff);
		}
		if (rbuff != NULL)  {
			kfree(rbuff);
		}
#ifdef SL_INCLUDE_CSLIP
		if (cbuff != NULL)  {
			kfree(cbuff);
		}
#endif
		return;
	}

	save_flags(flags); cli();

	oxbuff    = sl->xbuff;
	sl->xbuff = xbuff;
	orbuff    = sl->rbuff;
	sl->rbuff = rbuff;
#ifdef SL_INCLUDE_CSLIP
	ocbuff    = sl->cbuff;
	sl->cbuff = cbuff;
#endif
	if (sl->xleft)  {
		if (sl->xleft <= len)  {
			memcpy(sl->xbuff, sl->xhead, sl->xleft);
		} else  {
			sl->xleft = 0;
			sl->tx_dropped++;
		}
	}
	sl->xhead = sl->xbuff;

	if (sl->rcount)  {
		if (sl->rcount <= len) {
			memcpy(sl->rbuff, orbuff, sl->rcount);
		} else  {
			sl->rcount = 0;
			sl->rx_over_errors++;
			set_bit(SLF_ERROR, &sl->flags);
		}
	}
#ifdef CONFIG_AX25
	sl->mtu      = dev->mtu + 73;
#else
	sl->mtu      = dev->mtu;
#endif
	sl->buffsize = len;

	restore_flags(flags);

	if (oxbuff != NULL)   {
		kfree(oxbuff);
	}
	if (orbuff != NULL)    {
		kfree(orbuff);
	}
#ifdef SL_INCLUDE_CSLIP
	if (ocbuff != NULL)  {
		kfree(ocbuff);
	}
#endif
}


/* Set the "sending" flag.  This must be atomic, hence the ASM. */
static inline void
sl_lock(struct slip *sl)
{
	if (set_bit(0, (void *) &sl->dev->tbusy))  {
		printk("%s: trying to lock already locked device!\n", sl->dev->name);
        }
}


/* Clear the "sending" flag.  This must be atomic, hence the ASM. */
static inline void
sl_unlock(struct slip *sl)
{
	if (!clear_bit(0, (void *)&sl->dev->tbusy))  {
		printk("%s: trying to unlock already unlocked device!\n", sl->dev->name);
        }
}

/* Send one completely decapsulated IP datagram to the IP layer. */
static void
sl_bump(struct slip *sl)
{
	struct sk_buff *skb;
	int count;

	count = sl->rcount;
#ifdef SL_INCLUDE_CSLIP
	if (sl->mode & (SL_MODE_ADAPTIVE | SL_MODE_CSLIP)) {
		unsigned char c;
		if ((c = sl->rbuff[0]) & SL_TYPE_COMPRESSED_TCP) {
			/* ignore compressed packets when CSLIP is off */
			if (!(sl->mode & SL_MODE_CSLIP)) {
				printk("%s: compressed packet ignored\n", sl->dev->name);
				return;
			}
			/* make sure we've reserved enough space for uncompress to use */
			if (count + 80 > sl->buffsize) {
				sl->rx_over_errors++;
				return;
			}
			count = slhc_uncompress(sl->slcomp, sl->rbuff, count);
			if (count <= 0) {
				return;
			}
		} else if (c >= SL_TYPE_UNCOMPRESSED_TCP) {
			if (!(sl->mode & SL_MODE_CSLIP)) {
				/* turn on header compression */
				sl->mode |= SL_MODE_CSLIP;
				sl->mode &= ~SL_MODE_ADAPTIVE;
				printk("%s: header compression turned on\n", sl->dev->name);
			}
			sl->rbuff[0] &= 0x4f;
			if (slhc_remember(sl->slcomp, sl->rbuff, count) <= 0) {
				return;
			}
		}
	}
#endif  /* SL_INCLUDE_CSLIP */

	skb = alloc_skb(count, GFP_ATOMIC);
	if (skb == NULL)  {
		printk("%s: memory squeeze, dropping packet.\n", sl->dev->name);
		sl->rx_dropped++;
		return;
	}
	skb->len = count;
	skb->dev = sl->dev;
	memcpy(skb->data, sl->rbuff, count);
	netif_rx(skb);
	sl->rx_packets++;
}

/* Encapsulate one IP datagram and stuff into a TTY queue. */
static void
sl_encaps(struct slip *sl, unsigned char *icp, int len)
{
	unsigned char *p;
	int actual, count;


#ifdef CONFIG_AX25
	if (sl->mtu != sl->dev->mtu + 73) {	/* Someone has been ifconfigging */
#else
	if (sl->mtu != sl->dev->mtu) {	/* Someone has been ifconfigging */
#endif
		sl_changedmtu(sl);
	}

	if (len > sl->mtu) {		/* Sigh, shouldn't occur BUT ... */
		len = sl->mtu;
		printk ("%s: truncating oversized transmit packet!\n", sl->dev->name);
		sl->tx_dropped++;
		sl_unlock(sl);
		return;
	}

	p = icp;
#ifdef SL_INCLUDE_CSLIP
	if (sl->mode & SL_MODE_CSLIP)  {
		len = slhc_compress(sl->slcomp, p, len, sl->cbuff, &p, 1);
	}
#endif
#ifdef CONFIG_SLIP_MODE_SLIP6
	if(sl->mode & SL_MODE_SLIP6)
		count = slip_esc6(p, (unsigned char *) sl->xbuff, len);
	else
#endif
		count = slip_esc(p, (unsigned char *) sl->xbuff, len);

	/* Order of next two lines is *very* important.
	 * When we are sending a little amount of data,
	 * the transfer may be completed inside driver.write()
	 * routine, because it's running with interrupts enabled.
	 * In this case we *never* got WRITE_WAKEUP event,
	 * if we did not request it before write operation.
	 *       14 Oct 1994  Dmitry Gorodchanin.
	 */
	sl->tty->flags |= (1 << TTY_DO_WRITE_WAKEUP);
	actual = sl->tty->driver.write(sl->tty, 0, sl->xbuff, count);
#ifdef SL_CHECK_TRANSMIT
	sl->dev->trans_start = jiffies;
#endif
	sl->xleft = count - actual;
	sl->xhead = sl->xbuff + actual;
}

/*
 * Called by the driver when there's room for more data.  If we have
 * more packets to send, we send them here.
 */
static void slip_write_wakeup(struct tty_struct *tty)
{
	int actual;
	struct slip *sl = (struct slip *) tty->disc_data;

	/* First make sure we're connected. */
	if (!sl || sl->magic != SLIP_MAGIC || !sl->dev->start) {
		return;
	}

	if (sl->xleft <= 0)  {
		/* Now serial buffer is almost free & we can start
		 * transmission of another packet */
		sl->tx_packets++;
		tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
		sl_unlock(sl);
		mark_bh(NET_BH);
		return;
	}

	actual = tty->driver.write(tty, 0, sl->xhead, sl->xleft);
	sl->xleft -= actual;
	sl->xhead += actual;
}

/* Encapsulate an IP datagram and kick it into a TTY queue. */
static int
sl_xmit(struct sk_buff *skb, struct device *dev)
{
	struct slip *sl = &sl_ctrl[dev->base_addr];

	if (!dev->start)  {
		printk("%s: xmit call when iface is down\n", dev->name);
		return 1;
	}
	/*
	 * If we are busy already- too bad.  We ought to be able
	 * to queue things at this point, to allow for a little
	 * frame buffer.  Oh well...
	 * -----------------------------------------------------
	 * I hate queues in SLIP driver. May be it's efficient,
	 * but for me latency is more important. ;)
	 * So, no queues !
	 *        14 Oct 1994  Dmitry Gorodchanin.
	 */
	if (dev->tbusy) {
		/* May be we must check transmitter timeout here ?
		 *      14 Oct 1994 Dmitry Gorodchanin.
		 */
#ifdef SL_CHECK_TRANSMIT
		if (jiffies - dev->trans_start  < 20 * HZ)  {
			/* 20 sec timeout not reached */
			return 1;
		}
		printk("%s: transmit timed out, %s?\n", dev->name,
		       (sl->tty->driver.chars_in_buffer(sl->tty) || sl->xleft) ?
		       "bad line quality" : "driver error");
		sl->xleft = 0;
		sl->tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
		sl_unlock(sl);
#else
		return 1;
#endif
	}

	/* We were not busy, so we are now... :-) */
	if (skb != NULL) {
		sl_lock(sl);
		sl_encaps(sl, skb->data, skb->len);
		dev_kfree_skb(skb, FREE_WRITE);
	}
	return 0;
}


/* Return the frame type ID.  This is normally IP but maybe be AX.25. */
static unsigned short
sl_type_trans (struct sk_buff *skb, struct device *dev)
{
#ifdef CONFIG_AX25
	struct slip *sl = &sl_ctrl[dev->base_addr];

	if (sl->mode & SL_MODE_AX25)  {
		return htons(ETH_P_AX25);
	}
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
	struct slip *sl = &sl_ctrl[dev->base_addr];

	if ((sl->mode & SL_MODE_AX25) && type != htons(ETH_P_AX25))  {
		return ax25_encapsulate(buff, dev, type, daddr, saddr, len, skb);
	}
#endif
#endif
	return 0;
}


/* Rebuild the MAC-level header.  Not used by SLIP. */
static int
sl_rebuild_header(void *buff, struct device *dev, unsigned long raddr,
		  struct sk_buff *skb)
{
#ifdef CONFIG_AX25
#ifdef CONFIG_INET
	struct slip *sl = &sl_ctrl[dev->base_addr];

	if (sl->mode & SL_MODE_AX25)  {
		return ax25_rebuild_header(buff, dev, raddr, skb);
	}
#endif
#endif
	return 0;
}


/* Open the low-level part of the SLIP channel. Easy! */
static int
sl_open(struct device *dev)
{
	struct slip *sl = &sl_ctrl[dev->base_addr];
	unsigned long len;

	if (sl->tty == NULL) {
		return -ENODEV;
	}

	/*
	 * Allocate the SLIP frame buffers:
	 *
	 * rbuff	Receive buffer.
	 * xbuff	Transmit buffer.
	 * cbuff        Temporary compression buffer.
	 */
	len = dev->mtu * 2;
	/*
	 * allow for arrival of larger UDP packets, even if we say not to
	 * also fixes a bug in which SunOS sends 512-byte packets even with
	 * an MSS of 128
	 */
	if (len < 576 * 2)  {
		len = 576 * 2;
	}
	sl->rbuff = (unsigned char *) kmalloc(len + 4, GFP_KERNEL);
	if (sl->rbuff == NULL)   {
		goto norbuff;
	}
	sl->xbuff = (unsigned char *) kmalloc(len + 4, GFP_KERNEL);
	if (sl->xbuff == NULL)   {
		goto noxbuff;
	}
#ifdef SL_INCLUDE_CSLIP
	sl->cbuff = (unsigned char *) kmalloc(len + 4, GFP_KERNEL);
	if (sl->cbuff == NULL)   {
		goto nocbuff;
	}
	sl->slcomp = slhc_init(16, 16);
	if (sl->slcomp == NULL)  {
		goto noslcomp;
	}
#endif

#ifdef CONFIG_AX25
	sl->mtu	     = dev->mtu + 73;
#else
	sl->mtu	     = dev->mtu;
#endif
	sl->buffsize = len;
	sl->rcount   = 0;
	sl->xleft    = 0;
#ifdef CONFIG_SLIP_MODE_SLIP6
	sl->xdata    = 0;
	sl->xbits    = 0;
#endif
	sl->flags   &= (1 << SLF_INUSE);      /* Clear ESCAPE & ERROR flags */

	/* Needed because address '0' is special */
	if (dev->pa_addr == 0)  {
		dev->pa_addr=ntohl(0xC0A80001);
	}
	dev->tbusy  = 0;
/*	dev->flags |= IFF_UP; */
	dev->start  = 1;

	return 0;

	/* Cleanup */
#ifdef SL_INCLUDE_CSLIP
noslcomp:
	kfree(sl->cbuff);
nocbuff:
#endif
	kfree(sl->xbuff);
noxbuff:
	kfree(sl->rbuff);
norbuff:
	return -ENOMEM;
}


/* Close the low-level part of the SLIP channel. Easy! */
static int
sl_close(struct device *dev)
{
	struct slip *sl = &sl_ctrl[dev->base_addr];

	if (sl->tty == NULL) {
		return -EBUSY;
	}
	sl->tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
	dev->tbusy = 1;
	dev->start = 0;
	
/*	dev->flags &= ~IFF_UP; */

	return 0;
}

static int
slip_receive_room(struct tty_struct *tty)
{
	return 65536;  /* We can handle an infinite amount of data. :-) */
}

/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of SLIP data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing.
 */
static void
slip_receive_buf(struct tty_struct *tty, unsigned char *cp, char *fp, int count)
{
	struct slip *sl = (struct slip *) tty->disc_data;

	if (!sl || sl->magic != SLIP_MAGIC || !sl->dev->start)
		return;

	/*
	 * Argh! mtu change time! - costs us the packet part received
	 * at the change
	 */
#ifdef CONFIG_AX25
	if (sl->mtu != sl->dev->mtu + 73)  {
#else
	if (sl->mtu != sl->dev->mtu)  {
#endif
		sl_changedmtu(sl);
	}

	/* Read the characters out of the buffer */
	while (count--) {
		if (fp && *fp++) {
			if (!set_bit(SLF_ERROR, &sl->flags))  {
				sl->rx_errors++;
			}
			cp++;
			continue;
		}
#ifdef CONFIG_SLIP_MODE_SLIP6
		if (sl->mode & SL_MODE_SLIP6)
			slip_unesc6(sl, *cp++);
		else
#endif
			slip_unesc(sl, *cp++);
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
	int err;

	/* First make sure we're not already connected. */
	if (sl && sl->magic == SLIP_MAGIC) {
		return -EEXIST;
	}

	/* OK.  Find a free SLIP channel to use. */
	if ((sl = sl_alloc()) == NULL) {
		return -ENFILE;
	}

	sl->tty = tty;
	tty->disc_data = sl;
	if (tty->driver.flush_buffer)  {
		tty->driver.flush_buffer(tty);
	}
	if (tty->ldisc.flush_buffer)  {
		tty->ldisc.flush_buffer(tty);
	}

	/* Restore default settings */
	sl->mode      = SL_MODE_DEFAULT;
	sl->dev->type = ARPHRD_SLIP + sl->mode;
#ifdef CONFIG_AX25	
	if (sl->dev->type == 260) {	/* KISS */
		sl->dev->type = ARPHRD_AX25;
	}
#endif	
	/* Perform the low-level SLIP initialization. */
	if ((err = sl_open(sl->dev)))  {
		return err;
	}
	
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif

	/* Done.  We have linked the TTY line to a channel. */
	return sl->dev->base_addr;
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
	
	tty->disc_data = 0;
	sl->tty = NULL;
	sl_free(sl);
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}


static struct enet_statistics *
sl_get_stats(struct device *dev)
{
	static struct enet_statistics stats;
	struct slip *sl = &sl_ctrl[dev->base_addr];
#ifdef SL_INCLUDE_CSLIP
	struct slcompress *comp;
#endif

	memset(&stats, 0, sizeof(struct enet_statistics));

	stats.rx_packets     = sl->rx_packets;
	stats.tx_packets     = sl->tx_packets;
	stats.rx_dropped     = sl->rx_dropped;
	stats.tx_dropped     = sl->tx_dropped;
	stats.tx_errors      = sl->tx_errors;
	stats.rx_errors      = sl->rx_errors;
	stats.rx_over_errors = sl->rx_over_errors;
#ifdef SL_INCLUDE_CSLIP
	stats.rx_fifo_errors = sl->rx_compressed;
	stats.tx_fifo_errors = sl->tx_compressed;
	stats.collisions     = sl->tx_misses;
	comp = sl->slcomp;
	if (comp) {
		stats.rx_fifo_errors += comp->sls_i_compressed;
		stats.rx_dropped     += comp->sls_i_tossed;
		stats.tx_fifo_errors += comp->sls_o_compressed;
		stats.collisions     += comp->sls_o_misses;
	}
#endif /* CONFIG_INET */
	return (&stats);
}


 /************************************************************************
  *			STANDARD SLIP ENCAPSULATION		  	 *
  ************************************************************************/

int
slip_esc(unsigned char *s, unsigned char *d, int len)
{
	unsigned char *ptr = d;
	unsigned char c;

	/*
	 * Send an initial END character to flush out any
	 * data that may have accumulated in the receiver
	 * due to line noise.
	 */

	*ptr++ = END;

	/*
	 * For each byte in the packet, send the appropriate
	 * character sequence, according to the SLIP protocol.
	 */

	while (len-- > 0) {
		switch(c = *s++) {
		 case END:
			*ptr++ = ESC;
			*ptr++ = ESC_END;
			break;
		 case ESC:
			*ptr++ = ESC;
			*ptr++ = ESC_ESC;
			break;
		 default:
			*ptr++ = c;
			break;
		}
	}
	*ptr++ = END;
	return (ptr - d);
}

static void
slip_unesc(struct slip *sl, unsigned char s)
{

	switch(s) {
	 case END:
		if (!clear_bit(SLF_ERROR, &sl->flags) && (sl->rcount > 2))  {
			sl_bump(sl);
		}
		clear_bit(SLF_ESCAPE, &sl->flags);
		sl->rcount = 0;
		return;

	 case ESC:
		set_bit(SLF_ESCAPE, &sl->flags);
		return;
	 case ESC_ESC:
		if (clear_bit(SLF_ESCAPE, &sl->flags))  {
			s = ESC;
		}
		break;
	 case ESC_END:
		if (clear_bit(SLF_ESCAPE, &sl->flags))  {
			s = END;
		}
		break;
	}
	if (!test_bit(SLF_ERROR, &sl->flags))  {
		if (sl->rcount < sl->buffsize)  {
			sl->rbuff[sl->rcount++] = s;
			return;
		}
		sl->rx_over_errors++;
		set_bit(SLF_ERROR, &sl->flags);
	}
}


#ifdef CONFIG_SLIP_MODE_SLIP6
/************************************************************************
 *			 6 BIT SLIP ENCAPSULATION			*
 ************************************************************************/

int
slip_esc6(unsigned char *s, unsigned char *d, int len)
{
	unsigned char *ptr = d;
	unsigned char c;
	int i;
	unsigned short v = 0;
	short bits = 0;

	/*
	 * Send an initial END character to flush out any
	 * data that may have accumulated in the receiver
	 * due to line noise.
	 */

	*ptr++ = 0x70;

	/*
	 * Encode the packet into printable ascii characters
	 */

	for (i = 0; i < len; ++i) {
		v = (v << 8) | s[i];
		bits += 8;
		while (bits >= 6) {
			bits -= 6;
			c = 0x30 + ((v >> bits) & 0x3F);
			*ptr++ = c;
		}
	}
	if (bits) {
		c = 0x30 + ((v << (6 - bits)) & 0x3F);
		*ptr++ = c;
	}
	*ptr++ = 0x70;
	return ptr - d;
}

void
slip_unesc6(struct slip *sl, unsigned char s)
{
	unsigned char c;

	if (s == 0x70) {
		if (!clear_bit(SLF_ERROR, &sl->flags) && (sl->rcount > 2))  {
			sl_bump(sl);
		}
		sl->rcount = 0;
		sl->xbits = 0;
		sl->xdata = 0;
 	} else if (s >= 0x30 && s < 0x70) {
		sl->xdata = (sl->xdata << 6) | ((s - 0x30) & 0x3F);
		sl->xbits += 6;
		if (sl->xbits >= 8) {
			sl->xbits -= 8;
			c = (unsigned char)(sl->xdata >> sl->xbits);
			if (!test_bit(SLF_ERROR, &sl->flags))  {
				if (sl->rcount < sl->buffsize)  {
					sl->rbuff[sl->rcount++] = c;
					return;
				}
				sl->rx_over_errors++;
				set_bit(SLF_ERROR, &sl->flags);
			}
		}
 	}
}
#endif /* CONFIG_SLIP_MODE_SLIP6 */

#ifdef CONFIG_AX25
int
sl_set_mac_address(struct device *dev, void *addr)
{
	int err;

	err = verify_area(VERIFY_READ, addr, 7);
	if (err)  {
		return err;
	}

	memcpy_fromfs(dev->dev_addr, addr, 7);	/* addr is an AX.25 shifted ASCII mac address */

	return 0;
}

static int
sl_set_dev_mac_address(struct device *dev, void *addr)
{
	memcpy(dev->dev_addr, addr, 7);
	return 0;
}
#endif /* CONFIG_AX25 */


/* Perform I/O control on an active SLIP channel. */
static int
slip_ioctl(struct tty_struct *tty, void *file, int cmd, void *arg)
{
	struct slip *sl = (struct slip *) tty->disc_data;
	int err;
	unsigned int tmp;

	/* First make sure we're connected. */
	if (!sl || sl->magic != SLIP_MAGIC) {
		return -EINVAL;
	}

	switch(cmd) {
	 case SIOCGIFNAME:
		err = verify_area(VERIFY_WRITE, arg, 16);
		if (err)  {
			return -err;
		}
		memcpy_tofs(arg, sl->dev->name, strlen(sl->dev->name) + 1);
		return 0;

	case SIOCGIFENCAP:
		err = verify_area(VERIFY_WRITE, arg, sizeof(long));
		if (err)  {
			return -err;
		}
		put_fs_long(sl->mode, (long *)arg);
		return 0;

	case SIOCSIFENCAP:
		err = verify_area(VERIFY_READ, arg, sizeof(long));
		if (err)  {
			return -err;
		}
		tmp = get_fs_long((long *)arg);
#ifndef SL_INCLUDE_CSLIP
		if (tmp & (SL_MODE_CSLIP|SL_MODE_ADAPTIVE))  {
			return -EINVAL;
		}
#else
		if ((tmp & (SL_MODE_ADAPTIVE | SL_MODE_CSLIP)) ==
		    (SL_MODE_ADAPTIVE | SL_MODE_CSLIP))  {
			/* return -EINVAL; */
			tmp &= ~SL_MODE_ADAPTIVE;
		}
#endif
#ifndef CONFIG_SLIP_MODE_SLIP6
		if (tmp & SL_MODE_SLIP6)  {
			return -EINVAL;
		}
#endif
#ifndef CONFIG_AX25
		if (tmp & SL_MODE_AX25)  {
			return -EINVAL;
		}
#else
		if (tmp & SL_MODE_AX25)	{
			sl->dev->addr_len=7;	/* sizeof an AX.25 addr */
			sl->dev->hard_header_len=17;	/* We don't do digipeaters */
		} else	{
			sl->dev->addr_len=0;	/* No mac addr in slip mode */
			sl->dev->hard_header_len=0;
		}
#endif
		sl->mode = tmp;
		sl->dev->type = ARPHRD_SLIP+sl->mode;
#ifdef CONFIG_AX25		
		if (sl->dev->type == 260)  {
			sl->dev->type = ARPHRD_AX25;
		}
#endif		
		return 0;

	 case SIOCSIFHWADDR:
#ifdef CONFIG_AX25
		return sl_set_mac_address(sl->dev, arg);
#else
		return -EINVAL;
#endif

	/* Allow stty to read, but not set, the serial port */
	case TCGETS:
	case TCGETA:
		return n_tty_ioctl(tty, (struct file *) file, cmd, (unsigned long) arg);

	default:
		return -ENOIOCTLCMD;
	}
}

static int sl_open_dev(struct device *dev)
{
	struct slip *sl = &sl_ctrl[dev->base_addr];
	if(sl->tty==NULL)
		return -ENODEV;
	return 0;
}

/* Initialize the SLIP driver.  Called by DDI. */
int
slip_init(struct device *dev)
{
	struct slip *sl = &sl_ctrl[dev->base_addr];
	int i;
#ifdef CONFIG_AX25
	static char ax25_bcast[7] =
		{'Q'<<1,'S'<<1,'T'<<1,' '<<1,' '<<1,' '<<1,'0'<<1};
	static char ax25_test[7] =
		{'L'<<1,'I'<<1,'N'<<1,'U'<<1,'X'<<1,' '<<1,'1'<<1};
#endif

	if (already++ == 0) {
		printk("SLIP: version %s (%d channels) %s\n",
		       SLIP_VERSION, SL_NRUNIT,
#ifdef CONFIG_SLIP_MODE_SLIP6
		       "(6 bit encapsulation enabled)"
#else
		       ""
#endif
		       );
#if defined(SL_INCLUDE_CSLIP) && !defined(MODULE)
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
		if ((i = tty_register_ldisc(N_SLIP, &sl_ldisc)) != 0)  {
			printk("SLIP: can't register line discipline (err = %d)\n", i);
		}
	}

	/* Set up the "SLIP Control Block". (And clear statistics) */
	
	memset(sl, 0, sizeof (struct slip));
	sl->magic  = SLIP_MAGIC;
	sl->dev	   = dev;
	
	/* Finish setting up the DEVICE info. */
	dev->mtu		= SL_MTU;
	dev->hard_start_xmit	= sl_xmit;
	dev->open		= sl_open_dev;
	dev->stop		= sl_close;
	dev->hard_header	= sl_header;
	dev->type_trans	        = sl_type_trans;
	dev->get_stats	        = sl_get_stats;
#ifdef HAVE_SET_MAC_ADDR
#ifdef CONFIG_AX25
	dev->set_mac_address    = sl_set_dev_mac_address;
#endif
#endif
	dev->hard_header_len	= 0;
	dev->addr_len		= 0;
	dev->type		= ARPHRD_SLIP + SL_MODE_DEFAULT;
#ifdef CONFIG_AX25
	if (sl->dev->type == 260)  {
		sl->dev->type = ARPHRD_AX25;
	}
	memcpy(dev->broadcast, ax25_bcast, 7); /* Only activated in AX.25 mode */
	memcpy(dev->dev_addr, ax25_test, 7);   /*    ""      ""       ""    "" */
#endif
	dev->rebuild_header	= sl_rebuild_header;

	for (i = 0; i < DEV_NUMBUFFS; i++)  {
		skb_queue_head_init(&dev->buffs[i]);
	}

	/* New-style flags. */
	dev->flags		= 0;
	dev->family		= AF_INET;
	dev->pa_addr		= 0;
	dev->pa_brdaddr	        = 0;
	dev->pa_mask		= 0;
	dev->pa_alen		= sizeof(unsigned long);

	return 0;
}
#ifdef MODULE
char kernel_version[] = UTS_RELEASE;

static struct device dev_slip[SL_NRUNIT] =  {
	{
		"sl0",		/* slip */
		0, 0, 0, 0,	/* memory */
		0, 0,		/* base, irq */
		0, 0, 0, NULL, slip_init,
	},
	{ "sl1" , 0, 0, 0, 0,  1, 0, 0, 0, 0, NULL, slip_init },
	{ "sl2" , 0, 0, 0, 0,  2, 0, 0, 0, 0, NULL, slip_init },
	{ "sl3" , 0, 0, 0, 0,  3, 0, 0, 0, 0, NULL, slip_init },
#ifdef SL_SLIP_LOTS
	{ "sl4" , 0, 0, 0, 0,  4, 0, 0, 0, 0, NULL, slip_init },
	{ "sl5" , 0, 0, 0, 0,  5, 0, 0, 0, 0, NULL, slip_init },
	{ "sl6" , 0, 0, 0, 0,  6, 0, 0, 0, 0, NULL, slip_init },
	{ "sl7" , 0, 0, 0, 0,  7, 0, 0, 0, 0, NULL, slip_init },
	{ "sl8" , 0, 0, 0, 0,  8, 0, 0, 0, 0, NULL, slip_init },
	{ "sl9" , 0, 0, 0, 0,  9, 0, 0, 0, 0, NULL, slip_init },
	{ "sl10", 0, 0, 0, 0, 10, 0, 0, 0, 0, NULL, slip_init },
	{ "sl11", 0, 0, 0, 0, 11, 0, 0, 0, 0, NULL, slip_init },
	{ "sl12", 0, 0, 0, 0, 12, 0, 0, 0, 0, NULL, slip_init },
	{ "sl13", 0, 0, 0, 0, 13, 0, 0, 0, 0, NULL, slip_init },
	{ "sl14", 0, 0, 0, 0, 14, 0, 0, 0, 0, NULL, slip_init },
	{ "sl15", 0, 0, 0, 0, 15, 0, 0, 0, 0, NULL, slip_init },
#endif /* SL_SLIP_LOTS */
};

int
init_module(void)
{
	int err;
	int i;

	for (i = 0; i < SL_NRUNIT; i++)  {
		if ((err = register_netdev(&dev_slip[i])))  {
			if (err == -EEXIST)  {
				printk("SLIP: devices already present. Module not loaded.\n");
			}
			return err;
		}
	}
	return 0;
}

void
cleanup_module(void)
{
	int i;

	if (MOD_IN_USE)  {
		printk("SLIP: device busy, remove delayed\n");
		return;
	}
	for (i = 0; i < SL_NRUNIT; i++)  {
		unregister_netdev(&dev_slip[i]);
	}
	if ((i = tty_register_ldisc(N_SLIP, NULL)))  {
		printk("SLIP: can't unregister line discipline (err = %d)\n", i);
	}
	already = 0;
}
#endif /* MODULE */
