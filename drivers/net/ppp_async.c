/*
 * PPP async serial channel driver for Linux.
 *
 * Copyright 1999 Paul Mackerras.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * This driver provides the encapsulation and framing for sending
 * and receiving PPP frames over async serial lines.  It relies on
 * the generic PPP layer to give it frames to send and to process
 * received frames.  It implements the PPP line discipline.
 *
 * Part of the code in this driver was inspired by the old async-only
 * PPP driver, written by Michael Callahan and Al Longyear, and
 * subsequently hacked by Paul Mackerras.
 *
 * ==FILEVERSION 990806==
 */

/* $Id: ppp_async.c,v 1.3 1999/09/02 05:30:10 paulus Exp $ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/tty.h>
#include <linux/netdevice.h>
#include <linux/poll.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/ppp_channel.h>
#include <asm/uaccess.h>

#define PPP_VERSION	"2.4.0"

#define OBUFSIZE	256

/* Structure for storing local state. */
struct asyncppp {
	struct tty_struct *tty;
	unsigned int	flags;
	unsigned int	state;
	unsigned int	rbits;
	int		mru;
	unsigned long	busy;
	u32		xaccm[8];
	u32		raccm;
	unsigned int	bytes_sent;
	unsigned int	bytes_rcvd;

	struct sk_buff	*tpkt;
	int		tpkt_pos;
	u16		tfcs;
	unsigned char	*optr;
	unsigned char	*olim;
	struct sk_buff_head xq;
	unsigned long	last_xmit;

	struct sk_buff	*rpkt;
	struct sk_buff_head rq;
	wait_queue_head_t rwait;

	struct ppp_channel chan;	/* interface to generic ppp layer */
	int		connected;
	int	index;
	unsigned char	obuf[OBUFSIZE];
};

/* Bit numbers in busy */
#define XMIT_BUSY	0
#define RECV_BUSY	1
#define XMIT_WAKEUP	2
#define XMIT_FULL	3

/* State bits */
#define SC_TOSS		0x20000000
#define SC_ESCAPE	0x40000000

/* Bits in rbits */
#define SC_RCV_BITS	(SC_RCV_B7_1|SC_RCV_B7_0|SC_RCV_ODDP|SC_RCV_EVNP)

#define PPPASYNC_MAX_RQLEN	32	/* arbitrary */

static int flag_time = HZ;
MODULE_PARM(flag_time, "i");

/*
 * Prototypes.
 */
static int ppp_async_encode(struct asyncppp *ap);
static int ppp_async_send(struct ppp_channel *chan, struct sk_buff *skb);
static int ppp_async_push(struct asyncppp *ap);
static void ppp_async_flush_output(struct asyncppp *ap);
static void ppp_async_input(struct asyncppp *ap, const unsigned char *buf,
			    char *flags, int count);

struct ppp_channel_ops async_ops = {
	ppp_async_send
};

/*
 * Routines for locking and unlocking the transmit and receive paths.
 */
static inline void
lock_path(struct asyncppp *ap, int bit)
{
	do {
		while (test_bit(bit, &ap->busy))
		       mb();
	} while (test_and_set_bit(bit, &ap->busy));
	mb();
}

static inline int
trylock_path(struct asyncppp *ap, int bit)
{
	if (test_and_set_bit(bit, &ap->busy))
		return 0;
	mb();
	return 1;
}

static inline void
unlock_path(struct asyncppp *ap, int bit)
{
	mb();
	clear_bit(bit, &ap->busy);
}

#define lock_xmit_path(ap)	lock_path(ap, XMIT_BUSY)
#define trylock_xmit_path(ap)	trylock_path(ap, XMIT_BUSY)
#define unlock_xmit_path(ap)	unlock_path(ap, XMIT_BUSY)
#define lock_recv_path(ap)	lock_path(ap, RECV_BUSY)
#define trylock_recv_path(ap)	trylock_path(ap, RECV_BUSY)
#define unlock_recv_path(ap)	unlock_path(ap, RECV_BUSY)

static inline void
flush_skb_queue(struct sk_buff_head *q)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(q)) != 0)
		kfree_skb(skb);
}

/*
 * Routines implementing the PPP line discipline.
 */

/*
 * Called when a tty is put into PPP line discipline.
 */
