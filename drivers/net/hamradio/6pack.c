/*
 * 6pack.c	This module implements the 6pack protocol for kernel-based
 *		devices like TTY. It interfaces between a raw TTY and the
 *		kernel's AX.25 protocol layers.
 *
 * Version:	@(#)6pack.c	0.3.0	04/07/98
 *
 * Authors:	Andreas Könsgen <ajk@iehk.rwth-aachen.de>
 *
 * Quite a lot of stuff "stolen" by Jörg Reuter from slip.c, written by
 *
 *		Laurence Culhane, <loz@holmes.demon.co.uk>
 *		Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *
 */
 
#include <linux/config.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <net/ax25.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/if_slip.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/tcp.h>
/* 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h> 
*/

#include "6pack.h"

typedef unsigned char byte;


typedef struct sixpack_ctrl {
	char		if_name[8];	/* "sp0\0" .. "sp99999\0"	*/
	struct sixpack	ctrl;		/* 6pack things			*/
	struct device	dev;		/* the device			*/
} sixpack_ctrl_t;
static sixpack_ctrl_t	**sixpack_ctrls = NULL;
int sixpack_maxdev = SIXP_NRUNIT;	/* Can be overridden with insmod! */

static struct tty_ldisc	sp_ldisc;

static void sp_start_tx_timer(struct sixpack *);
static void sp_xmit_on_air(unsigned long);
static void resync_tnc(unsigned long);
void sixpack_decode(struct sixpack *, unsigned char[], int);
int encode_sixpack(unsigned char *, unsigned char *, int, unsigned char);

void decode_prio_command(byte, struct sixpack *);
void decode_std_command(byte, struct sixpack *);
void decode_data(byte, struct sixpack *);

static int tnc_init(struct sixpack *);

/* Find a free 6pack channel, and link in this `tty' line. */
static inline struct sixpack *
sp_alloc(void)
{
	sixpack_ctrl_t *spp = NULL;
	int i;

	if (sixpack_ctrls == NULL) return NULL;	/* Master array missing ! */

	for (i = 0; i < sixpack_maxdev; i++) 
	{
		spp = sixpack_ctrls[i];

		if (spp == NULL)
			break;

		if (!test_and_set_bit(SIXPF_INUSE, &spp->ctrl.flags))
			break;
	}

	/* Too many devices... */
	if (i >= sixpack_maxdev) 
		return NULL;

	/* If no channels are available, allocate one */
	if (!spp &&
	    (sixpack_ctrls[i] = (sixpack_ctrl_t *)kmalloc(sizeof(sixpack_ctrl_t),
						    GFP_KERNEL)) != NULL) 
	{
		spp = sixpack_ctrls[i];
		memset(spp, 0, sizeof(sixpack_ctrl_t));

		/* Initialize channel control data */
		set_bit(SIXPF_INUSE, &spp->ctrl.flags);
		spp->ctrl.tty         = NULL;
		sprintf(spp->if_name, "sp%d", i);
		spp->dev.name         = spp->if_name;
		spp->dev.base_addr    = i;
		spp->dev.priv         = (void*)&(spp->ctrl);
		spp->dev.next         = NULL;
		spp->dev.init         = sixpack_init;
	}

	if (spp != NULL) 
	{
		/* register device so that it can be ifconfig'ed       */
		/* sixpack_init() will be called as a side-effect         */
		/* SIDE-EFFECT WARNING: sixpack_init() CLEARS spp->ctrl ! */

		if (register_netdev(&(spp->dev)) == 0) 
		{
			set_bit(SIXPF_INUSE, &spp->ctrl.flags);
			spp->ctrl.dev = &(spp->dev);
			spp->dev.priv = (void*)&(spp->ctrl);

			return (&(spp->ctrl));
		} else {
		  	clear_bit(SIXPF_INUSE,&(spp->ctrl.flags));
			printk(KERN_WARNING "sp_alloc() - register_netdev() failure.\n");
		}
	}

	return NULL;
}


/* Free a 6pack channel. */
static inline void
sp_free(struct sixpack *sp)
{
	/* Free all 6pack frame buffers. */
	if (sp->rbuff)
		kfree(sp->rbuff);
	sp->rbuff = NULL;
	if (sp->xbuff) {
		kfree(sp->xbuff);
	}
	sp->xbuff = NULL;

	if (!test_and_clear_bit(SIXPF_INUSE, &sp->flags)) 
	{
		printk(KERN_WARNING "%s: sp_free for already free unit.\n", sp->dev->name);
	}
}


