/* arcnet.c
	Written 1994-95 by Avery Pennarun, derived from skeleton.c by
        Donald Becker.

	Contact Avery at: apenwarr@foxnet.net or
	RR #5 Pole Line Road, Thunder Bay, ON, Canada P7C 5M9
	
	**********************

	skeleton.c Written 1993 by Donald Becker.
	Copyright 1993 United States Government as represented by the
        Director, National Security Agency.  This software may only be used
        and distributed according to the terms of the GNU Public License as
        modified by SRC, incorporated herein by reference.
         
	**********************

	v1.02 (95/06/21)
	  - A fix to make "exception" packets sent from Linux receivable
	    on other systems.  (The protocol_id byte was sometimes being set
	    incorrectly, and Linux wasn't checking it on receive so it
	    didn't show up)
	  - Updated my email address.  Please use apenwarr@foxnet.net
	    from now on.
	v1.01 (95/03/24)
	  - Fixed some IPX-related bugs. (Thanks to Tomasz Motylewski
            <motyl@tichy.ch.uj.edu.pl> for the patches to make arcnet work
            with dosemu!)
	v1.0 (95/02/15)
	  - Initial non-alpha release.
	
         
	TO DO:
	
         - Test in systems with NON-ARCnet network cards, just to see if
           autoprobe kills anything.  With any luck, it won't.  (It's pretty
           careful.)
           	- Except some unfriendly NE2000's die. (as of 0.40-ALPHA)
         - cards with shared memory that can be "turned off?"
         - NFS mount freezes after several megabytes to SOSS for DOS. 
 	   unmount/remount works.  Is this arcnet-specific?  I don't know.
 	 - Add support for the various stupid bugs ("I didn't read the RFC"
           syndrome) in Windows for Workgroups and LanMan.
 */
 
/**************************************************************************/

/* define this if you want to use the new but possibly dangerous ioprobe
 * If you get lockups right after status5, you probably need
 * to undefine this.  It should make more cards probe correctly,
 * I hope.
 */
#define DANGER_PROBE

/* define this if you want to use the "extra delays" which were removed
 * in 0.41 since they seemed needless.
 */
#undef EXTRA_DELAYS

/* undefine this if you want to use the non-IRQ-driven transmitter. (possibly
 * safer, although it takes more CPU time and IRQ_XMIT seems fine right now)
 */
#define IRQ_XMIT
 
/* define this for "careful" transmitting.  Try with and without if you have
 * problems.  If you use IRQ_XMIT, do NOT define this.
 */
#undef CAREFUL_XMIT

/* define this for an extra-careful memory detect.  This should work all
 * the time now, but you never know.
 */
#define STRICT_MEM_DETECT

/* define this to use the "old-style" limited MTU by default.  It basically
 * disables packet splitting.  ifconfig can still be used to reset the MTU.
 *
 * leave this disabled if possible, so it will use ethernet defaults,
 * which is our goal.
 */
#undef LIMIT_MTU

/* define this if you have a problem with the card getting "stuck" now and
 * then, which can only be fixed by a reboot or resetting the card manually
 * via ifconfig up/down.  ARCnet will set a timer function which is called
 * 8 times every second.
 *
 * This should no longer be necessary.  if you experience "stuck" ARCnet
 * drivers, please email apenwarr@foxnet.net or I will remove
 * this feature in a future release.
 */
#undef USE_TIMER_HANDLER

/**************************************************************************/
 
static char *version =
 "arcnet.c:v1.02 95/06/21 Avery Pennarun <apenwarr@foxnet.net>\n";

/*
  Sources:
	Crynwr arcnet.com/arcether.com packet drivers.
	arcnet.c v0.00 dated 1/1/94 and apparently by
		Donald Becker - it didn't work :)
	skeleton.c v0.05 dated 11/16/93 by Donald Becker
		(from Linux Kernel 1.1.45)
	...I sure wish I had the ARCnet data sheets right about now!
	RFC's 1201 and 1051 (mostly 1201) - re: ARCnet IP packets
	net/inet/eth.c (from kernel 1.1.50) for header-building info...
	Alternate Linux ARCnet source by V.Shergin <vsher@sao.stavropol.su>
	Textual information and more alternate source from Joachim Koenig
		<jojo@repas.de>
*/

#include <linux/config.h>
#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif /* MODULE */

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
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/arp.h>


/* debug levels:
 * D_OFF	production
 * D_NORMAL	verification
 * D_INIT	show init/detect messages
 * D_DURING	show messages during normal use (ie interrupts)
 * D_DATA   show packets data from skb's, not on Arcnet card
 * D_TX		show tx packets
 * D_RX		show tx+rx packets
 */
#define D_OFF		0
#define D_NORMAL	1
#define	D_INIT		2
#define D_EXTRA		3
#define D_DURING	4
#define D_DATA		6
#define D_TX		8
#define D_RX		9

#ifndef NET_DEBUG
#define NET_DEBUG 	D_INIT
#endif
static unsigned int net_debug = NET_DEBUG;

#ifndef HAVE_AUTOIRQ
/* From auto_irq.c, in ioport.h for later versions. */
extern void autoirq_setup(int waittime);
extern int autoirq_report(int waittime);
/* The map from IRQ number (as passed to the interrupt handler) to
   'struct device'. */
extern struct device *irq2dev_map[16];
#endif

#ifndef HAVE_PORTRESERVE
#define check_region(ioaddr, size) 		0
#define	request_region(ioaddr, size)		do ; while (0)
#endif

/* macro to simplify debug checking */
#define BUGLVL(x) if (net_debug>=x)

/* The number of low I/O ports used by the ethercard. */
#define ETHERCARD_TOTAL_SIZE	16


/* Handy defines for ARCnet specific stuff */
	/* COM 9026 (?) --> ARCnet register addresses */
#define INTMASK	(ioaddr+0)		/* writable */
#define STATUS	(ioaddr+0)		/* readable */
#define COMMAND (ioaddr+1)	/* writable, returns random vals on read (?) */
#define RESET  (ioaddr+8)		/* software reset writable */

	/* time needed for various things (in clock ticks, 1/100 sec) */
#define RESETtime 40		/* reset */
#define XMITtime 10		/* send (?) */
#define ACKtime 10		/* acknowledge (?) */

	/* these are the max/min lengths of packet data. (including
	 * ClientData header)
	 * note: packet sizes 250, 251, 252 are impossible (God knows why)
	 *  so exception packets become necessary.
	 *
	 * These numbers are compared with the length of the full packet,
	 * including ClientData header.
	 */
#define MTU	(253+EXTRA_CLIENTDATA)  /* normal packet max size */
#define MinTU	(257+EXTRA_CLIENTDATA)	/* extended packet min size */
#define XMTU	(508+EXTRA_CLIENTDATA)  /* extended packet max size */

	/* status/interrupt mask bit fields */
#define TXFREEflag	0x001            /* transmitter available */
#define TXACKflag       0x002            /* transmitted msg. ackd */
#define RECONflag       0x004            /* system reconfigured */
#define TESTflag        0x008            /* test flag */
#define RESETflag       0x010            /* power-on-reset */
#define RES1flag        0x020            /* unused */
#define RES2flag        0x040            /* unused */
#define NORXflag        0x080            /* receiver inhibited */

       /* in the command register, the following bits have these meanings:
        *                0-2     command
        *                3-4     page number (for enable rcv/xmt command)
        *                 7      receive broadcasts
        */
#define NOTXcmd         0x001            /* disable transmitter */
#define NORXcmd         0x002            /* disable receiver */
#define TXcmd           0x003            /* enable transmitter */
#define RXcmd           0x004            /* enable receiver */
#define CONFIGcmd       0x005            /* define configuration */
#define CFLAGScmd       0x006            /* clear flags */
#define TESTcmd         0x007            /* load test flags */

       /* flags for "clear flags" command */
#define RESETclear      0x008            /* power-on-reset */
#define CONFIGclear     0x010            /* system reconfigured */

      /* flags for "load test flags" command */
#define TESTload        0x008            /* test flag (diagnostic) */

      /* byte deposited into first address of buffers on reset */
#define TESTvalue       0321		 /* that's octal for 0xD1 :) */

      /* for "enable receiver" command */
#define RXbcasts        0x080            /* receive broadcasts */

      /* flags for "define configuration" command */
#define NORMALconf      0x000            /* 1-249 byte packets */
#define EXTconf         0x008            /* 250-504 byte packets */

	/* buffers (4 total) used for receive and xmit.
	 */
#define EnableReceiver()	outb(RXcmd|(recbuf<<3)|RXbcasts,COMMAND)
/*#define TXbuf		2 (Obsoleted by ping-pong xmits) */

	/* Protocol ID's */
