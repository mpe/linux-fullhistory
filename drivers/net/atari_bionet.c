/* bionet.c     BioNet-100 device driver for linux68k.
 *
 * Version:	@(#)bionet.c	1.0	02/06/96
 *
 * Author:	Hartmut Laue <laue@ifk-mp.uni-kiel.de>
 * and		Torsten Narjes <narjes@ifk-mp.uni-kiel.de>
 *
 * Little adaptions for integration into pl7 by Roman Hodek
 *
	What is it ?
	------------
	This driver controls the BIONET-100 LAN-Adapter which connects
	an ATARI ST/TT via the ACSI-port to an Ethernet-based network.

	This version can be compiled as a loadable module (See the
	compile command at the bottom of this file).
	At load time, you can optionally set the debugging level and the
	fastest response time on the command line of 'insmod'.

	'bionet_debug'
		controls the amount of diagnostic messages:
	  0  : no messages
	  >0 : see code for meaning of printed messages

	'bionet_min_poll_time' (always >=1)
		gives the time (in jiffies) between polls. Low values
		increase the system load (beware!)

	When loaded, a net device with the name 'bio0' becomes available,
	which can be controlled with the usual 'ifconfig' command.

	It is possible to compile this driver into the kernel like other
	(net) drivers. For this purpose, some source files (e.g. config-files
	makefiles, Space.c) must be changed accordingly. (You may refer to
	other drivers how to do it.) In this case, the device will be detected
	at boot time and (probably) appear as 'eth0'.

	This code is based on several sources:
	- The driver code for a parallel port ethernet adapter by
	  Donald Becker (see file 'atp.c' from the PC linux distribution)
	- The ACSI code by Roman Hodek for the ATARI-ACSI harddisk support
	  and DMA handling.
	- Very limited information about moving packets in and out of the
	  BIONET-adapter from the TCP package for TOS by BioData GmbH.

	Theory of Operation
	-------------------
	Because the ATARI DMA port is usually shared between several
	devices (eg. harddisk, floppy) we cannot block the ACSI bus
	while waiting for interrupts. Therefore we use a polling mechanism
	to fetch packets from the adapter. For the same reason, we send
	packets without checking that the previous packet has been sent to
	the LAN. We rely on the higher levels of the networking code to detect
	missing packets and resend them.

	Before we access the ATARI DMA controller, we check if another
	process is using the DMA. If not, we lock the DMA, perform one or
	more packet transfers and unlock the DMA before returning.
	We do not use 'stdma_lock' unconditionally because it is unclear
	if the networking code can be set to sleep, which will happen if
	another (possibly slow) device is using the DMA controller.

	The polling is done via timer interrupts which periodically
	'simulate' an interrupt from the Ethernet adapter. The time (in jiffies)
	between polls varies depending on an estimate of the net activity.
	The allowed range is given by the variable 'bionet_min_poll_time'
	for the lower (fastest) limit and the constant 'MAX_POLL_TIME'
	for the higher (slowest) limit.

	Whenever a packet arrives, we switch to fastest response by setting
	the polling time to its lowest limit. If the following poll fails,
	because no packets have arrived, we increase the time for the next
	poll. When the net activity is low, the polling time effectively
	stays at its maximum value, resulting in the lowest load for the
	machine.
 */

#define MAX_POLL_TIME	10

static char *version =
	"bionet.c:v1.0 06-feb-96 (c) Hartmut Laue.\n";

#ifdef MODULE
#include <linux/module.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#include <linux/errno.h>
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
#include <linux/delay.h>
#include <linux/timer.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/setup.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_acsi.h>
#include <asm/atari_stdma.h>


extern struct device *init_etherdev(struct device *dev, int sizeof_private);

/* use 0 for production, 1 for verification, >2 for debug
 */
#ifndef NET_DEBUG
#define NET_DEBUG 0
#endif
/*
 * Global variable 'bionet_debug'. Can be set at load time by 'insmod'
 */
