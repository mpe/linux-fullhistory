/* $Id: wic.c,v 1.0 1995/02/11 10:26:05 hayes Exp $ */
/* WIC: A parallel port "network" driver for Linux. */
/* based on the plip network driver */
/* Modified for Linux 1.3.x by Alan Cox <Alan.Cox@linux.org> */

char *version = "NET3 WIC version 0.9 hayes@netplumbing.com";

/*
  Sources:
	Ideas and protocols came from Russ Nelson's <nelson@crynwr.com>
	"parallel.asm" parallel port packet driver and from the plip.c
	parallel networking linux driver from the 1.2.13 Linux
	distribution.

  The packet is encapsulated as if it were ethernet.

*/

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/if_ether.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/lp.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_wic.h>

#include <linux/tqueue.h>
#include <linux/ioport.h>
#include <asm/bitops.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <linux/string.h>

#define NET_DEBUG 1
/* Use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 1
#endif
unsigned int net_debug = NET_DEBUG;

/* Connection time out = WIC_TRIGGER_WAIT * WIC_DELAY_UNIT usec */
#define WIC_TRIGGER_WAIT	 500

/* Nibble time out = WIC_NIBBLE_WAIT * WIC_DELAY_UNIT usec */
#define WIC_NIBBLE_WAIT        3000

#define PAR_DATA(dev)		((dev)->base_addr+0)
#define PAR_STATUS(dev)		((dev)->base_addr+1)
#define PAR_CONTROL(dev)	((dev)->base_addr+2)

/* Bottom halfs */
void wic_kick_bh(struct device *dev);
void wic_bh(struct device *dev);

/* Interrupt handler */
void wic_interrupt(int irq, void *dev_ptr, struct pt_regs *regs);

/* Functions for DEV methods */
int wic_rebuild_header(void *buff, struct device *dev,
			       unsigned long raddr, struct sk_buff *skb);
int wic_tx_packet(struct sk_buff *skb, struct device *dev);
int wic_open(struct device *dev);
int wic_close(struct device *dev);
struct enet_statistics *wic_get_stats(struct device *dev);
int wic_config(struct device *dev, struct ifmap *map);
int wic_ioctl(struct device *dev, struct ifreq *ifr, int cmd);
int send_cmd(struct device *dev, unsigned char *cmd, char len);
int recv_cmd_resp(struct device *dev, unsigned char *cmd);
int send_byte(struct device *dev, unsigned char c);
int get_byte(struct device *dev, unsigned char *c);
int ack_resp(struct device *dev);
int check_bfr(struct device *dev);
void wic_reset(struct device *dev);
void wic_set_multicast_list(struct device *dev);

#define LOOPCNT 30000	
unsigned char tog = 3;
unsigned char save = 0;

enum wic_connection_state {
	WIC_CN_NONE=0,
	WIC_CN_RECEIVE,
	WIC_CN_SEND,
	WIC_CN_CLOSING,
	WIC_CN_ERROR
};

enum wic_packet_state {
	WIC_PK_DONE=0,
	WIC_PK_TRIGGER,
	WIC_PK_LENGTH_LSB,
	WIC_PK_LENGTH_MSB,
	WIC_PK_DATA,
	WIC_PK_CHECKSUM
};

enum wic_nibble_state {
	WIC_NB_BEGIN,
	WIC_NB_1,
	WIC_NB_2,
};

struct wic_local {
	enum wic_packet_state state;
	enum wic_nibble_state nibble;
	union {
		struct {
#if defined(__LITTLE_ENDIAN)
			unsigned char lsb;
			unsigned char msb;
#elif defined(__BIG_ENDIAN)
			unsigned char msb;
			unsigned char lsb;
#else
#error	"Please fix the endianness defines in <asm/byteorder.h>"
#endif						
		} b;
		unsigned short h;
	} length;
	unsigned short byte;
	unsigned char  checksum;
	unsigned char  data;
	struct sk_buff *skb;
};

struct net_local {
	struct enet_statistics enet_stats;
	struct tq_struct immediate;
	struct tq_struct deferred;
	struct wic_local snd_data;
	struct wic_local rcv_data;
	unsigned long  trigger;
	unsigned long  nibble;
	enum wic_connection_state connection;
	unsigned short timeout_count;
	char is_deferred;
	int (*orig_rebuild_header)(void *eth, struct device *dev,
				   unsigned long raddr, struct sk_buff *skb);
};

/* Entry point of WIC driver.
   Probe the hardware, and register/initialize the driver. */