#define ARC_P_IP	212		/* 0xD4 */
#define ARC_P_ARP	213		/* 0xD5 */
#define ARC_P_RARP	214		/* 0xD6 */
#define ARC_P_IPX	250		/* 0xFA */
#define ARC_P_LANSOFT	251		/* 0xFB */
#define ARC_P_ATALK	0xDD

	/* Length of time between "stuck" checks */
#define TIMERval	(HZ/8)		/* about 1/8 second */

	/* these structures define the format of an arcnet packet. */
#define NORMAL		0
#define EXTENDED	1
#define EXCEPTION	2

	/* the header required by the card itself */
struct HardHeader
{
	u_char	source,		/* source ARCnet - filled in automagically */
		destination,	/* destination ARCnet - 0 for broadcast */
		offset1,	/* offset of ClientData (256-byte packets) */
		offset2;	/* offset of ClientData (512-byte packets) */
};

	/* a complete ARCnet packet */
union ArcPacket
{
	struct HardHeader hardheader;	/* the hardware header */
	u_char raw[512];		/* raw packet info, incl ClientData */
};

	/* the "client data" header - RFC-1201 information
	 * notice that this screws up if it's not an even number of bytes
	 * <sigh>
	 */
struct ClientData
{
	/* data that's NOT part of real packet */
	u_char	daddr;		/* Destination address - stored here,
				 *   but WE MUST GET RID OF IT BEFORE SENDING A
				 *   PACKET!!
				 */
	u_char  saddr;		/* Source address - necessary for IPX protocol */
	
	/* data that IS part of real packet */
	u_char	protocol_id,	/* ARC_P_IP, ARC_P_ARP, or ARC_P_RARP */
		split_flag;	/* for use with split packets */
	u_short	sequence;	/* sequence number (?) */
};
#define EXTRA_CLIENTDATA (sizeof(struct ClientData)-4)


/* "Incoming" is information needed for each address that could be sending
 * to us.  Mostly for partially-received split packets.
 */
struct Incoming
{
	struct sk_buff *skb;		/* packet data buffer             */
	unsigned char lastpacket,	/* number of last packet (from 1) */
		      numpackets;	/* number of packets in split     */
	u_short sequence;		/* sequence number of assembly	  */
};

struct Outgoing
{
	struct sk_buff *skb;		/* buffer from upper levels */
	struct ClientData *hdr;		/* clientdata of last packet */
	u_char *data;			/* pointer to data in packet */
	short length,			/* bytes total */
	      dataleft,			/* bytes left */
	      segnum,			/* segment being sent */
	      numsegs,			/* number of segments */
	      seglen;			/* length of segment */
};


/* Information that needs to be kept for each board. */
struct arcnet_local {
	struct enet_statistics stats;
	u_char arcnum;		/* arcnet number - our 8-bit address */
	u_short sequence;	/* sequence number (incs with each packet) */
	u_char recbuf,		/* receive buffer # (0 or 1) */
	       txbuf,		/* transmit buffer # (2 or 3) */
	       txready;		/* buffer where a packet is ready to send */
	short intx,		/* in TX routine? */
	      in_txhandler,	/* in TX_IRQ handler? */
	      sending;		/* transmit in progress? */
	short tx_left;		/* segments of split packet left to TX */
	struct timer_list timer; /* the timer interrupt struct */
	struct Incoming incoming[256];	/* one from each address */
	struct Outgoing outgoing; /* packet currently being sent */
};


/* Index to functions, as function prototypes. */
extern int arcnet_probe(struct device *dev);
#ifndef MODULE
static int arcnet_memprobe(struct device *dev,u_char *addr);
static int arcnet_ioprobe(struct device *dev, short ioaddr);
#endif

static int arcnet_open(struct device *dev);
static int arcnet_close(struct device *dev);

static int arcnet_send_packet(struct sk_buff *skb, struct device *dev);
#ifdef CAREFUL_XMIT
 static void careful_xmit_wait(struct device *dev);
#else
 #define careful_xmit_wait(dev)
#endif
static void arcnet_continue_tx(struct device *dev);
static void arcnet_prepare_tx(struct device *dev,struct ClientData *hdr,
		short length,char *data);
static void arcnet_go_tx(struct device *dev);

static void arcnet_interrupt(int irq,struct pt_regs *regs);
static void arcnet_inthandler(struct device *dev);
static void arcnet_rx(struct device *dev,int recbuf);

#ifdef USE_TIMER_HANDLER
static void arcnet_timer(unsigned long arg);
#endif

static struct enet_statistics *arcnet_get_stats(struct device *dev);
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);

	/* annoying functions for header/arp/etc building */
int arc_header(unsigned char *buff,struct device *dev,unsigned short type,
		void *daddr,void *saddr,unsigned len,struct sk_buff *skb);
int arc_rebuild_header(void *eth,struct device *dev,unsigned long raddr,
		struct sk_buff *skb);
unsigned short arc_type_trans(struct sk_buff *skb,struct device *dev);

static int arcnet_reset(struct device *dev);

#ifdef MODULE
int  init_module(void);
void cleanup_module(void);
#endif

#define tx_done(dev) 1

/*
#define JIFFER(time) for (delayval=jiffies+(time); delayval>jiffies;);
*/
#define JIFFER(time) for (delayval=0; delayval<(time*10); delayval++) \
		udelay(1000);

#ifdef EXTRA_DELAYS
 #define XJIFFER(time) JIFFER(time)
#else
 #define XJIFFER(time)
#endif


/* Check for a network adaptor of this type, and return '0' if one exists.
 *  If dev->base_addr == 0, probe all likely locations.
 *  If dev->base_addr == 1, always return failure.
 *  If dev->base_addr == 2, allocate space for the device and return success
 *  (detachable devices only).
 */
