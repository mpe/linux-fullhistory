/*
 * linux/drivers/net/am79c961.c
 *
 * Derived from various things including skeleton.c
 *
 * R.M.King 1995.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/ecard.h>

#define TX_BUFFERS 15
#define RX_BUFFERS 25

#include "am79c961a.h"

static int am79c961_probe1 (struct net_device *dev);
static int am79c961_open (struct net_device *dev);
static int am79c961_sendpacket (struct sk_buff *skb, struct net_device *dev);
static void am79c961_interrupt (int irq, void *dev_id, struct pt_regs *regs);
static void am79c961_rx (struct net_device *dev, struct dev_priv *priv);
static void am79c961_tx (struct net_device *dev, struct dev_priv *priv);
static int am79c961_close (struct net_device *dev);
static struct enet_statistics *am79c961_getstats (struct net_device *dev);
static void am79c961_setmulticastlist (struct net_device *dev);
static void am79c961_timeout(struct net_device *dev);

static unsigned int net_debug = NET_DEBUG;

static void
am79c961_setmulticastlist (struct net_device *dev);

static char *version = "am79c961 ethernet driver (c) 1995 R.M.King v0.01\n";

#define FUNC_PROLOGUE \
	struct dev_priv *priv = (struct dev_priv *)dev->priv

/* --------------------------------------------------------------------------- */

