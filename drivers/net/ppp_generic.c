/*
 * Generic PPP layer for Linux.
 *
 * Copyright 1999 Paul Mackerras.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * The generic PPP layer handles the PPP network interfaces, the
 * /dev/ppp device, packet and VJ compression, and multilink.
 * It talks to PPP `channels' via the interface defined in
 * include/linux/ppp_channel.h.  Channels provide the basic means for
 * sending and receiving PPP frames on some kind of communications
 * channel.
 *
 * Part of the code in this driver was inspired by the old async-only
 * PPP driver, written by Michael Callahan and Al Longyear, and
 * subsequently hacked by Paul Mackerras.
 *
 * ==FILEVERSION 990806==
 */

/* $Id$ */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/poll.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/ppp_channel.h>
#include <linux/ppp-comp.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/slhc_vj.h>
#include <asm/spinlock.h>

#define PPP_VERSION	"2.4.0"

EXPORT_SYMBOL(ppp_register_channel);
EXPORT_SYMBOL(ppp_unregister_channel);
EXPORT_SYMBOL(ppp_input);
EXPORT_SYMBOL(ppp_input_error);
EXPORT_SYMBOL(ppp_output_wakeup);
EXPORT_SYMBOL(ppp_register_compressor);
EXPORT_SYMBOL(ppp_unregister_compressor);

/*
 * Network protocols we support.
 */
#define NP_IP	0		/* Internet Protocol V4 */
#define NP_IPV6	1		/* Internet Protocol V6 */
#define NP_IPX	2		/* IPX protocol */
#define NP_AT	3		/* Appletalk protocol */
#define NUM_NP	4		/* Number of NPs. */

/*
 * Data structure describing one ppp unit.
 * A ppp unit corresponds to a ppp network interface device
 * and represents a multilink bundle.
 * It may have 0 or more ppp channels connected to it.
 */
struct ppp {
	struct list_head list;		/* link in list of ppp units */
	int		index;		/* interface unit number */
	char		name[16];	/* unit name */
	int		refcnt;		/* # open /dev/ppp attached */
	unsigned long	busy;		/* lock and other bits */
	struct list_head channels;	/* list of attached channels */
	int		n_channels;	/* how many channels are attached */
	int		mru;		/* max receive unit */
	unsigned int	flags;		/* control bits */
	unsigned int	xstate;		/* transmit state bits */
	unsigned int	rstate;		/* receive state bits */
	int		debug;		/* debug flags */
	struct slcompress *vj;		/* state for VJ header compression */
	struct sk_buff_head xq;		/* pppd transmit queue */
	struct sk_buff_head rq;		/* receive queue for pppd */
	wait_queue_head_t rwait;	/* for poll on reading /dev/ppp */
	enum NPmode	npmode[NUM_NP];	/* what to do with each net proto */
	struct sk_buff	*xmit_pending;	/* a packet ready to go out */
	struct sk_buff_head recv_pending;/* pending input packets */
	struct compressor *xcomp;	/* transmit packet compressor */
	void		*xc_state;	/* its internal state */
	struct compressor *rcomp;	/* receive decompressor */
	void		*rc_state;	/* its internal state */
	unsigned long	last_xmit;	/* jiffies when last pkt sent */
	unsigned long	last_recv;	/* jiffies when last pkt rcvd */
	struct device	dev;		/* network interface device */
	struct net_device_stats stats;	/* statistics */
};

static LIST_HEAD(all_ppp_units);
static spinlock_t all_ppp_lock = SPIN_LOCK_UNLOCKED;

/*
 * Private data structure for each channel.
 * Ultimately this will have multilink stuff etc. in it.
 */
struct channel {
	struct list_head list;		/* link in list of channels per unit */
	struct ppp_channel *chan;	/* public channel data structure */
	int		blocked;	/* if channel refused last packet */
	struct ppp	*ppp;		/* ppp unit we're connected to */
};

/* Bit numbers in busy */
#define XMIT_BUSY	0
#define RECV_BUSY	1
#define XMIT_WAKEUP	2

/*
 * Bits in flags: SC_NO_TCP_CCID, SC_CCP_OPEN, SC_CCP_UP, SC_LOOP_TRAFFIC.
 * Bits in rstate: SC_DECOMP_RUN, SC_DC_ERROR, SC_DC_FERROR.
 * Bits in xstate: SC_COMP_RUN
 */
#define SC_FLAG_BITS	(SC_NO_TCP_CCID|SC_CCP_OPEN|SC_CCP_UP|SC_LOOP_TRAFFIC)

/* Get the PPP protocol number from a skb */
#define PPP_PROTO(skb)	(((skb)->data[0] << 8) + (skb)->data[1])

/* We limit the length of ppp->rq to this (arbitrary) value */
#define PPP_MAX_RQLEN	32

/* Prototypes. */
static void ppp_xmit_unlock(struct ppp *ppp);
static void ppp_send_frame(struct ppp *ppp, struct sk_buff *skb);
static void ppp_push(struct ppp *ppp);
static void ppp_recv_unlock(struct ppp *ppp);
static void ppp_receive_frame(struct ppp *ppp, struct sk_buff *skb);
static struct sk_buff *ppp_decompress_frame(struct ppp *ppp,
					    struct sk_buff *skb);
static int ppp_set_compress(struct ppp *ppp, unsigned long arg);
static void ppp_ccp_peek(struct ppp *ppp, struct sk_buff *skb, int inbound);
static void ppp_ccp_closed(struct ppp *ppp);
static struct compressor *find_compressor(int type);
static void ppp_get_stats(struct ppp *ppp, struct ppp_stats *st);
static struct ppp *ppp_create_unit(int unit, int *retp);
static struct ppp *ppp_find_unit(int unit);