/* Set the "sending" flag. */
static inline void
sp_lock(struct sixpack *sp)
{
	if (test_and_set_bit(0, (void *) &sp->dev->tbusy))  
		printk(KERN_WARNING "%s: trying to lock already locked device!\n", sp->dev->name);
}


/* Clear the "sending" flag. */
static inline void
sp_unlock(struct sixpack *sp)
{
	if (!test_and_clear_bit(0, (void *)&sp->dev->tbusy))  
		printk(KERN_WARNING "%s: trying to unlock already unlocked device!\n", sp->dev->name);
}


/* Send one completely decapsulated IP datagram to the IP layer. */

/* This is the routine that sends the received data to the kernel AX.25.
   'cmd' is the KISS command. For AX.25 data, it is zero. */

static void
sp_bump(struct sixpack *sp, char cmd)
{
	struct sk_buff *skb;
	int count;
	unsigned char *ptr;

	count = sp->rcount+1;

	sp->rx_bytes+=count;

	skb = dev_alloc_skb(count);
	if (skb == NULL)
	{
		printk(KERN_DEBUG "%s: memory squeeze, dropping packet.\n", sp->dev->name);
		sp->rx_dropped++;
		return;
	}

	skb->dev = sp->dev;
	ptr = skb_put(skb, count);
	*ptr++ = cmd;	/* KISS command */

	memcpy(ptr, (sp->cooked_buf)+1, count);
	skb->mac.raw=skb->data;
	skb->protocol=htons(ETH_P_AX25);
	netif_rx(skb);
	sp->rx_packets++;
}


/* ----------------------------------------------------------------------- */

/* Encapsulate one AX.25 frame and stuff into a TTY queue. */
static void
sp_encaps(struct sixpack *sp, unsigned char *icp, int len)
{
	unsigned char *p;
	int actual, count;

	if (len > sp->mtu) 	/* sp->mtu = AX25_MTU = max. PACLEN = 256 */ 
	{
		len = sp->mtu;
		printk(KERN_DEBUG "%s: truncating oversized transmit packet!\n", sp->dev->name);
		sp->tx_dropped++;
		sp_unlock(sp);
		return;
	}

	p = icp;

	if (p[0] > 5)
	{
		printk(KERN_DEBUG "%s: invalid KISS command -- dropped\n", sp->dev->name);
		sp_unlock(sp);
		return;
	}

	if ((p[0] != 0) && (len > 2))
	{
		printk(KERN_DEBUG "%s: KISS control packet too long -- dropped\n", sp->dev->name);
		sp_unlock(sp);
		return;
	}

	if ((p[0] == 0) && (len < 15))
	{
		printk(KERN_DEBUG "%s: bad AX.25 packet to transmit -- dropped\n", sp->dev->name);
		sp_unlock(sp);
		sp->tx_dropped++;
		return;
	}

	count = encode_sixpack(p, (unsigned char *) sp->xbuff, len, sp->tx_delay);
	sp->tty->flags |= (1 << TTY_DO_WRITE_WAKEUP);

	switch(p[0])
	{
		case 1:	sp->tx_delay = p[1]; 		return;
		case 2:	sp->persistance = p[1];		return;
		case 3: sp->slottime = p[1];		return;
		case 4: /* ignored */			return;
		case 5: sp->duplex = p[1];		return;
	}

	if (p[0] == 0) {
		/* in case of fullduplex or DAMA operation, we don't take care
		   about the state of the DCD or of any timers, as the determination
		   of the correct time to send is the job of the AX.25 layer. We send
		   immediately after data has arrived. */
		if (sp->duplex == 1){
			sp->led_state = 0x70;
			sp->tty->driver.write(sp->tty, 0, &(sp->led_state), 1);
			sp->tx_enable = 1;
			actual = sp->tty->driver.write(sp->tty, 0, sp->xbuff, count);
			sp->xleft = count - actual;
			sp->xhead = sp->xbuff + actual;
			sp->led_state = 0x60;
			sp->tty->driver.write(sp->tty, 0, &(sp->led_state), 1);
		}
		else {
			sp->xleft = count;
			sp->xhead = sp->xbuff;
			sp->status2 = count;
			if (sp->duplex == 0)
				sp_start_tx_timer(sp);
		}
	}
}

/*
 * Called by the TTY driver when there's room for more data.  If we have
 * more packets to send, we send them here.
 */