int
wic_init(struct device *dev)
{
	struct net_local *nl;
	struct wicconf wc;
	int i;

	/* Check region before the probe */
	if (check_region(PAR_DATA(dev), 3) < 0)
		return -ENODEV;
	
	/* Check that there is something at base_addr. */
	outb(0, PAR_DATA(dev));
	udelay(1000);
	if (inb(PAR_DATA(dev)) != 0)
		return -ENODEV;

	printk("%s\n",version);
	printk("%s: Parallel port at %#3lx, ", dev->name, dev->base_addr);
	if (dev->irq) {
		printk("using assigned IRQ %d.\n", dev->irq);
	} else {
		int irq = 0;
#ifdef MODULE
		/* dev->irq==0 means autoprobe, but we don't try to do so
		   with module.  We can change it by ifconfig */
#else
		unsigned int irqs = probe_irq_on();

		outb(0x00, PAR_CONTROL(dev));
		udelay(1000);
		udelay(1000);
		irq = probe_irq_off(irqs);
#endif
		if (irq > 0) {
			dev->irq = irq;
			printk("using probed IRQ %d.\n", dev->irq);
		} else
			printk("failed to detect IRQ(%d) --"
			       " Please set IRQ by ifconfig.\n", irq);
	}

	request_region(PAR_DATA(dev), 3, dev->name);

	/* Fill in the generic fields of the device structure. */
	ether_setup(dev);

	/* Then, override parts of it */
	dev->hard_start_xmit	= wic_tx_packet;
	dev->open		= wic_open;
	dev->stop		= wic_close;
	dev->get_stats 		= wic_get_stats;
	dev->set_config		= wic_config;
	dev->do_ioctl		= wic_ioctl;
	dev->mtu		= 1514;
	dev->set_multicast_list = wic_set_multicast_list;
	dev->flags	        = IFF_BROADCAST | IFF_RUNNING | IFF_NOTRAILERS;

	/* get the MAC address from the controller */
	wc.len = 1;
	wc.pcmd = WIC_GETNET;
	check_bfr(dev);
	send_cmd(dev, (unsigned char *)&wc, 1);
	wc.len = recv_cmd_resp(dev, (unsigned char *)&wc.data);
	while ((wc.len == 1) && (wc.data[0] == 0x7)) /* controller int */
		wc.len = recv_cmd_resp(dev, (unsigned char *)&wc.data);
	
	printk("%s:MAC address: ",dev->name);	
	for (i=0; i < ETH_ALEN ; i++) {
		dev->dev_addr[i] = wc.data[i];
		printk("%2x ",dev->dev_addr[i]);
	}
	printk("\n");

	/* Set the private structure */
	dev->priv = kmalloc(sizeof (struct net_local), GFP_KERNEL);
	if (dev->priv == NULL)
		return EAGAIN;
	memset(dev->priv, 0, sizeof(struct net_local));
	nl = (struct net_local *) dev->priv;

	nl->orig_rebuild_header = dev->rebuild_header;
	dev->rebuild_header 	= wic_rebuild_header;

	/* Initialize constants */
	nl->trigger	= WIC_TRIGGER_WAIT;
	nl->nibble	= WIC_NIBBLE_WAIT;

	/* Initialize task queue structures */
	nl->immediate.next = NULL;
	nl->immediate.sync = 0;
	nl->immediate.routine = (void *)(void *)wic_bh;
	nl->immediate.data = dev;

	nl->deferred.next = NULL;
	nl->deferred.sync = 0;
	nl->deferred.routine = (void *)(void *)wic_kick_bh;
	nl->deferred.data = dev;

	return 0;
}

/* Bottom half handler for the delayed request.
   This routine is kicked by do_timer().
   Request `wic_bh' to be invoked. */