/* Translates a PPP protocol number to a NP index (NP == network protocol) */
static inline int proto_to_npindex(int proto)
{
	switch (proto) {
	case PPP_IP:
		return NP_IP;
	case PPP_IPV6:
		return NP_IPV6;
	case PPP_IPX:
		return NP_IPX;
	case PPP_AT:
		return NP_AT;
	}
	return -EINVAL;
}

/* Translates an NP index into a PPP protocol number */
static const int npindex_to_proto[NUM_NP] = {
	PPP_IP,
	PPP_IPV6,
	PPP_IPX,
	PPP_AT,
};
	
/* Translates an ethertype into an NP index */
static inline int ethertype_to_npindex(int ethertype)
{
	switch (ethertype) {
	case ETH_P_IP:
		return NP_IP;
	case ETH_P_IPV6:
		return NP_IPV6;
	case ETH_P_IPX:
		return NP_IPX;
	case ETH_P_PPPTALK:
	case ETH_P_ATALK:
		return NP_AT;
	}
	return -1;
}

/* Translates an NP index into an ethertype */
static const int npindex_to_ethertype[NUM_NP] = {
	ETH_P_IP,
	ETH_P_IPV6,
	ETH_P_IPX,
	ETH_P_PPPTALK,
};

/*
 * Routines for locking and unlocking the transmit and receive paths
 * of each unit.
 */
static inline void
lock_path(struct ppp *ppp, int bit)
{
	int timeout = 1000000;

	do {
		while (test_bit(bit, &ppp->busy)) {
		       mb();
		       if (--timeout == 0) {
			       printk(KERN_ERR "lock_path timeout ppp=%p bit=%x\n", ppp, bit);
			       return;
		       }
		}
	} while (test_and_set_bit(bit, &ppp->busy));
	mb();
}

static inline int
trylock_path(struct ppp *ppp, int bit)
{
	if (test_and_set_bit(bit, &ppp->busy))
		return 0;
	mb();
	return 1;
}

static inline void
unlock_path(struct ppp *ppp, int bit)
{
	mb();
	clear_bit(bit, &ppp->busy);
}

#define lock_xmit_path(ppp)	lock_path(ppp, XMIT_BUSY)
#define trylock_xmit_path(ppp)	trylock_path(ppp, XMIT_BUSY)
#define unlock_xmit_path(ppp)	unlock_path(ppp, XMIT_BUSY)
#define lock_recv_path(ppp)	lock_path(ppp, RECV_BUSY)
#define trylock_recv_path(ppp)	trylock_path(ppp, RECV_BUSY)
#define unlock_recv_path(ppp)	unlock_path(ppp, RECV_BUSY)

static inline void
free_skbs(struct sk_buff_head *head)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(head)) != 0)
		kfree_skb(skb);
}

/*
 * /dev/ppp device routines.
 * The /dev/ppp device is used by pppd to control the ppp unit.
 * It supports the read, write, ioctl and poll functions.
 */
static int ppp_open(struct inode *inode, struct file *file)
{
	/*
	 * This could (should?) be enforced by the permissions on /dev/ppp.
	 */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	MOD_INC_USE_COUNT;
	return 0;
}

static int ppp_release(struct inode *inode, struct file *file)
{
	struct ppp *ppp = (struct ppp *) file->private_data;
	struct list_head *list, *next;
	int ref;

	if (ppp == 0)
		goto out;
	file->private_data = 0;
	spin_lock(&all_ppp_lock);
	ref = --ppp->refcnt;
	if (ref == 0)
		list_del(&ppp->list);
	spin_unlock(&all_ppp_lock);
	if (ref != 0)
		goto out;

	/* Last fd open to this ppp unit is being closed -
	   mark the interface down, free the ppp unit */
	rtnl_lock();
	dev_close(&ppp->dev);
	rtnl_unlock();
	for (list = ppp->channels.next; list != &ppp->channels; list = next) {
		/* forcibly detach this channel */
		struct channel *chan;
		chan = list_entry(list, struct channel, list);
		chan->chan->ppp = 0;
		next = list->next;
		kfree(chan);
	}

	/* Free up resources. */
	ppp_ccp_closed(ppp);
	lock_xmit_path(ppp);
	lock_recv_path(ppp);
	if (ppp->vj) {
		slhc_free(ppp->vj);
		ppp->vj = 0;
	}
	free_skbs(&ppp->xq);
	free_skbs(&ppp->rq);
	free_skbs(&ppp->recv_pending);
	unregister_netdev(&ppp->dev);
	kfree(ppp);

 out:
	MOD_DEC_USE_COUNT;
	return 0;
}