static int
ppp_async_open(struct tty_struct *tty)
{
	struct asyncppp *ap;

	ap = kmalloc(sizeof(*ap), GFP_KERNEL);
	if (ap == 0)
		return -ENOMEM;

	MOD_INC_USE_COUNT;

	/* initialize the asyncppp structure */
	memset(ap, 0, sizeof(*ap));
	ap->tty = tty;
	ap->mru = PPP_MRU;
	ap->xaccm[0] = ~0U;
	ap->xaccm[3] = 0x60000000U;
	ap->raccm = ~0U;
	ap->optr = ap->obuf;
	ap->olim = ap->obuf;
	skb_queue_head_init(&ap->xq);
	skb_queue_head_init(&ap->rq);
	init_waitqueue_head(&ap->rwait);

	tty->disc_data = ap;

	return 0;
}

/*
 * Called when the tty is put into another line discipline
 * (or it hangs up).
 */
static void
ppp_async_close(struct tty_struct *tty)
{
	struct asyncppp *ap = tty->disc_data;

	if (ap == 0)
		return;
	tty->disc_data = 0;
	lock_xmit_path(ap);
	lock_recv_path(ap);
	if (ap->rpkt != 0)
		kfree_skb(ap->rpkt);
	flush_skb_queue(&ap->rq);
	if (ap->tpkt != 0)
		kfree_skb(ap->tpkt);
	flush_skb_queue(&ap->xq);
	if (ap->connected)
		ppp_unregister_channel(&ap->chan);
	kfree(ap);
	MOD_DEC_USE_COUNT;
}

/*
 * Read a PPP frame.  pppd can use this to negotiate over the
 * channel before it joins it to a bundle.
 */
static ssize_t
ppp_async_read(struct tty_struct *tty, struct file *file,
	       unsigned char *buf, size_t count)
{
	struct asyncppp *ap = tty->disc_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	struct sk_buff *skb = 0;

	ret = -ENXIO;
	if (ap == 0)
		goto out;		/* should never happen */

	add_wait_queue(&ap->rwait, &wait);
	current->state = TASK_INTERRUPTIBLE;
	for (;;) {
		ret = -EAGAIN;
		skb = skb_dequeue(&ap->rq);
		if (skb)
			break;
		if (file->f_flags & O_NONBLOCK)
			break;
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&ap->rwait, &wait);

	if (skb == 0)
		goto out;

	ret = -EOVERFLOW;
	if (skb->len > count)
		goto outf;
	ret = -EFAULT;
	if (copy_to_user(buf, skb->data, skb->len))
		goto outf;
	ret = skb->len;

 outf:
	kfree_skb(skb);
 out:
	return ret;
}

/*
 * Write a ppp frame.  pppd can use this to send frames over
 * this particular channel.
 */
static ssize_t
ppp_async_write(struct tty_struct *tty, struct file *file,
		const unsigned char *buf, size_t count)
{
	struct asyncppp *ap = tty->disc_data;
	struct sk_buff *skb;
	ssize_t ret;

	ret = -ENXIO;
	if (ap == 0)
		goto out;	/* should never happen */

	ret = -ENOMEM;
	skb = alloc_skb(count + 2, GFP_KERNEL);
	if (skb == 0)
		goto out;
	skb_reserve(skb, 2);
	ret = -EFAULT;
	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		goto out;
	}

	skb_queue_tail(&ap->xq, skb);
	ppp_async_push(ap);

	ret = count;

 out:
	return ret;
}