unsigned int bionet_debug = NET_DEBUG;
MODULE_PARM(bionet_debug, "i");

static unsigned int bionet_min_poll_time = 2;


/* Information that need to be kept for each board.
 */
struct net_local {
	struct enet_statistics stats;
	long open_time;			/* for debugging */
	int  poll_time;			/* polling time varies with net load */
};

static struct nic_pkt_s {		/* packet format */
	unsigned char	status;
	unsigned char	dummy;
	unsigned char	l_lo, l_hi;
	unsigned char	buffer[3000];
} *nic_packet;
unsigned char *phys_nic_packet;

/* Index to functions, as function prototypes.
 */
extern int bionet_probe(struct device *dev);

static int bionet_open(struct device *dev);
static int bionet_send_packet(struct sk_buff *skb, struct device *dev);
static void bionet_poll_rx(struct device *);
static int bionet_close(struct device *dev);
static struct enet_statistics *net_get_stats(struct device *dev);
static void bionet_tick(unsigned long);

static struct timer_list bionet_timer = { NULL, NULL, 0, 0, bionet_tick };

#define STRAM_ADDR(a)	(((a) & 0xff000000) == 0)

/* The following routines access the ethernet board connected to the
 * ACSI port via the st_dma chip.
 */
#define NODE_ADR 0x60

#define C_READ 8
#define C_WRITE 0x0a
#define C_GETEA 0x0f
#define C_SETCR 0x0e

static int
sendcmd(unsigned int a0, unsigned int mod, unsigned int cmd) {
	unsigned int c;

	dma_wd.dma_mode_status = (mod | ((a0) ? 2 : 0) | 0x88);
	dma_wd.fdc_acces_seccount = cmd;
	dma_wd.dma_mode_status = (mod | 0x8a);

	if( !acsi_wait_for_IRQ(HZ/2) )	/* wait for cmd ack */
		return -1;		/* timeout */

	c = dma_wd.fdc_acces_seccount;
	return (c & 0xff);
}


static void
set_status(int cr) {
	sendcmd(0,0x100,NODE_ADR | C_SETCR);    /* CMD: SET CR */
	sendcmd(1,0x100,cr);

	dma_wd.dma_mode_status = 0x80;
}

static int
get_status(unsigned char *adr) {
	int i,c;

	DISABLE_IRQ();
	c = sendcmd(0,0x00,NODE_ADR | C_GETEA);  /* CMD: GET ETH ADR*/
	if( c < 0 ) goto gsend;

	/* now read status bytes */

	for (i=0; i<6; i++) {
		dma_wd.fdc_acces_seccount = 0;	/* request next byte */

    		if( !acsi_wait_for_IRQ(HZ/2) ) {	/* wait for cmd ack */
			c = -1;
			goto gsend;		/* timeout */
		}
		c = dma_wd.fdc_acces_seccount;
		*adr++ = (unsigned char)c;
	}
	c = 1;
gsend:
  	dma_wd.dma_mode_status = 0x80;
	return c;
}

static void
bionet_intr(int irq, void *data, struct pt_regs *fp) {
	return;
}