static ssize_t ppp_read(struct file *file, char *buf,
			size_t count, loff_t *ppos)
{
	struct ppp *ppp = (struct ppp *) file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	struct sk_buff *skb = 0;

	ret = -ENXIO;
	if (ppp == 0)
		goto out;		/* not currently attached */

	add_wait_queue(&ppp->rwait, &wait);
	current->state = TASK_INTERRUPTIBLE;
	for (;;) {
		ret = -EAGAIN;
		skb = skb_dequeue(&ppp->rq);
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
	remove_wait_queue(&ppp->rwait, &wait);

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

static ssize_t ppp_write(struct file *file, const char *buf,
			 size_t count, loff_t *ppos)
{
	struct ppp *ppp = (struct ppp *) file->private_data;
	struct sk_buff *skb;
	ssize_t ret;

	ret = -ENXIO;
	if (ppp == 0)
		goto out;

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

	skb_queue_tail(&ppp->xq, skb);
	if (trylock_xmit_path(ppp))
		ppp_xmit_unlock(ppp);

	ret = count;

 out:
	return ret;
}

static unsigned int ppp_poll(struct file *file, poll_table *wait)
{
	struct ppp *ppp = (struct ppp *) file->private_data;
	unsigned int mask;

	if (ppp == 0)
		return 0;
	poll_wait(file, &ppp->rwait, wait);
	mask = POLLOUT | POLLWRNORM;
	if (skb_peek(&ppp->rq) != 0)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static int ppp_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct ppp *ppp = (struct ppp *) file->private_data;
	int err, val, val2, i;
	struct ppp_idle idle;
	struct npioctl npi;

	if (cmd == PPPIOCNEWUNIT) {
		/* Create a new ppp unit */
		int unit, ret;

		if (ppp != 0)
			return -EINVAL;
		if (get_user(unit, (int *) arg))
			return -EFAULT;
		ppp = ppp_create_unit(unit, &ret);
		if (ppp == 0)
			return ret;
		file->private_data = ppp;
		if (put_user(ppp->index, (int *) arg))
			return -EFAULT;
		return 0;
	}
	if (cmd == PPPIOCATTACH) {
		/* Attach to an existing ppp unit */
		int unit;

		if (ppp != 0)
			return -EINVAL;
		if (get_user(unit, (int *) arg))
			return -EFAULT;
		spin_lock(&all_ppp_lock);
		ppp = ppp_find_unit(unit);
		if (ppp != 0)
			++ppp->refcnt;
		spin_unlock(&all_ppp_lock);
		if (ppp == 0)
			return -ENXIO;
		file->private_data = ppp;
		return 0;
	}

	if (ppp == 0)
		return -ENXIO;
	err = -EFAULT;
	switch (cmd) {
	case PPPIOCSMRU:
		if (get_user(val, (int *) arg))
			break;
		ppp->mru = val;
		err = 0;
		break;

	case PPPIOCSFLAGS:
		if (get_user(val, (int *) arg))
			break;
		if (ppp->flags & ~val & SC_CCP_OPEN)
			ppp_ccp_closed(ppp);
		ppp->flags = val & SC_FLAG_BITS;
		err = 0;
		break;

	case PPPIOCGFLAGS:
		val = ppp->flags | ppp->xstate | ppp->rstate;
		if (put_user(val, (int *) arg))
			break;
		err = 0;
		break;

	case PPPIOCSCOMPRESS:
		err = ppp_set_compress(ppp, arg);
		break;

	case PPPIOCGUNIT:
		if (put_user(ppp->index, (int *) arg))
			break;
		err = 0;
		break;

	case PPPIOCSDEBUG:
		if (get_user(val, (int *) arg))
			break;
		ppp->debug = val;
		err = 0;
		break;

	case PPPIOCGDEBUG:
		if (put_user(ppp->debug, (int *) arg))
			break;
		err = 0;
		break;

	case PPPIOCGIDLE:
		idle.xmit_idle = (jiffies - ppp->last_xmit) / HZ;
		idle.recv_idle = (jiffies - ppp->last_recv) / HZ;
		if (copy_to_user((void *) arg, &idle, sizeof(idle)))
			break;
		err = 0;
		break;

	case PPPIOCSMAXCID:
		if (get_user(val, (int *) arg))
			break;
		val2 = 15;
		if ((val >> 16) != 0) {
			val2 = val >> 16;
			val &= 0xffff;
		}
		lock_xmit_path(ppp);
		lock_recv_path(ppp);
		if (ppp->vj != 0)
			slhc_free(ppp->vj);
		ppp->vj = slhc_init(val2+1, val+1);
		ppp_recv_unlock(ppp);
		ppp_xmit_unlock(ppp);
		err = -ENOMEM;
		if (ppp->vj == 0) {
			printk(KERN_ERR "PPP: no memory (VJ compressor)\n");
			break;
		}
		err = 0;
		break;

	case PPPIOCGNPMODE:
	case PPPIOCSNPMODE:
		if (copy_from_user(&npi, (void *) arg, sizeof(npi)))
			break;
		err = proto_to_npindex(npi.protocol);
		if (err < 0)
			break;
		i = err;
		if (cmd == PPPIOCGNPMODE) {
			err = -EFAULT;
			npi.mode = ppp->npmode[i];
			if (copy_to_user((void *) arg, &npi, sizeof(npi)))
				break;
		} else {
			ppp->npmode[i] = npi.mode;
			/* we may be able to transmit more packets now (??) */
			mark_bh(NET_BH);
		}
		err = 0;
		break;

	default:
		err = -ENOTTY;
	}
	return err;
}

static struct file_operations ppp_device_fops = {
	NULL,		/* seek */
	ppp_read,
	ppp_write,
	NULL,		/* readdir */
	ppp_poll,
	ppp_ioctl,
	NULL,		/* mmap */
	ppp_open,
	NULL,		/* flush */
	ppp_release
};

#define PPP_MAJOR	108

/* Called at boot time if ppp is compiled into the kernel,
   or at module load time (from init_module) if compiled as a module. */
int
ppp_init(struct device *dev)
{
	int err;
#ifndef MODULE
	extern struct compressor ppp_deflate, ppp_deflate_draft;
	extern int ppp_async_init(void);
#endif

	printk(KERN_INFO "PPP generic driver version " PPP_VERSION "\n");
	err = register_chrdev(PPP_MAJOR, "ppp", &ppp_device_fops);
	if (err)
		printk(KERN_ERR "failed to register PPP device (%d)\n", err);
#ifndef MODULE
#ifdef CONFIG_PPP_ASYNC
	ppp_async_init();
#endif
#ifdef CONFIG_PPP_DEFLATE
	if (ppp_register_compressor(&ppp_deflate) == 0)
		printk(KERN_INFO "PPP Deflate compression module registered\n");
	ppp_register_compressor(&ppp_deflate_draft);
#endif
#endif /* MODULE */

	return -ENODEV;
}

/*
 * Network interface unit routines.
 */
static int
ppp_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct ppp *ppp = (struct ppp *) dev->priv;
	int npi, proto;
	unsigned char *pp;

	if (skb == 0)
		return 0;
	/* can skb->data ever be 0? */

	npi = ethertype_to_npindex(ntohs(skb->protocol));
	if (npi < 0)
		goto outf;

	/* Drop, accept or reject the packet */
	switch (ppp->npmode[npi]) {
	case NPMODE_PASS:
		break;
	case NPMODE_QUEUE:
		/* it would be nice to have a way to tell the network
		   system to queue this one up for later. */
		goto outf;
	case NPMODE_DROP:
	case NPMODE_ERROR:
		goto outf;
	}

	/* The transmit side of the ppp interface is serialized by
	   the XMIT_BUSY bit in ppp->busy. */
	if (!trylock_xmit_path(ppp)) {
		dev->tbusy = 1;
		return 1;
	}
	if (ppp->xmit_pending)
		ppp_push(ppp);
	if (ppp->xmit_pending) {
		dev->tbusy = 1;
		ppp_xmit_unlock(ppp);
		return 1;
	}
	dev->tbusy = 0;

	/* Put the 2-byte PPP protocol number on the front,
	   making sure there is room for the address and control fields. */
	if (skb_headroom(skb) < PPP_HDRLEN) {
		struct sk_buff *ns;

		ns = alloc_skb(skb->len + PPP_HDRLEN, GFP_ATOMIC);
		if (ns == 0)
			goto outnbusy;
		skb_reserve(ns, PPP_HDRLEN);
		memcpy(skb_put(ns, skb->len), skb->data, skb->len);
		kfree_skb(skb);
		skb = ns;
	}
	pp = skb_push(skb, 2);
	proto = npindex_to_proto[npi];
	pp[0] = proto >> 8;
	pp[1] = proto;

	ppp_send_frame(ppp, skb);
	ppp_xmit_unlock(ppp);
	return 0;

 outnbusy:
	ppp_xmit_unlock(ppp);

 outf:
	kfree_skb(skb);
	return 0;
}

static struct net_device_stats *
ppp_net_stats(struct device *dev)
{
	struct ppp *ppp = (struct ppp *) dev->priv;

	return &ppp->stats;
}

static int
ppp_net_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
	struct ppp *ppp = dev->priv;
	int err = -EFAULT;
	void *addr = (void *) ifr->ifr_ifru.ifru_data;
	struct ppp_stats stats;
	struct ppp_comp_stats cstats;
	char *vers;

	switch (cmd) {
	case SIOCGPPPSTATS:
		ppp_get_stats(ppp, &stats);
		if (copy_to_user(addr, &stats, sizeof(stats)))
			break;
		err = 0;
		break;

	case SIOCGPPPCSTATS:
		memset(&cstats, 0, sizeof(cstats));
		if (ppp->xc_state != 0)
			ppp->xcomp->comp_stat(ppp->xc_state, &cstats.c);
		if (ppp->rc_state != 0)
			ppp->rcomp->decomp_stat(ppp->rc_state, &cstats.d);
		if (copy_to_user(addr, &cstats, sizeof(cstats)))
			break;
		err = 0;
		break;

	case SIOCGPPPVER:
		vers = PPP_VERSION;
		if (copy_to_user(addr, vers, strlen(vers) + 1))
			break;
		err = 0;
		break;

	default:
		err = -EINVAL;
	}

	return err;
}