static int
ppp_async_ioctl(struct tty_struct *tty, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct asyncppp *ap = tty->disc_data;
	int err, val;
	u32 accm[8];
	struct sk_buff *skb;

	err = -ENXIO;
	if (ap == 0)
		goto out;	/* should never happen */
	err = -EPERM;
	if (!capable(CAP_NET_ADMIN))
		goto out;

	err = -EFAULT;
	switch (cmd) {
	case PPPIOCGFLAGS:
		val = ap->flags | ap->rbits;
		if (put_user(val, (int *) arg))
			break;
		err = 0;
		break;
	case PPPIOCSFLAGS:
		if (get_user(val, (int *) arg))
			break;
		ap->flags = val & ~SC_RCV_BITS;
		ap->rbits = val & SC_RCV_BITS;
		err = 0;
		break;

	case PPPIOCGASYNCMAP:
		if (put_user(ap->xaccm[0], (u32 *) arg))
			break;
		err = 0;
		break;
	case PPPIOCSASYNCMAP:
		if (get_user(ap->xaccm[0], (u32 *) arg))
			break;
		err = 0;
		break;

	case PPPIOCGRASYNCMAP:
		if (put_user(ap->raccm, (u32 *) arg))
			break;
		err = 0;
		break;
	case PPPIOCSRASYNCMAP:
		if (get_user(ap->raccm, (u32 *) arg))
			break;
		err = 0;
		break;

	case PPPIOCGXASYNCMAP:
		if (copy_to_user((void *) arg, ap->xaccm, sizeof(ap->xaccm)))
			break;
		err = 0;
		break;
	case PPPIOCSXASYNCMAP:
		if (copy_from_user(accm, (void *) arg, sizeof(accm)))
			break;
		accm[2] &= ~0x40000000U;	/* can't escape 0x5e */
		accm[3] |= 0x60000000U;		/* must escape 0x7d, 0x7e */
		memcpy(ap->xaccm, accm, sizeof(ap->xaccm));
		err = 0;
		break;

	case PPPIOCGMRU:
		if (put_user(ap->mru, (int *) arg))
			break;
		err = 0;
		break;
	case PPPIOCSMRU:
		if (get_user(val, (int *) arg))
			break;
		if (val < PPP_MRU)
			val = PPP_MRU;
		ap->mru = val;
		err = 0;
		break;

	case PPPIOCATTACH:
		if (get_user(val, (int *) arg))
			break;
		err = -EALREADY;
		if (ap->connected)
			break;
		ap->chan.private = ap;
		ap->chan.ops = &async_ops;
		err = ppp_register_channel(&ap->chan, val);
		if (err != 0)
			break;
		ap->connected = 1;
		ap->index = val;
		break;
	case PPPIOCDETACH:
		err = -ENXIO;
		if (!ap->connected)
			break;
		ppp_unregister_channel(&ap->chan);
		ap->connected = 0;
		err = 0;
		break;
	case PPPIOCGUNIT:
		err = -ENXIO;
		if (!ap->connected)
			break;
		if (put_user(ap->index, (int *) arg))
			break;
		err = 0;
		break;

	case TCGETS:
	case TCGETA:
		err = n_tty_ioctl(tty, file, cmd, arg);
		break;

	case TCFLSH:
		/* flush our buffers and the serial port's buffer */
		if (arg == TCIFLUSH || arg == TCIOFLUSH)
			flush_skb_queue(&ap->rq);
		if (arg == TCIOFLUSH || arg == TCOFLUSH)
			ppp_async_flush_output(ap);
		err = n_tty_ioctl(tty, file, cmd, arg);
		break;

	case FIONREAD:
		val = 0;
		if ((skb = skb_peek(&ap->rq)) != 0)
			val = skb->len;
		if (put_user(val, (int *) arg))
			break;
		err = 0;
		break;

	default:
		err = -ENOIOCTLCMD;
	}
 out:
	return err;
}

static unsigned int
ppp_async_poll(struct tty_struct *tty, struct file *file, poll_table *wait)
{
	struct asyncppp *ap = tty->disc_data;
	unsigned int mask;

	if (ap == 0)
		return 0;	/* should never happen */
	poll_wait(file, &ap->rwait, wait);
	mask = POLLOUT | POLLWRNORM;
	if (skb_peek(&ap->rq))
		mask |= POLLIN | POLLRDNORM;
	if (test_bit(TTY_OTHER_CLOSED, &tty->flags) || tty_hung_up_p(file))
		mask |= POLLHUP;
	return mask;
}

static int
ppp_async_room(struct tty_struct *tty)
{
	return 65535;
}

static void
ppp_async_receive(struct tty_struct *tty, const unsigned char *buf,
		  char *flags, int count)
{
	struct asyncppp *ap = tty->disc_data;

	if (ap == 0)
		return;
	trylock_recv_path(ap);
	ppp_async_input(ap, buf, flags, count);
	unlock_recv_path(ap);
	if (test_and_clear_bit(TTY_THROTTLED, &tty->flags)
	    && tty->driver.unthrottle)
		tty->driver.unthrottle(tty);
}

static void
ppp_async_wakeup(struct tty_struct *tty)
{
	struct asyncppp *ap = tty->disc_data;

	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	if (ap == 0)
		return;
	if (ppp_async_push(ap) && ap->connected)
		ppp_output_wakeup(&ap->chan);
}