static void sixpack_write_wakeup(struct tty_struct *tty)
{
	int actual;
	struct sixpack *sp = (struct sixpack *) tty->disc_data;

	/* First make sure we're connected. */
	if (!sp || sp->magic != SIXPACK_MAGIC || !sp->dev->start) {
		return;
	}
	if (sp->xleft <= 0)  {
		/* Now serial buffer is almost free & we can start
		 * transmission of another packet */
		sp->tx_packets++;
		tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
		sp_unlock(sp);
		sp->tx_enable = 0;
		mark_bh(NET_BH);
		return;
	}

	if (sp->tx_enable == 1) {
		actual = tty->driver.write(tty, 0, sp->xhead, sp->xleft);
		sp->xleft -= actual;
		sp->xhead += actual;
	}
}

/* ----------------------------------------------------------------------- */

/* Encapsulate an IP datagram and kick it into a TTY queue. */

static int
sp_xmit(struct sk_buff *skb, struct device *dev)
{
	struct sixpack *sp = (struct sixpack*)(dev->priv);

	if (!dev->start)  
	{
		printk(KERN_WARNING "%s: xmit call when iface is down\n", dev->name);
		return 1;
	}

	if (dev->tbusy)
		return 1;

	/* We were not busy, so we are now... :-) */
	if (skb != NULL) {
		sp_lock(sp);
		sp->tx_bytes+=skb->len; /*---2.1.x---*/
		sp_encaps(sp, skb->data, skb->len);
		dev_kfree_skb(skb);
	}
	return 0;
}
/* #endif */


/* perform the persistence/slottime algorithm for CSMA access. If the persistence
   check was successful, write the data to the serial driver. Note that in case
   of DAMA operation, the data is not sent here. */

static 
void sp_xmit_on_air(unsigned long channel)
{
	struct sixpack *sp = (struct sixpack *) channel;
	int actual;
	static unsigned char random;
	
	random = random * 17 + 41;

	if (((sp->status1 & SIXP_DCD_MASK) == 0) && (random < sp->persistance)) {
		sp->led_state = 0x70;
		sp->tty->driver.write(sp->tty, 0, &(sp->led_state),1);
		sp->tx_enable = 1;
		actual = sp->tty->driver.write(sp->tty, 0, sp->xbuff, sp->status2);
		sp->xleft -= actual;
		sp->xhead += actual;
		sp->led_state = 0x60;
		sp->tty->driver.write(sp->tty, 0, &(sp->led_state),1);
		sp->status2 = 0;
	} else
		sp_start_tx_timer(sp);
} /* sp_xmit */

/* #if defined(CONFIG_6PACK) || defined(CONFIG_6PACK_MODULE) */

/* Return the frame type ID */
static int sp_header(struct sk_buff *skb, struct device *dev, unsigned short type,
	  void *daddr, void *saddr, unsigned len)
{
#ifdef CONFIG_INET
	if (type != htons(ETH_P_AX25))
		return ax25_encapsulate(skb, dev, type, daddr, saddr, len);
#endif
	return 0;
}


static int sp_rebuild_header(struct sk_buff *skb)
{
#ifdef CONFIG_INET
	return ax25_rebuild_header(skb);
#else
	return 0;
#endif
}

/* #endif */ /* CONFIG_{AX25,AX25_MODULE} */

/* Open the low-level part of the 6pack channel. */
static int
sp_open(struct device *dev)
{
	struct sixpack *sp = (struct sixpack*)(dev->priv);
	unsigned long len;

	if (sp->tty == NULL) 
		return -ENODEV;

	/*
	 * Allocate the 6pack frame buffers:
	 *
	 * rbuff	Receive buffer.
	 * xbuff	Transmit buffer.
	 * cbuff        Temporary compression buffer.
	 */
	 
	/* !!! length of the buffers. MTU is IP MTU, not PACLEN!
	 */

	len = dev->mtu * 2;

	sp->rbuff = (unsigned char *) kmalloc(len + 4, GFP_KERNEL);
	if (sp->rbuff == NULL)   
		return -ENOMEM;

	sp->xbuff = (unsigned char *) kmalloc(len + 4, GFP_KERNEL);
	if (sp->xbuff == NULL)   
	{
		kfree(sp->rbuff);
		return -ENOMEM;
	}

	sp->mtu	     = AX25_MTU + 73;
	sp->buffsize = len;
	sp->rcount   = 0;
	sp->rx_count = 0;
	sp->rx_count_cooked = 0;
	sp->xleft    = 0;

	sp->flags   &= (1 << SIXPF_INUSE);      /* Clear ESCAPE & ERROR flags */

	sp->duplex = 0;
	sp->tx_delay    = SIXP_TXDELAY;
	sp->persistance = SIXP_PERSIST;
	sp->slottime    = SIXP_SLOTTIME;
	sp->led_state   = 0x60;
	sp->status      = 1;
	sp->status1     = 1;
	sp->status2     = 0;
	sp->tnc_ok      = 0;
	sp->tx_enable   = 0;
	
	dev->tbusy  = 0;
	dev->start  = 1;

	init_timer(&sp->tx_t);
	init_timer(&sp->resync_t);
	return 0;
}