int
ppp_net_init(struct device *dev)
{
	dev->hard_header_len = PPP_HDRLEN;
	dev->mtu = PPP_MTU;
	dev->hard_start_xmit = ppp_start_xmit;
	dev->get_stats = ppp_net_stats;
	dev->do_ioctl = ppp_net_ioctl;
	dev->addr_len = 0;
	dev->tx_queue_len = 3;
	dev->type = ARPHRD_PPP;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;

	dev_init_buffers(dev);
	return 0;
}

/*
 * Transmit-side routines.
 */

/*
 * Called to unlock the transmit side of the ppp unit,
 * making sure that any work queued up gets done.
 */
static void
ppp_xmit_unlock(struct ppp *ppp)
{
	struct sk_buff *skb;

	for (;;) {
		if (test_and_clear_bit(XMIT_WAKEUP, &ppp->busy))
			ppp_push(ppp);
		while (ppp->xmit_pending == 0
		       && (skb = skb_dequeue(&ppp->xq)) != 0)
			ppp_send_frame(ppp, skb);
		unlock_xmit_path(ppp);
		if (!(test_bit(XMIT_WAKEUP, &ppp->busy)
		      || (ppp->xmit_pending == 0 && skb_peek(&ppp->xq))))
			break;
		if (!trylock_xmit_path(ppp))
			break;
	}
}

/*
 * Compress and send a frame.
 * The caller should have locked the xmit path,
 * and xmit_pending should be 0.
 */