int
arcnet_probe(struct device *dev)
{
#ifndef MODULE
	/* I refuse to probe anything less than 0x200, because anyone using
	 * an address like that should probably be shot.
	 */
	int *port, ports[] = {/* first the suggested values! */
			      0x300,0x2E0,0x2F0,0x2D0,
			      /* ...now everything else possible. */
			      0x200,0x210,0x220,0x230,0x240,0x250,0x260,0x270,
			      0x280,0x290,0x2a0,0x2b0,0x2c0,
			            0x310,0x320,0x330,0x340,0x350,0x360,0x370,
			      0x380,0x390,0x3a0,/* video ports, */0x3e0,0x3f0,
			      /* a null ends the list */
			      0};
	/* I'm not going to probe below 0xA0000 either, for similar reasons.
	 */
	unsigned long *addr, addrs[] = {0xD0000,0xE0000,0xA0000,0xB0000,
					0xC0000,0xF0000,
					/* from <mdrejhon@magi.com> */
					0xE1000,
					0xDD000,0xDC000,
					0xD9000,0xD8000,0xD5000,0xD4000,0xD1000,
					0xCD000,0xCC000,
					0xC9000,0xC8000,0xC5000,0xC4000,
					/* terminator */
					0};
	int base_addr=dev->base_addr, status=0;
#endif /* MODULE */
	int delayval;
	struct arcnet_local *lp;

	if (net_debug)
	{
		printk(version);
		printk("arcnet: ***\n");
		printk("arcnet: * Read linux/drivers/net/README.arcnet for important release notes!\n");
		printk("arcnet: *\n");
		printk("arcnet: * This version should be stable, but e-mail me if you have any\n");
		printk("arcnet: * questions, comments, or bug reports!\n");
		printk("arcnet: ***\n");
	}

	BUGLVL(D_INIT)
		printk("arcnet: given: base %lXh, IRQ %Xh, shmem %lXh\n",
			dev->base_addr,dev->irq,dev->mem_start);
			
#ifndef MODULE
	if (base_addr > 0x1ff)		/* Check a single specified location. */
		status=arcnet_ioprobe(dev, base_addr);
	else if (base_addr > 0)		/* Don't probe at all. */
		return ENXIO;
	else for (port = &ports[0]; *port; port++)
	{
		int ioaddr = *port;
		if (check_region(ioaddr, ETHERCARD_TOTAL_SIZE))
		{
			BUGLVL(D_INIT)
				printk("arcnet: Skipping %Xh because of check_region...\n",
					ioaddr);
			continue;
		}

		status=arcnet_ioprobe(dev, ioaddr);
		if (!status) break;
	}
	
	if (status) return status;

	/* ioprobe turned out okay.  Now give it a couple seconds to finish
	 * initializing...
	 */
	BUGLVL(D_INIT)
		printk("arcnet: ioprobe okay!  Waiting for reset...\n");
	JIFFER(100);

	/* okay, now we have to find the shared memory area. */
	BUGLVL(D_INIT)
		printk("arcnet: starting memory probe, given %lXh\n",
			dev->mem_start);
	if (dev->mem_start)	/* value given - probe just that one */
	{
		status=arcnet_memprobe(dev,(u_char *)dev->mem_start);
		if (status) return status;
	}
	else			/* no value given - probe everything */
	{
		for (addr = &addrs[0]; *addr; addr++) {
			status=arcnet_memprobe(dev,(u_char *)(*addr));
			if (!status) break;
		}
		
		if (status) return status;
	}
#else /* MODULE */
	if (!dev->base_addr || !dev->irq || !dev->mem_start 
		|| !dev->rmem_start)
	{
		printk("arcnet: loadable modules can't autoprobe!\n");
		printk("arcnet:  try using io=, irqnum=, and shmem= on the insmod line.\n");
		printk("arcnet:  you may also need num= to change the device name. (ie. num=1 for arc1)\n");
		return ENODEV;
	}
#endif
	/* now reserve the irq... */
	{	
		int irqval = request_irq(dev->irq, &arcnet_interrupt, 0,
			"arcnet");
		if (irqval) {
			printk("%s: unable to get IRQ %d (irqval=%d).\n",
				dev->name,dev->irq, irqval);
			return EAGAIN;
		 }
	 }
	 
	/* Grab the region so we can find another board if autoIRQ fails. */
	request_region(dev->base_addr, ETHERCARD_TOTAL_SIZE,"arcnet");
	
	printk("%s: ARCnet card found at %03lXh, IRQ %d, ShMem at %lXh.\n", 
		dev->name, dev->base_addr, dev->irq, dev->mem_start);

	/* Initialize the device structure. */
	dev->priv = kmalloc(sizeof(struct arcnet_local), GFP_KERNEL);
	memset(dev->priv, 0, sizeof(struct arcnet_local));
	lp=(struct arcnet_local *)(dev->priv);

	dev->open		= arcnet_open;
	dev->stop		= arcnet_close;
	dev->hard_start_xmit = arcnet_send_packet;
	dev->get_stats	= arcnet_get_stats;
#ifdef HAVE_MULTICAST
	dev->set_multicast_list = &set_multicast_list;
#endif

	/* Fill in the fields of the device structure with ethernet-generic values. */
	ether_setup(dev);

	/* And now fill particular ones with arcnet values :) */
	
	dev->type=ARPHRD_ARCNET;
	dev->hard_header_len=sizeof(struct ClientData);
	BUGLVL(D_EXTRA)
		printk("arcnet: ClientData header size is %d.\narcnet: HardHeader size is %d.\n",
			sizeof(struct ClientData),sizeof(struct HardHeader));
#if LIMIT_MTU	/* the old way - normally, now use ethernet default */
	dev->mtu=512-sizeof(struct HardHeader)+EXTRA_CLIENTDATA;
#endif
		/* since we strip EXTRA_CLIENTDATA bytes off before sending,
		 * we let Linux add that many bytes to the packet data...
		 */
	dev->addr_len=1;
	dev->broadcast[0]=0x00;
	
	BUGLVL(D_INIT) printk("arcnet: arcnet_probe: resetting card.\n");
	arcnet_reset(dev);
	JIFFER(50);
	BUGLVL(D_NORMAL)
		printk("arcnet: We appear to be station %d (%02Xh)\n",
			lp->arcnum,lp->arcnum);
	if (lp->arcnum==0)
		printk("arcnet: WARNING!  Station address 0 is reserved for broadcasts!\n");
	if (lp->arcnum==255)
		printk("arcnet: WARNING!  Station address 255 may confuse DOS networking programs!\n");
	dev->dev_addr[0]=lp->arcnum;
	lp->sequence=1;
	lp->recbuf=0;

	dev->hard_header        = arc_header;
	dev->rebuild_header     = arc_rebuild_header;

	return 0;
}

#ifndef MODULE

int arcnet_ioprobe(struct device *dev, short ioaddr)
{
	int delayval,airq;

	BUGLVL(D_INIT)
		printk("arcnet: probing address %Xh\n",ioaddr);

	BUGLVL(D_INIT)
		printk("arcnet:  status1=%Xh\n",inb(STATUS));

		
	/* very simple - all we have to do is reset the card, and if there's
	 * no irq, it's not an ARCnet.  We can also kill two birds with
	 * one stone because we detect the IRQ at the same time :)
	 */
	 
	/* reset the card by reading the reset port */
	inb(RESET);
	JIFFER(RESETtime);

	/* if status port is FF, there's certainly no arcnet... give up. */
	if (inb(STATUS)==0xFF)
	{
		BUGLVL(D_INIT)
			printk("arcnet:  probe failed.  Status port empty.\n");
		return ENODEV;
	}

	/* we'll try to be reasonably sure it's an arcnet by making sure
	 * the value of the COMMAND port changes automatically once in a
	 * while.  I have no idea what those values ARE, but at least
	 * they work.
	 */
	{
		int initval,curval;
		
		curval=initval=inb(COMMAND);
		delayval=jiffies+5;
		while (delayval>=jiffies && curval==initval)
			curval=inb(COMMAND);
			
		if (curval==initval)
		{
			printk("arcnet:  probe failed.  never-changing command port (%02Xh).\n",
				initval);
			return ENODEV;
		}
	}

	BUGLVL(D_INIT)
		printk("arcnet:  status2=%Xh\n",inb(STATUS));

	/* now we turn the reset bit off so we can IRQ next reset... */
	outb(CFLAGScmd|RESETclear|CONFIGclear,COMMAND);
	XJIFFER(ACKtime);
	if (inb(STATUS) & RESETflag) /* reset flag STILL on */
	{
		BUGLVL(D_INIT)
			printk("arcnet:  probe failed.  eternal reset flag1...(status=%Xh)\n",
				inb(STATUS));
		return ENODEV;
	}

	/* set up automatic IRQ detection */
	autoirq_setup(0);
	
	/* enable reset IRQ's (shouldn't be necessary, but worth a try) */
	outb(RESETflag,INTMASK);

	/* now reset it again to generate an IRQ */
	inb(RESET);
	JIFFER(RESETtime);

	BUGLVL(D_INIT)
		printk("arcnet:  status3=%Xh\n",inb(STATUS));

	/* and turn the reset flag back off */
	outb(CFLAGScmd|RESETclear|CONFIGclear,COMMAND);
	XJIFFER(ACKtime);

	BUGLVL(D_INIT)
		printk("arcnet:  status4=%Xh\n",inb(STATUS));

	/* enable reset IRQ's again */
	outb(RESETflag,INTMASK);

	/* now reset it again to generate an IRQ */
	inb(RESET);
	JIFFER(RESETtime);
	
	BUGLVL(D_INIT)
		printk("arcnet:  status5=%Xh\n",inb(STATUS));

	/* if we do this, we're sure to get an IRQ since the card has
	 * just reset and the NORXflag is on until we tell it to start
	 * receiving.
	 *
	 * However, this could, theoretically, cause a lockup.  Maybe I'm just
	 * not very good at theory! :)
	 */
#ifdef DANGER_PROBE
	outb(NORXflag,INTMASK);
	JIFFER(RESETtime);
	outb(0,INTMASK);
#endif

	/* and turn the reset flag back off */
	outb(CFLAGScmd|RESETclear|CONFIGclear,COMMAND);
	XJIFFER(ACKtime);

	airq = autoirq_report(0);
	if (net_debug>=D_INIT && airq)
		printk("arcnet:  autoirq is %d\n", airq);

	/* if there was no autoirq AND the user hasn't set any defaults,
	 * give up.
	 */
	if (!airq && !(dev->base_addr && dev->irq))
	{
		BUGLVL(D_INIT)
			printk("arcnet:  probe failed.  no autoirq...\n");
		return ENODEV;
	}

	/* otherwise we probably have a card.  Let's make sure. */
	
	if (inb(STATUS) & RESETflag) /* reset flag on */
	{
		/* now we turn the reset bit off */
		outb(CFLAGScmd|RESETclear|CONFIGclear,COMMAND);
		XJIFFER(ACKtime);
	}
	
	if (inb(STATUS) & RESETflag) /* reset flag STILL on */
	{
		BUGLVL(D_INIT)
			printk("arcnet:  probe failed.  eternal reset flag...(status=%Xh)\n",
				inb(STATUS));
		return ENODEV;
	}
	
	/* okay, we've got a real, live ARCnet on our hands. */
	if (!dev->base_addr) dev->base_addr=ioaddr;
	
	if (dev->irq < 2)		/* "Auto-IRQ" */
	{
		/* we already did the autoirq above, so store the values */
		dev->irq=airq;
	}
	else if (dev->irq == 2)
	{
		if (net_debug)
			printk("arcnet: IRQ2 == IRQ9, don't worry.\n");
		dev->irq = 9;
	}

	BUGLVL(D_INIT)
		printk("arcnet: irq and base address seem okay. (%lXh, IRQ %d)\n",
			dev->base_addr,dev->irq);
	return 0;
}


/* A memory probe that is called after the card is reset.
 * It checks for the official TESTvalue in byte 0 and makes sure the buffer
 * has certain characteristics of an ARCnet...
 */