/* Close the low-level part of the 6pack channel. */
static int
sp_close(struct device *dev)
{
	struct sixpack *sp = (struct sixpack*)(dev->priv);

	if (sp->tty == NULL) {
		return -EBUSY;
	}
	sp->tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
	dev->tbusy = 1;
	dev->start = 0;
	
	return 0;
}

static int
sixpack_receive_room(struct tty_struct *tty)
{
	return 65536;  /* We can handle an infinite amount of data. :-) */
}

/* !!! receive state machine */

/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of 6pack data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing.
 */
static void
sixpack_receive_buf(struct tty_struct *tty, const unsigned char *cp, char *fp, int count)
{
	unsigned char buf[512];
	unsigned long flags;
	int count1;

	struct sixpack *sp = (struct sixpack *) tty->disc_data;

	if (!sp || sp->magic != SIXPACK_MAGIC || !sp->dev->start || !count)
		return;

	save_flags(flags);
	cli();
	memcpy(buf, cp, count<sizeof(buf)? count:sizeof(buf));
	restore_flags(flags);
	
	/* Read the characters out of the buffer */

	count1 = count;
	while(count)
	{
		count--;
		if (fp && *fp++)
		{
			if (!test_and_set_bit(SIXPF_ERROR, &sp->flags))  {
				sp->rx_errors++;
			}			
			continue;
		}
	}
	sixpack_decode(sp, buf, count1);
}

/*
 * Open the high-level part of the 6pack channel.
 * This function is called by the TTY module when the
 * 6pack line discipline is called for.  Because we are
 * sure the tty line exists, we only have to link it to
 * a free 6pcack channel...
 */
static int
sixpack_open(struct tty_struct *tty)
{
	struct sixpack *sp = (struct sixpack *) tty->disc_data;
	int err;

	/* First make sure we're not already connected. */

	if (sp && sp->magic == SIXPACK_MAGIC) 
		return -EEXIST;

	/* OK.  Find a free 6pack channel to use. */
	if ((sp = sp_alloc()) == NULL)
		return -ENFILE;
	sp->tty = tty;
	tty->disc_data = sp;
	if (tty->driver.flush_buffer) 
		tty->driver.flush_buffer(tty);

	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);


	/* Restore default settings */
	sp->dev->type = ARPHRD_AX25;

	/* Perform the low-level 6pack initialization. */
	if ((err = sp_open(sp->dev)))  
		return err;
	
	MOD_INC_USE_COUNT;

	/* Done.  We have linked the TTY line to a channel. */

	tnc_init(sp);

	return sp->dev->base_addr;
}


/*
 * Close down a 6pack channel.
 * This means flushing out any pending queues, and then restoring the
 * TTY line discipline to what it was before it got hooked to 6pack
 * (which usually is TTY again).
 */
static void
sixpack_close(struct tty_struct *tty)
{
	struct sixpack *sp = (struct sixpack *) tty->disc_data;

	/* First make sure we're connected. */
	if (!sp || sp->magic != SIXPACK_MAGIC)
		return;

	rtnl_lock();
	if (sp->dev->flags & IFF_UP)
		(void) dev_close(sp->dev);

	del_timer(&(sp->tx_t));
	del_timer(&(sp->resync_t));
	
	tty->disc_data = 0;
	sp->tty = NULL;
	/* VSV = very important to remove timers */

	sp_free(sp);
	unregister_netdev(sp->dev);
	rtnl_unlock();
	MOD_DEC_USE_COUNT;
}