static int
get_frame(unsigned long paddr, int odd) {
	int c;
	unsigned long flags;

	DISABLE_IRQ();
	save_flags(flags);
	cli();

	dma_wd.dma_mode_status		= 0x9a;
	dma_wd.dma_mode_status		= 0x19a;
	dma_wd.dma_mode_status		= 0x9a;
	dma_wd.fdc_acces_seccount	= 0x05;		/* sector count */
	dma_wd.dma_lo			= (unsigned char)paddr;
	paddr >>= 8;
	dma_wd.dma_md			= (unsigned char)paddr;
	paddr >>= 8;
	dma_wd.dma_hi			= (unsigned char)paddr;
	restore_flags(flags);

	c = sendcmd(0,0x00,NODE_ADR | C_READ);	/* CMD: READ */
	if( c < 128 ) goto rend;

	/* now read block */

	c = sendcmd(1,0x00,odd);	/* odd flag for address shift */
	dma_wd.dma_mode_status	= 0x0a;

	if( !acsi_wait_for_IRQ(100) ) {	/* wait for DMA to complete */
		c = -1;
		goto rend;
	}
	dma_wd.dma_mode_status	= 0x8a;
	dma_wd.dma_mode_status	= 0x18a;
	dma_wd.dma_mode_status	= 0x8a;
	c = dma_wd.fdc_acces_seccount;

	dma_wd.dma_mode_status	= 0x88;
	c = dma_wd.fdc_acces_seccount;
	c = 1;

rend:
	dma_wd.dma_mode_status	= 0x80;
	udelay(40);
	acsi_wait_for_noIRQ(20);
	return c;
}


static int
hardware_send_packet(unsigned long paddr, int cnt) {
	unsigned int c;
	unsigned long flags;

	DISABLE_IRQ();
	save_flags(flags);
	cli();

	dma_wd.dma_mode_status	= 0x19a;
	dma_wd.dma_mode_status	= 0x9a;
	dma_wd.dma_mode_status	= 0x19a;
	dma_wd.dma_lo		= (unsigned char)paddr;
	paddr >>= 8;
	dma_wd.dma_md		= (unsigned char)paddr;
	paddr >>= 8;
	dma_wd.dma_hi		= (unsigned char)paddr;

	dma_wd.fdc_acces_seccount	= 0xaa;		/* sector count */
	restore_flags(flags);

	c = sendcmd(0,0x100,NODE_ADR | C_WRITE);	/* CMD: WRITE */
	c = sendcmd(1,0x100,cnt&0xff);
	c = sendcmd(1,0x100,cnt>>8);

	/* now write block */

	dma_wd.dma_mode_status	= 0x10a;	/* DMA enable */
	if( !acsi_wait_for_IRQ(100) )		/* wait for DMA to complete */
		goto end;

	dma_wd.dma_mode_status	= 0x19a;	/* DMA disable ! */
	c = dma_wd.fdc_acces_seccount;

end:
	c = sendcmd(1,0x100,0);
	c = sendcmd(1,0x100,0);

	dma_wd.dma_mode_status	= 0x180;
	udelay(40);
	acsi_wait_for_noIRQ(20);
	return( c & 0x02);
}


/* Check for a network adaptor of this type, and return '0' if one exists.
 */
int
bionet_probe(struct device *dev) {
	unsigned char station_addr[6];
	static unsigned version_printed = 0;
	static int no_more_found = 0; /* avoid "Probing for..." printed 4 times */
	int i;

	if (!MACH_IS_ATARI || no_more_found)
		return ENODEV;

	printk("Probing for BioNet 100 Adapter...\n");

	stdma_lock(bionet_intr, NULL);
	i = get_status(station_addr);	/* Read the station address PROM.  */
	ENABLE_IRQ();
	stdma_release();

	/* Check the first three octets of the S.A. for the manufactor's code.
	 */

	if( i < 0
	||  station_addr[0] != 'B'
	||  station_addr[1] != 'I'
	||  station_addr[2] != 'O' ) {
		no_more_found = 1;
		printk( "No BioNet 100 found.\n" );
		return ENODEV;
	}


	if (dev == NULL)
		return ENODEV;
	if (bionet_debug > 0 && version_printed++ == 0)
		printk(version);

	printk("%s: %s found, eth-addr: %02x-%02x-%02x:%02x-%02x-%02x.\n",
		dev->name, "BioNet 100",
		station_addr[0], station_addr[1], station_addr[2],
		station_addr[3], station_addr[4], station_addr[5]);

	/* Initialize the device structure. */

	nic_packet = (struct nic_pkt_s *)acsi_buffer;
	phys_nic_packet = (unsigned char *)phys_acsi_buffer;
	if (bionet_debug > 0) {
		printk("nic_packet at 0x%p, phys at 0x%p\n",
			nic_packet, phys_nic_packet );
	}

	if (dev->priv == NULL)
		dev->priv = kmalloc(sizeof(struct net_local), GFP_KERNEL);
	memset(dev->priv, 0, sizeof(struct net_local));

	dev->open		= bionet_open;
	dev->stop		= bionet_close;
	dev->hard_start_xmit	= bionet_send_packet;
	dev->get_stats		= net_get_stats;

	/* Fill in the fields of the device structure with ethernet-generic
	 * values. This should be in a common file instead of per-driver.
	 */

	for (i = 0; i < ETH_ALEN; i++) {
#if 0
		dev->broadcast[i] = 0xff;
#endif
		dev->dev_addr[i]  = station_addr[i];
	}
	ether_setup(dev);
	return 0;
}