int arcnet_memprobe(struct device *dev,u_char *addr)
{
	BUGLVL(D_INIT)
		printk("arcnet: probing memory at %lXh\n",(u_long)addr);
		
	dev->mem_start=0;

#ifdef STRICT_MEM_DETECT /* probably better. */
	/* ARCnet memory byte 0 is TESTvalue */
	if (addr[0]!=TESTvalue)
	{
		BUGLVL(D_INIT)
			printk("arcnet:  probe failed.  addr=%lXh, addr[0]=%Xh (not %Xh)\n",
				(unsigned long)addr,addr[0],TESTvalue);
		return ENODEV;
	}
	
	/* now verify the shared memory writability */
	addr[0]=0x42;
	if (addr[0]!=0x42)
	{
		BUGLVL(D_INIT)
			printk("arcnet:  probe failed.  addr=%lXh, addr[0]=%Xh (not 42h)\n",
				(unsigned long)addr,addr[0]);
	 	return ENODEV;
	}
#else
	if (addr[0]!=TESTvalue)
	{
		BUGLVL(D_INIT)
			printk("arcnet:  probe failed.  addr=%lXh, addr[0]=%Xh (not %Xh)\n",
				(unsigned long)addr,addr[0],TESTvalue);
		return ENODEV;
	}
#endif

	/* got it!  fill in dev */
	dev->mem_start=(unsigned long)addr;
	dev->mem_end=dev->mem_start+512*4-1;
	dev->rmem_start=dev->mem_start+512*0;
	dev->rmem_end=dev->mem_start+512*2-1;

	return 0;
}

#endif /* MODULE */


/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is run.

   This routine should set everything up anew at each open, even
   registers that "should" only need to be set once at boot, so that
   there is non-reboot way to recover if something goes wrong.
   */
static int
arcnet_open(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
/*	int ioaddr = dev->base_addr;*/

	if (dev->metric>=10)
	{
		net_debug=dev->metric-10;
		dev->metric=1;
	}

	if (net_debug) printk(version);

#if 0	/* Yup, they're hardwired in arcnets */
	/* This is used if the interrupt line can turned off (shared).
	   See 3c503.c for an example of selecting the IRQ at config-time. */
	if (request_irq(dev->irq, &arcnet_interrupt, 0, "arcnet")) {
		return -EAGAIN;
	}
#endif

	irq2dev_map[dev->irq] = dev;

	/* Reset the hardware here. */
	BUGLVL(D_EXTRA) printk("arcnet: arcnet_open: resetting card.\n");
	
	/* try to reset - twice if it fails the first time */
	if (arcnet_reset(dev) && arcnet_reset(dev))
		return -ENODEV;
	
/*	chipset_init(dev, 1);*/
/*	outb(0x00, ioaddr);*/

/*	lp->open_time = jiffies;*/

	dev->tbusy=0;
	dev->interrupt=0;
	dev->start=1;
	lp->intx=0;
	lp->in_txhandler=0;

#ifdef USE_TIMER_HANDLER
	/* grab a timer handler to recover from any missed IRQ's */
	init_timer(&lp->timer);
	lp->timer.expires = TIMERval;         /* length of time */
	lp->timer.data = (unsigned long)dev;  /* pointer to "dev" structure */
	lp->timer.function = &arcnet_timer;    /* timer handler */
	add_timer(&lp->timer);
#endif

#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
                                 	
	return 0;
}


/* The inverse routine to arcnet_open(). */
static int
arcnet_close(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int ioaddr = dev->base_addr;
#ifdef EXTRA_DELAYS
	int delayval;
#endif

/*	lp->open_time = 0;*/

	dev->tbusy = 1;
	dev->start = 0;
	
	/* release the timer */
	del_timer(&lp->timer);

	/* Flush the Tx and disable Rx here.     */
	/* resetting the card should do the job. */
	/*inb(RESET);*/

	outb(0,INTMASK);	/* no IRQ's */
	outb(NOTXcmd,COMMAND);  /* disable transmit */
	XJIFFER(ACKtime);
	outb(NORXcmd,COMMAND);	/* disable receive */

	/* Update the statistics here. */
	
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif

	return 0;
}


static int
arcnet_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr;
/*	short daddr;*/

	lp->intx++;

	BUGLVL(D_DURING)
		printk("arcnet: transmit requested (status=%Xh, inTX=%d)\n",
			inb(STATUS),lp->intx);

	if (dev->tbusy || lp->in_txhandler)
	{
		/* If we get here, some higher level has decided we are broken.
		   There should really be a "kick me" function call instead. */
		int tickssofar = jiffies - dev->trans_start;
		int recbuf=lp->recbuf;
		int status=inb(STATUS);
		
		/* resume any stopped tx's */
#if 0
		if (lp->txready && (inb(STATUS)&TXFREEflag))
		{
			printk("arcnet: kickme: starting a TX (status=%Xh)\n",
				inb(STATUS));
			arcnet_go_tx(dev);
			lp->intx--;
			return 1;
		}
#endif
		
		if (tickssofar < 5) 
		{
			BUGLVL(D_DURING)
				printk("arcnet: premature kickme! (status=%Xh ticks=%d o.skb=%ph numsegs=%d segnum=%d\n",
					status,tickssofar,lp->outgoing.skb,
					lp->outgoing.numsegs,
					lp->outgoing.segnum);
			lp->intx--;
			return 1;
		}

		BUGLVL(D_INIT)
			printk("arcnet: transmit timed out (status=%Xh, inTX=%d, tickssofar=%d)\n",
				status,lp->intx,tickssofar);

		/* Try to restart the adaptor. */
		/*arcnet_reset(dev);*/
		
		if (status&NORXflag) EnableReceiver();
		if (!(status&TXFREEflag)) outb(NOTXcmd,COMMAND);
		dev->trans_start = jiffies;

		if (lp->outgoing.skb)
			dev_kfree_skb(lp->outgoing.skb,FREE_WRITE);
		lp->outgoing.skb=NULL;

		dev->tbusy=0;
		mark_bh(NET_BH);
		lp->intx=0;
		lp->in_txhandler=0;
		lp->txready=0;
		lp->sending=0;

		return 1;
	}

	/* If some higher layer thinks we've missed a tx-done interrupt
	   we are passed NULL. Caution: dev_tint() handles the cli()/sti()
	   itself. */
	if (skb == NULL) {
		BUGLVL(D_INIT)
			printk("arcnet: tx passed null skb (status=%Xh, inTX=%d, tickssofar=%ld)\n",
				inb(STATUS),lp->intx,jiffies-dev->trans_start);
		dev_tint(dev);
		lp->intx--;
		return 0;
	}
	
	if (lp->txready)	/* transmit already in progress! */
	{
		printk("arcnet: trying to start new packet while busy!\n");
		printk("arcnet: marking as not ready.\n");
		lp->txready=0;
		return 1;
	}

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
        if (set_bit(0, (void*)&dev->tbusy) != 0)
        {
            printk("arcnet: transmitter called with busy bit set! (status=%Xh, inTX=%d, tickssofar=%ld)\n",
            		inb(STATUS),lp->intx,jiffies-dev->trans_start);
            lp->intx--;
            return -EBUSY;
        }
	else {
		struct Outgoing *out=&(lp->outgoing);
		out->length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
		out->hdr=(struct ClientData*)skb->data;
		out->skb=skb;
		BUGLVL( D_DATA ) {
			short i;
			for( i=0; i< skb->len; i++)
			{
				if( i%16 == 0 ) printk("\n[%04hX] ",i);
				printk("%02hX ",((unsigned char*)skb->data)[i]);
			}
			printk("\n");
		}

#ifdef IRQ_XMIT
		if (lp->txready && inb(STATUS)&TXFREEflag)
			arcnet_go_tx(dev);
#endif

		
		if (out->length<=XMTU)	/* fits in one packet? */
		{
			BUGLVL(D_TX) printk("arcnet: not splitting %d-byte packet. (split_flag=%d)\n",
					out->length,out->hdr->split_flag);
			BUGLVL(D_INIT) if (out->hdr->split_flag)
				printk("arcnet: short packet has split_flag set?! (split_flag=%d)\n",
					out->hdr->split_flag);
			out->numsegs=1;
			out->segnum=1;
			arcnet_prepare_tx(dev,out->hdr,
				out->length-sizeof(struct ClientData),
				((char *)skb->data)+sizeof(struct ClientData));
			careful_xmit_wait(dev);

			/* done right away */
			dev_kfree_skb(out->skb,FREE_WRITE);
			out->skb=NULL;
					
			if (!lp->sending)
			{
				arcnet_go_tx(dev);
				
				/* inform upper layers */
				dev->tbusy=0;
				mark_bh(NET_BH);
			}
		}
		else			/* too big for one - split it */
		{
			int maxsegsize=XMTU-sizeof(struct ClientData);

			out->data=(u_char *)skb->data
					+ sizeof(struct ClientData);
			out->dataleft=out->length-sizeof(struct ClientData);
			out->numsegs=(out->dataleft+maxsegsize-1)/maxsegsize;
				
			out->segnum=0;
				
			BUGLVL(D_TX) printk("arcnet: packet (%d bytes) split into %d fragments:\n",
				out->length,out->numsegs);

#ifdef IRQ_XMIT
			/* if a packet waiting, launch it */
			if (lp->txready && inb(STATUS)&TXFREEflag)
				arcnet_go_tx(dev);

			if (!lp->txready)
			{
				/* prepare a packet, launch it and prepare
                                 * another.
                                 */
				arcnet_continue_tx(dev);
				if (!lp->sending)
				{
					arcnet_go_tx(dev);
					arcnet_continue_tx(dev);
					if (!lp->sending)
						arcnet_go_tx(dev);
				}
			}
			
			/* if segnum==numsegs, the transmission is finished;
			 * free the skb right away.
			 */
			if (out->segnum==out->numsegs)
			{
				/* transmit completed */
				out->segnum++;
				if (out->skb)
					dev_kfree_skb(out->skb,FREE_WRITE);
				out->skb=NULL;
#if 0
				/* inform upper layers */
				dev->tbusy=0;
				mark_bh(NET_BH);
#endif
			}
			
#else /* non-irq xmit */
			while (out->segnum<out->numsegs)
			{
				arcnet_continue_tx(dev);
				careful_xmit_wait(dev);
				arcnet_go_tx(dev);
				dev->trans_start=jiffies;
			}

			dev_kfree_skb(out->skb,FREE_WRITE);
			out->skb=NULL;

			/* inform upper layers */
			dev->tbusy = 0;
			mark_bh(NET_BH);
#endif
		}
	}

	lp->intx--;
	lp->stats.tx_packets++;
	dev->trans_start=jiffies;
	return 0;
}

