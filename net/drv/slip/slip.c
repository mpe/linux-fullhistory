/*
 * slip.c	This module implements the SLIP protocol for kernel-based
 *		devices like TTY.  It interfaces between a raw TTY, and the
 *		kernel's NET protocol layers (via DDI).
 *
 * Version:	@(#)slip.c	0.5.0	(02/11/93)
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
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/slip.h>
#include <netinet/in.h>


#define	SLIP_VERSION	"0.5.0"
#define	SL_DUMP


#define	SL_DEBUG
#ifdef	SL_DEBUG
#   define PRINTK(x)	printk x
#else
#   define PRINTK(x)	/**/
#endif


/* Define some IP layer stuff.  Not all systems have it. */
#ifdef SL_DUMP
#  define	IP_VERSION	4	/* version# of our IP software	*/
#  define	IPF_F_OFFSET	0x1fff	/* Offset field			*/
#  define	IPF_DF		0x4000	/* Don't fragment flag		*/
#  define	IPF_MF		0x2000	/* More Fragments flag		*/

   typedef struct ipheader {
	u_char	v_ihl;			/* Version + IP header length	*/
	u_char	tos;			/* Type of service		*/
	u_short	length;			/* Total length			*/
	u_short	id;			/* Identification		*/
	u_short	fl_offs;		/* Flags + fragment offset	*/
	u_char	ttl;			/* Time to live			*/
	u_char	protocol;		/* Protocol			*/
	u_short	checksum;		/* Header checksum		*/
	u_long	source;			/* Source address		*/
	u_long	dest;			/* Destination address		*/
    } IP;
#   define	IP_OF_COPIED	0x80	/* Copied-on-fragmentation flag	*/
#   define	IP_OF_CLASS	0x60	/* Option class			*/
#   define	IP_OF_NUMBER	0x1f	/* Option number		*/
#   define	IPO_EOL		0	/* End of options list		*/
#   define	IPO_NOOP	1	/* No Operation			*/
#   define	IPO_SECURITY	2	/* Security parameters		*/
#   define	IPO_LSROUTE	3	/* Loose Source Routing		*/
#   define	IPO_TIMESTAMP	4	/* Internet Timestamp		*/
#   define	IPO_RROUTE	7	/* Record Route			*/
#   define	IPO_STREAMID	8	/* Stream ID			*/
#   define	IPO_SSROUTE	9	/* Strict Source Routing	*/
#   define	IP_TS_ONLY	0	/* Time stamps only		*/
#   define	IP_TS_ADDRESS	1	/* Addresses + Time stamps	*/
#   define	IP_TS_PRESPEC	3	/* Prespecified addresses only	*/
#endif


/* This table holds the control blocks for all SLIP channels. */
static struct slip sl_ctrl[SL_NRUNIT];


#ifdef SL_DUMP
/* Dump the contents of an IP datagram. */
static void
ip_dump(unsigned char *ptr, int len)
{
  int hdr_ver, hdr_len, dta_len, dta_off;
  IP *ip;
  extern char *in_ntoa(long num);

  ip = (IP *) ptr;
  hdr_ver = (ip->v_ihl & 0xF0) >> 4;
  hdr_len = (ip->v_ihl & 0x0F) * sizeof(long);
  dta_len = ntohs(ip->length);
  dta_off = (ntohs(ip->fl_offs) & IPF_F_OFFSET) << 3 ;

  printk("\r*****\n");
  printk("SLIP: %s->", in_ntoa(ip->source));
  printk("%s\n", in_ntoa(ip->dest));
  printk(" len %u ihl %u ttl %u prot %u",
	dta_len, ip->v_ihl & 0xFF, ip->ttl & 0xFF, ip->protocol & 0xFF);

  if (ip->tos != 0) printk(" tos %u", ip->tos);
  if (dta_off != 0 || (ntohs(ip->fl_offs) & IPF_MF))
	printk(" id %u offs %u", ntohs(ip->id), dta_off);

  if (ntohs(ip->fl_offs) & IPF_DF) printk(" DF");
  if (ntohs(ip->fl_offs) & IPF_MF) printk(" MF");
  printk("\n*****\n");
}
#endif


/*
 * Read data from a TTY queue.  This function will eventually
 * be moved into the TTY layer itself, making it available for
 * other layers, too.
 */