/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is run.

   This routine should set everything up anew at each open, even
   registers that "should" only need to be set once at boot, so that
   there is non-reboot way to recover if something goes wrong.
 */
static int
bionet_open(struct device *dev) {
	struct net_local *lp = (struct net_local *)dev->priv;

	if (bionet_debug > 0)
		printk("bionet_open\n");
	stdma_lock(bionet_intr, NULL);

	/* Reset the hardware here.
	 */
	set_status(4);
	lp->open_time = 0;	/*jiffies*/
	lp->poll_time = MAX_POLL_TIME;

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	stdma_release();
	bionet_timer.data = (long)dev;
	bionet_timer.expires = jiffies + lp->poll_time;
	add_timer(&bionet_timer);
	MOD_INC_USE_COUNT;
	return 0;
}

static int
bionet_send_packet(struct sk_buff *skb, struct device *dev) {
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned long flags;

	/* If some higher layer thinks we've missed an tx-done interrupt we
	 * are passed NULL. Caution: dev_tint() handles the cli()/sti() itself.
	 */
	if (skb == NULL) {
		dev_tint(dev);
		return 0;
	}
	/* Block a timer-based transmit from overlapping.  This could better be
	 * done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
	 */
	save_flags(flags);
	cli();

	if (stdma_islocked()) {
		restore_flags(flags);
		lp->stats.tx_errors++;
	}
	else {
		int length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
		unsigned long buf = VTOP(skb->data);
		int stat;

		stdma_lock(bionet_intr, NULL);
		restore_flags(flags);
		if( !STRAM_ADDR(buf+length-1) ) {
			memcpy(nic_packet->buffer, skb->data, length);
			buf = (unsigned long)&((struct nic_pkt_s *)phys_nic_packet)->buffer;
		}

		dma_cache_maintenance(buf, length, 1);

		stat = hardware_send_packet(buf, length);
		ENABLE_IRQ();
		stdma_release();

		dev->trans_start = jiffies;
		dev->tbusy	 = 0;
	}
	dev_kfree_skb(skb, FREE_WRITE);

	/* You might need to clean up and record Tx statistics here.
	 */
	lp->stats.tx_packets++;
	return 0;
}

/* We have a good packet(s), get it/them out of the buffers.
 */