static void arcnet_continue_tx(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int maxsegsize=XMTU-sizeof(struct ClientData);
	struct Outgoing *out=&(lp->outgoing);
	
	if (lp->txready)
	{
		printk("arcnet: continue_tx: called with packet in buffer!\n");
		return;
	}

	if (out->segnum>=out->numsegs)
	{
		printk("arcnet: continue_tx: building segment %d of %d!\n",
			out->segnum+1,out->numsegs);
	}

	if (!out->segnum)	/* first packet */
		out->hdr->split_flag=((out->numsegs-2)<<1)+1;
	else
		out->hdr->split_flag=out->segnum<<1;

	out->seglen=maxsegsize;
	if (out->seglen>out->dataleft) out->seglen=out->dataleft;
			
	BUGLVL(D_TX) printk("arcnet: building packet #%d (%d bytes) of %d (%d total), splitflag=%d\n",
		out->segnum+1,out->seglen,out->numsegs,
		out->length,out->hdr->split_flag);

	arcnet_prepare_tx(dev,out->hdr,out->seglen,out->data);
		
	out->dataleft-=out->seglen;
	out->data+=out->seglen;
	out->segnum++;
}

#ifdef CAREFUL_XMIT
static void careful_xmit_wait(struct device *dev)
{
	int ioaddr=dev->base_addr;
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

	/* wait patiently for tx to become available again */
	while ( !(inb(STATUS)&TXFREEflag) )
	{
		if (jiffies-dev->trans_start > 20 || !dev->tbusy)
		{
			BUGLVL(D_INIT)
				printk("arcnet: CAREFUL_XMIT timeout. (busy=%d, status=%Xh)\n",
					dev->tbusy,inb(STATUS));
			lp->stats.tx_errors++;
			
			outb(NOTXcmd,COMMAND);
			return;
		}
	}
	BUGLVL(D_TX) printk("arcnet: transmit completed successfully. (status=%Xh)\n",
				inb(STATUS));
}
#endif

static void
arcnet_prepare_tx(struct device *dev,struct ClientData *hdr,short length,
		char *data)
{
/*	int ioaddr = dev->base_addr;*/
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	struct ClientData *arcsoft;
	union ArcPacket *arcpacket = 
		(union ArcPacket *)(dev->mem_start+512*(lp->txbuf^1));
	u_char pkttype;
	int offset;
	short daddr;
	
	lp->txbuf=lp->txbuf^1;	/* XOR with 1 to alternate between 2 and 3 */
	
	length+=sizeof(struct ClientData);

	BUGLVL(D_TX)
		printk("arcnet: arcnet_prep_tx: hdr:%ph, length:%d, data:%ph\n",
			hdr,length,data);

	/* clean out the page to make debugging make more sense :) */
	BUGLVL(D_DURING)
		memset((void *)dev->mem_start+lp->txbuf*512,0x42,512);

	daddr=arcpacket->hardheader.destination=hdr->daddr;

	/* load packet into shared memory */
	if (length<=MTU)	/* Normal (256-byte) Packet */
	{
		pkttype=NORMAL;
			
		arcpacket->hardheader.offset1=offset=256-length
				+ EXTRA_CLIENTDATA;
		arcsoft=(struct ClientData *)
			(&arcpacket->raw[offset-EXTRA_CLIENTDATA]);
	}
	else if (length>=MinTU)	/* Extended (512-byte) Packet */
	{
		pkttype=EXTENDED;
		
		arcpacket->hardheader.offset1=0;
		arcpacket->hardheader.offset2=offset=512-length
			+ EXTRA_CLIENTDATA;
		arcsoft=(struct ClientData *)
			(&arcpacket->raw[offset-EXTRA_CLIENTDATA]);
	}
	else				/* Exception Packet */
	{
		pkttype=EXCEPTION;
		
		arcpacket->hardheader.offset1=0;
		arcpacket->hardheader.offset2=offset=512-length-4
			+ EXTRA_CLIENTDATA;
		arcsoft=(struct ClientData *)
			(&arcpacket->raw[offset+4-EXTRA_CLIENTDATA]);
		
		/* exception-specific stuff - these four bytes
		 * make the packet long enough to fit in a 512-byte
		 * frame.
		 */
		arcpacket->raw[offset+0]=hdr->protocol_id;
		arcpacket->raw[offset+1]=0xFF; /* FF flag */
			arcpacket->raw[offset+2]=0xFF; /* FF padding */
			arcpacket->raw[offset+3]=0xFF; /* FF padding */
	}


	/* copy the packet into ARCnet shmem
	 *  - the first bytes of ClientData header are skipped
	 */
	memcpy((u_char*)arcsoft+EXTRA_CLIENTDATA,
		(u_char*)hdr+EXTRA_CLIENTDATA,
		sizeof(struct ClientData)-EXTRA_CLIENTDATA);
	memcpy((u_char*)arcsoft+sizeof(struct ClientData),
		data,
		length-sizeof(struct ClientData));
		
	BUGLVL(D_DURING) printk("arcnet: transmitting packet to station %02Xh (%d bytes, type=%d)\n",
			daddr,length,pkttype);
			
	BUGLVL(D_TX)
	{
		int countx,county;
		
		printk("arcnet: packet dump [tx] follows:");

 		for (county=0; county<16+(pkttype!=NORMAL)*16; county++)
		{
			printk("\n[%04X] ",county*16);
			for (countx=0; countx<16; countx++)
				printk("%02X ",
					arcpacket->raw[county*16+countx]);
		}
		
		printk("\n");
	}
#ifdef CAREFUL_XMIT
 #if 0
	careful_xmit_wait(dev);

	/* if we're not broadcasting, make sure the xmit was ack'd.
	 * if it wasn't, there is probably no card with that
	 * address... or else it missed our tx somehow.
	 */
	if (daddr && !(inb(STATUS)&TXACKflag))
	{	
		BUGLVL(D_INIT)
			printk("arcnet: transmit not acknowledged. (status=%Xh, daddr=%02Xh)\n",
				inb(STATUS),daddr);
		lp->stats.tx_errors++;
		return -ENONET; /* "machine is not on the network" */
	}
 #endif
#endif
	lp->txready=lp->txbuf;	/* packet is ready for sending */

#if 0
#ifdef IRQ_XMIT
	if (inb(STATUS)&TXFREEflag) arcnet_go_tx(dev);
#endif
#endif
}

static void
arcnet_go_tx(struct device *dev)
{
	struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr;

	BUGLVL(D_DURING)
		printk("arcnet: go_tx: status=%Xh\n",
			inb(STATUS));
	
	if (!(inb(STATUS)&TXFREEflag) || !lp->txready) return;

	/* start sending */
	outb(TXcmd|(lp->txready<<3),COMMAND);

#ifdef IRQ_XMIT
	outb(TXFREEflag|NORXflag,INTMASK);
#endif

	dev->trans_start = jiffies;
	lp->txready=0;
	lp->sending++;
}


