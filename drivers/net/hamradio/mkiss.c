/*
 *	MKISS Driver
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * 		This module implements the AX.25 protocol for kernel-based
 *		devices like TTYs. It interfaces between a raw TTY, and the
 *		kernel's AX.25 protocol layers, just like slip.c.
 *		AX.25 needs to be separated from slip.c while slip.c is no
 *		longer a static kernel device since it is a module.
 *		This method clears the way to implement other kiss protocols
 *		like mkiss smack g8bpq ..... so far only mkiss is implemented.
 *
 * Hans Alblas <hans@esrac.ele.tue.nl>
 *
 *	History
 *	Jonathan (G4KLX)	Fixed to match Linux networking changes - 2.1.15.
 *	Matthias (DG2FEF)       Added support for FlexNet CRC (on special request)
 *                              Fixed bug in ax25_close(): dev_lock_wait() was
 *                              called twice, causing a deadlock.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/major.h>
#include <linux/init.h>

#include <linux/timer.h>

#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>

#include <net/ax25.h>

#include "mkiss.h"

#ifdef CONFIG_INET
#include <linux/ip.h>
#include <linux/tcp.h>
#endif

#ifdef MODULE
#define AX25_VERSION    "AX25-MODULAR-NET3.019-NEWTTY"
#define	min(a,b)	(a < b ? a : b)
#else
#define	AX25_VERSION	"AX25-NET3.019-NEWTTY"
#endif

#define NR_MKISS 4
#define MKISS_SERIAL_TYPE_NORMAL 1

struct mkiss_channel {
	int magic;		/* magic word */
	int init;		/* channel exists? */
	struct tty_struct *tty; /* link to tty control structure */
};

typedef struct ax25_ctrl {
	char if_name[8];	/* "ax0\0" .. "ax99999\0"	*/
	struct ax_disp ctrl;	/* 				*/
	struct device  dev;	/* the device			*/
} ax25_ctrl_t;

static ax25_ctrl_t **ax25_ctrls = NULL;

int ax25_maxdev = AX25_MAXDEV;		/* Can be overridden with insmod! */

static struct tty_ldisc	ax_ldisc;
static struct tty_driver mkiss_driver;
static int mkiss_refcount;
static struct tty_struct *mkiss_table[NR_MKISS];
static struct termios *mkiss_termios[NR_MKISS];
static struct termios *mkiss_termios_locked[NR_MKISS];
struct mkiss_channel MKISS_Info[NR_MKISS];

static int ax25_init(struct device *);
static int mkiss_init(void);
static int mkiss_write(struct tty_struct *, int, const unsigned char *, int);
static int kiss_esc(unsigned char *, unsigned char *, int);
static int kiss_esc_crc(unsigned char *, unsigned char *, unsigned short, int);
static void kiss_unesc(struct ax_disp *, unsigned char);

/*---------------------------------------------------------------------------*/