static void
bionet_poll_rx(struct device *dev) {
	struct net_local *lp = (struct net_local *)dev->priv;
	int boguscount = 10;
	int pkt_len, status;
	unsigned long flags;

	save_flags(flags);
	cli();
	/* ++roman: Take care at locking the ST-DMA... This must be done with ints
	 * off, since otherwise an int could slip in between the question and the
	 * locking itself, and then we'd go to sleep... And locking itself is
	 * necessary to keep the floppy_change timer from working with ST-DMA
	 * registers. */
	if (stdma_islocked()) {
		restore_flags(flags);
		return;
	}
	stdma_lock(bionet_intr, NULL);
	DISABLE_IRQ();
	restore_flags(flags);

	if( lp->poll_time < MAX_POLL_TIME ) lp->poll_time++;

	while(boguscount--) {
		status = get_frame((unsigned long)phys_nic_packet, 0);

		if( status != 1 ) break;

		/* Good packet... */

		dma_cache_maintenance((unsigned long)phys_nic_packet, 1520, 0);

		pkt_len = (nic_packet->l_hi << 8) | nic_packet->l_lo;

		lp->poll_time = bionet_min_poll_time;    /* fast poll */
		if( pkt_len >= 60 && pkt_len <= 1514 ) {

			/* Malloc up new buffer.
			 */
			struct sk_buff *skb = alloc_skb(pkt_len, GFP_ATOMIC);
			if (skb == NULL) {
				printk("%s: Memory squeeze, dropping packet.\n",
					dev->name);
				lp->stats.rx_dropped++;
				break;
			}
			skb->len = pkt_len;
			skb->dev = dev;

			/* 'skb->data' points to the start of sk_buff data area.
			 */
			memcpy(skb->data, nic_packet->buffer, pkt_len);
			netif_rx(skb);
			lp->stats.rx_packets++;
		}
	}

	/* If any worth-while packets have been received, dev_rint()
	   has done a mark_bh(INET_BH) for us and will work on them
	   when we get to the bottom-half routine.
	 */

	stdma_release();
	ENABLE_IRQ();
	return;
}

/* bionet_tick: called by bionet_timer. Reads packets from the adapter,
 * passes them to the higher layers and restarts the timer.
 */
static void
bionet_tick(unsigned long data) {
	struct device	 *dev = (struct device *)data;
	struct net_local *lp = (struct net_local *)dev->priv;

	if( bionet_debug > 0 && (lp->open_time++ & 7) == 8 )
		printk("bionet_tick: %ld\n", lp->open_time);

	if( !stdma_islocked() ) bionet_poll_rx(dev);

	bionet_timer.expires = jiffies + lp->poll_time;
	add_timer(&bionet_timer);
}

/* The inverse routine to bionet_open().
 */
static int
bionet_close(struct device *dev) {
	struct net_local *lp = (struct net_local *)dev->priv;

	if (bionet_debug > 0)
		printk("bionet_close, open_time=%ld\n", lp->open_time);
	del_timer(&bionet_timer);
	stdma_lock(bionet_intr, NULL);

	set_status(0);
	lp->open_time = 0;

	dev->tbusy = 1;
	dev->start = 0;

	stdma_release();
	MOD_DEC_USE_COUNT;
	return 0;
}

/* Get the current statistics.
   This may be called with the card open or closed.
 */
static struct enet_statistics *
net_get_stats(struct device *dev) {
	struct net_local *lp = (struct net_local *)dev->priv;
	return &lp->stats;
}


#ifdef MODULE

#include <linux/version.h>

/* We should include the kernel identification string in the module.
 */
static char kernel_version[] = UTS_RELEASE;

#undef	NEXT_DEV
#define NEXT_DEV	(&bio_dev)

static struct device bio_dev =
	{
		"        ",	/* filled in by register_netdev() */
		0, 0, 0, 0,	/* memory */
		0, 0,		/* base, irq */
		0, 0, 0, NEXT_DEV, bionet_probe,
	};

int
init_module(void) {
	int err;

	if ((err = register_netdev(&bio_dev))) {
		if (err == -EEXIST)  {
			printk("BIONET: devices already present. Module not loaded.\n");
		}
		return err;
	}
	return 0;
}

void
cleanup_module(void) {
	unregister_netdev(&bio_dev);
}

#endif /* MODULE */

/* Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include
	-b m68k-linuxaout -Wall -Wstrict-prototypes -O2
	-fomit-frame-pointer -pipe -DMODULE -I../../net/inet -c bionet.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 8
 * End:
 */