static struct net_device_stats *
sp_get_stats(struct device *dev)
{
	static struct net_device_stats stats;
	struct sixpack *sp = (struct sixpack*)(dev->priv);

	memset(&stats, 0, sizeof(struct net_device_stats));

	stats.rx_packets     = sp->rx_packets;
	stats.tx_packets     = sp->tx_packets;
	stats.rx_bytes       = sp->rx_bytes;
	stats.tx_bytes       = sp->tx_bytes;
	stats.rx_dropped     = sp->rx_dropped;
	stats.tx_dropped     = sp->tx_dropped;
	stats.tx_errors      = sp->tx_errors;
	stats.rx_errors      = sp->rx_errors;
	stats.rx_over_errors = sp->rx_over_errors;
	return (&stats);
}


int
sp_set_mac_address(struct device *dev, void *addr)
{
	int err;

	err = verify_area(VERIFY_READ, addr, AX25_ADDR_LEN);
	if (err)  {
		return err;
	}

	copy_from_user(dev->dev_addr, addr, AX25_ADDR_LEN);	/* addr is an AX.25 shifted ASCII mac address */

	return 0;
}

static int
sp_set_dev_mac_address(struct device *dev, void *addr)
{
	struct sockaddr *sa=addr;
	memcpy(dev->dev_addr, sa->sa_data, AX25_ADDR_LEN);
	return 0;
}


/* Perform I/O control on an active 6pack channel. */
static int
sixpack_ioctl(struct tty_struct *tty, void *file, int cmd, void *arg)
{
	struct sixpack *sp = (struct sixpack *) tty->disc_data;
	int err;
	unsigned int tmp;

	/* First make sure we're connected. */
	if (!sp || sp->magic != SIXPACK_MAGIC) {
		return -EINVAL;
	}

	switch(cmd) {
	 case SIOCGIFNAME:
		err = verify_area(VERIFY_WRITE, arg, strlen(sp->dev->name) + 1);
		if (err)  {
			return err;
		}
		copy_to_user(arg, sp->dev->name, strlen(sp->dev->name) + 1);
		return 0;

	case SIOCGIFENCAP:
		err = verify_area(VERIFY_WRITE, arg, sizeof(int));
		if (err)  {
			return err;
		}
		put_user(0, (int *)arg);
		return 0;

	case SIOCSIFENCAP:
		err = verify_area(VERIFY_READ, arg, sizeof(int));
		if (err)  {
			return err;
		}
		get_user(tmp,(int *)arg);

 		sp->mode = tmp;
		sp->dev->addr_len        = AX25_ADDR_LEN;	  /* sizeof an AX.25 addr */
		sp->dev->hard_header_len = AX25_KISS_HEADER_LEN + AX25_MAX_HEADER_LEN + 3;
		sp->dev->type            = ARPHRD_AX25;

		return 0;

	 case SIOCSIFHWADDR:
		return sp_set_mac_address(sp->dev, arg);

	/* Allow stty to read, but not set, the serial port */
	case TCGETS:
	case TCGETA:
		return n_tty_ioctl(tty, (struct file *) file, cmd, (unsigned long) arg);

	default:
		return -ENOIOCTLCMD;
	}
}

static int sp_open_dev(struct device *dev)
{
	struct sixpack *sp = (struct sixpack*)(dev->priv);
	if(sp->tty==NULL)
		return -ENODEV;
	return 0;
}

/* Initialize 6pack control device -- register 6pack line discipline */

#ifdef MODULE
static int sixpack_init_ctrl_dev(void)
#else	/* !MODULE */
__initfunc(int sixpack_init_ctrl_dev(struct device *dummy))
#endif	/* !MODULE */
{
	int status;

	if (sixpack_maxdev < 4) sixpack_maxdev = 4; /* Sanity */

	printk(KERN_INFO "6pack: %s (dynamic channels, max=%d)\n",
	       SIXPACK_VERSION, sixpack_maxdev);

	sixpack_ctrls = (sixpack_ctrl_t **) kmalloc(sizeof(void*)*sixpack_maxdev, GFP_KERNEL);
	if (sixpack_ctrls == NULL) 
	{
		printk(KERN_WARNING "6pack: Can't allocate sixpack_ctrls[] array!  Uaargh! (-> No 6pack available)\n");
		return -ENOMEM;
	}

	/* Clear the pointer array, we allocate devices when we need them */
	memset(sixpack_ctrls, 0, sizeof(void*)*sixpack_maxdev); /* Pointers */


	/* Fill in our line protocol discipline, and register it */
	memset(&sp_ldisc, 0, sizeof(sp_ldisc));
	sp_ldisc.magic  = TTY_LDISC_MAGIC;
	sp_ldisc.name   = "6pack";
	sp_ldisc.flags  = 0;
	sp_ldisc.open   = sixpack_open;
	sp_ldisc.close  = sixpack_close;
	sp_ldisc.read   = NULL;
	sp_ldisc.write  = NULL;
	sp_ldisc.ioctl  = (int (*)(struct tty_struct *, struct file *,
				   unsigned int, unsigned long)) sixpack_ioctl;
	sp_ldisc.poll = NULL;
	sp_ldisc.receive_buf = sixpack_receive_buf;
	sp_ldisc.receive_room = sixpack_receive_room;
	sp_ldisc.write_wakeup = sixpack_write_wakeup;
	if ((status = tty_register_ldisc(N_6PACK, &sp_ldisc)) != 0)  {
		printk(KERN_WARNING "6pack: can't register line discipline (err = %d)\n", status);
	}

#ifdef MODULE
	return status;
#else
	/* Return "not found", so that dev_init() will unlink
	 * the placeholder device entry for us.
	 */
	return ENODEV;
#endif
}