/* The typical workload of the driver:
   Handle the network interface interrupts. */
static void
arcnet_interrupt(int irq,struct pt_regs *regs)
{
	struct device *dev = (struct device *)(irq2dev_map[irq]);

	if (dev == NULL) {
		if (net_debug >= D_DURING)
			printk("arcnet: irq %d for unknown device.\n", irq);
		return;
	}
	
	arcnet_inthandler(dev);
}

static void
arcnet_inthandler(struct device *dev)
{	
	struct arcnet_local *lp;
	int ioaddr, status, boguscount = 3, didsomething;
	
	dev->interrupt = 1;
	sti();

	ioaddr = dev->base_addr;
	lp = (struct arcnet_local *)dev->priv;

#ifdef IRQ_XMIT
	outb(0,INTMASK);
#endif
	
	BUGLVL(D_DURING)
		printk("arcnet: in net_interrupt (status=%Xh)\n",inb(STATUS));

	do
	{
		status = inb(STATUS);
		didsomething=0;
	
		if (!dev->start)
		{
			BUGLVL(D_EXTRA)
				printk("arcnet: ARCnet not yet initialized.  irq ignored. (status=%Xh)\n",
					status);
#ifdef IRQ_XMIT
			if (!(status&NORXflag))
				outb(NORXflag,INTMASK);
#endif
			dev->interrupt=0;
			return;
		}
	
		/* RESET flag was enabled - card is resetting and if RX
		 * is disabled, it's NOT because we just got a packet.
		 */
		if (status & RESETflag)
		{
			BUGLVL(D_INIT)
				printk("arcnet: reset irq (status=%Xh)\n",
					status);
			dev->interrupt=0;
			return;
		}

#if 1	/* yes, it's silly to disable this part but it makes good testing */
		/* RX is inhibited - we must have received something. */
		if (status & NORXflag)
		{
			int recbuf=lp->recbuf=!lp->recbuf;

			BUGLVL(D_DURING)
				printk("arcnet: receive irq (status=%Xh)\n",
					status);

			/* enable receive of our next packet */
			EnableReceiver();
		
			/* Got a packet. */
			arcnet_rx(dev,!recbuf);
			
			didsomething++;
		}
#endif
#ifdef IRQ_XMIT
		/* it can only be an xmit-done irq if we're xmitting :) */
		if (status&TXFREEflag && !lp->in_txhandler && lp->sending)
		{
			struct Outgoing *out=&(lp->outgoing);
			
			lp->in_txhandler++;
			lp->sending--;
			
			BUGLVL(D_DURING)
				printk("arcnet: TX IRQ (stat=%Xh, numsegs=%d, segnum=%d, skb=%ph)\n",
					status,out->numsegs,out->segnum,out->skb);
					
			/* send packet if there is one */
			if (lp->txready)
			{
				arcnet_go_tx(dev);
				didsomething++;
			}
			
			if (lp->intx)
			{
				lp->in_txhandler--;
				continue;
			}

			if (!lp->outgoing.skb)
			{
				BUGLVL(D_DURING)
					printk("arcnet: TX IRQ done: no split to continue.\n");
				
				/* inform upper layers */
				if (!lp->txready && dev->tbusy)
				{
					dev->tbusy=0;
					mark_bh(NET_BH);
				}
				
				lp->in_txhandler--;
				continue;
			}
			
			/*lp->stats.tx_packets++;*/
			
			/* if more than one segment, and not all segments
			 * are done, then continue xmit.
			 */
			if (out->segnum<out->numsegs)
				arcnet_continue_tx(dev);
			if (lp->txready && !lp->sending)
				arcnet_go_tx(dev);

			/* if segnum==numsegs, the transmission is finished;
			 * free the skb.
			 */
			if (out->segnum>=out->numsegs)
			{
				/* transmit completed */
				out->segnum++;
				if (out->skb)
					dev_kfree_skb(out->skb,FREE_WRITE);
				out->skb=NULL;

				/* inform upper layers */
				if (!lp->txready && dev->tbusy)
				{
					dev->tbusy=0;
					mark_bh(NET_BH);
				}
			}
			didsomething++;
			
			lp->in_txhandler--;
		}
#endif /* IRQ_XMIT */
	} while (--boguscount && didsomething);

	BUGLVL(D_DURING)
		printk("arcnet: net_interrupt complete (status=%Xh)\n",
			inb(STATUS));

#ifdef IRQ_XMIT
	if (dev->start && lp->sending )
		outb(NORXflag|TXFREEflag,INTMASK);
	else
		outb(NORXflag,INTMASK);
#endif

	dev->interrupt=0;
}

/* A packet has arrived; grab it from the buffers and possibly unsplit it.
 */
