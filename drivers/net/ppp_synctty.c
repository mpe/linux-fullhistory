/*
 * PPP synchronous tty channel driver for Linux.
 *
 * This is a ppp channel driver that can be used with tty device drivers
 * that are frame oriented, such as synchronous HDLC devices.
 *
 * Complete PPP frames without encoding/decoding are exchanged between
 * the channel driver and the device driver.
 * 
 * The async map IOCTL codes are implemented to keep the user mode
 * applications happy if they call them. Synchronous PPP does not use
 * the async maps.
 *
 * Copyright 1999 Paul Mackerras.
 *
 * Also touched by the grubby hands of Paul Fulghum paulkf@microgate.com
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * This driver provides the encapsulation and framing for sending
 * and receiving PPP frames over sync serial lines.  It relies on
 * the generic PPP layer to give it frames to send and to process
 * received frames.  It implements the PPP line discipline.
 *
 * Part of the code in this driver was inspired by the old sync-only
 * PPP driver, written by Michael Callahan and Al Longyear, and
 * subsequently hacked by Paul Mackerras.
 *
 * ==FILEVERSION 991018==
 */

/* $Id: ppp_synctty.c,v 1.3 1999/09/02 05:30:10 paulus Exp $ */

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

/* Structure for storing local state. */
struct syncppp {
	struct tty_struct *tty;
	unsigned int	flags;
	unsigned int	rbits;
	int		mru;
	unsigned long	busy;
	u32		xaccm[8];
	u32		raccm;
	unsigned int	bytes_sent;
	unsigned int	bytes_rcvd;

	struct sk_buff	*tpkt;
	struct sk_buff_head xq;
	unsigned long	last_xmit;

	struct sk_buff	*rpkt;
	struct sk_buff_head rq;
	wait_queue_head_t rwait;

	struct ppp_channel chan;	/* interface to generic ppp layer */
	int		connected;
};

/* Bit numbers in busy */
#define XMIT_BUSY	0
#define RECV_BUSY	1
#define XMIT_WAKEUP	2
#define XMIT_FULL	3

/* Bits in rbits */
#define SC_RCV_BITS	(SC_RCV_B7_1|SC_RCV_B7_0|SC_RCV_ODDP|SC_RCV_EVNP)

#define PPPSYNC_MAX_RQLEN	32	/* arbitrary */

/*
 * Prototypes.
 */
static struct sk_buff* ppp_sync_txdequeue(struct syncppp *ap);
static int ppp_sync_send(struct ppp_channel *chan, struct sk_buff *skb);
static int ppp_sync_push(struct syncppp *ap);
static void ppp_sync_flush_output(struct syncppp *ap);
static void ppp_sync_input(struct syncppp *ap, const unsigned char *buf,
			    char *flags, int count);

struct ppp_channel_ops sync_ops = {
	ppp_sync_send
};

/*
 * Utility procedures to print a buffer in hex/ascii
 */
static void
ppp_print_hex (register __u8 * out, const __u8 * in, int count)
{
	register __u8 next_ch;
	static char hex[] = "0123456789ABCDEF";

	while (count-- > 0) {
		next_ch = *in++;
		*out++ = hex[(next_ch >> 4) & 0x0F];
		*out++ = hex[next_ch & 0x0F];
		++out;
	}
}

static void
ppp_print_char (register __u8 * out, const __u8 * in, int count)
{
	register __u8 next_ch;

	while (count-- > 0) {
		next_ch = *in++;

		if (next_ch < 0x20 || next_ch > 0x7e)
			*out++ = '.';
		else {
			*out++ = next_ch;
			if (next_ch == '%')   /* printk/syslogd has a bug !! */
				*out++ = '%';
		}
	}
	*out = '\0';
}

static void
ppp_print_buffer (const char *name, const __u8 *buf, int count)
{
	__u8 line[44];

	if (name != NULL)
		printk(KERN_DEBUG "ppp_synctty: %s, count = %d\n", name, count);

	while (count > 8) {
		memset (line, 32, 44);
		ppp_print_hex (line, buf, 8);
		ppp_print_char (&line[8 * 3], buf, 8);
		printk(KERN_DEBUG "%s\n", line);
		count -= 8;
		buf += 8;
	}

	if (count > 0) {
		memset (line, 32, 44);
		ppp_print_hex (line, buf, count);
		ppp_print_char (&line[8 * 3], buf, count);
		printk(KERN_DEBUG "%s\n", line);
	}
}

/*
 * Routines for locking and unlocking the transmit and receive paths.
 */
static inline void
lock_path(struct syncppp *ap, int bit)
{
	do {
		while (test_bit(bit, &ap->busy))
		       mb();
	} while (test_and_set_bit(bit, &ap->busy));
	mb();
}

static inline int
trylock_path(struct syncppp *ap, int bit)
{
	if (test_and_set_bit(bit, &ap->busy))
		return 0;
	mb();
	return 1;
}

static inline void
unlock_path(struct syncppp *ap, int bit)
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
 * Routines implementing the synchronous PPP line discipline.
 */