static const unsigned short Crc_flex_table[] = {
  0x0f87, 0x1e0e, 0x2c95, 0x3d1c, 0x49a3, 0x582a, 0x6ab1, 0x7b38,
  0x83cf, 0x9246, 0xa0dd, 0xb154, 0xc5eb, 0xd462, 0xe6f9, 0xf770,
  0x1f06, 0x0e8f, 0x3c14, 0x2d9d, 0x5922, 0x48ab, 0x7a30, 0x6bb9,
  0x934e, 0x82c7, 0xb05c, 0xa1d5, 0xd56a, 0xc4e3, 0xf678, 0xe7f1,
  0x2e85, 0x3f0c, 0x0d97, 0x1c1e, 0x68a1, 0x7928, 0x4bb3, 0x5a3a,
  0xa2cd, 0xb344, 0x81df, 0x9056, 0xe4e9, 0xf560, 0xc7fb, 0xd672,
  0x3e04, 0x2f8d, 0x1d16, 0x0c9f, 0x7820, 0x69a9, 0x5b32, 0x4abb,
  0xb24c, 0xa3c5, 0x915e, 0x80d7, 0xf468, 0xe5e1, 0xd77a, 0xc6f3,
  0x4d83, 0x5c0a, 0x6e91, 0x7f18, 0x0ba7, 0x1a2e, 0x28b5, 0x393c,
  0xc1cb, 0xd042, 0xe2d9, 0xf350, 0x87ef, 0x9666, 0xa4fd, 0xb574,
  0x5d02, 0x4c8b, 0x7e10, 0x6f99, 0x1b26, 0x0aaf, 0x3834, 0x29bd,
  0xd14a, 0xc0c3, 0xf258, 0xe3d1, 0x976e, 0x86e7, 0xb47c, 0xa5f5,
  0x6c81, 0x7d08, 0x4f93, 0x5e1a, 0x2aa5, 0x3b2c, 0x09b7, 0x183e,
  0xe0c9, 0xf140, 0xc3db, 0xd252, 0xa6ed, 0xb764, 0x85ff, 0x9476,
  0x7c00, 0x6d89, 0x5f12, 0x4e9b, 0x3a24, 0x2bad, 0x1936, 0x08bf,
  0xf048, 0xe1c1, 0xd35a, 0xc2d3, 0xb66c, 0xa7e5, 0x957e, 0x84f7,
  0x8b8f, 0x9a06, 0xa89d, 0xb914, 0xcdab, 0xdc22, 0xeeb9, 0xff30,
  0x07c7, 0x164e, 0x24d5, 0x355c, 0x41e3, 0x506a, 0x62f1, 0x7378,
  0x9b0e, 0x8a87, 0xb81c, 0xa995, 0xdd2a, 0xcca3, 0xfe38, 0xefb1,
  0x1746, 0x06cf, 0x3454, 0x25dd, 0x5162, 0x40eb, 0x7270, 0x63f9,
  0xaa8d, 0xbb04, 0x899f, 0x9816, 0xeca9, 0xfd20, 0xcfbb, 0xde32,
  0x26c5, 0x374c, 0x05d7, 0x145e, 0x60e1, 0x7168, 0x43f3, 0x527a,
  0xba0c, 0xab85, 0x991e, 0x8897, 0xfc28, 0xeda1, 0xdf3a, 0xceb3,
  0x3644, 0x27cd, 0x1556, 0x04df, 0x7060, 0x61e9, 0x5372, 0x42fb,
  0xc98b, 0xd802, 0xea99, 0xfb10, 0x8faf, 0x9e26, 0xacbd, 0xbd34,
  0x45c3, 0x544a, 0x66d1, 0x7758, 0x03e7, 0x126e, 0x20f5, 0x317c,
  0xd90a, 0xc883, 0xfa18, 0xeb91, 0x9f2e, 0x8ea7, 0xbc3c, 0xadb5,
  0x5542, 0x44cb, 0x7650, 0x67d9, 0x1366, 0x02ef, 0x3074, 0x21fd,
  0xe889, 0xf900, 0xcb9b, 0xda12, 0xaead, 0xbf24, 0x8dbf, 0x9c36,
  0x64c1, 0x7548, 0x47d3, 0x565a, 0x22e5, 0x336c, 0x01f7, 0x107e,
  0xf808, 0xe981, 0xdb1a, 0xca93, 0xbe2c, 0xafa5, 0x9d3e, 0x8cb7,
  0x7440, 0x65c9, 0x5752, 0x46db, 0x3264, 0x23ed, 0x1176, 0x00ff
};

/*---------------------------------------------------------------------------*/

static unsigned short 
calc_crc_flex(unsigned char *cp, int size)
{
    unsigned short crc = 0xffff;
    
    while (size--)
	crc = (crc << 8) ^ Crc_flex_table[((crc >> 8) ^ *cp++) & 0xff];

    return crc;
}

/*---------------------------------------------------------------------------*/

static int 
check_crc_flex(unsigned char *cp, int size)
{
  unsigned short crc = 0xffff;

  if (size < 3)
      return -1;

  while (size--)
      crc = (crc << 8) ^ Crc_flex_table[((crc >> 8) ^ *cp++) & 0xff];

  if ((crc & 0xffff) != 0x7070) 
      return -1;

  return 0;
}

/*---------------------------------------------------------------------------*/

/* Find a free channel, and link in this `tty' line. */
static inline struct ax_disp *ax_alloc(void)
{
	ax25_ctrl_t *axp;
	int i;

	if (ax25_ctrls == NULL)		/* Master array missing ! */
		return NULL;

	for (i = 0; i < ax25_maxdev; i++) {
		axp = ax25_ctrls[i];

		/* Not allocated ? */
		if (axp == NULL)
			break;

		/* Not in use ? */
		if (!test_and_set_bit(AXF_INUSE, &axp->ctrl.flags))
			break;
	}

	/* Sorry, too many, all slots in use */
	if (i >= ax25_maxdev)
		return NULL;

	/* If no channels are available, allocate one */
	if (axp == NULL && (ax25_ctrls[i] = kmalloc(sizeof(ax25_ctrl_t), GFP_KERNEL)) != NULL) {
		axp = ax25_ctrls[i];
		memset(axp, 0, sizeof(ax25_ctrl_t));

		/* Initialize channel control data */
		set_bit(AXF_INUSE, &axp->ctrl.flags);
		sprintf(axp->if_name, "ax%d", i++);
		axp->ctrl.tty      = NULL;
		axp->dev.name      = axp->if_name;
		axp->dev.base_addr = i;
		axp->dev.priv      = (void *)&axp->ctrl;
		axp->dev.next      = NULL;
		axp->dev.init      = ax25_init;
	}

	if (axp != NULL) {
		/*
		 * register device so that it can be ifconfig'ed
		 * ax25_init() will be called as a side-effect
		 * SIDE-EFFECT WARNING: ax25_init() CLEARS axp->ctrl !
		 */
		if (register_netdev(&axp->dev) == 0) {
			/* (Re-)Set the INUSE bit.   Very Important! */
			set_bit(AXF_INUSE, &axp->ctrl.flags);
			axp->ctrl.dev = &axp->dev;
			axp->dev.priv = (void *)&axp->ctrl;

			return &axp->ctrl;
		} else {
			clear_bit(AXF_INUSE,&axp->ctrl.flags);
			printk(KERN_ERR "mkiss: ax_alloc() - register_netdev() failure.\n");
		}
	}

	return NULL;
}