static void
ppp_send_frame(struct ppp *ppp, struct sk_buff *skb)
{
	int proto = PPP_PROTO(skb);
	struct sk_buff *new_skb;
	int len;
	unsigned char *cp;

	++ppp->stats.tx_packets;
	ppp->stats.tx_bytes += skb->len - 2;

	switch (proto) {
	case PPP_IP:
		if (ppp->vj == 0 || (ppp->flags & SC_COMP_TCP) == 0)
			break;
		/* try to do VJ TCP header compression */
		new_skb = alloc_skb(skb->len + 2, GFP_ATOMIC);
		if (new_skb == 0) {
			printk(KERN_ERR "PPP: no memory (VJ comp pkt)\n");
			goto drop;
		}
		skb_reserve(new_skb, 2);
		cp = skb->data + 2;
		len = slhc_compress(ppp->vj, cp, skb->len - 2,
				    new_skb->data + 2, &cp,
				    !(ppp->flags & SC_NO_TCP_CCID));
		if (cp == skb->data + 2) {
			/* didn't compress */
			kfree_skb(new_skb);
		} else {
			if (cp[0] & SL_TYPE_COMPRESSED_TCP) {
				proto = PPP_VJC_COMP;
				cp[0] &= ~SL_TYPE_COMPRESSED_TCP;
			} else {
				proto = PPP_VJC_UNCOMP;
				cp[0] = skb->data[2];
			}
			kfree_skb(skb);
			skb = new_skb;
			cp = skb_put(skb, len + 2);
			cp[0] = 0;
			cp[1] = proto;
		}
		break;

	case PPP_CCP:
		/* peek at outbound CCP frames */
		ppp_ccp_peek(ppp, skb, 0);
		break;
	}

	/* try to do packet compression */
	if ((ppp->xstate & SC_COMP_RUN) && ppp->xc_state != 0
	    && proto != PPP_LCP && proto != PPP_CCP) {
		new_skb = alloc_skb(ppp->dev.mtu + PPP_HDRLEN, GFP_ATOMIC);
		if (new_skb == 0) {
			printk(KERN_ERR "PPP: no memory (comp pkt)\n");
			goto drop;
		}

		/* compressor still expects A/C bytes in hdr */
		len = ppp->xcomp->compress(ppp->xc_state, skb->data - 2,
					   new_skb->data, skb->len + 2,
					   ppp->dev.mtu + PPP_HDRLEN);
		if (len > 0 && (ppp->flags & SC_CCP_UP)) {
			kfree_skb(skb);
			skb = new_skb;
			skb_put(skb, len);
			skb_pull(skb, 2);	/* pull off A/C bytes */
		} else {
			/* didn't compress, or CCP not up yet */
			kfree_skb(new_skb);
		}
	}

	/*
	 * If we are waiting for traffic (demand dialling),
	 * queue it up for pppd to receive.
	 */
	if (ppp->flags & SC_LOOP_TRAFFIC) {
		if (ppp->rq.qlen > PPP_MAX_RQLEN)
			goto drop;
		skb_queue_tail(&ppp->rq, skb);
		wake_up_interruptible(&ppp->rwait);
		return;
	}

	ppp->xmit_pending = skb;
	ppp_push(ppp);
	return;

 drop:
	kfree_skb(skb);
	++ppp->stats.tx_errors;
}

/*
 * Try to send the frame in xmit_pending.
 * The caller should have the xmit path locked.
 */
static void
ppp_push(struct ppp *ppp)
{
	struct list_head *list;
	struct channel *chan;
	struct sk_buff *skb = ppp->xmit_pending;

	if (skb == 0)
		return;

	list = &ppp->channels;
	if (list_empty(list)) {
		/* nowhere to send the packet, just drop it */
		ppp->xmit_pending = 0;
		kfree_skb(skb);
		return;
	}

	/* If we are doing multilink, decide which channel gets the
	   packet, and/or fragment the packet over several links. */
	/* XXX for now, just take the first channel */
	list = list->next;
	chan = list_entry(list, struct channel, list);

	if (chan->chan->ops->start_xmit(chan->chan, skb)) {
		ppp->xmit_pending = 0;
		chan->blocked = 0;
	} else
		chan->blocked = 1;
}

/*
 * Receive-side routines.
 */
static inline void
ppp_do_recv(struct ppp *ppp, struct sk_buff *skb)
{
	skb_queue_tail(&ppp->recv_pending, skb);
	if (trylock_recv_path(ppp))
		ppp_recv_unlock(ppp);
}

void
ppp_input(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct channel *pch = chan->ppp;

	if (pch == 0 || skb->len == 0) {
		kfree_skb(skb);
		return;
	}
	ppp_do_recv(pch->ppp, skb);
}

/* Put a 0-length skb in the receive queue as an error indication */
void
ppp_input_error(struct ppp_channel *chan, int code)
{
	struct channel *pch = chan->ppp;
	struct sk_buff *skb;

	if (pch == 0)
		return;
	skb = alloc_skb(0, GFP_ATOMIC);
	if (skb == 0)
		return;
	skb->len = 0;		/* probably unnecessary */
	skb->cb[0] = code;
	ppp_do_recv(pch->ppp, skb);
}

static void
ppp_recv_unlock(struct ppp *ppp)
{
	struct sk_buff *skb;

	for (;;) {
		while ((skb = skb_dequeue(&ppp->recv_pending)) != 0)
			ppp_receive_frame(ppp, skb);
		unlock_recv_path(ppp);
		if (skb_peek(&ppp->recv_pending) == 0)
			break;
		if (!trylock_recv_path(ppp))
			break;
	}
}