/* Initialize the 6pack driver.  Called by DDI. */
int
sixpack_init(struct device *dev)
{
	struct sixpack *sp = (struct sixpack*)(dev->priv);

	static char ax25_bcast[AX25_ADDR_LEN] =
		{'Q'<<1,'S'<<1,'T'<<1,' '<<1,' '<<1,' '<<1,'0'<<1};
	static char ax25_test[AX25_ADDR_LEN] =
		{'L'<<1,'I'<<1,'N'<<1,'U'<<1,'X'<<1,' '<<1,'1'<<1};

	if (sp == NULL)		/* Allocation failed ?? */
	  return -ENODEV;

	/* Set up the "6pack Control Block". (And clear statistics) */
	
	memset(sp, 0, sizeof (struct sixpack));
	sp->magic  = SIXPACK_MAGIC;
	sp->dev	   = dev;
	
	/* Finish setting up the DEVICE info. */
	dev->mtu		= SIXP_MTU;
	dev->hard_start_xmit	= sp_xmit;
	dev->open		= sp_open_dev;
	dev->stop		= sp_close;
	dev->hard_header	= sp_header;
	dev->get_stats	        = sp_get_stats;
	dev->set_mac_address    = sp_set_dev_mac_address;
	dev->hard_header_len	= AX25_MAX_HEADER_LEN;
	dev->addr_len		= AX25_ADDR_LEN;
	dev->type		= ARPHRD_AX25;
	dev->tx_queue_len	= 10;
	dev->rebuild_header	= sp_rebuild_header;

	memcpy(dev->broadcast, ax25_bcast, AX25_ADDR_LEN);	/* Only activated in AX.25 mode */
	memcpy(dev->dev_addr, ax25_test, AX25_ADDR_LEN);	/*    ""      ""       ""    "" */

	dev_init_buffers(dev);

	/* New-style flags. */
	dev->flags		= 0;

	return 0;
}

#ifdef MODULE

int
init_module(void)
{
	return sixpack_init_ctrl_dev();
}

void
cleanup_module(void)
{
	int i;

	if (sixpack_ctrls != NULL) 
	{
		for (i = 0; i < sixpack_maxdev; i++)  
		{
			if (sixpack_ctrls[i])
			{
				/*
				 * VSV = if dev->start==0, then device
				 * unregistered while close proc.
				 */ 
				if (sixpack_ctrls[i]->dev.start)
					unregister_netdev(&(sixpack_ctrls[i]->dev));

				kfree(sixpack_ctrls[i]);
				sixpack_ctrls[i] = NULL;
			}
		}
		kfree(sixpack_ctrls);
		sixpack_ctrls = NULL;
	}
	if ((i = tty_register_ldisc(N_6PACK, NULL)))  
	{
		printk(KERN_WARNING "6pack: can't unregister line discipline (err = %d)\n", i);
	}
}
#endif /* MODULE */

/* ----> 6pack timer interrupt handler and friends. <---- */
static void 
sp_start_tx_timer(struct sixpack *sp)
{
	int when = sp->slottime;
	
	del_timer(&(sp->tx_t));
	sp->tx_t.data = (unsigned long) sp;
	sp->tx_t.function = sp_xmit_on_air;
	sp->tx_t.expires = jiffies + ((when+1)*HZ)/100;
	add_timer(&(sp->tx_t));
}


/* encode an AX.25 packet into 6pack */