/* Free an AX25 channel. */
static inline void ax_free(struct ax_disp *ax)
{
	/* Free all AX25 frame buffers. */
	if (ax->rbuff)
		kfree(ax->rbuff);
	ax->rbuff = NULL;
	if (ax->xbuff)
		kfree(ax->xbuff);
	ax->xbuff = NULL;
	if (!test_and_clear_bit(AXF_INUSE, &ax->flags))
		printk(KERN_ERR "mkiss: %s: ax_free for already free unit.\n", ax->dev->name);
}

static void ax_changedmtu(struct ax_disp *ax)
{
	struct device *dev = ax->dev;
	unsigned char *xbuff, *rbuff, *oxbuff, *orbuff;
	int len;
	unsigned long flags;

	len = dev->mtu * 2;

	/*
	 * allow for arrival of larger UDP packets, even if we say not to
	 * also fixes a bug in which SunOS sends 512-byte packets even with
	 * an MSS of 128
	 */
	if (len < 576 * 2)
		len = 576 * 2;

	xbuff = kmalloc(len + 4, GFP_ATOMIC);
	rbuff = kmalloc(len + 4, GFP_ATOMIC);

	if (xbuff == NULL || rbuff == NULL)  {
		printk(KERN_ERR "mkiss: %s: unable to grow ax25 buffers, MTU change cancelled.\n",
		       ax->dev->name);
		dev->mtu = ax->mtu;
		if (xbuff != NULL)
			kfree(xbuff);
		if (rbuff != NULL)
			kfree(rbuff);
		return;
	}

	save_flags(flags);
	cli();

	oxbuff    = ax->xbuff;
	ax->xbuff = xbuff;
	orbuff    = ax->rbuff;
	ax->rbuff = rbuff;

	if (ax->xleft) {
		if (ax->xleft <= len) {
			memcpy(ax->xbuff, ax->xhead, ax->xleft);
		} else  {
			ax->xleft = 0;
			ax->tx_dropped++;
		}
	}

	ax->xhead = ax->xbuff;

	if (ax->rcount) {
		if (ax->rcount <= len) {
			memcpy(ax->rbuff, orbuff, ax->rcount);
		} else  {
			ax->rcount = 0;
			ax->rx_over_errors++;
			set_bit(AXF_ERROR, &ax->flags);
		}
	}

	ax->mtu      = dev->mtu + 73;
	ax->buffsize = len;

	restore_flags(flags);

	if (oxbuff != NULL)
		kfree(oxbuff);
	if (orbuff != NULL)
		kfree(orbuff);
}


/* Set the "sending" flag.  This must be atomic, hence the ASM. */
static inline void ax_lock(struct ax_disp *ax)
{
	if (test_and_set_bit(0, (void *)&ax->dev->tbusy))
		printk(KERN_ERR "mkiss: %s: trying to lock already locked device!\n", ax->dev->name);
}


/* Clear the "sending" flag.  This must be atomic, hence the ASM. */
static inline void ax_unlock(struct ax_disp *ax)
{
	if (!test_and_clear_bit(0, (void *)&ax->dev->tbusy))
		printk(KERN_ERR "mkiss: %s: trying to unlock already unlocked device!\n", ax->dev->name);
}

/* Send one completely decapsulated AX.25 packet to the AX.25 layer. */
static void ax_bump(struct ax_disp *ax)
{
	struct ax_disp *tmp_ax;
	struct sk_buff *skb;
	struct mkiss_channel *mkiss;
	int count;

        tmp_ax = ax;

	if (ax->rbuff[0] > 0x0f) {
		if (ax->mkiss != NULL) {
			mkiss= ax->mkiss->tty->driver_data;
			if (mkiss->magic == MKISS_DRIVER_MAGIC)
				tmp_ax = ax->mkiss;
		} else if (ax->rbuff[0] & 0x20) {
		        ax->crcmode = CRC_MODE_FLEX;
			if (check_crc_flex(ax->rbuff, ax->rcount) < 0) {
			        ax->rx_errors++;
				return;
			}
			ax->rcount -= 2;
		}
 	}

	count = ax->rcount;

	if ((skb = dev_alloc_skb(count)) == NULL) {
		printk(KERN_ERR "mkiss: %s: memory squeeze, dropping packet.\n", ax->dev->name);
		ax->rx_dropped++;
		return;
	}

	skb->dev      = tmp_ax->dev;
	memcpy(skb_put(skb,count), ax->rbuff, count);
	skb->mac.raw  = skb->data;
	skb->protocol = htons(ETH_P_AX25);
	netif_rx(skb);
	tmp_ax->rx_packets++;
}