int tty_read_data(struct tty_struct *tty, unsigned char *buf, int max)
{
	register int count;
	register unsigned char c;

	/* Keep fetching characters from TTY until done or full. */
	count = 0;
	PRINTK (("SLIP: tty_read:"));
	while (max-- > 0) {
		if (EMPTY(&tty->read_q)) break;
		c = (get_tty_queue(&tty->read_q) & 0377);
		*buf++ = c;
		PRINTK ((" %02x", (int) (c & 255)));
		count++;
	}
	PRINTK (("\r\nSLIP: tty_read: read %d bytes\r\n", count));
	return(count);
}


/*
 * Write data to a TTY queue.  This function will eventually
 * be moved into the TTY layer itself, making it available for
 * other layers, too.
 */
void tty_write_data(struct tty_struct *tty, char *buf, int count)
{
	/* PRINTK (("SLIP: tty_write: writing %d bytes\r\n", count)); */
	while(count--) {
		put_tty_queue(*buf++, &tty->write_q);
	}
}


/*
 * Flush a TTY write queue by calling the TTY layer.  This
 * function will eventually be moved into the TTY layer itself,
 * making it available for other layers, too.
 */
void tty_flush(struct tty_struct *tty)
{
	/* PRINTK (("SLIP: tty_flush: flusing the toilet...\r\n")); */
	/*
	 * This should also tell TTY which function to call-back
	 * when the work is done, allowing us to clean up and
	 * possibly start another output...
	 */
	tty_write_flush(tty);
}