static struct tty_ldisc ppp_ldisc = {
	magic:	TTY_LDISC_MAGIC,
	name:	"ppp",
	open:	ppp_async_open,
	close:	ppp_async_close,
	read:	ppp_async_read,
	write:	ppp_async_write,
	ioctl:	ppp_async_ioctl,
	poll:	ppp_async_poll,
	receive_room: ppp_async_room,
	receive_buf: ppp_async_receive,
	write_wakeup: ppp_async_wakeup,
};

int
ppp_async_init(void)
{
	int err;

	err = tty_register_ldisc(N_PPP, &ppp_ldisc);
	if (err != 0)
		printk(KERN_ERR "PPP_async: error %d registering line disc.\n",
		       err);
	return err;
}

/*
 * Procedures for encapsulation and framing.
 */

u16 ppp_crc16_table[256] = {
	0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
	0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
	0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
	0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
	0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
	0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
	0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
	0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
	0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
	0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
	0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
	0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
	0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
	0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
	0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
	0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
	0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
	0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
	0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
	0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
	0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
	0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
	0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
	0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
	0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
	0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
	0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
	0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
	0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
	0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
	0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
	0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};
EXPORT_SYMBOL(ppp_crc16_table);
#define fcstab	ppp_crc16_table		/* for PPP_FCS macro */

/*
 * Procedure to encode the data for async serial transmission.
 * Does octet stuffing (escaping), puts the address/control bytes
 * on if A/C compression is disabled, and does protocol compression.
 * Assumes ap->tpkt != 0 on entry.
 * Returns 1 if we finished the current frame, 0 otherwise.
 */

#define PUT_BYTE(ap, buf, c, islcp)	do {		\
	if ((islcp && c < 0x20) || (ap->xaccm[c >> 5] & (1 << (c & 0x1f)))) {\
		*buf++ = PPP_ESCAPE;			\
		*buf++ = c ^ 0x20;			\
	} else						\
		*buf++ = c;				\
} while (0)

static int
ppp_async_encode(struct asyncppp *ap)
{
	int fcs, i, count, c, proto;
	unsigned char *buf, *buflim;
	unsigned char *data;
	int islcp;

	buf = ap->obuf;
	ap->olim = buf;
	ap->optr = buf;
	i = ap->tpkt_pos;
	data = ap->tpkt->data;
	count = ap->tpkt->len;
	fcs = ap->tfcs;
	proto = (data[0] << 8) + data[1];

	/*
	 * LCP packets with code values between 1 (configure-reqest)
	 * and 7 (code-reject) must be sent as though no options
	 * had been negotiated.
	 */
	islcp = proto == PPP_LCP && 1 <= data[2] && data[2] <= 7;

	if (i == 0) {
		/*
		 * Start of a new packet - insert the leading FLAG
		 * character if necessary.
		 */
		if (islcp || flag_time == 0
		    || jiffies - ap->last_xmit >= flag_time)
			*buf++ = PPP_FLAG;
		ap->last_xmit = jiffies;
		fcs = PPP_INITFCS;

		/*
		 * Put in the address/control bytes if necessary
		 */
		if ((ap->flags & SC_COMP_AC) == 0 || islcp) {
			PUT_BYTE(ap, buf, 0xff, islcp);
			fcs = PPP_FCS(fcs, 0xff);
			PUT_BYTE(ap, buf, 0x03, islcp);
			fcs = PPP_FCS(fcs, 0x03);
		}
	}

	/*
	 * Once we put in the last byte, we need to put in the FCS
	 * and closing flag, so make sure there is at least 7 bytes
	 * of free space in the output buffer.
	 */
	buflim = ap->obuf + OBUFSIZE - 6;
	while (i < count && buf < buflim) {
		c = data[i++];
		if (i == 1 && c == 0 && (ap->flags & SC_COMP_PROT))
			continue;	/* compress protocol field */
		fcs = PPP_FCS(fcs, c);
		PUT_BYTE(ap, buf, c, islcp);
	}

	if (i < count) {
		/*
		 * Remember where we are up to in this packet.
		 */
		ap->olim = buf;
		ap->tpkt_pos = i;
		ap->tfcs = fcs;
		return 0;
	}

	/*
	 * We have finished the packet.  Add the FCS and flag.
	 */
	fcs = ~fcs;
	c = fcs & 0xff;
	PUT_BYTE(ap, buf, c, islcp);
	c = (fcs >> 8) & 0xff;
	PUT_BYTE(ap, buf, c, islcp);
	*buf++ = PPP_FLAG;
	ap->olim = buf;

	kfree_skb(ap->tpkt);
	ap->tpkt = 0;
	return 1;
}