int encode_sixpack(byte *tx_buf, byte *tx_buf_raw, int length, byte tx_delay)
{
	int count = 0;
	byte checksum = 0, buf[400];
	int raw_count = 0;

	tx_buf_raw[raw_count++] = SIXP_PRIO_CMD_MASK | SIXP_TX_MASK;
	tx_buf_raw[raw_count++] = SIXP_SEOF;

	buf[0] = tx_delay;
	for(count = 1; count < length; count++)
		buf[count] = tx_buf[count];

	for(count = 0; count < length; count++)
		checksum += buf[count];
	buf[length] = (byte)0xff - checksum;
	
	for(count = 0; count <= length; count++) {
		if((count % 3) == 0) {
			tx_buf_raw[raw_count++] = (buf[count] & 0x3f);
			tx_buf_raw[raw_count] = ((buf[count] >> 2) & 0x30);
		}
		else if((count % 3) == 1) {
			tx_buf_raw[raw_count++] |= (buf[count] & 0x0f);
			tx_buf_raw[raw_count] =
				((buf[count] >> 2) & 0x3c);
		} else {
			tx_buf_raw[raw_count++] |= (buf[count] & 0x03);
			tx_buf_raw[raw_count++] =
				(buf[count] >> 2);
		} /* else */
	} /* for */
	if ((length % 3) != 2)
		raw_count++;
	tx_buf_raw[raw_count++] = SIXP_SEOF;
	return(raw_count);
}


/* decode a 6pack packet */

void
sixpack_decode(struct sixpack *sp, unsigned char pre_rbuff[], int count)
{
	byte inbyte;
	int count1;

	for (count1 = 0; count1 < count; count1++) {
		inbyte = pre_rbuff[count1];
		if (inbyte == SIXP_FOUND_TNC) {
			printk(KERN_INFO "6pack: TNC found.\n");
			sp->tnc_ok = 1;
			del_timer(&(sp->resync_t));
		}
		if((inbyte & SIXP_PRIO_CMD_MASK) != 0)
			decode_prio_command(inbyte, sp);
		else if((inbyte & SIXP_STD_CMD_MASK) != 0)
			decode_std_command(inbyte, sp);
		else {
			if ((sp->status & SIXP_RX_DCD_MASK) == SIXP_RX_DCD_MASK)
				decode_data(inbyte, sp);
		} /* else */
	} /* for */
}

static int
tnc_init(struct sixpack *sp)
{
	static byte inbyte;
	
	inbyte = 0xe8;
	sp->tty->driver.write(sp->tty, 0, &inbyte, 1);

	del_timer(&(sp->resync_t));
	sp->resync_t.data = (unsigned long) sp;
	sp->resync_t.function = resync_tnc;
	sp->resync_t.expires = jiffies + SIXP_RESYNC_TIMEOUT;
	add_timer(&(sp->resync_t));

	return 0;
}


/* identify and execute a 6pack priority command byte */

void decode_prio_command(byte cmd, struct sixpack *sp)
{
	byte channel;
	int actual;

	channel = cmd & SIXP_CHN_MASK;
	if ((cmd & SIXP_PRIO_DATA_MASK) != 0) {     /* idle ? */

	/* RX and DCD flags can only be set in the same prio command,
	   if the DCD flag has been set without the RX flag in the previous
	   prio command. If DCD has not been set before, something in the
	   transmission has gone wrong. In this case, RX and DCD are
	   cleared in order to prevent the decode_data routine from
	   reading further data that might be corrupt. */

		if (((sp->status & SIXP_DCD_MASK) == 0) &&
			((cmd & SIXP_RX_DCD_MASK) == SIXP_RX_DCD_MASK)) {
				if (sp->status != 1)
					printk(KERN_DEBUG "6pack: protocol violation\n");
				else
					sp->status = 0;
				cmd &= !SIXP_RX_DCD_MASK;
		}
		sp->status = cmd & SIXP_PRIO_DATA_MASK;
	} /* if */
	else { /* output watchdog char if idle */
		if ((sp->status2 != 0) && (sp->duplex == 1)) {
			sp->led_state = 0x70;
			sp->tty->driver.write(sp->tty, 0, &(sp->led_state), 1);
			sp->tx_enable = 1;
			actual = sp->tty->driver.write(sp->tty, 0, sp->xbuff, sp->status2);
			sp->xleft -= actual;
			sp->xhead += actual;
			sp->led_state = 0x60;
			sp->status2 = 0;

		} /* if */
	} /* else */

	/* needed to trigger the TNC watchdog */
	sp->tty->driver.write(sp->tty, 0, &(sp->led_state), 1);

        /* if the state byte has been received, the TNC is present,
           so the resync timer can be reset. */

	if (sp->tnc_ok == 1) {
		del_timer(&(sp->resync_t));
		sp->resync_t.data = (unsigned long) sp;
		sp->resync_t.function = resync_tnc;
		sp->resync_t.expires = jiffies + SIXP_INIT_RESYNC_TIMEOUT;
		add_timer(&(sp->resync_t));
	}

	sp->status1 = cmd & SIXP_PRIO_DATA_MASK;
}