static void
arcnet_rx(struct device *dev,int recbuf)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int ioaddr = dev->base_addr;
/*	int status = inb(STATUS);*/

	struct sk_buff *skb;

	union ArcPacket *arcpacket=
		(union ArcPacket *)(dev->mem_start+recbuf*512);
	struct ClientData *soft,*arcsoft;
	short length,offset;
	u_char pkttype,daddr,saddr;

	daddr=arcpacket->hardheader.destination;
	saddr=arcpacket->hardheader.source;
	
	/* if source is 0, it's not a "used" packet! */
	if (saddr==0)
	{
		/*BUGLVL(D_DURING)*/
			printk("arcnet: discarding old packet. (status=%Xh)\n",
				inb(STATUS));
		lp->stats.rx_errors++;
		return;
	}
	arcpacket->hardheader.source=0;
	
	if (arcpacket->hardheader.offset1) /* Normal Packet */
	{
		offset=arcpacket->hardheader.offset1;
		arcsoft=(struct ClientData *)
			(&arcpacket->raw[offset-EXTRA_CLIENTDATA]);
		length=256-offset+EXTRA_CLIENTDATA;
		pkttype=NORMAL;
	}
	else		/* ExtendedPacket or ExceptionPacket */
	{
		offset=arcpacket->hardheader.offset2;
		arcsoft=(struct ClientData *)
			(&arcpacket->raw[offset-EXTRA_CLIENTDATA]);

		if (arcsoft->split_flag!=0xFF)  /* Extended Packet */
		{
			length=512-offset+EXTRA_CLIENTDATA;
			pkttype=EXTENDED;
		}
		else				/* Exception Packet */
		{
			/* skip over 4-byte junkola */
			arcsoft=(struct ClientData *)
				((u_char *)arcsoft + 4);
			length=512-offset+EXTRA_CLIENTDATA-4;
			pkttype=EXCEPTION;
		}
	}
	
	if (!arcsoft->split_flag)		/* not split */
	{
		struct Incoming *in=&lp->incoming[saddr];

		BUGLVL(D_RX) printk("arcnet: incoming is not split (splitflag=%d)\n",
			arcsoft->split_flag);
			
        if (in->skb)	/* already assembling one! */
        	{
        		BUGLVL(D_INIT) printk("arcnet: aborting assembly (seq=%d) for unsplit packet (splitflag=%d, seq=%d)\n",
        			in->sequence,arcsoft->split_flag,
        			arcsoft->sequence);
        		kfree_skb(in->skb,FREE_WRITE);
        		in->skb=NULL;
        	}
        	
        	in->sequence=arcsoft->sequence;

         	skb = alloc_skb(length, GFP_ATOMIC);
         	if (skb == NULL) {
         		printk("%s: Memory squeeze, dropping packet.\n", 
         			dev->name);
         		lp->stats.rx_dropped++;
         		return;
         	}
         	soft=(struct ClientData *)skb->data;
         	
         	skb->len = length;
         	skb->dev = dev;
         	
         	memcpy((u_char *)soft+EXTRA_CLIENTDATA,
         		(u_char *)arcsoft+EXTRA_CLIENTDATA,
         		length-EXTRA_CLIENTDATA);
         	soft->daddr=daddr;
         	soft->saddr=saddr;
         	
         	BUGLVL(D_DURING)
         		printk("arcnet: received packet from %02Xh to %02Xh (%d bytes, type=%d)\n",
         			saddr,daddr,length,pkttype);
         	BUGLVL(D_RX)
        	{
        		int countx,county;
        		
        		printk("arcnet: packet dump [rx-unsplit] follows:");

         		for (county=0; county<16+(pkttype!=NORMAL)*16; county++)
        		{
        			printk("\n[%04X] ",county*16);
        			for (countx=0; countx<16; countx++)
        				printk("%02X ",
        					arcpacket->raw[county*16+countx]);
        		}
        		
        		printk("\n");
        	}

         	/* ARP packets have problems when sent from DOS.
         	 * source address is always 0!  So we take the hardware
         	 * source addr (which is impossible to fumble) and insert
         	 * it ourselves.
         	 */
         	if (soft->protocol_id == ARC_P_ARP)
         	{
         		struct arphdr *arp=(struct arphdr *)
         				((char *)soft+sizeof(struct ClientData));

         		/* make sure addresses are the right length */
         		if (arp->ar_hln==1 && arp->ar_pln==4)
         		{
         			char *cptr=(char *)(arp)+sizeof(struct arphdr);
         			
         			if (!*cptr)	/* is saddr = 00? */
         			{
         				BUGLVL(D_DURING)
         					printk("arcnet: ARP source address was 00h, set to %02Xh.\n",
         						saddr);
         				*cptr=saddr;
         			}
         			else BUGLVL(D_DURING) 
         			{
         				printk("arcnet: ARP source address (%Xh) is fine.\n",
         					*cptr);
         			}
         		}
         		else
         		{
         			printk("arcnet: funny-shaped ARP packet. (%Xh, %Xh)\n",
         				arp->ar_hln,arp->ar_pln);
         		}
         	}
		skb->protocol=arc_type_trans(skb,dev);
         	netif_rx(skb);
         	lp->stats.rx_packets++;
         }
         else			/* split packet */
         {
	         /* NOTE:  MSDOS ARP packet correction should only need to
	          * apply to unsplit packets, since ARP packets are so short.
	          *
	          * My interpretation of the RFC1201 (ARCnet) document is that
	          * if a packet is received out of order, the entire assembly
	          * process should be aborted.
	          *
	          * The RFC also mentions "it is possible for successfully
	          * received packets to be retransmitted."  As of 0.40 all
	          * previously received packets are allowed, not just the
	          * most recent one.
	          *
	          * We allow multiple assembly processes, one for each
	          * ARCnet card possible on the network.  Seems rather like
	          * a waste of memory.  Necessary?
	          */
	          
	        struct Incoming *in=&lp->incoming[saddr];
	        
	        BUGLVL(D_RX) printk("arcnet: packet is split (splitflag=%d, seq=%d)\n",
	        	arcsoft->split_flag,in->sequence);

		if (in->skb && in->sequence!=arcsoft->sequence)
		{
			BUGLVL(D_INIT) printk("arcnet: wrong seq number, aborting assembly (expected=%d, seq=%d, splitflag=%d)\n",	
				in->sequence,arcsoft->sequence,
				arcsoft->split_flag);
	        	kfree_skb(in->skb,FREE_WRITE);
	        	in->skb=NULL;
	        	in->lastpacket=in->numpackets=0;
	        }
	          
	        if (arcsoft->split_flag & 1)	/* first packet in split */
	        {
	        	BUGLVL(D_RX) printk("arcnet: brand new splitpacket (splitflag=%d)\n",
	        		arcsoft->split_flag);
	        	if (in->skb)	/* already assembling one! */
	        	{
	        		BUGLVL(D_INIT) printk("arcnet: aborting previous (seq=%d) assembly (splitflag=%d, seq=%d)\n",
	        			in->sequence,arcsoft->split_flag,
	        			arcsoft->sequence);
	        		kfree_skb(in->skb,FREE_WRITE);
	        	}

			in->sequence=arcsoft->sequence;
	        	in->numpackets=((unsigned)arcsoft->split_flag>>1)+2;
	        	in->lastpacket=1;
	        	
	        	if (in->numpackets>16)
	        	{
	        		printk("arcnet: incoming packet more than 16 segments; dropping. (splitflag=%d)\n",
	        			arcsoft->split_flag);
	        		lp->stats.rx_dropped++;
	        		return;
	        	}
	        
                	in->skb=skb=alloc_skb(508*in->numpackets
                			+ sizeof(struct ClientData),
	                		GFP_ATOMIC);
                	if (skb == NULL) {
                		printk("%s: (split) memory squeeze, dropping packet.\n", 
                			dev->name);
                		lp->stats.rx_dropped++;
                		return;
                	}
	                		
	                /* I don't know what this is for, but it DOES avoid
	                 * warnings...
	                 */
	                skb->free=1;
	                
                	soft=(struct ClientData *)skb->data;
                	
                	skb->len=sizeof(struct ClientData);
                	skb->dev=dev;

	         	memcpy((u_char *)soft+EXTRA_CLIENTDATA,
        	 		(u_char *)arcsoft+EXTRA_CLIENTDATA,
         			sizeof(struct ClientData)-EXTRA_CLIENTDATA);
         		soft->split_flag=0; /* final packet won't be split */
	        }
	        else			/* not first packet */
	        {
			int packetnum=((unsigned)arcsoft->split_flag>>1) + 1;

			/* if we're not assembling, there's no point
			 * trying to continue.
			 */			
			if (!in->skb)
			{
				BUGLVL(D_INIT) printk("arcnet: can't continue split without starting first! (splitflag=%d, seq=%d)\n",
					arcsoft->split_flag,arcsoft->sequence);
				return;
			}

	        	in->lastpacket++;
	        	if (packetnum!=in->lastpacket) /* not the right flag! */
	        	{
				/* harmless duplicate? ignore. */
				if (packetnum<=in->lastpacket-1)
				{
					BUGLVL(D_INIT) printk("arcnet: duplicate splitpacket ignored! (splitflag=%d)\n",
						arcsoft->split_flag);
					return;
				}
				
				/* "bad" duplicate, kill reassembly */
				BUGLVL(D_INIT) printk("arcnet: out-of-order splitpacket, reassembly (seq=%d) aborted (splitflag=%d, seq=%d)\n",	
					in->sequence,arcsoft->split_flag,
					arcsoft->sequence);
	        		kfree_skb(in->skb,FREE_WRITE);
	        		in->skb=NULL;
	        		in->lastpacket=in->numpackets=0;
	        		return;
	        	}

	               	soft=(struct ClientData *)in->skb->data;
	        }
	        
	        skb=in->skb;
	        
	        memcpy(skb->data+skb->len,
	               (u_char *)arcsoft+sizeof(struct ClientData),
	               length-sizeof(struct ClientData));

         	skb->len+=length-sizeof(struct ClientData);
         	
         	soft->daddr=daddr;
         	soft->saddr=saddr;
         	
         	BUGLVL(D_DURING)
         		printk("arcnet: received packet from %02Xh to %02Xh (%d bytes, type=%d)\n",
         			saddr,daddr,length,pkttype);
         	BUGLVL(D_RX)
        	{
        		int countx,county;
        		
        		printk("arcnet: packet dump [rx-split] follows:");

         		for (county=0; county<16+(pkttype!=NORMAL)*16; county++)
        		{
        			printk("\n[%04X] ",county*16);
        			for (countx=0; countx<16; countx++)
        				printk("%02X ",
        					arcpacket->raw[county*16+countx]);
        		}
        		
        		printk("\n");
        	}
         	
         	/* are we done? */
         	if (in->lastpacket == in->numpackets)
         	{
         		if (!skb || !in->skb)
         			printk("arcnet: ?!? done reassembling packet, no skb? (skb=%ph, in->skb=%ph)\n",
         				skb,in->skb);
         		in->skb=NULL;
         		in->lastpacket=in->numpackets=0;
			skb->protocol=arc_type_trans(skb,dev);         		
	         	netif_rx(skb);
        	 	lp->stats.rx_packets++;
        	}
         }
	
	/* If any worth-while packets have been received, netif_rx()
	   has done a mark_bh(NET_BH) for us and will work on them
	   when we get to the bottom-half routine. */
}


#ifdef USE_TIMER_HANDLER
/* this function is called every once in a while to make sure the ARCnet
 * isn't stuck.
 *
 * If we miss a receive IRQ, the receiver (and IRQ) is permanently disabled
 * and we might never receive a packet again!  This will check if this
 * is the case, and if so, re-enable the receiver.
 */
static void
arcnet_timer(unsigned long arg)
{
	struct device *dev=(struct device *)arg;
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	short ioaddr=dev->base_addr;
	int status=inb(STATUS);

	/* if we didn't interrupt the IRQ handler, and RX's are still
	 * disabled, and we're not resetting the card... then we're stuck!
	 */
	if (!dev->interrupt && dev->start
	  && status&NORXflag && !status&RESETflag)
	{
		BUGLVL(D_INIT)
			printk("arcnet: timer: ARCnet was stuck!  (status=%Xh)\n",
				status);
		
		arcnet_inthandler(dev);
	}

	/* requeue ourselves */
	init_timer(&lp->timer);
	lp->timer.expires=TIMERval;
	add_timer(&lp->timer);
}
#endif

/* Get the current statistics.	This may be called with the card open or
   closed. */
static struct enet_statistics *
arcnet_get_stats(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
/*	short ioaddr = dev->base_addr;*/

	return &lp->stats;
}

/* Set or clear the multicast filter for this adaptor.
   num_addrs == -1	Promiscuous mode, receive all packets
   num_addrs == 0	Normal mode, clear multicast list
   num_addrs > 0	Multicast mode, receive normal and MC packets, and do
			best-effort filtering.
 */