/*
 * Transmit-side routines.
 */

/*
 * Send a packet to the peer over an async tty line.
 * Returns 1 iff the packet was accepted.
 * If the packet was not accepted, we will call ppp_output_wakeup
 * at some later time.
 */
static int
ppp_async_send(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct asyncppp *ap = chan->private;

	ppp_async_push(ap);

	if (test_and_set_bit(XMIT_FULL, &ap->busy))
		return 0;	/* already full */
	ap->tpkt = skb;
	ap->tpkt_pos = 0;

	ppp_async_push(ap);
	return 1;
}

/*
 * Push as much data as possible out to the tty.
 */
static int
ppp_async_push(struct asyncppp *ap)
{
	int avail, sent, done = 0;
	struct tty_struct *tty = ap->tty;
	int tty_stuffed = 0;

	if (!trylock_xmit_path(ap)) {
		set_bit(XMIT_WAKEUP, &ap->busy);
		return 0;
	}
	for (;;) {
		if (test_and_clear_bit(XMIT_WAKEUP, &ap->busy))
			tty_stuffed = 0;
		if (!tty_stuffed && ap->optr < ap->olim) {
			avail = ap->olim - ap->optr;
			set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
			sent = tty->driver.write(tty, 0, ap->optr, avail);
			if (sent < 0)
				goto flush;	/* error, e.g. loss of CD */
			ap->optr += sent;
			if (sent < avail)
				tty_stuffed = 1;
			continue;
		}
		if (ap->optr == ap->olim && ap->tpkt != 0) {
			if (ppp_async_encode(ap)) {
				/* finished processing ap->tpkt */
				struct sk_buff *skb = skb_dequeue(&ap->xq);
				if (skb != 0) {
					ap->tpkt = skb;
				} else {
					clear_bit(XMIT_FULL, &ap->busy);
					done = 1;
				}
			}
			continue;
		}
		/* haven't made any progress */
		unlock_xmit_path(ap);
		if (!(test_bit(XMIT_WAKEUP, &ap->busy)
		      || (!tty_stuffed && ap->tpkt != 0)))
			break;
		if (!trylock_xmit_path(ap))
			break;
	}
	return done;

flush:
	if (ap->tpkt != 0) {
		kfree_skb(ap->tpkt);
		ap->tpkt = 0;
		clear_bit(XMIT_FULL, &ap->busy);
		done = 1;
	}
	ap->optr = ap->olim;
	unlock_xmit_path(ap);
	return done;
}

/*
 * Flush output from our internal buffers.
 * Called for the TCFLSH ioctl.
 */
static void
ppp_async_flush_output(struct asyncppp *ap)
{
	int done = 0;

	flush_skb_queue(&ap->xq);
	lock_xmit_path(ap);
	ap->optr = ap->olim;
	if (ap->tpkt != NULL) {
		kfree_skb(ap->tpkt);
		ap->tpkt = 0;
		clear_bit(XMIT_FULL, &ap->busy);
		done = 1;
	}
	unlock_xmit_path(ap);
	if (done && ap->connected)
		ppp_output_wakeup(&ap->chan);
}

/*
 * Receive-side routines.
 */

/* see how many ordinary chars there are at the start of buf */
static inline int
scan_ordinary(struct asyncppp *ap, const unsigned char *buf, int count)
{
	int i, c;

	for (i = 0; i < count; ++i) {
		c = buf[i];
		if (c == PPP_ESCAPE || c == PPP_FLAG
		    || (c < 0x20 && (ap->raccm & (1 << c)) != 0))
			break;
	}
	return i;
}