/* try to resync the TNC. Called by the resync timer defined in
  decode_prio_command */

static void
resync_tnc(unsigned long channel)
{
	static char resync_cmd = 0xe8;
	struct sixpack *sp = (struct sixpack *) channel;

	printk(KERN_INFO "6pack: resyncing TNC\n");

	/* clear any data that might have been received */
	
	sp->rx_count = 0;
	sp->rx_count_cooked = 0;

	/* reset state machine */

	sp->status = 1;
	sp->status1 = 1;
	sp->status2 = 0;
	sp->tnc_ok = 0;
	
	/* resync the TNC */

	sp->led_state = 0x60;
	sp->tty->driver.write(sp->tty, 0, &(sp->led_state), 1);
	sp->tty->driver.write(sp->tty, 0, &resync_cmd, 1);


	/* Start resync timer again -- the TNC might be still absent */

	del_timer(&(sp->resync_t));
	sp->resync_t.data = (unsigned long) sp;
	sp->resync_t.function = resync_tnc;
	sp->resync_t.expires = jiffies + SIXP_RESYNC_TIMEOUT;
	add_timer(&(sp->resync_t));
}



/* identify and execute a standard 6pack command byte */

void decode_std_command(byte cmd, struct sixpack *sp)
{
	byte checksum = 0, rest = 0, channel;
	short i;

	channel = cmd & SIXP_CHN_MASK;
	switch(cmd & SIXP_CMD_MASK) {     /* normal command */
		case SIXP_SEOF:
			if ((sp->rx_count == 0) && (sp->rx_count_cooked == 0)) {
				if ((sp->status & SIXP_RX_DCD_MASK) ==
					SIXP_RX_DCD_MASK) {
					sp->led_state = 0x68;
					sp->tty->driver.write(sp->tty, 0, &(sp->led_state), 1);
				} /* if */
			} else {
				sp->led_state = 0x60;
				/* fill trailing bytes with zeroes */
				sp->tty->driver.write(sp->tty, 0, &(sp->led_state), 1);
				rest = sp->rx_count;
				if (rest != 0)
					 for(i=rest; i<=3; i++)
						decode_data(0, sp);
				if (rest == 2)
					sp->rx_count_cooked -= 2;
				else if (rest == 3)
					sp->rx_count_cooked -= 1;
				for (i=0; i<sp->rx_count_cooked; i++)
					checksum+=sp->cooked_buf[i];
				if (checksum != SIXP_CHKSUM) {
					printk(KERN_DEBUG "6pack: bad checksum %2.2x\n", checksum);
				} else {
					sp->rcount = sp->rx_count_cooked-2;
					sp_bump(sp, 0);
				} /* else */
				sp->rx_count_cooked = 0;
			} /* else */
			break;
		case SIXP_TX_URUN: printk(KERN_DEBUG "6pack: TX underrun\n");
			break;
		case SIXP_RX_ORUN: printk(KERN_DEBUG "6pack: RX overrun\n");
			break;
		case SIXP_RX_BUF_OVL:
			printk(KERN_DEBUG "6pack: RX buffer overflow\n");
	} /* switch */
} /* function */

/* decode 4 sixpack-encoded bytes into 3 data bytes */

void decode_data(byte inbyte, struct sixpack *sp)
{

	unsigned char *buf;

	if (sp->rx_count != 3)
		sp->raw_buf[sp->rx_count++] = inbyte;
	else {
		buf = sp->raw_buf;
		sp->cooked_buf[sp->rx_count_cooked++] =
			buf[0] | ((buf[1] << 2) & 0xc0);
		sp->cooked_buf[sp->rx_count_cooked++] =
			(buf[1] & 0x0f) | ((buf[2] << 2) & 0xf0);
		sp->cooked_buf[sp->rx_count_cooked++] =
			(buf[2] & 0x03) | (inbyte << 2);
		sp->rx_count = 0;
	}
}