/* Find a SLIP channel from its `tty' link. */
static struct slip *
sl_find(struct tty_struct *tty)
{
	int i;
	struct slip *sl;

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
	int i;
	struct slip *sl;
	unsigned long flags;

	for (i = 0; i < SL_NRUNIT; i++) {
		sl = &sl_ctrl[i];
		if (sl->inuse == 0) {
			__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
			sl->inuse++;
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

	if (sl->inuse == 1) {
		__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
		sl->inuse--;
		sl->tty = NULL;
		__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
	}
}


/* Stuff one byte into a SLIP queue. */
static inline void
put_sl_queue(struct sl_queue * queue, char c)
{
	int head;
	unsigned long flags;

	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
	head = (queue->head + 1) & (SL_BUF_SIZE-1);
	if (head != queue->tail) {
		queue->buf[queue->head] = c;
		queue->head = head;
	}
	__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}


/* Release 'i' bytes from a SLIP queue. */
static inline void
eat_sl_queue(struct sl_queue * queue, int i)
{
	unsigned long flags;

	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
	if (queue->tail != queue->head)
		queue->tail = (queue->tail + i) & (SL_BUF_SIZE-1);
	__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}


/* Set the "sending" flag.  This must be atomic, hence the ASM. */
static inline void
sl_lock(struct slip *sl)
{
	unsigned long flags;

	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
	sl->sending = 1;
	__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}


/* Clear the "sending" flag.  This must be atomic, hence the ASM. */
static inline void
sl_unlock(struct slip *sl)
{
	unsigned long flags;

	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=r" (flags));
	sl->sending = 0;
	__asm__ __volatile__("pushl %0 ; popfl"::"r" (flags));
}


/* Send one completely decapsulated IP datagram to the IP layer. */
static void
sl_recv(struct slip *sl, int len)
{
#if 0
	struct device *dev;
#endif
	register unsigned char *p;
	int done;

	PRINTK (("SLIP: sending one dgram to IP (len=%d)\r\n", len));
#ifdef SL_DUMP
	printk("<< iface \"sl%d\" recv:\r\n", sl->line);
	ip_dump((unsigned char *) &sl->rcv_queue.buf[sl->rcv_queue.tail], len);
#endif

	/* Bump the datagram to the upper layers... */
#if 0
	dev = sl->dev;
	p = (unsigned char *) &sl->rcv_queue.buf[sl->rcv_queue.tail];
	do {
		done = dev_rint(p, len, 0, dev);
		if (done == 1) break;
	} while(1);
#endif
	eat_sl_queue(&sl->rcv_queue, len);
	sl->rcvd++;
}


/* Encapsulate one IP datagram and stuff into a TTY queue. */
static void
sl_send(struct slip *sl, unsigned char *p, int len)
{
	register unsigned char *bp;
	register int count;

	/* PRINTK (("SLIP: sl_send(0x%X, %d) called\n", p, len)); */
	bp = (unsigned char *)sl->xbuff;
#ifdef SL_DUMP
	printk(">> iface \"sl%d\" sent:\r\n", sl->line);
	ip_dump(p, len);
#endif
	count = 0;

	/*
	 * Send an initial END character to flush out any
	 * data that may have accumulated in the receiver
	 * due to line noise.
	 */
	*bp++ = END;
	count++;

	/*
	 * For each byte in the packet, send the appropriate
	 * character sequence, according to the SLIP protocol.
	 * FIXME: change this to copy blocks of characters between
	 *	  special characters to improve speed.
	 */
	while(len--) {
                switch(*p) {
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
				*bp++ = *p;
				count++;
		}
		p++;
	}
	*bp++ = END;
	count++;
	sl->sent++;
	tty_write_data(sl->tty, sl->xbuff, count);	/* stuff into TTY	*/
}


/* Encapsulate an IP datagram and kick it into a TTY queue. */
static int
sl_start_xmit(void /*struct sk_buff*/ *skb, void /*struct device*/ *dev)
{
	struct slip *sl;
	struct tty_struct *tty;

#if 0
	/* Find the correct SLIP channel to use. */
	sl = &sl_ctrl[dev->base_addr];
	tty = sl->tty;
	/* PRINTK (("SLIP: sl_start_xmit(\"%s\") skb=0x%X busy=%d\n",
					dev->name, skb, sl->sending)); */

	/*
	 * If we are busy already- too bad.  We ought to be able
	 * to queue things at this point, to allow for a little
	 * frame buffer.  Oh well...
	 */
	if (sl->sending) {
		PRINTK (("SLIP: sl_start_xmit: BUSY\r\n"));
		return(1);
	}

	/* We were not, so we are now... :-) */
	sti();
	sl_lock(sl);

	if (skb != NULL) {
		/* PRINTK (("SLIP: sl_start_xmit: encaps(0x%X, %d)\r\n",
					(unsigned) skb, skb->len));	*/
		sl_send(sl, (unsigned char *) (skb + 1), skb->len);
	}

	/* PRINTK (("SLIP: sl_start_xmit: kicking TTY!\n")); */
	tty_flush(tty);			/* kick TTY in the butt */
	sl_unlock(sl);
#endif
	return(0);
}


/*
 * Return the frame type ID.  Shouldn't we pick this up from the
 * frame on which we have to operate, like in 'eth' ? - FvK
 */
static unsigned short
sl_type_trans (void /*struct sk_buff*/ *skb, void /*struct device*/ *dev)
{
#ifdef notdef
	struct slip *sl;

	sl = sl_ctrl[dev->base_addr];
	return(sl->type);
#else
	return(NET16(ETHERTYPE_IP));
#endif
}


/* Open the low-level part of the SLIP channel. Easy! */
static int
sl_open(void /*struct device*/ *dev)
{
	struct slip *sl;

#if 0
	sl = &sl_ctrl[dev->base_addr];
	if (sl->tty == NULL) {
		PRINTK (("SLIP: channel sl%d not connected!\n", sl->line));
		return(-ENXIO);
	}

	sl->escape = 0;			/* SLIP state machine		*/
	sl->received = 0;		/* SLIP receiver count		*/
	PRINTK (("SLIP: channel sl%d opened.\n", sl->line));
#endif
	return(0);
}


/* Close the low-level part of the SLIP channel. Easy! */
static int
sl_close(void /*struct device*/ *dev)
{
	struct slip *sl;

#if 0
	sl = &sl_ctrl[dev->base_addr];
	if (sl->tty == NULL) {
		PRINTK (("SLIP: channel sl%d not connected!\n", sl->line));
		return(-EBUSY);
	}
	sl_free(sl);

	/*
	 * The next two lines should be handled by a "dev_down()"
	 * function, which takes care of shutting down an inter-
	 * face.  It would also be called by the "ip" module when
	 * an interface is brought down manually.
	 */
	del_devroute(dev);
	dev->up = 0;
	PRINTK (("SLIP: channel sl%d closed.\n", sl->line));
#endif
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
	unsigned char buff[SL_MTU * 2];
	register unsigned char *p;
	register int count;
	struct slip *sl;
	unsigned char c;

#if 0
PRINTK (("SLIP: slip_recv(%d) called\n", tty->line));
	if ((sl = sl_find(tty)) == NULL) return;	/* not connected */

	if (SL_FULL(&sl->rcv_queue)) {
		PRINTK (("SLIP: recv queue full\r\n"));
		return;
	}

	while((count = tty_read_data(tty, buff, (SL_MTU * 2))) > 0) {
		p = buff;
		while(count-- > 0) {
			c = *p++;
			switch(c) {
				case ESC:
					sl->escape = 1;
					break;
				case ESC_ESC:
					if (sl->escape) c = ESC;
					put_sl_queue(&sl->rcv_queue, c);
					sl->escape = 0;
					sl->received++;
					break;
				case ESC_END:
					if (sl->escape) c = END;
					put_sl_queue(&sl->rcv_queue, c);
					sl->escape = 0;
					sl->received++;
					break;
				case END:
					sl->escape = 0;
					if (sl->received < 3) {
						if (sl->received)
							eat_sl_queue(&sl->rcv_queue,
								sl->received);
						sl->received = 0;
				  	} else {
	PRINTK (("SLIP: full frame received!\r\n"));
						sl_recv(sl, sl->received);
						sl->received = 0;
				  	}
					break;
				default:
					put_sl_queue(&sl->rcv_queue, c);
					sl->escape = 0;
					sl->received++;
			}
		}
	}
#endif
}


/* Return the channel number of a SLIP connection. */
static int
slip_chan(struct tty_struct *tty)
{
	struct slip *sl;

	if ((sl = sl_find(tty)) == NULL) return(-ENXIO);  /* not connected */
	return(sl->line);
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
		PRINTK (("SLIP: TTY %d already connected to sl%d !\n",
			tty->line, sl->line));
		return(-EEXIST);
	}

	/* OK.  Find a free SLIP channel to use. */
	if ((sl = sl_alloc()) == NULL) {
		PRINTK (("SLIP: TTY %d not connected: all channels in use!\n",
							tty->line));
		return(-ENFILE);
	}
	sl->tty = tty;

	/* Link the TTY line to this channel. */
	(void) sl_open(sl->dev);
	PRINTK (("SLIP: TTY %d connected to sl%d.\n", tty->line, sl->line));

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
		PRINTK (("SLIP: TTY %d not connected !\n", tty->line));
		return;
	}

	(void) sl_close(sl->dev);
	PRINTK (("SLIP: TTY %d disconnected from sl%d.\n", tty->line, sl->line));
}