/* Encapsulate one AX.25 packet and stuff into a TTY queue. */
static void ax_encaps(struct ax_disp *ax, unsigned char *icp, int len)
{
	unsigned char *p;
	int actual, count;
	struct mkiss_channel *mkiss = ax->tty->driver_data;

	if (ax->mtu != ax->dev->mtu + 73)	/* Someone has been ifconfigging */
		ax_changedmtu(ax);

	if (len > ax->mtu) {		/* Sigh, shouldn't occur BUT ... */
		len = ax->mtu;
		printk(KERN_ERR "mkiss: %s: truncating oversized transmit packet!\n", ax->dev->name);
		ax->tx_dropped++;
		ax_unlock(ax);
		return;
	}

	p = icp;

	if (mkiss->magic  != MKISS_DRIVER_MAGIC) {
	        switch (ax->crcmode) {
		         unsigned short crc;
			 
		case CRC_MODE_FLEX:
		         *p |= 0x20;
		         crc = calc_crc_flex(p, len);
			 count = kiss_esc_crc(p, (unsigned char *)ax->xbuff, crc, len+2);
			 break;

		default:
		         count = kiss_esc(p, (unsigned char *)ax->xbuff, len);
			 break;
		}
		ax->tty->flags |= (1 << TTY_DO_WRITE_WAKEUP);
		actual = ax->tty->driver.write(ax->tty, 0, ax->xbuff, count);
		ax->tx_packets++;
		ax->dev->trans_start = jiffies;
		ax->xleft = count - actual;
		ax->xhead = ax->xbuff + actual;
	} else {
		count = kiss_esc(p, (unsigned char *) ax->mkiss->xbuff, len);
		ax->mkiss->tty->flags |= (1 << TTY_DO_WRITE_WAKEUP);
		actual = ax->mkiss->tty->driver.write(ax->mkiss->tty, 0, ax->mkiss->xbuff, count);
		ax->tx_packets++;
		ax->mkiss->dev->trans_start = jiffies;
		ax->mkiss->xleft = count - actual;
		ax->mkiss->xhead = ax->mkiss->xbuff + actual;
	}
}

/*
 * Called by the driver when there's room for more data.  If we have
 * more packets to send, we send them here.
 */
static void ax25_write_wakeup(struct tty_struct *tty)
{
	int actual;
	struct ax_disp *ax = (struct ax_disp *)tty->disc_data;
	struct mkiss_channel *mkiss;

	/* First make sure we're connected. */
	if (ax == NULL || ax->magic != AX25_MAGIC || !ax->dev->start)
		return;
	if (ax->xleft <= 0)  {
		/* Now serial buffer is almost free & we can start
		 * transmission of another packet
		 */
		tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);

		if (ax->mkiss != NULL) {
			mkiss= ax->mkiss->tty->driver_data;
	        	if (mkiss->magic  == MKISS_DRIVER_MAGIC)
				ax_unlock(ax->mkiss);
	        }

		ax_unlock(ax);
		mark_bh(NET_BH);
		return;
	}

	actual = tty->driver.write(tty, 0, ax->xhead, ax->xleft);
	ax->xleft -= actual;
	ax->xhead += actual;
}

/* Encapsulate an AX.25 packet and kick it into a TTY queue. */
static int ax_xmit(struct sk_buff *skb, struct device *dev)
{
	struct ax_disp *ax = (struct ax_disp*)dev->priv;
	struct mkiss_channel *mkiss = ax->tty->driver_data;
	struct ax_disp *tmp_ax;

	tmp_ax = NULL;

	if (mkiss->magic  == MKISS_DRIVER_MAGIC) {
		if (skb->data[0] < 0x10)
			skb->data[0] = skb->data[0] + 0x10;
		tmp_ax = ax->mkiss;
	}

	if (!dev->start)  {
		printk(KERN_ERR "mkiss: %s: xmit call when iface is down\n", dev->name);
		return 1;
	}

	if (tmp_ax != NULL)
		if (tmp_ax->dev->tbusy)
			return 1;

	if (tmp_ax != NULL)
		if (dev->tbusy) {
			printk(KERN_ERR "mkiss: dev busy while serial dev is free\n");
			ax_unlock(ax);
	        }

	if (dev->tbusy) {
		/*
		 * May be we must check transmitter timeout here ?
		 *      14 Oct 1994 Dmitry Gorodchanin.
		 */
		if (jiffies - dev->trans_start  < 20 * HZ) {
			/* 20 sec timeout not reached */
			return 1;
		}

		printk(KERN_ERR "mkiss: %s: transmit timed out, %s?\n", dev->name,
		       (ax->tty->driver.chars_in_buffer(ax->tty) || ax->xleft) ?
		       "bad line quality" : "driver error");

		ax->xleft = 0;
		ax->tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
		ax_unlock(ax);
	}

	/* We were not busy, so we are now... :-) */
	if (skb != NULL) {
		ax_lock(ax);
		if (tmp_ax != NULL)
			ax_lock(tmp_ax);
		ax_encaps(ax, skb->data, skb->len);
		kfree_skb(skb);
	}

	return 0;
}

#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)