static void
ppp_receive_frame(struct ppp *ppp, struct sk_buff *skb)
{
	struct sk_buff *ns;
	int proto, len, npi;

	if (skb->len == 0) {
		/* XXX should do something with code in skb->cb[0] */
		goto err;	/* error indication */
	}

	if (skb->len < 2) {
		++ppp->stats.rx_length_errors;
		goto err;
	}

	/* Decompress the frame, if compressed. */
	if (ppp->rc_state != 0 && (ppp->rstate & SC_DECOMP_RUN)
	    && (ppp->rstate & (SC_DC_FERROR | SC_DC_ERROR)) == 0)
		skb = ppp_decompress_frame(ppp, skb);

	proto = PPP_PROTO(skb);
	switch (proto) {
	case PPP_VJC_COMP:
		/* decompress VJ compressed packets */
		if (ppp->vj == 0 || (ppp->flags & SC_REJ_COMP_TCP))
			goto err;
		if (skb_tailroom(skb) < 124) {
			/* copy to a new sk_buff with more tailroom */
			ns = alloc_skb(skb->len + 128, GFP_ATOMIC);
			if (ns == 0) {
				printk(KERN_ERR"PPP: no memory (VJ decomp)\n");
				goto err;
			}
			skb_reserve(ns, 2);
			memcpy(skb_put(ns, skb->len), skb->data, skb->len);
			kfree_skb(skb);
			skb = ns;
		}
		len = slhc_uncompress(ppp->vj, skb->data + 2, skb->len - 2);
		if (len <= 0) {
			printk(KERN_ERR "PPP: VJ decompression error\n");
			goto err;
		}
		len += 2;
		if (len > skb->len)
			skb_put(skb, len - skb->len);
		else if (len < skb->len)
			skb_trim(skb, len);
		proto = PPP_IP;
		break;

	case PPP_VJC_UNCOMP:
		if (ppp->vj == 0 || (ppp->flags & SC_REJ_COMP_TCP))
			goto err;
		if (slhc_remember(ppp->vj, skb->data + 2, skb->len - 2) <= 0) {
			printk(KERN_ERR "PPP: VJ uncompressed error\n");
			goto err;
		}
		proto = PPP_IP;
		break;

	case PPP_CCP:
		ppp_ccp_peek(ppp, skb, 1);
		break;
	}

	++ppp->stats.rx_packets;
	ppp->stats.rx_bytes += skb->len - 2;

	npi = proto_to_npindex(proto);
	if (npi < 0) {
		/* control or unknown frame - pass it to pppd */
		skb_queue_tail(&ppp->rq, skb);
		/* limit queue length by dropping old frames */
		while (ppp->rq.qlen > PPP_MAX_RQLEN) {
			skb = skb_dequeue(&ppp->rq);
			if (skb)
				kfree_skb(skb);
		}
		/* wake up any process polling or blocking on read */
		wake_up_interruptible(&ppp->rwait);

	} else {
		/* network protocol frame - give it to the kernel */
		ppp->last_recv = jiffies;
		if ((ppp->dev.flags & IFF_UP) == 0
		    || ppp->npmode[npi] != NPMODE_PASS) {
			kfree_skb(skb);
		} else {
			skb_pull(skb, 2);	/* chop off protocol */
			skb->dev = &ppp->dev;
			skb->protocol = htons(npindex_to_ethertype[npi]);
			skb->mac.raw = skb->data;
			netif_rx(skb);
		}
	}
	return;

 err:
	++ppp->stats.rx_errors;
	if (ppp->vj != 0)
		slhc_toss(ppp->vj);
	kfree_skb(skb);
}

static struct sk_buff *
ppp_decompress_frame(struct ppp *ppp, struct sk_buff *skb)
{
	int proto = PPP_PROTO(skb);
	struct sk_buff *ns;
	int len;

	if (proto == PPP_COMP) {
		ns = alloc_skb(ppp->mru + PPP_HDRLEN, GFP_ATOMIC);
		if (ns == 0) {
			printk(KERN_ERR "ppp_receive: no memory\n");
			goto err;
		}
		/* the decompressor still expects the A/C bytes in the hdr */
		len = ppp->rcomp->decompress(ppp->rc_state, skb->data - 2,
				skb->len + 2, ns->data, ppp->mru + PPP_HDRLEN);
		if (len < 0) {
			/* Pass the compressed frame to pppd as an
			   error indication. */
			if (len == DECOMP_FATALERROR)
				ppp->rstate |= SC_DC_FERROR;
			goto err;
		}

		kfree_skb(skb);
		skb = ns;
		skb_put(skb, len);
		skb_pull(skb, 2);	/* pull off the A/C bytes */

	} else {
		/* Uncompressed frame - pass to decompressor so it
		   can update its dictionary if necessary. */
		if (ppp->rcomp->incomp)
			ppp->rcomp->incomp(ppp->rc_state, skb->data - 2,
					   skb->len + 2);
	}

	return skb;

 err:
	ppp->rstate |= SC_DC_ERROR;
	if (ppp->vj != 0)
		slhc_toss(ppp->vj);
	++ppp->stats.rx_errors;
	return skb;
}

/*
 * Channel interface.
 */

/*
 * Connect a channel to a given PPP unit.
 * The channel MUST NOT be connected to a PPP unit already.
 */
int
ppp_register_channel(struct ppp_channel *chan, int unit)
{
	struct ppp *ppp;
	struct channel *pch;
	int ret = -ENXIO;

	spin_lock(&all_ppp_lock);
	ppp = ppp_find_unit(unit);
	if (ppp == 0)
		goto out;
	pch = kmalloc(sizeof(struct channel), GFP_ATOMIC);
	ret = -ENOMEM;
	if (pch == 0)
		goto out;
	memset(pch, 0, sizeof(struct channel));
	pch->ppp = ppp;
	pch->chan = chan;
	list_add(&pch->list, &ppp->channels);
	chan->ppp = pch;
	++ppp->n_channels;
	ret = 0;
 out:
	spin_unlock(&all_ppp_lock);
	return ret;
}

/*
 * Disconnect a channel from its PPP unit.
 */
void
ppp_unregister_channel(struct ppp_channel *chan)
{
	struct channel *pch;

	spin_lock(&all_ppp_lock);
	if ((pch = chan->ppp) != 0) {
		chan->ppp = 0;
		list_del(&pch->list);
		--pch->ppp->n_channels;
		kfree(pch);
	}
	spin_unlock(&all_ppp_lock);
}

/*
 * Callback from a channel when it can accept more to transmit.
 * This should ideally be called at BH level, not interrupt level.
 */
void
ppp_output_wakeup(struct ppp_channel *chan)
{
	struct channel *pch = chan->ppp;
	struct ppp *ppp;

	if (pch == 0)
		return;
	ppp = pch->ppp;
	pch->blocked = 0;
	set_bit(XMIT_WAKEUP, &ppp->busy);
	if (trylock_xmit_path(ppp))
		ppp_xmit_unlock(ppp);
	if (ppp->xmit_pending == 0) {
		ppp->dev.tbusy = 0;
		mark_bh(NET_BH);
	}
}

/*
 * Compression control.
 */