/* Initialize the SLIP driver.  Called by DDI. */
int
slip_init(struct ddi *dev)
{
	int i;
	struct slip *sl;

#if 1
	PRINTK(("SLIP/DDI: version %s (%d channels, buffer=0x%X:%d)\n",
					ddi->ioaddr, ddi->memaddr, ddi->memsize));
#else
	sl = &sl_ctrl[dev->base_addr];

	if (already++ == 0) {
		printk("SLIP: version %s (%d channels): ",
					SLIP_VERSION, SL_NRUNIT);
		if ((i = tty_set_ldisc(N_SLIP, slip_open, slip_close,
			slip_chan, slip_recv)) == 0) printk("OK\n");
		  else printk("ERROR: %d\n", i);
	}

	/* Set up the "SLIP Control Block". */
	sl->inuse            = 0;		/* not allocated now	*/
	sl->line             = dev->base_addr;	/* SLIP channel number	*/
	sl->tty              = NULL;		/* pointer to TTY line	*/
	sl->dev              = dev;		/* pointer to DEVICE	*/
	sl->sending          = 0;		/* locked on output	*/
	sl->rcv_queue.head   = 0;		/* ptr to RECV queue	*/
	sl->rcv_queue.tail   = 0;		/* ptr to RECV queue	*/
	sl->escape           = 0;		/* SLIP state machine	*/
	sl->received         = 0;		/* SLIP receiver count	*/
	sl->sent             = 0;		/* #frames sent out	*/
	sl->rcvd             = 0;		/* #frames received	*/
	sl->errors           = 0;		/* not used at present	*/

	/* Finish setting up the DEVICE info. */
	dev->mtu             = SL_MTU;
	dev->rmem_end        = (unsigned long)&sl->rcv_queue.buf[SL_BUF_SIZE-1];
	dev->rmem_start      = (unsigned long)&sl->rcv_queue.buf[0];
	dev->mem_end         = (unsigned long)&sl->xbuff[(SL_MTU * 2) -1];
	dev->mem_start       = (unsigned long)&sl->xbuff[0];
	dev->hard_start_xmit = sl_start_xmit;
	dev->open            = sl_open;
	dev->stop            = sl_close;
	dev->hard_header     = sl_hard_header;
	dev->add_arp         = sl_add_arp;
	dev->type_trans      = sl_type_trans;
	dev->hard_header_len = 0;
	dev->addr_len        = 0;
	dev->type            = 0;	/* FIXME: ??? */
	dev->queue_xmit      = dev_queue_xmit;
	dev->rebuild_header  = sl_rebuild_header;
	for (i = 0; i < DEV_NUMBUFFS; i++) dev->buffs[i] = NULL;

#endif
	return(0);
}