/* Return the frame type ID */
static int ax_header(struct sk_buff *skb, struct device *dev, unsigned short type,
	  void *daddr, void *saddr, unsigned len)
{
#ifdef CONFIG_INET
	if (type != htons(ETH_P_AX25))
		return ax25_encapsulate(skb, dev, type, daddr, saddr, len);
#endif
	return 0;
}


static int ax_rebuild_header(struct sk_buff *skb)
{
#ifdef CONFIG_INET
	return ax25_rebuild_header(skb);
#else
	return 0;
#endif
}

#endif	/* CONFIG_{AX25,AX25_MODULE} */

/* Open the low-level part of the AX25 channel. Easy! */
static int ax_open(struct device *dev)
{
	struct ax_disp *ax = (struct ax_disp*)dev->priv;
	unsigned long len;

	if (ax->tty == NULL)
		return -ENODEV;

	/*
	 * Allocate the frame buffers:
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
	if (len < 576 * 2)
		len = 576 * 2;

	if ((ax->rbuff = kmalloc(len + 4, GFP_KERNEL)) == NULL)
		goto norbuff;

	if ((ax->xbuff = kmalloc(len + 4, GFP_KERNEL)) == NULL)
		goto noxbuff;

	ax->mtu	     = dev->mtu + 73;
	ax->buffsize = len;
	ax->rcount   = 0;
	ax->xleft    = 0;

	ax->flags   &= (1 << AXF_INUSE);      /* Clear ESCAPE & ERROR flags */
	dev->tbusy  = 0;
	dev->start  = 1;

	return 0;

	/* Cleanup */
	kfree(ax->xbuff);

noxbuff:
	kfree(ax->rbuff);

norbuff:
	return -ENOMEM;
}


/* Close the low-level part of the AX25 channel. Easy! */
static int ax_close(struct device *dev)
{
	struct ax_disp *ax = (struct ax_disp*)dev->priv;

	if (ax->tty == NULL)
		return -EBUSY;

	ax->tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);

	dev->tbusy = 1;
	dev->start = 0;

	return 0;
}

static int ax25_receive_room(struct tty_struct *tty)
{
	return 65536;  /* We can handle an infinite amount of data. :-) */
}

/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of data has been received, which can now be decapsulated
 * and sent on to the AX.25 layer for further processing.
 */
static void ax25_receive_buf(struct tty_struct *tty, const unsigned char *cp, char *fp, int count)
{
	struct ax_disp *ax = (struct ax_disp *)tty->disc_data;

	if (ax == NULL || ax->magic != AX25_MAGIC || !ax->dev->start)
		return;

	/*
	 * Argh! mtu change time! - costs us the packet part received
	 * at the change
	 */
	if (ax->mtu != ax->dev->mtu + 73)
		ax_changedmtu(ax);

	/* Read the characters out of the buffer */
	while (count--) {
		if (fp != NULL && *fp++) {
			if (!test_and_set_bit(AXF_ERROR, &ax->flags))
				ax->rx_errors++;
			cp++;
			continue;
		}

		kiss_unesc(ax, *cp++);
	}
}

static int ax25_open(struct tty_struct *tty)
{
	struct ax_disp *ax = (struct ax_disp *)tty->disc_data;
	struct ax_disp *tmp_ax;
	struct mkiss_channel *mkiss;
	int err, cnt;

	/* First make sure we're not already connected. */
	if (ax && ax->magic == AX25_MAGIC)
		return -EEXIST;

	/* OK.  Find a free AX25 channel to use. */
	if ((ax = ax_alloc()) == NULL)
		return -ENFILE;

	ax->tty = tty;
	tty->disc_data = ax;

	ax->mkiss = NULL;
	tmp_ax    = NULL;

	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);

	/* Restore default settings */
	ax->dev->type = ARPHRD_AX25;

	/* Perform the low-level AX25 initialization. */
	if ((err = ax_open(ax->dev)))
		return err;

	mkiss= ax->tty->driver_data;

	if (mkiss->magic  == MKISS_DRIVER_MAGIC) {
		for (cnt = 1; cnt < ax25_maxdev; cnt++) {
			if (ax25_ctrls[cnt]) {
				if (ax25_ctrls[cnt]->dev.start) {
					if (ax == &ax25_ctrls[cnt]->ctrl) {
						cnt--;
						tmp_ax = &ax25_ctrls[cnt]->ctrl;
						break;
					}
				}
			}
		}
	}

	if (tmp_ax != NULL) {
		ax->mkiss     = tmp_ax;
		tmp_ax->mkiss = ax;
	}

	MOD_INC_USE_COUNT;

	/* Done.  We have linked the TTY line to a channel. */
	return ax->dev->base_addr;
}

static void ax25_close(struct tty_struct *tty)
{
	struct ax_disp *ax = (struct ax_disp *)tty->disc_data;
	int mkiss ;

	/* First make sure we're connected. */
	if (ax == NULL || ax->magic != AX25_MAGIC)
		return;

	mkiss = ax->mode;

	dev_close(ax->dev);

	tty->disc_data = 0;
	ax->tty        = NULL;

	/* VSV = very important to remove timers */
	ax_free(ax);
	unregister_netdev(ax->dev);

	MOD_DEC_USE_COUNT;
}