#ifdef __arm__
static void
write_rreg (unsigned long base, unsigned int reg, unsigned short val)
{
	__asm__("str%?h	%1, [%2]	@ NET_RAP
		str%?h	%0, [%2, #-4]	@ NET_RDP
		" : : "r" (val), "r" (reg), "r" (0xf0000464));
}

static inline void
write_ireg (unsigned long base, unsigned int reg, unsigned short val)
{
	__asm__("str%?h	%1, [%2]	@ NET_RAP
		str%?h	%0, [%2, #8]	@ NET_RDP
		" : : "r" (val), "r" (reg), "r" (0xf0000464));
}

#define am_writeword(dev,off,val)\
	__asm__("str%?h	%0, [%1]" : : \
		"r" ((val) & 0xffff), "r" (0xe0000000 + ((off) << 1)));

static inline void
am_writebuffer(struct net_device *dev, unsigned int offset, unsigned char *buf, unsigned int length)
{
	offset = 0xe0000000 + (offset << 1);
	length = (length + 1) & ~1;
	if ((int)buf & 2) {
		__asm__ __volatile__("str%?h	%2, [%0], #4"
		 : "=&r" (offset) : "0" (offset), "r" (buf[0] | (buf[1] << 8)));
		buf += 2;
		length -= 2;
	}
	while (length > 8) {
		unsigned int tmp, tmp2;
		__asm__ __volatile__("
			ldm%?ia	%1!, {%2, %3}
			str%?h	%2, [%0], #4
			mov%?	%2, %2, lsr #16
			str%?h	%2, [%0], #4
			str%?h	%3, [%0], #4
			mov%?	%3, %3, lsr #16
			str%?h	%3, [%0], #4
		" : "=&r" (offset), "=&r" (buf), "=r" (tmp), "=r" (tmp2)
		  : "0" (offset), "1" (buf));
		length -= 8;
	}
	while (length > 0) {
		__asm__ __volatile__("str%?h	%2, [%0], #4"
		 : "=&r" (offset) : "0" (offset), "r" (buf[0] | (buf[1] << 8)));
		buf += 2;
		length -= 2;
	}
}

static inline unsigned short
read_rreg (unsigned int base_addr, unsigned int reg)
{
	unsigned short v;
	__asm__("str%?h	%1, [%2]	@ NET_RAP
		ldr%?h	%0, [%2, #-4]	@ NET_IDP
		" : "=r" (v): "r" (reg), "r" (0xf0000464));
	return v;
}

static inline unsigned short
am_readword (struct net_device *dev, unsigned long off)
{
	unsigned long address = 0xe0000000 + (off << 1);
	unsigned short val;

	__asm__("ldr%?h	%0, [%1]" : "=r" (val): "r" (address));
	return val;
}

static inline void
am_readbuffer(struct net_device *dev, unsigned int offset, unsigned char *buf, unsigned int length)
{
	offset = 0xe0000000 + (offset << 1);
	length = (length + 1) & ~1;
	if ((int)buf & 2) {
		unsigned int tmp;
		__asm__ __volatile__("
			ldr%?h	%2, [%0], #4
			str%?b	%2, [%1], #1
			mov%?	%2, %2, lsr #8
			str%?b	%2, [%1], #1
		" : "=&r" (offset), "=&r" (buf), "=r" (tmp): "0" (offset), "1" (buf));
		length -= 2;
	}
	while (length > 8) {
		unsigned int tmp, tmp2, tmp3;
		__asm__ __volatile__("
			ldr%?h	%2, [%0], #4
			ldr%?h	%3, [%0], #4
			orr%?	%2, %2, %3, lsl #16
			ldr%?h	%3, [%0], #4
			ldr%?h	%4, [%0], #4
			orr%?	%3, %3, %4, lsl #16
			stm%?ia	%1!, {%2, %3}
		" : "=&r" (offset), "=&r" (buf), "=r" (tmp), "=r" (tmp2), "=r" (tmp3)
		  : "0" (offset), "1" (buf));
		length -= 8;
	}
	while (length > 0) {
		unsigned int tmp;
		__asm__ __volatile__("
			ldr%?h	%2, [%0], #4
			str%?b	%2, [%1], #1
			mov%?	%2, %2, lsr #8
			str%?b	%2, [%1], #1
		" : "=&r" (offset), "=&r" (buf), "=r" (tmp) : "0" (offset), "1" (buf));
		length -= 2;
	}
}
#else
#error Not compatable
#endif

static int
am79c961_ramtest(struct net_device *dev, unsigned int val)
{
	unsigned char *buffer = kmalloc (65536, GFP_KERNEL);
	int i, error = 0, errorcount = 0;

	if (!buffer)
		return 0;
	memset (buffer, val, 65536);
	am_writebuffer(dev, 0, buffer, 65536);
	memset (buffer, val ^ 255, 65536);
	am_readbuffer(dev, 0, buffer, 65536);
	for (i = 0; i < 65536; i++) {
		if (buffer[i] != val && !error) {
			printk ("%s: buffer error (%02X %02X) %05X - ", dev->name, val, buffer[i], i);
			error = 1;
			errorcount ++;
		} else if (error && buffer[i] == val) {
			printk ("%05X\n", i);
			error = 0;
		}
	}
	if (error)
		printk ("10000\n");
	kfree (buffer);
	return errorcount;
}

static void
am79c961_init_for_open(struct net_device *dev)
{
	struct dev_priv *priv = (struct dev_priv *)dev->priv;
	unsigned long hdr_addr, first_free_addr;
	unsigned long flags;
	unsigned char *p;
	int i;

	save_flags_cli (flags);

	write_ireg (dev->base_addr, 2, 0x4000); /* autoselect media */
	write_rreg (dev->base_addr, CSR0, CSR0_BABL|CSR0_CERR|CSR0_MISS|CSR0_MERR|CSR0_TINT|CSR0_RINT|CSR0_STOP);

	restore_flags (flags);

	first_free_addr = RX_BUFFERS * 8 + TX_BUFFERS * 8 + 16;
	hdr_addr = 0;

	priv->rxhead = 0;
	priv->rxtail = 0;
	priv->rxhdr = hdr_addr;

	for (i = 0; i < RX_BUFFERS; i++) {
		priv->rxbuffer[i] = first_free_addr;
		am_writeword (dev, hdr_addr, first_free_addr);
		am_writeword (dev, hdr_addr + 2, RMD_OWN);
		am_writeword (dev, hdr_addr + 4, (-1600));
		am_writeword (dev, hdr_addr + 6, 0);
		first_free_addr += 1600;
		hdr_addr += 8;
	}
	priv->txhead = 0;
	priv->txtail = 0;
	priv->txhdr = hdr_addr;
	for (i = 0; i < TX_BUFFERS; i++) {
		priv->txbuffer[i] = first_free_addr;
		am_writeword (dev, hdr_addr, first_free_addr);
		am_writeword (dev, hdr_addr + 2, 0);
		am_writeword (dev, hdr_addr + 4, 0);
		am_writeword (dev, hdr_addr + 6, 0);
		first_free_addr += 1600;
		hdr_addr += 8;
	}

	for (i = LADRL; i <= LADRH; i++)
		write_rreg (dev->base_addr, i, 0);

	for (i = PADRL, p = dev->dev_addr; i <= PADRH; i++, p += 2)
		write_rreg (dev->base_addr, i, p[0] | (p[1] << 8));

	i = MODE_PORT0;
	if (dev->flags & IFF_PROMISC)
		i |= MODE_PROMISC;

	write_rreg (dev->base_addr, MODE, i);
	write_rreg (dev->base_addr, BASERXL, priv->rxhdr);
	write_rreg (dev->base_addr, BASERXH, 0);
	write_rreg (dev->base_addr, BASETXL, priv->txhdr);
	write_rreg (dev->base_addr, BASERXH, 0);
	write_rreg (dev->base_addr, POLLINT, 0);
	write_rreg (dev->base_addr, SIZERXR, -RX_BUFFERS);
	write_rreg (dev->base_addr, SIZETXR, -TX_BUFFERS);
	write_rreg (dev->base_addr, CSR0, CSR0_STOP);
	write_rreg (dev->base_addr, CSR3, CSR3_IDONM|CSR3_BABLM|CSR3_DXSUFLO);
	write_rreg (dev->base_addr, CSR0, CSR0_IENA|CSR0_STRT);
}

static int
am79c961_init(struct net_device *dev)
{
	unsigned long flags;

	am79c961_ramtest(dev, 0x66);
	am79c961_ramtest(dev, 0x99);

	save_flags_cli (flags);

	write_ireg (dev->base_addr, 2, 0x4000); /* autoselect media */
	write_rreg (dev->base_addr, CSR0, CSR0_STOP);
	write_rreg (dev->base_addr, CSR3, CSR3_MASKALL);

	restore_flags (flags);

	return 0;
}

/*
 * This is the real probe routine.
 */
static int
am79c961_probe1(struct net_device *dev)
{
	static unsigned version_printed = 0;
	struct dev_priv *priv;
	int i;

	if (!dev->priv) {
		dev->priv = kmalloc (sizeof (struct dev_priv), GFP_KERNEL);
		if (!dev->priv)
			return -ENOMEM;
	}

	priv = (struct dev_priv *) dev->priv;
	memset (priv, 0, sizeof(struct dev_priv));

	/*
	 * The PNP initialisation should have been done by the ether bootp loader.
	 */
	inb((dev->base_addr + NET_RESET) >> 1);	/* reset the device */

	udelay (5);

	if (inb (dev->base_addr >> 1) != 0x08 ||
	    inb ((dev->base_addr >> 1) + 1) != 00 ||
	    inb ((dev->base_addr >> 1) + 2) != 0x2b) {
		kfree (dev->priv);
		dev->priv = NULL;
		return -ENODEV;
	}

	/*
	 * Ok, we've found a valid hw ID
	 */

	if (net_debug  &&  version_printed++ == 0)
		printk (KERN_INFO "%s", version);

	printk(KERN_INFO "%s: am79c961 found [%04lx, %d] ", dev->name, dev->base_addr, dev->irq);
	request_region (dev->base_addr, 0x18, "am79c961");

	/* Retrive and print the ethernet address. */
	for (i = 0; i < 6; i++) {
		dev->dev_addr[i] = inb ((dev->base_addr >> 1) + i) & 0xff;
		printk (i == 5 ? "%02x\n" : "%02x:", dev->dev_addr[i]);
	}

	if (am79c961_init(dev)) {
		kfree (dev->priv);
		dev->priv = NULL;
		return -ENODEV;
	}

	dev->open = am79c961_open;
	dev->stop = am79c961_close;
	dev->hard_start_xmit = am79c961_sendpacket;
	dev->get_stats = am79c961_getstats;
	dev->set_multicast_list = am79c961_setmulticastlist;
	dev->tx_timeout = am79c961_timeout;

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);

	return 0;
}

int
am79c961_probe(struct net_device *dev)
{
	static int initialised = 0;

	if (initialised)
		return -ENODEV;
	initialised = 1;

	dev->base_addr = 0x220;
	dev->irq = 3;

	return am79c961_probe1(dev);
}

/*
 * Open/initialize the board.  This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
static int
am79c961_open(struct net_device *dev)
{
	struct dev_priv *priv = (struct dev_priv *)dev->priv;

	MOD_INC_USE_COUNT;

	memset (&priv->stats, 0, sizeof (priv->stats));

	if (request_irq(dev->irq, am79c961_interrupt, 0, "am79c961", dev)) {
		MOD_DEC_USE_COUNT;
		return -EAGAIN;
	}

	am79c961_init_for_open(dev);

	netif_start_queue(dev);

	return 0;
}

/*
 * The inverse routine to am79c961_open().
 */
static int
am79c961_close(struct net_device *dev)
{
	netif_stop_queue(dev);

	am79c961_init(dev);

	free_irq (dev->irq, dev);

	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * Get the current statistics.	This may be called with the card open or
 * closed.
 */
static struct enet_statistics *am79c961_getstats (struct net_device *dev)
{
	struct dev_priv *priv = (struct dev_priv *)dev->priv;
	return &priv->stats;
}

static inline u32 update_crc(u32 crc, u8 byte)
{
	int i;

	for (i = 8; i != 0; i--) {
		byte ^= crc & 1;
		crc >>= 1;

		if (byte & 1)
			crc ^= 0xedb88320;

		byte >>= 1;
	}

	return crc;
}

static void am79c961_mc_hash(struct dev_mc_list *dmi, unsigned short *hash)
{
	if (dmi->dmi_addrlen == ETH_ALEN && dmi->dmi_addr[0] & 0x01) {
		int i, idx, bit;
		u32 crc;

		crc = 0xffffffff;

		for (i = 0; i < ETH_ALEN; i++)
			crc = update_crc(crc, dmi->dmi_addr[i]);

		idx = crc >> 30;
		bit = (crc >> 26) & 15;

		hash[idx] |= 1 << bit;
	}
}

/*
 * Set or clear promiscuous/multicast mode filter for this adaptor.
 */
static void am79c961_setmulticastlist (struct net_device *dev)
{
	unsigned long flags;
	unsigned short multi_hash[4], mode;
	int i, stopped;

	mode = MODE_PORT0;

	if (dev->flags & IFF_PROMISC) {
		mode |= MODE_PROMISC;
	} else if (dev->flags & IFF_ALLMULTI) {
		memset(multi_hash, 0xff, sizeof(multi_hash));
	} else {
		struct dev_mc_list *dmi;

		memset(multi_hash, 0x00, sizeof(multi_hash));

		for (dmi = dev->mc_list; dmi; dmi = dmi->next)
			am79c961_mc_hash(dmi, multi_hash);
	}

	save_flags_cli(flags);

	stopped = read_rreg(dev->base_addr, CSR0) & CSR0_STOP;

	if (!stopped) {
		/*
		 * Put the chip into suspend mode
		 */
		write_rreg(dev->base_addr, CTRL1, CTRL1_SPND);

		/*
		 * Spin waiting for chip to report suspend mode
		 */
		while ((read_rreg(dev->base_addr, CTRL1) & CTRL1_SPND) == 0) {
			restore_flags(flags);
			nop();
			save_flags_cli(flags);
		}
	}

	/*
	 * Update the multicast hash table
	 */
	for (i = 0; i < sizeof(multi_hash) / sizeof(multi_hash[0]); i++)
		write_rreg(dev->base_addr, i + LADRL, multi_hash[i]);

	/*
	 * Write the mode register
	 */
	write_rreg(dev->base_addr, MODE, mode);

	if (!stopped) {
		/*
		 * Put the chip back into running mode
		 */
		write_rreg(dev->base_addr, CTRL1, 0);
	}

	restore_flags(flags);
}

static void am79c961_timeout(struct net_device *dev)
{
	printk(KERN_WARNING "%s: transmit timed out, network cable problem?\n",
		dev->name);

	/*
	 * ought to do some setup of the tx side here
	 */

	netif_wake_queue(dev);
}

/*
 * Transmit a packet
 */
static int
am79c961_sendpacket(struct sk_buff *skb, struct net_device *dev)
{
	struct dev_priv *priv = (struct dev_priv *)dev->priv;
	unsigned int length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	unsigned int hdraddr, bufaddr;
	unsigned int head;
	unsigned long flags;

	head = priv->txhead;
	hdraddr = priv->txhdr + (head << 3);
	bufaddr = priv->txbuffer[head];
	head += 1;
	if (head >= TX_BUFFERS)
		head = 0;

	am_writebuffer (dev, bufaddr, skb->data, length);
	am_writeword (dev, hdraddr + 4, -length);
	am_writeword (dev, hdraddr + 2, TMD_OWN|TMD_STP|TMD_ENP);
	priv->txhead = head;

	save_flags_cli (flags);
	write_rreg (dev->base_addr, CSR0, CSR0_TDMD|CSR0_IENA);
	dev->trans_start = jiffies;
	restore_flags (flags);

	if (!(am_readword (dev, priv->txhdr + (priv->txhead << 3) + 2) & TMD_OWN))
		netif_stop_queue(dev);

	dev_kfree_skb(skb);

	return 0;
}

static void
am79c961_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct dev_priv *priv = (struct dev_priv *)dev->priv;
	unsigned int status;

#if NET_DEBUG > 1
	if(net_debug & DEBUG_INT)
		printk(KERN_DEBUG "am79c961irq: %d ", irq);
#endif

	status = read_rreg (dev->base_addr, CSR0);
	write_rreg (dev->base_addr, CSR0, status & (CSR0_TINT|CSR0_RINT|CSR0_MISS|CSR0_IENA));

	if (status & CSR0_RINT) /* Got a packet(s). */
		am79c961_rx (dev, priv);
	if (status & CSR0_TINT) /* Packets transmitted */
		am79c961_tx (dev, priv);
	if (status & CSR0_MISS)
		priv->stats.rx_dropped ++;

#if NET_DEBUG > 1
	if(net_debug & DEBUG_INT)
		printk("done\n");
#endif
}

/*
 * If we have a good packet(s), get it/them out of the buffers.
 */
static void
am79c961_rx(struct net_device *dev, struct dev_priv *priv)
{
	unsigned long hdraddr;
	unsigned long pktaddr;

	do {
		unsigned long status;
		struct sk_buff *skb;
		int len;

		hdraddr = priv->rxhdr + (priv->rxtail << 3);
		pktaddr = priv->rxbuffer[priv->rxtail];

		status = am_readword (dev, hdraddr + 2);
		if (status & RMD_OWN) /* do we own it? */
			break;

		priv->rxtail ++;
		if (priv->rxtail >= RX_BUFFERS)
			priv->rxtail = 0;

		if ((status & (RMD_ERR|RMD_STP|RMD_ENP)) != (RMD_STP|RMD_ENP)) {
			am_writeword (dev, hdraddr + 2, RMD_OWN);
			priv->stats.rx_errors ++;
			if (status & RMD_ERR) {
				if (status & RMD_FRAM)
					priv->stats.rx_frame_errors ++;
				if (status & RMD_CRC)
					priv->stats.rx_crc_errors ++;
			} else if (status & RMD_STP)
				priv->stats.rx_length_errors ++;
			continue;
		}

		len = am_readword (dev, hdraddr + 6);
		skb = dev_alloc_skb (len + 2);

		if (skb) {
			unsigned char *buf;

			skb->dev = dev;
			skb_reserve (skb, 2);
			buf = skb_put (skb, len);

			am_readbuffer (dev, pktaddr, buf, len);
			am_writeword (dev, hdraddr + 2, RMD_OWN);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx (skb);
			priv->stats.rx_packets ++;
		} else {
			am_writeword (dev, hdraddr + 2, RMD_OWN);
			printk (KERN_WARNING "%s: memory squeeze, dropping packet.\n", dev->name);
			priv->stats.rx_dropped ++;
			break;
		}
	} while (1);
}

/*
 * Update stats for the transmitted packet
 */
static void
am79c961_tx(struct net_device *dev, struct dev_priv *priv)
{
	do {
		unsigned long hdraddr;
		unsigned long status;

		hdraddr = priv->txhdr + (priv->txtail << 3);
		status = am_readword (dev, hdraddr + 2);
		if (status & TMD_OWN)
			break;

		priv->txtail ++;
		if (priv->txtail >= TX_BUFFERS)
			priv->txtail = 0;

		if (status & TMD_ERR) {
			unsigned long status2;

			priv->stats.tx_errors ++;

			status2 = am_readword (dev, hdraddr + 6);
			am_writeword (dev, hdraddr + 6, 0);

			if (status2 & TST_RTRY)
				priv->stats.collisions += 16;
			if (status2 & TST_LCOL)
				priv->stats.tx_window_errors ++;
			if (status2 & TST_LCAR)
				priv->stats.tx_carrier_errors ++;
			if (status2 & TST_UFLO)
				priv->stats.tx_fifo_errors ++;
			continue;
		}
		priv->stats.tx_packets ++;
	} while (priv->txtail != priv->txhead);

	netif_wake_queue(dev);
}