/* Process the PPPIOCSCOMPRESS ioctl. */
static int
ppp_set_compress(struct ppp *ppp, unsigned long arg)
{
	int err;
	struct compressor *cp;
	struct ppp_option_data data;
	unsigned char ccp_option[CCP_MAX_OPTION_LENGTH];
#ifdef CONFIG_KMOD
	char modname[32];
#endif

	err = -EFAULT;
	if (copy_from_user(&data, (void *) arg, sizeof(data))
	    || (data.length <= CCP_MAX_OPTION_LENGTH
		&& copy_from_user(ccp_option, data.ptr, data.length)))
		goto out;
	err = -EINVAL;
	if (data.length > CCP_MAX_OPTION_LENGTH
	    || ccp_option[1] < 2 || ccp_option[1] > data.length)
		goto out;

	cp = find_compressor(ccp_option[0]);
#ifdef CONFIG_KMOD
	if (cp == 0) {
		sprintf(modname, "ppp-compress-%d", ccp_option[0]);
		request_module(modname);
		cp = find_compressor(ccp_option[0]);
	}
#endif /* CONFIG_KMOD */
	if (cp == 0)
		goto out;

	err = -ENOBUFS;
	if (data.transmit) {
		lock_xmit_path(ppp);
		ppp->xstate &= ~SC_COMP_RUN;
		if (ppp->xc_state != 0) {
			ppp->xcomp->comp_free(ppp->xc_state);
			ppp->xc_state = 0;
		}

		ppp->xcomp = cp;
		ppp->xc_state = cp->comp_alloc(ccp_option, data.length);
		ppp_xmit_unlock(ppp);
		if (ppp->xc_state == 0)
			goto out;

	} else {
		lock_recv_path(ppp);
		ppp->rstate &= ~SC_DECOMP_RUN;
		if (ppp->rc_state != 0) {
			ppp->rcomp->decomp_free(ppp->rc_state);
			ppp->rc_state = 0;
		}

		ppp->rcomp = cp;
		ppp->rc_state = cp->decomp_alloc(ccp_option, data.length);
		ppp_recv_unlock(ppp);
		if (ppp->rc_state == 0)
			goto out;
	}
	err = 0;

 out:
	return err;
}

/*
 * Look at a CCP packet and update our state accordingly.
 * We assume the caller has the xmit or recv path locked.
 */
static void
ppp_ccp_peek(struct ppp *ppp, struct sk_buff *skb, int inbound)
{
	unsigned char *dp = skb->data + 2;
	int len;

	if (skb->len < CCP_HDRLEN + 2
	    || skb->len < (len = CCP_LENGTH(dp)) + 2)
		return;		/* too short */

	switch (CCP_CODE(dp)) {
	case CCP_CONFREQ:
	case CCP_TERMREQ:
	case CCP_TERMACK:
		/*
		 * CCP is going down - disable compression.
		 */
		if (inbound)
			ppp->rstate &= ~SC_DECOMP_RUN;
		else
			ppp->xstate &= ~SC_COMP_RUN;
		break;

	case CCP_CONFACK:
		if ((ppp->flags & (SC_CCP_OPEN | SC_CCP_UP)) != SC_CCP_OPEN)
			break;
		dp += CCP_HDRLEN;
		len -= CCP_HDRLEN;
		if (len < CCP_OPT_MINLEN || len < CCP_OPT_LENGTH(dp))
			break;
		if (inbound) {
			/* we will start receiving compressed packets */
			if (ppp->rc_state == 0)
				break;
			if (ppp->rcomp->decomp_init(ppp->rc_state, dp, len,
					ppp->index, 0, ppp->mru, ppp->debug)) {
				ppp->rstate |= SC_DECOMP_RUN;
				ppp->rstate &= ~(SC_DC_ERROR | SC_DC_FERROR);
			}
		} else {
			/* we will soon start sending compressed packets */
			if (ppp->xc_state == 0)
				break;
			if (ppp->xcomp->comp_init(ppp->xc_state, dp, len,
					ppp->index, 0, ppp->debug))
				ppp->xstate |= SC_COMP_RUN;
		}
		break;

	case CCP_RESETACK:
		/* reset the [de]compressor */
		if ((ppp->flags & SC_CCP_UP) == 0)
			break;
		if (inbound) {
			if (ppp->rc_state && (ppp->rstate & SC_DECOMP_RUN)) {
				ppp->rcomp->decomp_reset(ppp->rc_state);
				ppp->rstate &= ~SC_DC_ERROR;
			}
		} else {
			if (ppp->xc_state && (ppp->xstate & SC_COMP_RUN))
				ppp->xcomp->comp_reset(ppp->xc_state);
		}
		break;
	}
}

/* Free up compression resources. */
static void
ppp_ccp_closed(struct ppp *ppp)
{
	ppp->flags &= ~(SC_CCP_OPEN | SC_CCP_UP);

	lock_xmit_path(ppp);
	ppp->xstate &= ~SC_COMP_RUN;
	if (ppp->xc_state) {
		ppp->xcomp->comp_free(ppp->xc_state);
		ppp->xc_state = 0;
	}
	ppp_xmit_unlock(ppp);

	lock_recv_path(ppp);
	ppp->xstate &= ~SC_DECOMP_RUN;
	if (ppp->rc_state) {
		ppp->rcomp->decomp_free(ppp->rc_state);
		ppp->rc_state = 0;
	}
	ppp_recv_unlock(ppp);
}

/* List of compressors. */
static LIST_HEAD(compressor_list);
static spinlock_t compressor_list_lock = SPIN_LOCK_UNLOCKED;

struct compressor_entry {
	struct list_head list;
	struct compressor *comp;
};

static struct compressor_entry *
find_comp_entry(int proto)
{
	struct compressor_entry *ce;
	struct list_head *list = &compressor_list;

	while ((list = list->next) != &compressor_list) {
		ce = list_entry(list, struct compressor_entry, list);
		if (ce->comp->compress_proto == proto)
			return ce;
	}
	return 0;
}