static struct net_device_stats *ax_get_stats(struct device *dev)
{
	static struct net_device_stats stats;
	struct ax_disp *ax = (struct ax_disp*)dev->priv;

	memset(&stats, 0, sizeof(struct net_device_stats));

	stats.rx_packets     = ax->rx_packets;
	stats.tx_packets     = ax->tx_packets;
	stats.rx_dropped     = ax->rx_dropped;
	stats.tx_dropped     = ax->tx_dropped;
	stats.tx_errors      = ax->tx_errors;
	stats.rx_errors      = ax->rx_errors;
	stats.rx_over_errors = ax->rx_over_errors;

	return &stats;
}


/************************************************************************
 *			   STANDARD ENCAPSULATION	        	 *
 ************************************************************************/

int kiss_esc(unsigned char *s, unsigned char *d, int len)
{
	unsigned char *ptr = d;
	unsigned char c;

	/*
	 * Send an initial END character to flush out any
	 * data that may have accumulated in the receiver
	 * due to line noise.
	 */

	*ptr++ = END;

	while (len-- > 0) {
		switch (c = *s++) {
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

	return ptr - d;
}

/*
 * MW:
 * OK its ugly, but tell me a better solution without copying the
 * packet to a temporary buffer :-)
 */
static int kiss_esc_crc(unsigned char *s, unsigned char *d, unsigned short crc, int len)
{
	unsigned char *ptr = d;
	unsigned char c;

	*ptr++ = END;
	while (len > 0) {
		if (len > 2) 
			c = *s++;
		else if (len > 1)
			c = crc >> 8;
		else if (len > 0)
			c = crc & 0xff;

		len--;

		switch (c) {
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
	return ptr - d;		
}

static void kiss_unesc(struct ax_disp *ax, unsigned char s)
{
	switch (s) {
		case END:
			/* drop keeptest bit = VSV */
			if (test_bit(AXF_KEEPTEST, &ax->flags))
				clear_bit(AXF_KEEPTEST, &ax->flags);

			if (!test_and_clear_bit(AXF_ERROR, &ax->flags) && (ax->rcount > 2))
				ax_bump(ax);

			clear_bit(AXF_ESCAPE, &ax->flags);
			ax->rcount = 0;
			return;

		case ESC:
			set_bit(AXF_ESCAPE, &ax->flags);
			return;
		case ESC_ESC:
			if (test_and_clear_bit(AXF_ESCAPE, &ax->flags))
				s = ESC;
			break;
		case ESC_END:
			if (test_and_clear_bit(AXF_ESCAPE, &ax->flags))
				s = END;
			break;
	}

	if (!test_bit(AXF_ERROR, &ax->flags)) {
		if (ax->rcount < ax->buffsize) {
			ax->rbuff[ax->rcount++] = s;
			return;
		}

		ax->rx_over_errors++;
		set_bit(AXF_ERROR, &ax->flags);
	}
}


int ax_set_mac_address(struct device *dev, void *addr)
{
	if (copy_from_user(dev->dev_addr, addr, AX25_ADDR_LEN))
		return -EFAULT;
	return 0;
}

static int ax_set_dev_mac_address(struct device *dev, void *addr)
{
	struct sockaddr *sa = addr;

	memcpy(dev->dev_addr, sa->sa_data, AX25_ADDR_LEN);

	return 0;
}


/* Perform I/O control on an active ax25 channel. */
static int ax25_disp_ioctl(struct tty_struct *tty, void *file, int cmd, void *arg)
{
	struct ax_disp *ax = (struct ax_disp *)tty->disc_data;
	unsigned int tmp;

	/* First make sure we're connected. */
	if (ax == NULL || ax->magic != AX25_MAGIC)
		return -EINVAL;

	switch (cmd) {
	 	case SIOCGIFNAME:
			if (copy_to_user(arg, ax->dev->name, strlen(ax->dev->name) + 1))
				return -EFAULT;
			return 0;

		case SIOCGIFENCAP:
			put_user(4, (int *)arg);
			return 0;

		case SIOCSIFENCAP:
			get_user(tmp, (int *)arg);
	 		ax->mode = tmp;
			ax->dev->addr_len        = AX25_ADDR_LEN;	  /* sizeof an AX.25 addr */
			ax->dev->hard_header_len = AX25_KISS_HEADER_LEN + AX25_MAX_HEADER_LEN + 3;
			ax->dev->type            = ARPHRD_AX25;
			return 0;

		 case SIOCSIFHWADDR:
			return ax_set_mac_address(ax->dev, arg);

		default:
			return -ENOIOCTLCMD;
	}
}

static int ax_open_dev(struct device *dev)
{
	struct ax_disp *ax = (struct ax_disp*)dev->priv;

	if (ax->tty==NULL)
		return -ENODEV;

	return 0;
}

/* Initialize AX25 control device -- register AX25 line discipline */
__initfunc(int mkiss_init_ctrl_dev(void))
{
	int status;

	if (ax25_maxdev < 4) ax25_maxdev = 4; /* Sanity */

	if ((ax25_ctrls = kmalloc(sizeof(void*) * ax25_maxdev, GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "mkiss: Can't allocate ax25_ctrls[] array !  No mkiss available\n");
		return -ENOMEM;
	}

	/* Clear the pointer array, we allocate devices when we need them */
	memset(ax25_ctrls, 0, sizeof(void*) * ax25_maxdev); /* Pointers */

	/* Fill in our line protocol discipline, and register it */
	memset(&ax_ldisc, 0, sizeof(ax_ldisc));
	ax_ldisc.magic  = TTY_LDISC_MAGIC;
	ax_ldisc.name   = "mkiss";
	ax_ldisc.flags  = 0;
	ax_ldisc.open   = ax25_open;
	ax_ldisc.close  = ax25_close;
	ax_ldisc.read   = NULL;
	ax_ldisc.write  = NULL;
	ax_ldisc.ioctl  = (int (*)(struct tty_struct *, struct file *, unsigned int, unsigned long))ax25_disp_ioctl;
	ax_ldisc.poll   = NULL;

	ax_ldisc.receive_buf  = ax25_receive_buf;
	ax_ldisc.receive_room = ax25_receive_room;
	ax_ldisc.write_wakeup = ax25_write_wakeup;

	if ((status = tty_register_ldisc(N_AX25, &ax_ldisc)) != 0)
		printk(KERN_ERR "mkiss: can't register line discipline (err = %d)\n", status);

	mkiss_init();

#ifdef MODULE
	return status;
#else
	/*
	 * Return "not found", so that dev_init() will unlink
	 * the placeholder device entry for us.
	 */
	return ENODEV;
#endif
}


/* Initialize the driver.  Called by network startup. */

static int ax25_init(struct device *dev)
{
	struct ax_disp *ax = (struct ax_disp*)dev->priv;

	static char ax25_bcast[AX25_ADDR_LEN] =
		{'Q'<<1,'S'<<1,'T'<<1,' '<<1,' '<<1,' '<<1,'0'<<1};
	static char ax25_test[AX25_ADDR_LEN] =
		{'L'<<1,'I'<<1,'N'<<1,'U'<<1,'X'<<1,' '<<1,'1'<<1};

	if (ax == NULL)		/* Allocation failed ?? */
		return -ENODEV;

	/* Set up the "AX25 Control Block". (And clear statistics) */
	memset(ax, 0, sizeof (struct ax_disp));
	ax->magic  = AX25_MAGIC;
	ax->dev	   = dev;

	/* Finish setting up the DEVICE info. */
	dev->mtu             = AX_MTU;
	dev->hard_start_xmit = ax_xmit;
	dev->open            = ax_open_dev;
	dev->stop            = ax_close;
	dev->get_stats	     = ax_get_stats;
#ifdef HAVE_SET_MAC_ADDR
	dev->set_mac_address = ax_set_dev_mac_address;
#endif
	dev->hard_header_len = 0;
	dev->addr_len        = 0;
	dev->type            = ARPHRD_AX25;
	dev->tx_queue_len    = 10;

	memcpy(dev->broadcast, ax25_bcast, AX25_ADDR_LEN);
	memcpy(dev->dev_addr,  ax25_test,  AX25_ADDR_LEN);

#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
	dev->hard_header     = ax_header;
	dev->rebuild_header  = ax_rebuild_header;
#endif

	dev_init_buffers(dev);

	/* New-style flags. */
	dev->flags      = 0;

	return 0;
}

static int mkiss_open(struct tty_struct *tty, struct file *filp)
{
	struct mkiss_channel *mkiss;
	int chan;

	chan = MINOR(tty->device) - tty->driver.minor_start;

	if (chan < 0 || chan >= NR_MKISS)
		return -ENODEV;

	mkiss = &MKISS_Info[chan];

	mkiss->magic =  MKISS_DRIVER_MAGIC;
	mkiss->init  = 1;
	mkiss->tty   = tty;

	tty->driver_data = mkiss;

	tty->termios->c_iflag  = IGNBRK | IGNPAR;
	tty->termios->c_cflag  = B9600 | CS8 | CLOCAL;
	tty->termios->c_cflag &= ~CBAUD;

	return 0;
}

static void mkiss_close(struct tty_struct *tty, struct file * filp)
{
	struct mkiss_channel *mkiss = tty->driver_data;

	if (mkiss == NULL || mkiss->magic != MKISS_DRIVER_MAGIC)
                return;

	mkiss->tty   = NULL;
	mkiss->init  = 0;
	tty->stopped = 0;
}

static int mkiss_write(struct tty_struct *tty, int from_user, const unsigned char *buf, int count)
{
	return 0;
}

static int mkiss_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
{
	/* Ignore serial ioctl's */
	switch (cmd) {
		case TCSBRK:
		case TIOCMGET:
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
		case TCSETS:
		case TCSETSF:		/* should flush first, but... */
		case TCSETSW:		/* should wait until flush, but... */
			return 0;
		default:
			return -ENOIOCTLCMD;
	}
}


static void mkiss_dummy(struct tty_struct *tty)
{
	struct mkiss_channel *mkiss = tty->driver_data;

	if (tty == NULL)
		return;

	if (mkiss == NULL)
		return;
}

static void mkiss_dummy2(struct tty_struct *tty, unsigned char ch)
{
	struct mkiss_channel *mkiss = tty->driver_data;

	if (tty == NULL)
		return;

	if (mkiss == NULL)
		return;
}


static int mkiss_write_room(struct tty_struct * tty)
{
	struct mkiss_channel *mkiss = tty->driver_data;

	if (tty == NULL)
		return 0;

	if (mkiss == NULL)
		return 0;

	return 65536;  /* We can handle an infinite amount of data. :-) */
}


static int mkiss_chars_in_buffer(struct tty_struct *tty)
{
	struct mkiss_channel *mkiss = tty->driver_data;

	if (tty == NULL)
		return 0;

	if (mkiss == NULL)
		return 0;

	return 0;
}


static void mkiss_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	/* we don't do termios */
}

/* ******************************************************************** */
/* * 			Init MKISS driver 			      * */
/* ******************************************************************** */

__initfunc(static int mkiss_init(void))
{
	memset(&mkiss_driver, 0, sizeof(struct tty_driver));

	mkiss_driver.magic       = MKISS_DRIVER_MAGIC;
	mkiss_driver.name        = "mkiss";
	mkiss_driver.major       = MKISS_MAJOR;
	mkiss_driver.minor_start = 0;
	mkiss_driver.num         = NR_MKISS;
	mkiss_driver.type        = TTY_DRIVER_TYPE_SERIAL;
	mkiss_driver.subtype     = MKISS_SERIAL_TYPE_NORMAL;	/* not needed */

	mkiss_driver.init_termios         = tty_std_termios;
	mkiss_driver.init_termios.c_iflag = IGNBRK | IGNPAR;
	mkiss_driver.init_termios.c_cflag = B9600 | CS8 | CLOCAL;

	mkiss_driver.flags           = TTY_DRIVER_REAL_RAW;
	mkiss_driver.refcount        = &mkiss_refcount;
	mkiss_driver.table           = mkiss_table;
	mkiss_driver.termios         = (struct termios **)mkiss_termios;
	mkiss_driver.termios_locked  = (struct termios **)mkiss_termios_locked;

	mkiss_driver.ioctl           = mkiss_ioctl;
	mkiss_driver.open            = mkiss_open;
	mkiss_driver.close           = mkiss_close;
	mkiss_driver.write           = mkiss_write;
	mkiss_driver.write_room      = mkiss_write_room;
	mkiss_driver.chars_in_buffer = mkiss_chars_in_buffer;
	mkiss_driver.set_termios     = mkiss_set_termios;

	/* some unused functions */
	mkiss_driver.flush_buffer = mkiss_dummy;
	mkiss_driver.throttle     = mkiss_dummy;
	mkiss_driver.unthrottle   = mkiss_dummy;
	mkiss_driver.stop         = mkiss_dummy;
	mkiss_driver.start        = mkiss_dummy;
	mkiss_driver.hangup       = mkiss_dummy;
	mkiss_driver.flush_chars  = mkiss_dummy;
	mkiss_driver.put_char     = mkiss_dummy2;

	if (tty_register_driver(&mkiss_driver)) {
		printk(KERN_ERR "mkiss: couldn't register Mkiss device\n");
		return -EIO;
	}

	printk(KERN_INFO "AX.25 Multikiss device enabled\n");

	return 0;
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

MODULE_PARM(ax25_maxdev, "i");
MODULE_PARM_DESC(ax25_maxdev, "number of MKISS devices");

MODULE_AUTHOR("Hans Albas PE1AYX <hans@esrac.ele.tue.nl>");
MODULE_DESCRIPTION("KISS driver for AX.25 over TTYs");

int init_module(void)
{
	return mkiss_init_ctrl_dev();
}

void cleanup_module(void)
{
	int i;

	if (ax25_ctrls != NULL) {
		for (i = 0; i < ax25_maxdev; i++) {
			if (ax25_ctrls[i]) {
				/*
				 * VSV = if dev->start==0, then device
				 * unregistred while close proc.
				 */
				if (ax25_ctrls[i]->dev.start)
					unregister_netdev(&(ax25_ctrls[i]->dev));

				kfree(ax25_ctrls[i]);
				ax25_ctrls[i] = NULL;
			}
		}

		kfree(ax25_ctrls);
		ax25_ctrls = NULL;
	}

	if ((i = tty_register_ldisc(N_AX25, NULL)))
		printk(KERN_ERR "mkiss: can't unregister line discipline (err = %d)\n", i);

	if (tty_unregister_driver(&mkiss_driver))	/* remove devive */
		printk(KERN_ERR "mkiss: can't unregister MKISS device\n");
}

#endif /* MODULE */