static void
set_multicast_list(struct device *dev, int num_addrs, void *addrs)
{
#if 0	  /* no promiscuous mode at all */
	struct arcnet_local *lp=(struct arcnet_local *)(dev->priv);

	short ioaddr = dev->base_addr;
	if (num_addrs) {
		outw(69, ioaddr);		/* Enable promiscuous mode */
	} else
		outw(99, ioaddr);		/* Disable promiscuous mode, use normal mode */
#endif
}

int arcnet_reset(struct device *dev)
{
	struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
	short ioaddr=dev->base_addr;
	int delayval,recbuf=lp->recbuf;
	
	outb(0,INTMASK);	/* no IRQ's, please! */
	
	BUGLVL(D_INIT)
		printk("arcnet: Resetting %s (status=%Xh)\n",
			dev->name,inb(STATUS));

	inb(RESET);		/* Reset by reading this port */
	JIFFER(RESETtime);

	outb(CFLAGScmd|RESETclear, COMMAND); /* clear flags & end reset */
	outb(CFLAGScmd|CONFIGclear,COMMAND);

	/* after a reset, the first byte of shared mem is TESTvalue and the
	 * second byte is our 8-bit ARCnet address
	 */
	{
		u_char *cardmem = (u_char *) dev->mem_start;
		if (cardmem[0] != TESTvalue)
		{
			BUGLVL(D_INIT)
				printk("arcnet: reset failed: TESTvalue not present.\n");
			return 1;
		}
		lp->arcnum=cardmem[1];  /* save address for later use */
	}
	
	/* clear out status variables */
	recbuf=lp->recbuf=0;
	lp->txbuf=2;
	/*dev->tbusy=0;*/

	/* enable extended (512-byte) packets */
	outb(CONFIGcmd|EXTconf,COMMAND);
	XJIFFER(ACKtime);
	
	/* clean out all the memory to make debugging make more sense :) */
	BUGLVL(D_DURING)
		memset((void *)dev->mem_start,0x42,2048);
	
	/* and enable receive of our first packet to the first buffer */
	EnableReceiver();

	/* re-enable interrupts */
	outb(NORXflag,INTMASK);
	
	/* done!  return success. */
	return 0;
}


/*
 *	 Create the ARCnet ClientData header for an arbitrary protocol layer
 *
 *	saddr=NULL	means use device source address (always will anyway)
 *	daddr=NULL	means leave destination address (eg unresolved arp)
 */
int arc_header(unsigned char *buff,struct device *dev,unsigned short type,
		void *daddr,void *saddr,unsigned len,struct sk_buff *skb)
{
	struct ClientData *head = (struct ClientData *)buff;
	struct arcnet_local *lp=(struct arcnet_local *)(dev->priv);

	/* set the protocol ID according to RFC-1201 */
	switch(type)
	{
	case ETH_P_IP:
		head->protocol_id=ARC_P_IP;
		break;
	case ETH_P_ARP:
		head->protocol_id=ARC_P_ARP;
		break;
	case ETH_P_RARP:
		head->protocol_id=ARC_P_RARP;
		break;
	case ETH_P_IPX:
		head->protocol_id=ARC_P_IPX;
		break;
	case ETH_P_ATALK:
		head->protocol_id=ARC_P_ATALK;
		break;
	default:
		printk("arcnet: I don't understand protocol %d (%Xh)\n",
			type,type);
		return 0;
	}	

#if 1
	/*
	 *	Set the source hardware address.
	 *	AVE: we can't do this, so we don't.  Code below is directly
	 *	     stolen from eth.c driver and won't work.
	 ** TM: but for debugging I would like to have saddr in the header
	 */
	if(saddr)
		head->saddr=((u_char*)saddr)[0];
	else
		head->saddr=((u_char*)(dev->dev_addr))[0];
#endif

#if 0
	/*
	 *	Anyway, the loopback-device should never use this function... 
	 *
	 *	And the chances of it using the ARCnet version of it are so
	 *	tiny that I don't think we have to worry :)
	 */
	if (dev->flags & IFF_LOOPBACK) 
	{
		head->daddr=0;
		return(dev->hard_header_len);
	}
#endif

	head->split_flag=0;	/* split packets are done elsewhere */
	head->sequence=(lp->sequence++);

	/* supposedly if daddr is NULL, we should ignore it... */
	if(daddr)
	{
		head->daddr=((u_char*)daddr)[0];
		return dev->hard_header_len;
	}
	else
		head->daddr=0;	/* better fill one in anyway */
		
	return -dev->hard_header_len;
}


/*
 *	Rebuild the ARCnet ClientData header. This is called after an ARP
 *	(or in future other address resolution) has completed on this
 *	sk_buff. We now let ARP fill in the other fields.
 */
int arc_rebuild_header(void *buff,struct device *dev,unsigned long dst,
		struct sk_buff *skb)
{
	struct ClientData *head = (struct ClientData *)buff;

	/*
	 *	Only ARP/IP is currently supported
	 */
	 
	if(head->protocol_id != ARC_P_IP) 
	{
		printk("arcnet: I don't understand resolve type %d (%Xh) addresses!\n",
			head->protocol_id,head->protocol_id);
		head->daddr=0;
		/*memcpy(eth->h_source, dev->dev_addr, dev->addr_len);*/
		return 0;
	}

	/*
	 *	Try and get ARP to resolve the header.
	 */
#ifdef CONFIG_INET	 
	return arp_find(&(head->daddr), dst, dev, dev->pa_addr, skb)? 1 : 0;
#else
	return 0;	
#endif	
}
		
/*
 *	Determine the packet's protocol ID.
 *
 *	With ARCnet we have to convert everything to Ethernet-style stuff.
 */
unsigned short arc_type_trans(struct sk_buff *skb,struct device *dev)
{
	struct ClientData *head = (struct ClientData *) skb->data;
	
	if (head->daddr==0)
		skb->pkt_type=PACKET_BROADCAST;
	else if(dev->flags&IFF_PROMISC)
	{
		/* if we're not sending to ourselves :) */
		if (head->daddr != dev->dev_addr[0])
			skb->pkt_type=PACKET_OTHERHOST;
	}
	
	/* now return the protocol number */
	switch (head->protocol_id)
	{
	case ARC_P_IP:		return htons(ETH_P_IP);
	case ARC_P_ARP:		return htons(ETH_P_ARP);
	case ARC_P_RARP:	return htons(ETH_P_RARP);
	case ARC_P_IPX:		return htons(ETH_P_IPX);
	case ARC_P_ATALK:   	return htons(ETH_P_ATALK);	/* Doesn't work yet */
	case ARC_P_LANSOFT: /* don't understand.  fall through. */
	default:
		BUGLVL(D_DURING)
			printk("arcnet: received packet of unknown protocol id %d (%Xh)\n",
				head->protocol_id,head->protocol_id);
		return 0;
	}
	return htons(ETH_P_IP);
}

#ifdef MODULE
char kernel_version[] = UTS_RELEASE;
static struct device thisARCnet = {
  "      ",/* if blank, device name inserted by /linux/drivers/net/net_init.c */
  0, 0, 0, 0,
  0, 0,  /* I/O address, IRQ */
  0, 0, 0, NULL, arcnet_probe };
	
	
int io=0x0;	/* <--- EDIT THESE LINES FOR YOUR CONFIGURATION */
int irqnum=0;	/* or use the insmod io= irq= shmem= options */
int shmem=0;
int num=0;	/* number of device (ie for arc0, arc1, arc2...) */

int
init_module(void)
{
	sprintf(thisARCnet.name,"arc%d",num);

	thisARCnet.base_addr=io;

	thisARCnet.irq=irqnum;
	if (thisARCnet.irq==2) thisARCnet.irq=9;

	if (shmem)
	{
		thisARCnet.mem_start=shmem;
		thisARCnet.mem_end=thisARCnet.mem_start+512*4-1;
		thisARCnet.rmem_start=thisARCnet.mem_start+512*0;
		thisARCnet.rmem_end=thisARCnet.mem_start+512*2-1;
	}

	if (register_netdev(&thisARCnet) != 0)
		return -EIO;
	return 0;
}

void
cleanup_module(void)
{
  if (MOD_IN_USE) {
    printk("%s: device busy, remove delayed\n",thisARCnet.name);
  } else {
    if (thisARCnet.start) arcnet_close(&thisARCnet);
    if (thisARCnet.irq) free_irq(thisARCnet.irq);
    if (thisARCnet.base_addr) release_region(thisARCnet.base_addr,
    						ETHERCARD_TOTAL_SIZE);
    unregister_netdev(&thisARCnet);
  }
}

#endif /* MODULE */



/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c skeleton.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 4
 * End:
 */