/*
 * Called when a tty is put into line discipline.
 */
static int
ppp_sync_open(struct tty_struct *tty)
{
	struct syncppp *ap;

	ap = kmalloc(sizeof(*ap), GFP_KERNEL);
	if (ap == 0)
		return -ENOMEM;

	MOD_INC_USE_COUNT;

	/* initialize the syncppp structure */
	memset(ap, 0, sizeof(*ap));
	ap->tty = tty;
	ap->mru = PPP_MRU;
	ap->xaccm[0] = ~0U;
	ap->xaccm[3] = 0x60000000U;
	ap->raccm = ~0U;
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
ppp_sync_close(struct tty_struct *tty)
{
	struct syncppp *ap = tty->disc_data;

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
ppp_sync_read(struct tty_struct *tty, struct file *file,
	       unsigned char *buf, size_t count)
{
	struct syncppp *ap = tty->disc_data;
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
ppp_sync_write(struct tty_struct *tty, struct file *file,
		const unsigned char *buf, size_t count)
{
	struct syncppp *ap = tty->disc_data;
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
	ppp_sync_push(ap);

	ret = count;

 out:
	return ret;
}

static int
ppp_sync_ioctl(struct tty_struct *tty, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct syncppp *ap = tty->disc_data;
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
		ap->chan.ops = &sync_ops;
		err = ppp_register_channel(&ap->chan, val);
		if (err != 0)
			break;
		ap->connected = 1;
		break;
	case PPPIOCDETACH:
		err = -ENXIO;
		if (!ap->connected)
			break;
		ppp_unregister_channel(&ap->chan);
		ap->connected = 0;
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
			ppp_sync_flush_output(ap);
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
ppp_sync_poll(struct tty_struct *tty, struct file *file, poll_table *wait)
{
	struct syncppp *ap = tty->disc_data;
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
ppp_sync_room(struct tty_struct *tty)
{
	return 65535;
}

static void
ppp_sync_receive(struct tty_struct *tty, const unsigned char *buf,
		  char *flags, int count)
{
	struct syncppp *ap = tty->disc_data;

	if (ap == 0)
		return;
	trylock_recv_path(ap);
	ppp_sync_input(ap, buf, flags, count);
	unlock_recv_path(ap);
	if (test_and_clear_bit(TTY_THROTTLED, &tty->flags)
	    && tty->driver.unthrottle)
		tty->driver.unthrottle(tty);
}

static void
ppp_sync_wakeup(struct tty_struct *tty)
{
	struct syncppp *ap = tty->disc_data;

	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	if (ap == 0)
		return;
	if (ppp_sync_push(ap) && ap->connected)
		ppp_output_wakeup(&ap->chan);
}


static struct tty_ldisc ppp_sync_ldisc = {
	magic:	TTY_LDISC_MAGIC,
	name:	"pppsync",
	open:	ppp_sync_open,
	close:	ppp_sync_close,
	read:	ppp_sync_read,
	write:	ppp_sync_write,
	ioctl:	ppp_sync_ioctl,
	poll:	ppp_sync_poll,
	receive_room: ppp_sync_room,
	receive_buf: ppp_sync_receive,
	write_wakeup: ppp_sync_wakeup,
};

int
ppp_sync_init(void)
{
	int err;

	err = tty_register_ldisc(N_SYNC_PPP, &ppp_sync_ldisc);
	if (err != 0)
		printk(KERN_ERR "PPP_sync: error %d registering line disc.\n",
		       err);
	return err;
}

/*
 * Procedures for encapsulation and framing.
 */

struct sk_buff*
ppp_sync_txdequeue(struct syncppp *ap)
{
	int proto;
	unsigned char *data;
	int islcp;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&ap->xq)) != NULL) {

		data  = skb->data;
		proto = (data[0] << 8) + data[1];

		/* LCP packets with codes between 1 (configure-request)
		 * and 7 (code-reject) must be sent as though no options
		 * have been negotiated.
		 */
		islcp = proto == PPP_LCP && 1 <= data[2] && data[2] <= 7;

		/* compress protocol field if option enabled */
                if (data[0] == 0 && (ap->flags & SC_COMP_PROT) && !islcp)
			skb_pull(skb,1);

		/* prepend address/control fields if necessary */
		if ((ap->flags & SC_COMP_AC) == 0 || islcp) {
			if (skb_headroom(skb) < 2) {
				struct sk_buff *npkt = dev_alloc_skb(skb->len + 2);
				if (npkt == NULL) {
					kfree_skb(skb);
					continue;
				}
				skb_reserve(npkt,2);
				memcpy(skb_put(npkt,skb->len), skb->data, skb->len);
				kfree_skb(skb);
				skb = npkt;
			}
			skb_push(skb,2);
			skb->data[0] = PPP_ALLSTATIONS;
			skb->data[1] = PPP_UI;
		}

		ap->last_xmit = jiffies;
		break;
	}

	if (skb && ap->flags & SC_LOG_OUTPKT)
		ppp_print_buffer ("send buffer", skb->data, skb->len);

	return skb;
}

/*
 * Transmit-side routines.
 */

/*
 * Send a packet to the peer over an sync tty line.
 * Returns 1 iff the packet was accepted.
 * If the packet was not accepted, we will call ppp_output_wakeup
 * at some later time.
 */
static int
ppp_sync_send(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct syncppp *ap = chan->private;

	ppp_sync_push(ap);

	if (test_and_set_bit(XMIT_FULL, &ap->busy))
		return 0;	/* already full */
	skb_queue_head(&ap->xq,skb);

	ppp_sync_push(ap);
	return 1;
}

/*
 * Push as much data as possible out to the tty.
 */
static int
ppp_sync_push(struct syncppp *ap)
{
	int sent, done = 0;
	struct tty_struct *tty = ap->tty;
	int tty_stuffed = 0;

	if (!trylock_xmit_path(ap)) {
		set_bit(XMIT_WAKEUP, &ap->busy);
		return 0;
	}
	for (;;) {
		if (test_and_clear_bit(XMIT_WAKEUP, &ap->busy))
			tty_stuffed = 0;
		if (ap->tpkt == 0) {
			if ((ap->tpkt = ppp_sync_txdequeue(ap)) == 0) {
				clear_bit(XMIT_FULL, &ap->busy);
			        done = 1;
			}
		}
		if (!tty_stuffed && ap->tpkt != NULL) {
			set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
			sent = tty->driver.write(tty, 0, ap->tpkt->data, ap->tpkt->len);
			if (sent < 0)
				goto flush;	/* error, e.g. loss of CD */
			if (sent < ap->tpkt->len) {
				tty_stuffed = 1;
			} else {
				kfree_skb(ap->tpkt);
				ap->tpkt = 0;
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
	unlock_xmit_path(ap);
	return done;
}

/*
 * Flush output from our internal buffers.
 * Called for the TCFLSH ioctl.
 */
static void
ppp_sync_flush_output(struct syncppp *ap)
{
	int done = 0;

	flush_skb_queue(&ap->xq);
	lock_xmit_path(ap);
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

static inline void
process_input_packet(struct syncppp *ap)
{
	struct sk_buff *skb;
	unsigned char *p;
	int code = 0;

	skb = ap->rpkt;
	ap->rpkt = 0;

	/* strip address/control field if present */
	p = skb->data;
	if (p[0] == PPP_ALLSTATIONS && p[1] == PPP_UI) {
		/* chop off address/control */
		if (skb->len < 3)
			goto err;
		p = skb_pull(skb, 2);
	}

	/* decompress protocol field if compressed */
	if (p[0] & 1) {
		/* protocol is compressed */
		skb_push(skb, 1)[0] = 0;
	} else if (skb->len < 2)
		goto err;

	/* pass to generic layer or queue it */
	if (ap->connected) {
		ppp_input(&ap->chan, skb);
	} else {
		skb_queue_tail(&ap->rq, skb);
		/* drop old frames if queue too long */
		while (ap->rq.qlen > PPPSYNC_MAX_RQLEN
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
input_error(struct syncppp *ap, int code)
{
	if (ap->connected)
		ppp_input_error(&ap->chan, code);
}

/* called when the tty driver has data for us. 
 *
 * Data is frame oriented: each call to ppp_sync_input is considered
 * a whole frame. If the 1st flag byte is non-zero then the whole
 * frame is considered to be in error and is tossed.
 */
static void
ppp_sync_input(struct syncppp *ap, const unsigned char *buf,
		char *flags, int count)
{
	struct sk_buff *skb;
	unsigned char *sp;

	if (count == 0)
		return;

	/* if flag set, then error, ignore frame */
	if (flags != 0 && *flags) {
		input_error(ap, *flags);
		return;
	}

	if (ap->flags & SC_LOG_INPKT)
		ppp_print_buffer ("receive buffer", buf, count);

	/* stuff the chars in the skb */
	if ((skb = ap->rpkt) == 0) {
		if ((skb = dev_alloc_skb(ap->mru + PPP_HDRLEN + 2)) == 0) {
			printk(KERN_ERR "PPPsync: no memory (input pkt)\n");
			input_error(ap, 0);
			return;
		}
		/* Try to get the payload 4-byte aligned */
		if (buf[0] != PPP_ALLSTATIONS)
			skb_reserve(skb, 2 + (buf[0] & 1));
		ap->rpkt = skb;
	}
	if (count > skb_tailroom(skb)) {
		/* packet overflowed MRU */
		input_error(ap, 1);
	} else {
		sp = skb_put(skb, count);
		memcpy(sp, buf, count);
		process_input_packet(ap);
	}
}

#ifdef MODULE
int
init_module(void)
{
	return ppp_sync_init();
}

void
cleanup_module(void)
{
	if (tty_register_ldisc(N_SYNC_PPP, NULL) != 0)
		printk(KERN_ERR "failed to unregister Sync PPP line discipline\n");
}
#endif /* MODULE */