/* Register a compressor */
int
ppp_register_compressor(struct compressor *cp)
{
	struct compressor_entry *ce;
	int ret;

	spin_lock(&compressor_list_lock);
	ret = -EEXIST;
	if (find_comp_entry(cp->compress_proto) != 0)
		goto out;
	ret = -ENOMEM;
	ce = kmalloc(sizeof(struct compressor_entry), GFP_KERNEL);
	if (ce == 0)
		goto out;
	ret = 0;
	ce->comp = cp;
	list_add(&ce->list, &compressor_list);
 out:
	spin_unlock(&compressor_list_lock);
	return ret;
}

/* Unregister a compressor */
void
ppp_unregister_compressor(struct compressor *cp)
{
	struct compressor_entry *ce;

	spin_lock(&compressor_list_lock);
	ce = find_comp_entry(cp->compress_proto);
	if (ce != 0 && ce->comp == cp) {
		list_del(&ce->list);
		kfree(ce);
	}
	spin_unlock(&compressor_list_lock);
}

/* Find a compressor. */
static struct compressor *
find_compressor(int type)
{
	struct compressor_entry *ce;
	struct compressor *cp = 0;

	spin_lock(&compressor_list_lock);
	ce = find_comp_entry(type);
	if (ce != 0)
		cp = ce->comp;
	spin_unlock(&compressor_list_lock);
	return cp;
}

/*
 * Miscelleneous stuff.
 */

static void
ppp_get_stats(struct ppp *ppp, struct ppp_stats *st)
{
	struct slcompress *vj = ppp->vj;

	memset(st, 0, sizeof(*st));
	st->p.ppp_ipackets = ppp->stats.rx_packets;
	st->p.ppp_ierrors = ppp->stats.rx_errors;
	st->p.ppp_ibytes = ppp->stats.rx_bytes;
	st->p.ppp_opackets = ppp->stats.tx_packets;
	st->p.ppp_oerrors = ppp->stats.tx_errors;
	st->p.ppp_obytes = ppp->stats.tx_bytes;
	if (vj == 0)
		return;
	st->vj.vjs_packets = vj->sls_o_compressed + vj->sls_o_uncompressed;
	st->vj.vjs_compressed = vj->sls_o_compressed;
	st->vj.vjs_searches = vj->sls_o_searches;
	st->vj.vjs_misses = vj->sls_o_misses;
	st->vj.vjs_errorin = vj->sls_i_error;
	st->vj.vjs_tossed = vj->sls_i_tossed;
	st->vj.vjs_uncompressedin = vj->sls_i_uncompressed;
	st->vj.vjs_compressedin = vj->sls_i_compressed;
}

/*
 * Stuff for handling the list of ppp units and for initialization.
 */

/*
 * Create a new ppp unit.  Fails if it can't allocate memory or
 * if there is already a unit with the requested number.
 * unit == -1 means allocate a new number.
 */
static struct ppp *
ppp_create_unit(int unit, int *retp)
{
	struct ppp *ppp;
	struct list_head *list;
	int last_unit = -1;
	int ret = -EEXIST;
	int i;

	spin_lock(&all_ppp_lock);
	list = &all_ppp_units;
	while ((list = list->next) != &all_ppp_units) {
		ppp = list_entry(list, struct ppp, list);
		if ((unit < 0 && ppp->index > last_unit + 1)
		    || (unit >= 0 && unit < ppp->index))
			break;
		if (unit == ppp->index)
			goto out;	/* unit already exists */
		last_unit = ppp->index;
	}
	if (unit < 0)
		unit = last_unit + 1;

	/* Create a new ppp structure and link it before `list'. */
	ret = -ENOMEM;
	ppp = kmalloc(sizeof(struct ppp), GFP_KERNEL);
	if (ppp == 0)
		goto out;
	memset(ppp, 0, sizeof(struct ppp));

	ppp->index = unit;
	sprintf(ppp->name, "ppp%d", unit);
	ppp->mru = PPP_MRU;
	skb_queue_head_init(&ppp->xq);
	skb_queue_head_init(&ppp->rq);
	init_waitqueue_head(&ppp->rwait);
	ppp->refcnt = 1;
	for (i = 0; i < NUM_NP; ++i)
		ppp->npmode[i] = NPMODE_PASS;
	INIT_LIST_HEAD(&ppp->channels);
	skb_queue_head_init(&ppp->recv_pending);

	ppp->dev.init = ppp_net_init;
	ppp->dev.name = ppp->name;
	ppp->dev.priv = ppp;

	ret = register_netdev(&ppp->dev);
	if (ret != 0) {
		printk(KERN_ERR "PPP: couldn't register device (%d)\n", ret);
		kfree(ppp);
		goto out;
	}

	list_add(&ppp->list, list->prev);
 out:
	spin_unlock(&all_ppp_lock);
	*retp = ret;
	if (ret != 0)
		ppp = 0;
	return ppp;
}

/*
 * Locate an existing ppp unit.
 * The caller should have locked the all_ppp_lock.
 */
static struct ppp *
ppp_find_unit(int unit)
{
	struct ppp *ppp;
	struct list_head *list;

	list = &all_ppp_units;
	while ((list = list->next) != &all_ppp_units) {
		ppp = list_entry(list, struct ppp, list);
		if (ppp->index == unit)
			return ppp;
	}
	return 0;
}

/*
 * Module stuff.
 */
#ifdef MODULE
int
init_module(void)
{
	ppp_init(0);
	return 0;
}

void
cleanup_module(void)
{
	/* should never happen */
	if (all_ppp_units.next != &all_ppp_units)
		printk(KERN_ERR "PPP: removing module but units remain!\n");
	if (unregister_chrdev(PPP_MAJOR, "ppp") != 0)
		printk(KERN_ERR "PPP: failed to unregister PPP device\n");
}
#endif /* MODULE */