/* called when a flag is seen - do end-of-packet processing */
static inline void
process_input_packet(struct asyncppp *ap)
{
	struct sk_buff *skb;
	unsigned char *p;
	unsigned int len, fcs;
	int code = 0;

	skb = ap->rpkt;
	ap->rpkt = 0;
	if ((ap->state & (SC_TOSS | SC_ESCAPE)) || skb == 0) {
		ap->state &= ~(SC_TOSS | SC_ESCAPE);
		if (skb != 0)
			kfree_skb(skb);
		return;
	}

	/* check the FCS */
	p = skb->data;
	len = skb->len;
	if (len < 3)
		goto err;	/* too short */
	fcs = PPP_INITFCS;
	for (; len > 0; --len)
		fcs = PPP_FCS(fcs, *p++);
	if (fcs != PPP_GOODFCS)
		goto err;	/* bad FCS */
	skb_trim(skb, skb->len - 2);

	/* check for address/control and protocol compression */
	p = skb->data;
	if (p[0] == PPP_ALLSTATIONS && p[1] == PPP_UI) {
		/* chop off address/control */
		if (skb->len < 3)
			goto err;
		p = skb_pull(skb, 2);
	}
	if (p[0] & 1) {
		/* protocol is compressed */
		skb_push(skb, 1)[0] = 0;
	} else if (skb->len < 2)
		goto err;

	/* all OK, give it to the generic layer or queue it */
	if (ap->connected) {
		ppp_input(&ap->chan, skb);
	} else {
		skb_queue_tail(&ap->rq, skb);
		/* drop old frames if queue too long */
		while (ap->rq.qlen > PPPASYNC_MAX_RQLEN
		       && (skb = skb_dequeue(&ap->rq)) != 0)
			kfree(skb);
		wake_up_interruptible(&ap->rwait);
	}
	return;

 err:
	kfree_skb(skb);
	if (ap->connected)
		ppp_input_error(&ap->chan, code);
}

static inline void
input_error(struct asyncppp *ap, int code)
{
	ap->state |= SC_TOSS;
	if (ap->connected)
		ppp_input_error(&ap->chan, code);
}

/* called when the tty driver has data for us. */
static void
ppp_async_input(struct asyncppp *ap, const unsigned char *buf,
		char *flags, int count)
{
	struct sk_buff *skb;
	int c, i, j, n, s, f;
	unsigned char *sp;

	/* update bits used for 8-bit cleanness detection */
	if (~ap->rbits & SC_RCV_BITS) {
		s = 0;
		for (i = 0; i < count; ++i) {
			c = buf[i];
			if (flags != 0 && flags[i] != 0)
				continue;
			s |= (c & 0x80)? SC_RCV_B7_1: SC_RCV_B7_0;
			c = ((c >> 4) ^ c) & 0xf;
			s |= (0x6996 & (1 << c))? SC_RCV_ODDP: SC_RCV_EVNP;
		}
		ap->rbits |= s;
	}

	while (count > 0) {
		/* scan through and see how many chars we can do in bulk */
		if ((ap->state & SC_ESCAPE) && buf[0] == PPP_ESCAPE)
			n = 1;
		else
			n = scan_ordinary(ap, buf, count);

		f = 0;
		if (flags != 0 && (ap->state & SC_TOSS) == 0) {
			/* check the flags to see if any char had an error */
			for (j = 0; j < n; ++j)
				if ((f = flags[j]) != 0)
					break;
		}
		if (f != 0) {
			/* start tossing */
			input_error(ap, f);

		} else if (n > 0 && (ap->state & SC_TOSS) == 0) {
			/* stuff the chars in the skb */
			skb = ap->rpkt;
			if (skb == 0) {
				skb = dev_alloc_skb(ap->mru + PPP_HDRLEN + 2);
				if (skb == 0)
					goto nomem;
				/* Try to get the payload 4-byte aligned */
				if (buf[0] != PPP_ALLSTATIONS)
					skb_reserve(skb, 2 + (buf[0] & 1));
				ap->rpkt = skb;
			}
			if (n > skb_tailroom(skb)) {
				/* packet overflowed MRU */
				input_error(ap, 1);
			} else {
				sp = skb_put(skb, n);
				memcpy(sp, buf, n);
				if (ap->state & SC_ESCAPE) {
					sp[0] ^= 0x20;
					ap->state &= ~SC_ESCAPE;
				}
			}
		}

		if (n >= count)
			break;

		c = buf[n];
		if (c == PPP_FLAG) {
			process_input_packet(ap);
		} else if (c == PPP_ESCAPE) {
			ap->state |= SC_ESCAPE;
		}
		/* otherwise it's a char in the recv ACCM */
		++n;

		buf += n;
		if (flags != 0)
			flags += n;
		count -= n;
	}
	return;

 nomem:
	printk(KERN_ERR "PPPasync: no memory (input pkt)\n");
	input_error(ap, 0);
}

#ifdef MODULE
int
init_module(void)
{
	return ppp_async_init();
}

void
cleanup_module(void)
{
	if (tty_register_ldisc(N_PPP, NULL) != 0)
		printk(KERN_ERR "failed to unregister PPP line discipline\n");
}
#endif /* MODULE */