void
wic_kick_bh(struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;

	if (nl->is_deferred) {
		queue_task(&nl->immediate, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}
}

/* Forward declarations of internal routines */
int wic_none(struct device *, struct net_local *,
		     struct wic_local *, struct wic_local *);
int wic_receive_packet(struct device *, struct net_local *,
			       struct wic_local *, struct wic_local *);
int wic_send_packet(struct device *, struct net_local *,
			    struct wic_local *, struct wic_local *);
int wic_connection_close(struct device *, struct net_local *,
				 struct wic_local *, struct wic_local *);
int wic_error(struct device *, struct net_local *,
		      struct wic_local *, struct wic_local *);
int wic_bh_timeout_error(struct device *dev, struct net_local *nl,
				 struct wic_local *snd,
				 struct wic_local *rcv,
				 int error);

#define OK        0
#define TIMEOUT   1
#define ERROR     2

typedef int (*wic_func)(struct device *dev, struct net_local *nl,
			 struct wic_local *snd, struct wic_local *rcv);

wic_func connection_state_table[] =
{
	wic_none,
	wic_receive_packet,
	wic_send_packet,
	wic_connection_close,
	wic_error
};

void 
wic_set_multicast_list(struct device *dev)
{
	struct wicconf wc;
	struct wic_net *wn;
	
	disable_irq(dev->irq);
	save &= 0xef; /* disable */
	outb(save, PAR_CONTROL(dev));
	
	wc.len = 1;
	wc.pcmd = WIC_GETNET;
	check_bfr(dev);
	tog = 3;
	send_cmd(dev, (unsigned char *)&wc, 1);
	wc.len = recv_cmd_resp(dev, (unsigned char *)&wc.data);
	while ((wc.len == 1) && (wc.data[0] == 0x7)) /* controller int */
		wc.len = recv_cmd_resp(dev, (unsigned char *)&wc.data);
	wn = (struct wic_net *)&wc.data;
	if(dev->flags&IFF_PROMISC)
	{
		/* promiscuous mode */
		wn->mode |= (NET_MODE_ME | NET_MODE_BCAST | 
			NET_MODE_MCAST | NET_MODE_PROM);
		printk("%s: Setting promiscuous mode\n", dev->name);
	}
	else if((dev->flags&IFF_ALLMULTI) || dev->mc_count)
	{
		wn->mode &= ~NET_MODE_PROM;
		wn->mode |= (NET_MODE_MCAST | NET_MODE_ME | NET_MODE_BCAST);
	}
	else
	{
		wn->mode &= ~(NET_MODE_PROM | NET_MODE_MCAST);
		wn->mode |= (NET_MODE_ME | NET_MODE_BCAST);
	}
	wc.len = 23;
	wc.pcmd = WIC_SETNET;
	check_bfr(dev);
	tog = 3;
	send_cmd(dev, (unsigned char *)&wc, wc.len);

	save |= 0x10; /* enable */
	outb(save, PAR_CONTROL(dev));
	enable_irq(dev->irq);
	return;
}

/* Bottom half handler of WIC. */
void
wic_bh(struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;
	struct wic_local *snd = &nl->snd_data;
	struct wic_local *rcv = &nl->rcv_data;
	wic_func f;
	int r;

	nl->is_deferred = 0;
	f = connection_state_table[nl->connection];
	if ((r = (*f)(dev, nl, snd, rcv)) != OK
	    && (r = wic_bh_timeout_error(dev, nl, snd, rcv, r)) != OK) {
		nl->is_deferred = 1;
		queue_task(&nl->deferred, &tq_timer);
	}
}

int
wic_bh_timeout_error(struct device *dev, struct net_local *nl,
		      struct wic_local *snd, struct wic_local *rcv,
		      int error)
{
	unsigned char c0;
	unsigned long flags;

	save_flags(flags);
	cli();
	if (nl->connection == WIC_CN_SEND) {

		if (error != ERROR) { /* Timeout */
			nl->timeout_count++;
			if ((snd->state == WIC_PK_TRIGGER
			     && nl->timeout_count <= 10)
			    || nl->timeout_count <= 3) {
				restore_flags(flags);
				/* Try again later */
				return TIMEOUT;
			}
			c0 = inb(PAR_STATUS(dev));
			printk("%s: transmit timeout(%d,%02x)\n",
			       dev->name, snd->state, c0);
		}
		nl->enet_stats.tx_errors++;
		nl->enet_stats.tx_aborted_errors++;
	} else if (nl->connection == WIC_CN_RECEIVE) {
		if (rcv->state == WIC_PK_TRIGGER) {
			/* Transmission was interrupted. */
			restore_flags(flags);
			return OK;
		}
		if (error != ERROR) { /* Timeout */
			if (++nl->timeout_count <= 3) {
				restore_flags(flags);
				/* Try again later */
				return TIMEOUT;
			}
			c0 = inb(PAR_STATUS(dev));
			printk("%s: receive timeout(%d,%02x)\n",
			       dev->name, rcv->state, c0);
		}
		nl->enet_stats.rx_dropped++;
	}
	rcv->state = WIC_PK_DONE;
	if (rcv->skb) {
		rcv->skb->free = 1;
		kfree_skb(rcv->skb, FREE_READ);
		rcv->skb = NULL;
	}
	snd->state = WIC_PK_DONE;
	if (snd->skb) {
		snd->skb->free = 1;
		dev_kfree_skb(snd->skb, FREE_WRITE);
		snd->skb = NULL;
	}
#if (0)
	disable_irq(dev->irq);
	save &= 0xef; /* disable */
	outb(save, PAR_CONTROL(dev));
	dev->tbusy = 1;
	outb(0x00, PAR_DATA(dev));
#endif /* (0) */
	nl->connection = WIC_CN_ERROR;
	restore_flags(flags);

	return TIMEOUT;
}

int
wic_none(struct device *dev, struct net_local *nl,
	  struct wic_local *snd, struct wic_local *rcv)
{
	return OK;
}

/* WIC_RECEIVE --- receive a byte(two nibbles)
   Returns OK on success, TIMEOUT on timeout */
inline int
wic_receive(unsigned short nibble_timeout, unsigned short status_addr,
	     enum wic_nibble_state *ns_p, unsigned char *data_p)
{
unsigned int cx;

	cx = LOOPCNT;
	while ((inb(status_addr) & 0x08) != ((tog<<3) & 0x08)) {
		if (--cx == 0) {
			return TIMEOUT;
		}
	}
	*data_p = inb(status_addr-1);
	tog ^= 0x01;
	outb(tog| save, status_addr+1);
	return OK;
}

/* WIC_RECEIVE_PACKET --- receive a packet */
int
wic_receive_packet(struct device *dev, struct net_local *nl,
		    struct wic_local *snd, struct wic_local *rcv)
{
	unsigned short status_addr = PAR_STATUS(dev);
	unsigned short nibble_timeout = nl->nibble;
	unsigned char *lbuf;
	unsigned char junk;
	unsigned long flags;

	save_flags(flags);
	cli();
	switch (rcv->state) {
	case WIC_PK_TRIGGER:
		disable_irq(dev->irq);
		save &= 0xef; /* disable */
		outb(save, PAR_CONTROL(dev));
		
		dev->interrupt = 0;

		tog &= 0xfe;
		ack_resp(dev);
		if (net_debug > 2)
			printk("%s: receive start\n", dev->name);
		rcv->state = WIC_PK_LENGTH_LSB;
		rcv->nibble = WIC_NB_BEGIN;

	case WIC_PK_LENGTH_LSB:
		if (net_debug > 2)
			printk("%s: WIC_PK_LENGTH_LSB\n", dev->name);
		if (snd->state != WIC_PK_DONE) {
			if (wic_receive(nl->trigger, status_addr,
					 &rcv->nibble, &rcv->length.b.lsb)) {
				/* collision, here dev->tbusy == 1 */
				rcv->state = WIC_PK_DONE;
				nl->is_deferred = 1;
				nl->connection = WIC_CN_SEND;
				restore_flags(flags);
				queue_task(&nl->deferred, &tq_timer);
				save |= 0x10; /* enable */
				outb(save, PAR_CONTROL(dev));
				enable_irq(dev->irq);
				return OK;
			}
		} else {
			if (wic_receive(nibble_timeout, status_addr,
					 &rcv->nibble, &rcv->length.b.lsb)) {
				restore_flags(flags);
				return TIMEOUT;
			}
		}
		rcv->state = WIC_PK_LENGTH_MSB;

	case WIC_PK_LENGTH_MSB:
		if (net_debug > 2)
			printk("%s: WIC_PK_LENGTH_MSB\n", dev->name);
		if (wic_receive(nibble_timeout, status_addr,
				 &rcv->nibble, &rcv->length.b.msb)) {
			restore_flags(flags);
			return TIMEOUT;
		}
		if (rcv->length.h > dev->mtu || rcv->length.h < 8) {
			printk("%s: bad packet size %d.\n", dev->name, rcv->length.h);
			restore_flags(flags);
			return ERROR;
		}
		/* Malloc up new buffer. */
		rcv->skb = dev_alloc_skb(rcv->length.h);
		if (rcv->skb == NULL) {
			printk("%s: Memory squeeze.\n", dev->name);
			restore_flags(flags);
			return ERROR;
		}
		skb_put(rcv->skb,rcv->length.h);
		rcv->skb->dev = dev;
		
		rcv->state = WIC_PK_DATA;
		rcv->byte = 0;
		rcv->checksum = 0;
		
		/* sequence numbers */
		if (net_debug > 2)
			printk("%s: WIC_PK_SEQ\n", dev->name);
		if (wic_receive(nibble_timeout, status_addr,
				 &rcv->nibble, &junk)) {
			restore_flags(flags);
			return TIMEOUT;
		}
		if (wic_receive(nibble_timeout, status_addr,
				 &rcv->nibble, &junk)) {
			restore_flags(flags);
			return TIMEOUT;
		}

	case WIC_PK_DATA:
		if (net_debug > 2)
			printk("%s: WIC_PK_DATA: length %i\n", dev->name, 
				rcv->length.h);
		lbuf = rcv->skb->data;
		do {
			if (wic_receive(nibble_timeout, status_addr, 
					 &rcv->nibble, &lbuf[rcv->byte])) {
				restore_flags(flags);
				return TIMEOUT;
			}
		} while (++rcv->byte < (rcv->length.h - 4));

		/* receive pad byte */
		if (rcv->length.h & 0x01)
			wic_receive(nibble_timeout, status_addr, 
					 &rcv->nibble, &lbuf[rcv->byte]);
		
		do {
			rcv->checksum += lbuf[--rcv->byte];
		} while (rcv->byte);

		rcv->state = WIC_PK_CHECKSUM;

	case WIC_PK_CHECKSUM:
		if (net_debug > 2)
			printk("%s: WIC_PK_CHECKSUM\n", dev->name);
		if (wic_receive(nibble_timeout, status_addr,
				 &rcv->nibble, &junk)) {
			restore_flags(flags);
			return TIMEOUT;
		}
		outb(0, PAR_DATA(dev));
		rcv->state = WIC_PK_DONE;

	case WIC_PK_DONE:
		if (net_debug > 2)
			printk("%s: WIC_PK_DONE\n", dev->name);
		/* Inform the upper layer for the arrival of a packet. */
		netif_rx(rcv->skb);
		nl->enet_stats.rx_packets++;
		rcv->skb = NULL;
		if (net_debug > 2)
			printk("%s: receive end\n", dev->name);

		/* Close the connection. */
		if (snd->state != WIC_PK_DONE) {
			nl->connection = WIC_CN_SEND;
			restore_flags(flags);
			queue_task(&nl->immediate, &tq_immediate);
			save |= 0x10; /* enable */
			outb(save, PAR_CONTROL(dev));
			enable_irq(dev->irq);
			return OK;
		} else {
			nl->connection = WIC_CN_NONE;
			restore_flags(flags);
			save |= 0x10; /* enable */
			outb(save, PAR_CONTROL(dev));
			enable_irq(dev->irq);
			return OK;
		}
	}
	restore_flags(flags);
	return OK;
}

/* WIC_SEND --- send a byte (two nibbles) 
   Returns OK on success, TIMEOUT when timeout    */
inline int
wic_send(unsigned short nibble_timeout, unsigned short data_addr,
	  enum wic_nibble_state *ns_p, unsigned char data)
{
unsigned int cx;

	cx = LOOPCNT;
	while ((inb(data_addr+1) & 0x80) == ((tog<<7) & 0x80)) {
		if (--cx == 0) {
			return -TIMEOUT;
		}
	}
	outb(data, data_addr);
	outb(tog | save, data_addr+2);
	tog ^= 0x01;
	return OK;
}

/* WIC_SEND_PACKET --- send a packet */
int
wic_send_packet(struct device *dev, struct net_local *nl,
		 struct wic_local *snd, struct wic_local *rcv)
{
	unsigned short data_addr = PAR_DATA(dev);
	unsigned short nibble_timeout = nl->nibble;
	unsigned char *lbuf;
	unsigned int cx;
	unsigned int pad = 2;
	unsigned long flags;

	if (snd->skb == NULL || (lbuf = snd->skb->data) == NULL) {
		printk("%s: send skb lost\n", dev->name);
		snd->state = WIC_PK_DONE;
		snd->skb = NULL;
		save |= 0x10; /* enable */
		outb(save, PAR_CONTROL(dev));
		enable_irq(dev->irq);
		return ERROR;
	}

	save_flags(flags);	
	cli();
	switch (snd->state) {
	case WIC_PK_TRIGGER:
	
		if (nl->connection == WIC_CN_RECEIVE) {
			/* interrupted */
			nl->enet_stats.collisions++;
			restore_flags(flags);
			if (net_debug > 1)
				printk("%s: collision.\n", dev->name);
			save |= 0x10; /* enable */
			outb(save, PAR_CONTROL(dev));
			enable_irq(dev->irq);
			return OK;
		}
		
		disable_irq(dev->irq);
		save &= 0xef; /* disable */
		outb(save, PAR_CONTROL(dev));
		
		/* interrupt controller */
		tog = 3;
		outb(0x06 | save, PAR_CONTROL(dev));
			
		cx = LOOPCNT;
		while ((inb(PAR_STATUS(dev)) & 0xe8) != 0xc0) {
			if (--cx == 0) {
				restore_flags(flags);
				return TIMEOUT;
			}
			if (cx == 10)
				outb(0x02, PAR_CONTROL(dev));
		}
		
		if (net_debug > 2)
			printk("%s: send start\n", dev->name);
		snd->state = WIC_PK_LENGTH_LSB;
		snd->nibble = WIC_NB_BEGIN;
		nl->timeout_count = 0;

	case WIC_PK_LENGTH_LSB:
		if (snd->length.h & 0x01)
			pad = 3;
		else
			pad = 2;
		snd->length.h += (4 + pad); /* len + seq + data + pad */
		if (net_debug > 2)
			printk("%s: WIC_PK_LENGTH_LSB: length = %i\n", 
				dev->name, snd->length.h);

		if (wic_send(nibble_timeout, data_addr,
			      &snd->nibble, snd->length.b.lsb)) {
			restore_flags(flags);
			return TIMEOUT;
		}
		snd->state = WIC_PK_LENGTH_MSB;

	case WIC_PK_LENGTH_MSB:
		if (net_debug > 2)
			printk("%s: WIC_PK_LENGTH_MSB\n", dev->name);
		if (wic_send(nibble_timeout, data_addr,
			      &snd->nibble, snd->length.b.msb)) {
			restore_flags(flags);
			return TIMEOUT;
		}
		snd->state = WIC_PK_DATA;
		snd->byte = 0;
		snd->checksum = 0;

	case WIC_PK_DATA:
		/* adjust length back to data only */
		snd->length.h -= (4 + pad); /* len + seq + data + pad */
		/* send 2 byte sequence number */
		if (net_debug > 2)
			printk("%s: WIC_SEQ\n", dev->name);
		if (wic_send(nibble_timeout, data_addr,
			      &snd->nibble, 0)) {
			restore_flags(flags);
			return TIMEOUT;
		}
		if (wic_send(nibble_timeout, data_addr,
			      &snd->nibble, 0)) {
			restore_flags(flags);
			return TIMEOUT;
		}	
		if (net_debug > 2)
			printk("%s: WIC_PK_DATA\n", dev->name);

		do {
			if (wic_send(nibble_timeout, data_addr,
				      &snd->nibble, lbuf[snd->byte])) {
				restore_flags(flags);
				return TIMEOUT;
			}
		}
		while (++snd->byte < snd->length.h);
		
		do
			snd->checksum += lbuf[--snd->byte];
		while (snd->byte);

		snd->state = WIC_PK_CHECKSUM;

	case WIC_PK_CHECKSUM:
		/* send pad bytes */
		if (net_debug > 2)
			printk("%s: WIC_PK_PAD: %i bytes\n", 
				dev->name, pad);
		while(pad--)
			if (wic_send(nibble_timeout, data_addr,
			      	&snd->nibble, 0)) {
				restore_flags(flags);
				return TIMEOUT;
			}
		dev_kfree_skb(snd->skb, FREE_WRITE);
		nl->enet_stats.tx_packets++;
		snd->state = WIC_PK_DONE;

	case WIC_PK_DONE:
		if (net_debug > 2)
			printk("%s: WIC_PK_DONE\n", dev->name);
		/* Close the connection */
		outb (0x00, PAR_DATA(dev));
		outb(save, PAR_CONTROL(dev));
		
		snd->skb = NULL;
		if (net_debug > 2)
			printk("%s: send end\n", dev->name);
		nl->connection = WIC_CN_CLOSING;
		nl->is_deferred = 1;
		restore_flags(flags);
		queue_task(&nl->deferred, &tq_timer);
		save |= 0x10; /* enable */
		outb(save, PAR_CONTROL(dev));
		enable_irq(dev->irq);
		return OK;
	}
	restore_flags(flags);
	return OK;
}

int
wic_connection_close(struct device *dev, struct net_local *nl,
		      struct wic_local *snd, struct wic_local *rcv)
{
unsigned long flags;

	save_flags(flags);
	cli();
	if (nl->connection == WIC_CN_CLOSING) {
		nl->connection = WIC_CN_NONE;
		dev->tbusy = 0;
		mark_bh(NET_BH);
	}
	restore_flags(flags);
	return OK;
}

/* WIC_ERROR --- wait till other end settled */
int
wic_error(struct device *dev, struct net_local *nl,
	   struct wic_local *snd, struct wic_local *rcv)
{
	unsigned char status;

	status = inb(PAR_STATUS(dev));
	if ((status & 0xf8) == 0x80) {
		if (net_debug > 2)
			printk("%s: reset interface.\n", dev->name);
		nl->connection = WIC_CN_NONE;
		dev->tbusy = 0;
		dev->interrupt = 0;
		save |= 0x10; /* enable */
		outb(save, PAR_CONTROL(dev));
		enable_irq(dev->irq);
		mark_bh(NET_BH);
	} else {
		nl->is_deferred = 1;
		queue_task(&nl->deferred, &tq_timer);
	}

	return OK;
}

/* Handle the parallel port interrupts. */
void
wic_interrupt(int irq, void *dev_ptr, struct pt_regs * regs)
{
	struct device *dev = (struct device *) irq2dev_map[irq];
	struct net_local *nl = (struct net_local *)dev->priv;
	struct wic_local *rcv = &nl->rcv_data;
	unsigned long flags;

	if (dev == NULL) {
		printk ("wic_interrupt: irq %d for unknown device.\n", irq);
		return;
	}

	if (dev->interrupt) {
		return;
	}

	if (check_bfr(dev) < 0) {
		return;
	}
	
	dev->interrupt = 1;
	if (net_debug > 3)
		printk("%s: interrupt.\n", dev->name);

	save_flags(flags);
	cli();
	switch (nl->connection) {
	case WIC_CN_CLOSING:
		dev->tbusy = 0;
	case WIC_CN_NONE:
	case WIC_CN_SEND:
		dev->last_rx = jiffies;
		rcv->state = WIC_PK_TRIGGER;
		nl->connection = WIC_CN_RECEIVE;
		nl->timeout_count = 0;
		restore_flags(flags);
		queue_task(&nl->immediate, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
		break;

	case WIC_CN_RECEIVE:
		printk("%s: receive interrupt when receiving packet\n", dev->name);
		restore_flags(flags);
		break;

	case WIC_CN_ERROR:
		printk("%s: receive interrupt in error state\n", dev->name);
		restore_flags(flags);
		break;
	}
}

int
wic_rebuild_header(void *buff, struct device *dev, unsigned long dst,
		    struct sk_buff *skb)
{
	struct net_local *nl = (struct net_local *)dev->priv;
	struct ethhdr *eth = (struct ethhdr *)buff;
	int i;

	if ((dev->flags & IFF_NOARP)==0)
		return nl->orig_rebuild_header(buff, dev, dst, skb);

	if (eth->h_proto != htons(ETH_P_IP)) {
		printk("wic_rebuild_header: Don't know how to resolve type %d addresses?\n", (int)eth->h_proto);
		memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
		return 0;
	}

	for (i=0; i < ETH_ALEN - sizeof(unsigned long); i++)
		eth->h_dest[i] = 0xfc;
	memcpy(&(eth->h_dest[i]), &dst, sizeof(unsigned long));
	return 0;
}

int
wic_tx_packet(struct sk_buff *skb, struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;
	struct wic_local *snd = &nl->snd_data;
	unsigned long flags;

	if (dev->tbusy)
		return 1;

	/* If some higher layer thinks we've missed an tx-done interrupt
	   we are passed NULL. Caution: dev_tint() handles the cli()/sti()
	   itself. */
	if (skb == NULL) {
		dev_tint(dev);
		return 0;
	}

	if (set_bit(0, (void*)&dev->tbusy) != 0) {
		printk("%s: Transmitter access conflict.\n", dev->name);
		return 1;
	}

	if (skb->len > dev->mtu) {
		printk("%s: packet too big, %d.\n", dev->name, (int)skb->len);
		dev->tbusy = 0;
		return 0;
	}

	if (net_debug > 2)
		printk("%s: send request\n", dev->name);

	save_flags(flags);
	cli();
	dev->trans_start = jiffies;
	snd->skb = skb;
	snd->length.h = skb->len;
	snd->state = WIC_PK_TRIGGER;
	if (nl->connection == WIC_CN_NONE) {
		nl->connection = WIC_CN_SEND;
		nl->timeout_count = 0;
	}
	restore_flags(flags);
	queue_task(&nl->immediate, &tq_immediate);
	mark_bh(IMMEDIATE_BH);

	return 0;
}

/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is run.

   This routine gets exclusive access to the parallel port by allocating
   its IRQ line.
 */
int
wic_open(struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;
	unsigned long flags;

	if (dev->irq == 0) {
		printk("%s: IRQ is not set.  Please set it by ifconfig.\n", dev->name);
		return -EAGAIN;
	}
	save_flags(flags);
	cli();
	check_bfr(dev);
	if (request_irq(dev->irq , wic_interrupt, 0, dev->name, NULL) != 0) {
		sti();
		printk("%s: couldn't get IRQ %d.\n", dev->name, dev->irq);
		return -EAGAIN;
	}
	irq2dev_map[dev->irq] = dev;
	restore_flags(flags);

	save |= 0x10; /* enable */
	outb(save, PAR_CONTROL(dev));
	/* Initialize the state machine. */
	nl->rcv_data.state = nl->snd_data.state = WIC_PK_DONE;
	nl->rcv_data.skb = nl->snd_data.skb = NULL;
	nl->connection = WIC_CN_NONE;
	nl->is_deferred = 0;

	dev->interrupt = 0;
	dev->start = 1;
	dev->tbusy = 0;
	MOD_INC_USE_COUNT;
	return 0;
}

/* The inverse routine to wic_open (). */
int
wic_close(struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;
	struct wic_local *snd = &nl->snd_data;
	struct wic_local *rcv = &nl->rcv_data;

	dev->tbusy = 1;
	dev->start = 0;
	cli();
	free_irq(dev->irq, NULL);
	irq2dev_map[dev->irq] = NULL;
	nl->is_deferred = 0;
	nl->connection = WIC_CN_NONE;
	sti();
	outb(0x00, PAR_DATA(dev));

	snd->state = WIC_PK_DONE;
	if (snd->skb) {
		snd->skb->free = 1;
		dev_kfree_skb(snd->skb, FREE_WRITE);
		snd->skb = NULL;
	}
	rcv->state = WIC_PK_DONE;
	if (rcv->skb) {
		rcv->skb->free = 1;
		kfree_skb(rcv->skb, FREE_READ);
		rcv->skb = NULL;
	}

	MOD_DEC_USE_COUNT;
	return 0;
}

struct enet_statistics *
wic_get_stats(struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;
	struct enet_statistics *r = &nl->enet_stats;

	return r;
}

int
wic_config(struct device *dev, struct ifmap *map)
{
	if (dev->flags & IFF_UP)
		return -EBUSY;

	if (map->base_addr != (unsigned long)-1
	    && map->base_addr != dev->base_addr)
		printk("%s: You cannot change base_addr of this interface (ignored).\n", dev->name);

	if (map->irq != (unsigned char)-1)
		dev->irq = map->irq;
	return 0;
}

int
wic_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
	struct wicconf wc;
	int err;
	char len = 0;
	unsigned long flags;

	err=verify_area(VERIFY_WRITE, rq->ifr_data, sizeof(struct wicconf));
	if (err)
		return err;
	memcpy_fromfs(&wc, rq->ifr_data, sizeof(struct wicconf));
	switch(wc.pcmd) {
		case WIC_AYT:
			strcpy(wc.data, version);
			wc.len = strlen(wc.data);
			memcpy_tofs(rq->ifr_data, &wc, sizeof(struct wicconf));
			/* return 0; */
			break;
		case WIC_RESET:
			wic_reset(dev);
			return(0);
			/* break; */
		case WIC_SETSN:
			len = 17;
			break;
		case WIC_SETPS:
			len = 3;
			break;
		case WIC_SETAF:
		case WIC_SETGPF:
			len = 2;
			break;
		case WIC_SETNET:
			len = 23;
			break;
		case WIC_SETSYS:
			len = 15;
			break;
		case WIC_GETVERH:
		case WIC_GETNL:
		case WIC_GETSN:
		case WIC_CLRSTATS:
		case WIC_GETSTATS:
		case WIC_GETVERM:
		case WIC_GETNET:
		case WIC_GETSYS:
			len = 1;
			break;	
		default:
			return -EOPNOTSUPP;
	}

	/* Wait for lock to free */
      	while (set_bit(0, (void *)&dev->tbusy) != 0); 
	save_flags(flags);
	cli();

	disable_irq(dev->irq);
	save &= 0xef; /* disable */
	outb(save, PAR_CONTROL(dev));
	err = check_bfr(dev);
	tog = 3;
	err = send_cmd(dev, (unsigned char *)&wc, len);

	if (wc.pcmd & 0x40) {	/* response */
		len = (char)recv_cmd_resp(dev, wc.data);
		while ((len == 1) && (wc.data[0] == 0x7)) { /* controller int */
			len = (char)recv_cmd_resp(dev, wc.data);
		}
		save |= 0x10; /* enable */
		outb(save, PAR_CONTROL(dev));
		enable_irq(dev->irq);
		wc.len = (len <0) ? 0 : len;
		memcpy_tofs(rq->ifr_data, &wc, sizeof(struct wicconf));
	} else {
		save |= 0x10; /* enable */
		outb(save, PAR_CONTROL(dev));
		enable_irq(dev->irq);
	}
	restore_flags(flags);

	outb(0, PAR_DATA(dev));
	dev->tbusy = 0;
	return 0;
}

int
get_byte(struct device *dev, unsigned char *c)
{
unsigned int cx;

	cx = LOOPCNT;
	while ((inb(PAR_STATUS(dev)) & 0x08) != ((tog << 3)&0x08)) {
		if (--cx == 0) {
			return(-TIMEOUT);
		}
	}
	/* receive a byte of data */
	*c = inb(PAR_DATA(dev));
	tog ^= 0x01;
	/* ack reception of data */
	outb(tog| save, PAR_CONTROL(dev));
	return OK;
}

int
ack_resp(struct device *dev)
{
unsigned int cx;
	
	outb(save | 0x27, PAR_CONTROL(dev));

	/* wait for controller to remove interrupt [Ack(low), Busy(low)] */
	cx = LOOPCNT;
	while ((inb(PAR_STATUS(dev)) & 0xc0) != 0x80) {
		if (--cx == 0) {
			return -TIMEOUT;
		}
	}
	
	outb(save | 0x22, PAR_CONTROL(dev));
	cx = LOOPCNT;
	while ((inb(PAR_STATUS(dev)) & 0x08) == 0x08) {
		if (--cx == 0) {
			return TIMEOUT;
		}
	}
	tog |= 0x20;
	tog &= 0xfe;
	return OK;
}

void
wic_reset(struct device *dev)
{
unsigned char stat;

	stat = inb(PAR_CONTROL(dev));
	outb(0, PAR_DATA(dev));
	outb(stat | 0x08, PAR_CONTROL(dev));
	outb(stat & 0xf7, PAR_CONTROL(dev));
	dev->tbusy = 0;
	dev->interrupt = 0;
	tog = 3;
	save = 0;
	return;
}

int
check_bfr(struct device *dev)
{
unsigned char c0, l;

	if ((inb(PAR_STATUS(dev)) & 0xc8) == 0x48) {
		save |= 0x80;
		outb(0x23| save, PAR_CONTROL(dev));
		ack_resp(dev);
		get_byte(dev, &l);	/* len */
		while (l--) {
			get_byte(dev, &c0);
		}
		get_byte(dev, &c0);
		save &=0x7f;
		outb(0, PAR_DATA(dev));
		return -l;
	} else
	return (0);
}


int
recv_cmd_resp(struct device *dev, unsigned char *buf)
{
unsigned char cksum = 0;
int err;
unsigned char c0 = 0;
int len;
int savelen;
unsigned int cx;
int i;

	tog &= 0xfe;
	cx = LOOPCNT;
	while ((inb(PAR_STATUS(dev)) & 0xc8) != 0x48) {
		if (--cx == 0) {
			/* clear Busy */
			outb(0, PAR_DATA(dev));
			printk("rcv_cmd_resp: timeout\n");
			return -TIMEOUT;
		}
	}
	
	/* acknowledge the interrupt */
	i = ack_resp(dev);

	/* get length */
	err = get_byte(dev, &c0);
	if (err < 0) {
		printk("get_byte1: failed\n");
		return(err);
	}
	len = c0;
	savelen = len;

	/* get data */
	while(len--) {
		err = get_byte(dev, &c0);
		if (err < 0) {
			printk("get_byte2: failed\n");
			return(err);
		}
		outb(0, PAR_DATA(dev));	
		*buf = c0;
		cksum += c0;
		buf++;
	}	
	/* get cksum */
	err = get_byte(dev, &c0);
	if (err < 0) {
		printk("get_byte3: failed\n");
		return(err);
	}
	if (cksum != c0) {
		printk("cksum failed\n");
		return(-3);
	}
	/* get trailing byte, if any... */
	get_byte(dev, &c0);
	return(savelen);
}	

int
send_byte(struct device *dev, unsigned char c)
{
unsigned int cx;

	cx = LOOPCNT;
	while ((inb(PAR_STATUS(dev)) & 0x80) == ((tog<<7) & 0x80)) {
		if (--cx == 0) {
			return(-TIMEOUT);
		}
	}
	outb(c, PAR_DATA(dev));
	outb(save |tog, PAR_CONTROL(dev));
	tog ^= 0x01;
	return OK;
}


int
send_cmd(struct device *dev, unsigned char *cmd, char len)
{
unsigned char cksum = 0;
int err = 0;
unsigned int cx;

	/* interrupt controller */
	outb(save | 0x04, PAR_CONTROL(dev));
	/* wait for ACK */
	cx = LOOPCNT;
	while ((inb(PAR_STATUS(dev)) & 0xe8) != 0xc0) {
		if (--cx == 0) 
			return -TIMEOUT;
		if (cx == 10)
			outb(0x02, PAR_CONTROL(dev));
	}
	/* cmd coming... */
	outb(save | 0x02, PAR_CONTROL(dev));
	/* send length byte */
	err = send_byte(dev, (unsigned char)len);
	
	/* send data */
	while (len--) {
		err = send_byte(dev, *cmd);	
		if (err < 0) {
			return err;
		}
		cksum += *cmd;
		cmd++;
	}
	
	/* send cksum byte */
	err = send_byte(dev, cksum);	
	if (err < 0)
		return err;

	cx = LOOPCNT;
	while ((inb(PAR_STATUS(dev)) & 0x80) == ((tog <<7)&0x80)) {
		if (--cx == 0) 
			return -TIMEOUT;
	}
	save |= 0x80;
	outb(save | 0x23, PAR_CONTROL(dev));
	outb(0, PAR_DATA(dev));
	return OK;	
}

#ifdef MODULE
struct device dev_wic0 = 
{
	"wic0" /*"wic"*/,
	0, 0, 0, 0,		/* memory */
	0x3BC, 5,		/* base, irq */
	0, 0, 0, NULL, wic_init 
};

struct device dev_wic1 = 
{
	"wic1" /*"wic"*/,
	0, 0, 0, 0,		/* memory */
	0x378, 7,		/* base, irq */
	0, 0, 0, NULL, wic_init 
};

struct device dev_wic2 = 
{
	"wic2" /*"wic"*/,
	0, 0, 0, 0,		/* memory */
	0x278, 2,		/* base, irq */
	0, 0, 0, NULL, wic_init 
};

int
init_module(void)
{
	int devices=0;

	if (register_netdev(&dev_wic0) != 0)
		devices++;
	if (register_netdev(&dev_wic1) != 0)
		devices++;
	if (register_netdev(&dev_wic2) != 0)
		devices++;
	if (devices == 0)
		return -EIO;
	return 0;
}

void
cleanup_module(void)
{
	if (dev_wic0.priv) {
		unregister_netdev(&dev_wic0);
		release_region(PAR_DATA(&dev_wic0), 3);
		kfree_s(dev_wic0.priv, sizeof(struct net_local));
		dev_wic0.priv = NULL;
	}
	if (dev_wic1.priv) {
		unregister_netdev(&dev_wic1);
		release_region(PAR_DATA(&dev_wic1), 3);
		kfree_s(dev_wic1.priv, sizeof(struct net_local));
		dev_wic1.priv = NULL;
	}
	if (dev_wic2.priv) {
		unregister_netdev(&dev_wic2);
		release_region(PAR_DATA(&dev_wic2), 3);
		kfree_s(dev_wic2.priv, sizeof(struct net_local));
		dev_wic2.priv = NULL;
	}
}
#endif /* MODULE */

/*
 * Local variables:
 * compile-command: "gcc -DMODULE -DCONFIG_MODVERSIONS -D__KERNEL__ -Wall -Wstrict-prototypes -O2 -g -fomit-frame-pointer -pipe -m486 -c wic.c"
 * End:
 */
