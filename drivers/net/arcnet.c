/* arcnet.c
	Written 1994-95 by Avery Pennarun, derived from skeleton.c by
        Donald Becker.

	Contact Avery at: apenwarr@foxnet.net or
	RR #5 Pole Line Road, Thunder Bay, ON, Canada P7C 5M9
	
	**********************
	
	The original copyright was as follows:

	skeleton.c Written 1993 by Donald Becker.
	Copyright 1993 United States Government as represented by the
        Director, National Security Agency.  This software may only be used
        and distributed according to the terms of the GNU Public License as
        modified by SRC, incorporated herein by reference.
         
	**********************
	
	v1.92 ALPHA (95/07/11)
	  - Fixes to make things work with kernel 1.3.x.  Completely broke
	    1.2.x support.  Oops?  1.2.x users keep using 1.91 ALPHA until I
	    get out a version that supports both.

	v1.91 ALPHA (95/07/02)
	  - Oops.  Exception packets hit us again!  I remembered to test
	    them in Windows-protocol mode, but due to the many various
	    changes they broke in RFC1201 instead.  All fixed.
	  - A long-standing bug with "exception" packets not setting
	    protocol_id properly has been corrected.  This would have caused
	    random problems talking to non-Linux servers.  I've also sent in
	    a patch to fix this in the latest stable ARCnet (now 1.02).
	  - ARC_P_IPX is an RFC1201 protocol too.  Thanks, Tomasz.
	  - We're now "properly" (I think) handling the multiple 'tbusy' and
	    'start' flags (one for each protocol device) better.
	  - The driver should now start without a NULL-pointer dereference
	    if you aren't connected to the network.
	  
	v1.90 ALPHA (95/06/18)
	  - Removal of some outdated and messy config options (no one has
	    ever complained about the defaults since they were introduced):
	    DANGER_PROBE, EXTRA_DELAYS, IRQ_XMIT, CAREFUL_XMIT,
	    STRICT_MEM_DETECT, LIMIT_MTU, USE_TIMER_HANDLER.  Also took out
	    a few "#if 0" sections which are no longer useful.
	  - Cleaned up debug levels - now instead of levels, there are
	    individual flags.  Watch out when changing with ifconfig.
	  - More cleanups and beautification.  Removed more dead code and
	    made sure every function was commented.
	  - Fixed the DETECT_RECONFIGS option so that it actually _won't_
	    detect reconfigs.  Previously, the RECON irq would be disabled
	    but the recon messages would still be logged on the next normal
	    IRQ.
	  - Initial support for "multiprotocol" ARCnet (this involved a LOT
	    of reorganizing!).  Added an arc0w device, which allows us to
	    talk to "Windows" ARCnet TCP/IP protocol.  To use it, ifconfig
	    arc0 and arc0w (in that order).  For now, Windows-protocol
	    hosts should have routes through arc0w - eventually I hope to
	    make things more automatic.
	v1.11 ALPHA (95/06/07)
	  - Tomasz saves the day again with patches to fix operation if the
	    new VERIFY_ACK option is disabled.
	  - LOTS of little code cleanups/improvements by Tomasz.
	  - Changed autoprobe, since the "never-changing command port"
	    probe was causing problems for some people.  I also reset the
	    card fewer times during the probe if DANGER_PROBE is defined,
	    since DANGER_PROBE seems to be a more reliable method anyway.
	  - It looks like the null-pointer problem was finally REALLY fixed
	    by some change from Linux 1.2.8 to 1.2.9.  How handy!
	v1.10 ALPHA (95/04/15)
	  - Fixed (?) some null-pointer dereference bugs
	  - Added better network error detection (from Tomasz) - in
	    particular, we now notice when our network isn't connected,
	    also known as a "network reconfiguration."
	  - We now increment lp->stats.tx_dropped in several more places,
	    on a suggestion from Tomasz.
	  - Minor cleanups/spelling fixes.
	  - We now monitor the TXACK bit in the status register: we don't do
	    anything with it yet, just notice when a transmitted packet isn't
	    acknowledged.
	  - Minor fix with sequence numbers (sometimes they were being sent in
	    the wrong order due to Linux's packet queuing).
	v1.01 (95/03/24)
	  - Fixed some IPX-related bugs. (Thanks to Tomasz Motylewski
            <motyl@tichy.ch.uj.edu.pl> for the patches to make arcnet work
            with dosemu!)
	v1.00 (95/02/15)
	  - Initial non-alpha release.
	
         
	TO DO:
	
         - Test in systems with NON-ARCnet network cards, just to see if
           autoprobe kills anything.  Currently, we do cause some NE2000's to
           die.
         - What about cards with shared memory that can be "turned off?"
         - NFS mount freezes after several megabytes to SOSS for DOS. 
 	   unmount/remount fixes it.  Is this arcnet-specific?  I don't know.
         - Add support for "old" (RFC1051) protocol arcnet, such as AmiTCP
           and NetBSD.  Work in Tomasz' initial support for this.
         - How about TCP/IP over netbios?
         - Some newer ARCnets support promiscuous mode, supposedly. 
           If someone sends me information, I'll try to implement it.
         - Remove excess lock variables that are probably not necessary
           anymore due to the changes in Linux 1.2.9.

           
	Sources:
	 - Crynwr arcnet.com/arcether.com packet drivers.
	 - arcnet.c v0.00 dated 1/1/94 and apparently by
	 	Donald Becker - it didn't work :)
	 - skeleton.c v0.05 dated 11/16/93 by Donald Becker
	 	(from Linux Kernel 1.1.45)
	 - The official ARCnet data sheets (!) thanks to Ken Cornetet
		<kcornete@nyx10.cs.du.edu>
	 - RFC's 1201 and 1051 (mostly 1201) - re: ARCnet IP packets
	 - net/inet/eth.c (from kernel 1.1.50) for header-building info...
	 - Alternate Linux ARCnet source by V.Shergin <vsher@sao.stavropol.su>
	 - Textual information and more alternate source from Joachim Koenig
	 	<jojo@repas.de>
*/

static const char *version =
 "arcnet.c:v1.92 ALPHA 95/07/11 Avery Pennarun <apenwarr@foxnet.net>\n";
 
/**************************************************************************/

/* Define this if you want to detect network reconfigurations.
 * They may be a real nuisance on a larger ARCnet network: but if you are
 * a network administrator you probably would like to count them. 
 * Reconfigurations will be recorded in stats.tx_carrier_errors 
 * (the last field of the /proc/net/dev file).
 *
 * The card sends the reconfiguration signal when it loses the connection
 * to the rest of its network. It is a 'Hello, is anybody there?' cry.  This
 * usually happens when a new computer on the network is powered on or when
 * the cable is broken.
 */
#define DETECT_RECONFIGS

/* Define this if you want to make sure transmitted packets are "acknowledged"
 * by the destination host, as long as they're not to the broadcast address.
 *
 * That way, if one segment of a split packet doesn't get through, it can
 * be resent immediately rather than confusing the other end.
 *
 * Disable this to return to 1.01-style behaviour, if you have problems.
 */
#define VERIFY_ACK

/* Define this if you want to make it easier to use the "call trace" when
 * a kernel NULL pointer assignment occurs.
 */
#undef static

/**************************************************************************/
 

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


/* new debugging bitflags: each option can be enabled individually.
 *
 * these can be set while the driver is running by typing:
 *	ifconfig arc0 down metric 1xxx HOSTNAME
 *		where 1xx is 1000 + the debug level you want
 *		and HOSTNAME is your hostname/ip address
 * and then resetting your routes.
 */
#define D_NORMAL	1	/* D_NORMAL  startup announcement	*/
#define	D_INIT		2	/* D_INIT    show init/probe messages	*/
#define D_EXTRA		4	/* D_EXTRA   extra information		*/
/* debug levels past this point give LOTS of output! */
#define D_DURING	8	/* D_DURING  during normal use (irq's)	*/
#define D_TX		16	/* D_TX	     show tx packets		*/
#define D_RX		32	/* D_RX	     show rx packets		*/
#define D_SKB		64	/* D_SKB     dump skb's			*/

#ifndef NET_DEBUG
#define NET_DEBUG D_NORMAL|D_INIT|D_EXTRA
#endif
int arcnet_debug = NET_DEBUG;

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
#define BUGLVL(x) if (arcnet_debug&(x))


/* Some useful multiprotocol macros */
#define TBUSY lp->adev->tbusy \
		=lp->wdev->tbusy
#define IF_TBUSY (lp->adev->tbusy \
		|| lp->wdev->tbusy)
#define START lp->adev->start \
		=lp->wdev->start


/* The number of low I/O ports used by the ethercard. */
#define ARCNET_TOTAL_SIZE	16


/* Handy defines for ARCnet specific stuff */
	/* COM 9026 (?) --> ARCnet register addresses */
#define INTMASK	(ioaddr+0)		/* writable */
#define STATUS	(ioaddr+0)		/* readable */
#define COMMAND (ioaddr+1)	/* writable, returns random vals on read (?) */
#define RESET  (ioaddr+8)		/* software reset writable */

	/* Time needed for various things (in clock ticks, 1/100 sec) */
	/* We mostly don't bother with these - watch out. */
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
#define MTU	253	/* normal packet max size */
#define MinTU	257	/* extended packet min size */
#define XMTU	508	/* extended packet max size */

	/* status/interrupt mask bit fields */
#define TXFREEflag	0x01            /* transmitter available */
#define TXACKflag       0x02            /* transmitted msg. ackd */
#define RECONflag       0x04            /* system reconfigured */
#define TESTflag        0x08            /* test flag */
#define RESETflag       0x10            /* power-on-reset */
#define RES1flag        0x20            /* unused */
#define RES2flag        0x40            /* unused */
#define NORXflag        0x80            /* receiver inhibited */

#ifdef DETECT_RECONFIGS
	#define RECON_flag RECONflag
#else
	#define RECON_flag 0
#endif

       /* in the command register, the following bits have these meanings:
        *                0-2     command
        *                3-4     page number (for enable rcv/xmt command)
        *                 7      receive broadcasts
        */
#define NOTXcmd         0x01            /* disable transmitter */
#define NORXcmd         0x02            /* disable receiver */
#define TXcmd           0x03            /* enable transmitter */
#define RXcmd           0x04            /* enable receiver */
#define CONFIGcmd       0x05            /* define configuration */
#define CFLAGScmd       0x06            /* clear flags */
#define TESTcmd         0x07            /* load test flags */

       /* flags for "clear flags" command */
#define RESETclear      0x08            /* power-on-reset */
#define CONFIGclear     0x10            /* system reconfigured */

      /* flags for "load test flags" command */
#define TESTload        0x08            /* test flag (diagnostic) */

      /* byte deposited into first address of buffers on reset */
#define TESTvalue       0321		 /* that's octal for 0xD1 :) */

      /* for "enable receiver" command */
#define RXbcasts        0x80            /* receive broadcasts */

      /* flags for "define configuration" command */
#define NORMALconf      0x00            /* 1-249 byte packets */
#define EXTconf         0x08            /* 250-504 byte packets */

	/* buffers (4 total) used for receive and xmit.
	 */
#define EnableReceiver()	outb(RXcmd|(recbuf<<3)|RXbcasts,COMMAND)

	/* RFC1201 Protocol ID's */
#define ARC_P_IP	212		/* 0xD4 */
#define ARC_P_ARP	213		/* 0xD5 */
#define ARC_P_RARP	214		/* 0xD6 */
#define ARC_P_IPX	250		/* 0xFA */

	/* MS LanMan/WfWg protocol */
#define ARC_P_MS_TCPIP	0xE8

	/* Unsupported/indirectly supported protocols */
#define ARC_P_LANSOFT	251		/* 0xFB */
#define ARC_P_ATALK	0xDD

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
	/* data that's NOT part of real packet - we MUST get rid of it before
	 * actually sending!!
	 */
	u_char  saddr,		/* Source address - needed for IPX */
		daddr;		/* Destination address */
	
	/* data that IS part of real packet */
	u_char	protocol_id,	/* ARC_P_IP, ARC_P_ARP, etc */
		split_flag;	/* for use with split packets */
	u_short	sequence;	/* sequence number */
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
#ifdef VERIFY_ACK
	short lastload_dest,		/* can last loaded packet be acked? */
	      lasttrans_dest;		/* can last TX'd packet be acked? */
#endif
	
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
	
	struct device *adev;	/* RFC1201 protocol device */
	struct device *wdev;	/* Windows protocol device */
};


/* Index to functions, as function prototypes. */
extern int arcnet_probe(struct device *dev);
#ifndef MODULE
static int arcnet_memprobe(struct device *dev,u_char *addr);
static int arcnet_ioprobe(struct device *dev, short ioaddr);
#endif
static int arcnetW_init(struct device *dev);

static int arcnet_open(struct device *dev);
static int arcnet_close(struct device *dev);
static int arcnet_reset(struct device *dev);

static int arcnetA_send_packet(struct sk_buff *skb, struct device *dev);
static void arcnetA_continue_tx(struct device *dev);
static void arcnetA_prepare_tx(struct device *dev,struct ClientData *hdr,
		short length,char *data);
static void arcnetA_go_tx(struct device *dev);

static int arcnetW_send_packet(struct sk_buff *skb, struct device *dev);

static void arcnet_interrupt(int irq,struct pt_regs *regs);
static void arcnet_inthandler(struct device *dev);

static void arcnet_rx(struct device *dev,int recbuf);
static void arcnetA_rx(struct device *dev,struct ClientData *arcsoft,
	int length,u_char saddr, u_char daddr);
static void arcnetW_rx(struct device *dev,u_char *arcsoft,
	int length,u_char saddr, u_char daddr);

static struct enet_statistics *arcnet_get_stats(struct device *dev);
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);

	/* annoying functions for header/arp/etc building */
int arcnetA_header(struct sk_buff *skb,struct device *dev,unsigned short type,
		void *daddr,void *saddr,unsigned len);
int arcnetA_rebuild_header(void *eth,struct device *dev,unsigned long raddr,
		struct sk_buff *skb);
unsigned short arcnetA_type_trans(struct sk_buff *skb,struct device *dev);

#ifdef MODULE
int  init_module(void);
void cleanup_module(void);
#endif

#define tx_done(dev) 1

#define JIFFER(time) for (delayval=0; delayval<(time*10); delayval++) \
		udelay(1000);
		

/****************************************************************************
 *                                                                          *
 * Probe and initialization                                                 *
 *                                                                          *
 ****************************************************************************/
 

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

	BUGLVL(D_NORMAL)
	{
		printk(version);
		printk("arcnet: ***\n");
		printk("arcnet: * Read linux/drivers/net/README.arcnet for important release notes!\n");
		printk("arcnet: *\n");
		printk("arcnet: * This is an ALPHA version!  (Last stable release: v1.02)  E-mail me if\n");
		printk("arcnet: * you have any questions, comments, or bug reports.\n");
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
		if (check_region(ioaddr, ARCNET_TOTAL_SIZE))
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
	request_region(dev->base_addr, ARCNET_TOTAL_SIZE,"arcnet");
	
	printk("%s: ARCnet card found at %03lXh, IRQ %d, ShMem at %lXh.\n", 
		dev->name, dev->base_addr, dev->irq, dev->mem_start);

	/* Initialize the device structure. */
	dev->priv = kmalloc(sizeof(struct arcnet_local), GFP_KERNEL);
	memset(dev->priv, 0, sizeof(struct arcnet_local));
	lp=(struct arcnet_local *)(dev->priv);

	dev->open=arcnet_open;
	dev->stop=arcnet_close;
	dev->hard_start_xmit=arcnetA_send_packet;
	dev->get_stats=arcnet_get_stats;
#ifdef HAVE_MULTICAST
	dev->set_multicast_list = &set_multicast_list;
#endif

	/* Fill in the fields of the device structure with ethernet-generic
	 * values.
	 */
	ether_setup(dev);
	
	/* And now fill particular fields with arcnet values */
	dev->type=ARPHRD_ARCNET;
	dev->hard_header_len=sizeof(struct ClientData);
	BUGLVL(D_DURING)
		printk("arcnet: ClientData header size is %d.\narcnet: HardHeader size is %d.\n",
			sizeof(struct ClientData),sizeof(struct HardHeader));

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

	dev->hard_header=arcnetA_header;
	dev->rebuild_header=arcnetA_rebuild_header;

	return 0;
}

#ifndef MODULE

int arcnet_ioprobe(struct device *dev, short ioaddr)
{
	int delayval,airq;

	BUGLVL(D_INIT)
	{
		printk("arcnet: probing address %Xh\n",ioaddr);
		printk("arcnet:  status1=%Xh\n",inb(STATUS));
	}

		
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

#if 0
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
#endif

	BUGLVL(D_INIT)
		printk("arcnet:  status2=%Xh\n",inb(STATUS));

	/* now we turn the reset bit off so we can IRQ next reset... */
	outb(CFLAGScmd|RESETclear|CONFIGclear,COMMAND);
	if (inb(STATUS) & RESETflag) /* reset flag STILL on */
	{
		BUGLVL(D_INIT)
			printk("arcnet:  probe failed.  eternal reset flag1...(status=%Xh)\n",
				inb(STATUS));
		return ENODEV;
	}

	/* set up automatic IRQ detection */
	autoirq_setup(0);
	

	/* if we do this, we're sure to get an IRQ since the card has
	 * just reset and the NORXflag is on until we tell it to start
	 * receiving.
	 *
	 * However, this could, theoretically, cause a lockup.  Maybe I'm just
	 * not very good at theory! :)
	 */
	outb(NORXflag,INTMASK);
	JIFFER(RESETtime);
	outb(0,INTMASK);

	/* and turn the reset flag back off */
	outb(CFLAGScmd|RESETclear|CONFIGclear,COMMAND);

	airq = autoirq_report(0);
	BUGLVL(D_INIT) if (airq)
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
		BUGLVL(D_NORMAL)
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
 * has certain characteristics of an ARCnet.
 */
int arcnet_memprobe(struct device *dev,u_char *addr)
{
	BUGLVL(D_INIT)
		printk("arcnet: probing memory at %lXh\n",(u_long)addr);
		
	dev->mem_start=0;

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

	/* got it!  fill in dev */
	dev->mem_start=(unsigned long)addr;
	dev->mem_end=dev->mem_start+512*4-1;
	dev->rmem_start=dev->mem_start+512*0;
	dev->rmem_end=dev->mem_start+512*2-1;

	return 0;
}

#endif /* MODULE */


int arcnet_reset(struct device *dev)
{
	struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
	short ioaddr=dev->base_addr;
	int delayval,recbuf=lp->recbuf;
	u_char *cardmem;
	
	outb(0,INTMASK);	/* no IRQ's, please! */
	
	BUGLVL(D_INIT)
		printk("arcnet: Resetting %s (status=%Xh)\n",
			dev->name,inb(STATUS));

	inb(RESET);		/* Reset by reading this port */
	JIFFER(RESETtime);

	outb(CFLAGScmd|RESETclear, COMMAND); /* clear flags & end reset */
	outb(CFLAGScmd|CONFIGclear,COMMAND);

	/* after a reset, the first byte of shared mem is TESTvalue and the
	 * second byte is our 8-bit ARCnet address.
	 */
	cardmem = (u_char *) dev->mem_start;
	if (cardmem[0] != TESTvalue)
	{
		BUGLVL(D_INIT)
			printk("arcnet: reset failed: TESTvalue not present.\n");
		return 1;
	}
	lp->arcnum=cardmem[1];  /* save address for later use */
	
	/* clear out status variables */
	recbuf=lp->recbuf=0;
	lp->txbuf=2;

	/* enable extended (512-byte) packets */
	outb(CONFIGcmd|EXTconf,COMMAND);
	
	/* clean out all the memory to make debugging make more sense :) */
	BUGLVL(D_DURING)
		memset((void *)dev->mem_start,0x42,2048);
	
	/* and enable receive of our first packet to the first buffer */
	EnableReceiver();

	/* re-enable interrupts */
	outb(NORXflag|RECON_flag,INTMASK);
	
	/* done!  return success. */
	return 0;
}

static int arcnetW_init(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

	ether_setup(lp->wdev);
	dev->dev_addr[0]=0;
	dev->dev_addr[5]=lp->arcnum;
	dev->mtu=493;	/* MTU is small because of missing packet splitting */
	lp->wdev->open=NULL;
	lp->wdev->stop=NULL;
	lp->wdev->hard_start_xmit=arcnetW_send_packet;

	BUGLVL(D_EXTRA)
		printk("%s: ARCnet \"Windows\" protocol initialized.\n",
			lp->wdev->name);
			
	return 0;
}


/****************************************************************************
 *                                                                          *
 * Open and close the driver                                                *
 *                                                                          *
 ****************************************************************************/
 

/* Open/initialize the board.  This is called sometime after booting when
 * the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
static int
arcnet_open(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

	if (dev->metric>=1000)
	{
		arcnet_debug=dev->metric-1000;
		printk("arcnet: debug level set to %d\n",arcnet_debug);
		dev->metric=1;
	}

	BUGLVL(D_NORMAL) printk(version);

	irq2dev_map[dev->irq] = dev;

	BUGLVL(D_EXTRA) printk("arcnet: arcnet_open: resetting card.\n");
	
	/* try to reset - twice if it fails the first time */
	if (arcnet_reset(dev) && arcnet_reset(dev))
		return -ENODEV;
	
	dev->tbusy=0;
	dev->interrupt=0;
	lp->intx=0;
	lp->in_txhandler=0;
	
	/* The RFC1201 driver is the default - just store */
	lp->adev=dev;
	BUGLVL(D_EXTRA)
		printk("%s:  ARCnet RFC1201 protocol initialized.\n",
			lp->adev->name);
	
	/* Initialize the Windows protocol driver */
	lp->wdev=(struct device *)kmalloc(sizeof(struct device),GFP_KERNEL);
	memcpy(lp->wdev,dev,sizeof(struct device));
	lp->wdev->name=(char *)kmalloc(10,GFP_KERNEL);
	sprintf(lp->wdev->name,"%sw",dev->name);
	lp->wdev->init=arcnetW_init;
	register_netdev(lp->wdev);

	/* we're started */
	START=1;

#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
                                 	
	return 0;
}


/* The inverse routine to arcnet_open - shuts down the card.
 */
static int
arcnet_close(struct device *dev)
{
	int ioaddr = dev->base_addr;
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

	TBUSY=1;
	START=0;
	
	/* Flush the Tx and disable Rx here.     */

	outb(0,INTMASK);	/* no IRQ's */
	outb(NOTXcmd,COMMAND);  /* disable transmit */
	outb(NORXcmd,COMMAND);	/* disable receive */
	
	/* do NOT free lp->adev!!  It's static! */
	lp->adev=NULL;
	
	/* free the Windows protocol device */
	lp->wdev->start=0;
	lp->wdev->priv=NULL;
	unregister_netdev(lp->wdev);
	kfree(lp->wdev->name);
	kfree(lp->wdev);
	lp->wdev=NULL;

	/* Update the statistics here. */
	
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif

	return 0;
}



/****************************************************************************
 *                                                                          *
 * Transmitter routines                                                     *
 *                                                                          *
 ****************************************************************************/


/* Called by the kernel in order to transmit a packet.
 */
static int
arcnetA_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr;

	lp->intx++;
	
	BUGLVL(D_DURING)
		printk("arcnet: transmit requested (status=%Xh, inTX=%d)\n",
			inb(STATUS),lp->intx);
			
	if (lp->in_txhandler)
	{
		printk("arcnet: send_packet called while in txhandler!\n");
		lp->intx--;
		return 1;
	}

	if (IF_TBUSY)
	{
		/* If we get here, some higher level has decided we are broken.
		   There should really be a "kick me" function call instead. */
		int tickssofar = jiffies - dev->trans_start;
		int recbuf=lp->recbuf;
		int status=inb(STATUS);
		
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

		BUGLVL(D_EXTRA)
			printk("arcnet: transmit timed out (status=%Xh, inTX=%d, inTXh=%d, tickssofar=%d)\n",
				status,lp->intx,lp->in_txhandler,tickssofar);
				
		lp->stats.tx_errors++;

		/* Try to restart the adaptor. */
		/*arcnet_reset(dev);*/
		
		outb(0,INTMASK);
		if (status&NORXflag) EnableReceiver();
		if (!(status&TXFREEflag)) outb(NOTXcmd,COMMAND);
		dev->trans_start = jiffies;

		if (lp->outgoing.skb)
		{
			dev_kfree_skb(lp->outgoing.skb,FREE_WRITE);
			lp->stats.tx_dropped++;
		}
		lp->outgoing.skb=NULL;

		TBUSY=0;
		lp->intx--;
		/*lp->intx=0;*/
		/*lp->in_txhandler=0;*/
		lp->txready=0;
		lp->sending=0;
		mark_bh(NET_BH);

		outb(NORXflag|RECON_flag,INTMASK);

		return 1;
	}

	/* If some higher layer thinks we've missed a tx-done interrupt
	   we are passed NULL. Caution: dev_tint() handles the cli()/sti()
	   itself. */
	if (skb == NULL) {
		BUGLVL(D_NORMAL)
			printk("arcnet: tx passed null skb (status=%Xh, inTX=%d, tickssofar=%ld)\n",
				inb(STATUS),lp->intx,jiffies-dev->trans_start);
		dev_tint(dev);
		lp->intx--;
		return 0;
	}
	
	if (lp->txready)	/* transmit already in progress! */
	{
		printk("arcnet: trying to start new packet while busy! (status=%Xh)\n",
			inb(STATUS));
		/*printk("arcnet: marking as not ready.\n");*/
		outb(0,INTMASK);
		outb(NOTXcmd,COMMAND); /* abort current send */
		arcnet_inthandler(dev); /* fake an interrupt */
		lp->stats.tx_errors++;
		lp->intx--;
		lp->txready=0;	/* we definitely need this line! */
		
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
		
		TBUSY=1;
		
		out->length = 1 < skb->len ? skb->len : 1;
		out->hdr=(struct ClientData*)skb->data;
		out->skb=skb;
		
		BUGLVL(D_SKB)
		{
			short i;
			for( i=0; i< skb->len; i++)
			{
				if( i%16 == 0 ) printk("\n[%04hX] ",i);
				printk("%02hX ",((unsigned char*)skb->data)[i]);
			}
			printk("\n");
		}

		out->hdr->sequence=(lp->sequence++);

		if (lp->txready && inb(STATUS)&TXFREEflag)
			arcnetA_go_tx(dev);
		
		/* fits in one packet? */
		if (out->length-EXTRA_CLIENTDATA<=XMTU)
		{
			BUGLVL(D_DURING)
				printk("arcnet: not splitting %d-byte packet. (split_flag=%d)\n",
					out->length,out->hdr->split_flag);
			BUGLVL(D_EXTRA) if (out->hdr->split_flag)
				printk("arcnet: short packet has split_flag set?! (split_flag=%d)\n",
					out->hdr->split_flag);
			out->numsegs=1;
			out->segnum=1;
			arcnetA_prepare_tx(dev,out->hdr,
				out->length-sizeof(struct ClientData),
				((char *)skb->data)+sizeof(struct ClientData));

			/* done right away */
			dev_kfree_skb(out->skb,FREE_WRITE);
			out->skb=NULL;
					
			if (!lp->sending)
			{
				arcnetA_go_tx(dev);
				
				/* inform upper layers */
				TBUSY=0;
				mark_bh(NET_BH);
			}
		}
		else			/* too big for one - split it */
		{
			int maxsegsize=XMTU-4;

			out->data=(u_char *)skb->data
					+ sizeof(struct ClientData);
			out->dataleft=out->length-sizeof(struct ClientData);
			out->numsegs=(out->dataleft+maxsegsize-1)/maxsegsize;
				
			out->segnum=0;
				
			BUGLVL(D_TX) printk("arcnet: packet (%d bytes) split into %d fragments:\n",
				out->length,out->numsegs);

			/* if a packet waiting, launch it */
			if (lp->txready && inb(STATUS)&TXFREEflag)
				arcnetA_go_tx(dev);

			if (!lp->txready)
			{
				/* prepare a packet, launch it and prepare
                                 * another.
                                 */
				arcnetA_continue_tx(dev);
				if (!lp->sending)
				{
					arcnetA_go_tx(dev);
					arcnetA_continue_tx(dev);
					if (!lp->sending)
						arcnetA_go_tx(dev);
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
			}
		}
	}

	lp->intx--;
	lp->stats.tx_packets++;
	dev->trans_start=jiffies;
	return 0;
}

/* After an RFC1201 split packet has been set up, this function calls
 * arcnetA_prepare_tx to load the next segment into the card.  This function
 * does NOT automatically call arcnetA_go_tx to allow for easier double-
 * buffering.
 */
static void arcnetA_continue_tx(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int maxsegsize=XMTU-4;
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

	arcnetA_prepare_tx(dev,out->hdr,out->seglen,out->data);
		
	out->dataleft-=out->seglen;
	out->data+=out->seglen;
	out->segnum++;
}


/* Given an skb, copy a packet into the ARCnet buffers for later transmission
 * by arcnetA_go_tx.
 */
static void
arcnetA_prepare_tx(struct device *dev,struct ClientData *hdr,short length,
		char *data)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	struct ClientData *arcsoft;
	union ArcPacket *arcpacket = 
		(union ArcPacket *)(dev->mem_start+512*(lp->txbuf^1));
	u_char pkttype;
	int offset;
	short daddr;
	
	lp->txbuf=lp->txbuf^1;	/* XOR with 1 to alternate between 2 and 3 */
	
	length+=4;

	BUGLVL(D_TX)
		printk("arcnet: arcnetA_prep_tx: hdr:%ph, length:%d, data:%ph\n",
			hdr,length,data);

	/* clean out the page to make debugging make more sense :) */
	BUGLVL(D_DURING)
		memset((void *)dev->mem_start+lp->txbuf*512,0x42,512);

	daddr=arcpacket->hardheader.destination=hdr->daddr;

	/* load packet into shared memory */
	if (length<=MTU)	/* Normal (256-byte) Packet */
	{
		pkttype=NORMAL;
			
		arcpacket->hardheader.offset1=offset=256-length;
		arcsoft=(struct ClientData *)
			(&arcpacket->raw[offset-EXTRA_CLIENTDATA]);
	}
	else if (length>=MinTU)	/* Extended (512-byte) Packet */
	{
		pkttype=EXTENDED;
		
		arcpacket->hardheader.offset1=0;
		arcpacket->hardheader.offset2=offset=512-length;
		arcsoft=(struct ClientData *)
			(&arcpacket->raw[offset-EXTRA_CLIENTDATA]);
	}
	else				/* Exception Packet */
	{
		pkttype=EXCEPTION;
		
		arcpacket->hardheader.offset1=0;
		arcpacket->hardheader.offset2=offset=512-length-4;
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
		(u_char*)hdr+EXTRA_CLIENTDATA,4);
	memcpy((u_char*)arcsoft+sizeof(struct ClientData),
		data,length-4);
		
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

#ifdef VERIFY_ACK
	lp->outgoing.lastload_dest=hdr->daddr;
#endif
	lp->txready=lp->txbuf;	/* packet is ready for sending */
}

/* Actually start transmitting a packet that was placed in the card's
 * buffer by arcnetA_prepare_tx.
 */
static void
arcnetA_go_tx(struct device *dev)
{
	struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr;

	BUGLVL(D_DURING)
		printk("arcnet: go_tx: status=%Xh\n",
			inb(STATUS));
	
	if (!(inb(STATUS)&TXFREEflag) || !lp->txready) return;

	/* start sending */
	outb(TXcmd|(lp->txready<<3),COMMAND);

	outb(TXFREEflag|NORXflag|RECON_flag,INTMASK);

	dev->trans_start = jiffies;
	lp->txready=0;
	lp->sending++;
#ifdef VERIFY_ACK	
	lp->outgoing.lasttrans_dest=lp->outgoing.lastload_dest;
	lp->outgoing.lastload_dest=0;
#endif
}


/* Called by the kernel in order to transmit a "Windows" packet.
 */
static int
arcnetW_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	
	BUGLVL(D_DURING)
		printk("%s: in arcnetW_send_packet (skb=%p)\n",dev->name,skb);

	if (IF_TBUSY)
	{
		/* If we get here, some higher level has decided we are broken.
		   There should really be a "kick me" function call instead. */
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 10)
			return 1;
		printk("%s: transmit timed out\n",dev->name);
		
		/* Try to restart the adaptor. */
		TBUSY=0;
		dev->trans_start = jiffies;
		return 0;
	}

	/* If some higher layer thinks we've missed an tx-done interrupt
	   we are passed NULL. Caution: dev_tint() handles the cli()/sti()
	   itself. */
	if (skb == NULL)
	{
		dev_tint(dev);
		return 0;
	}

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (set_bit(0, (void*)&dev->tbusy) != 0)
		printk("%s: Transmitter access conflict.\n", dev->name);
	else
	{
		union ArcPacket *arcpacket = 
			(union ArcPacket *)(dev->mem_start+512*(lp->txbuf^1));
		u_char *arcsoft,daddr;
		short offset,length=skb->len+1;
		
		TBUSY=1;
		
		if (length>XMTU)
		{
			printk("arcnet: MTU for %s and %s must be <= 493 for Windows protocol.\n",
				lp->adev->name,lp->wdev->name);
			printk("arcnet: transmit aborted.\n");

			dev_kfree_skb(skb,FREE_WRITE);
			return 0;
		}
		
		BUGLVL(D_DURING)
			printk("arcnet: starting tx sequence...\n");

		lp->txbuf=lp->txbuf^1; /* XOR with 1 to alternate btw 2 & 3 */

		/* clean out the page to make debugging make more sense :) */
		BUGLVL(D_DURING)
			memset((void *)dev->mem_start+lp->txbuf*512,0x42,512);

		/* broadcasts have address FF:FF:FF:FF:FF:FF in etherspeak */
		if (((struct ethhdr*)(skb->data))->h_dest[0] == 0xFF)
			daddr=arcpacket->hardheader.destination=0;
		else
			daddr=arcpacket->hardheader.destination=
				((struct ethhdr*)(skb->data))->h_dest[5];

		/* load packet into shared memory */
		offset=512-length;
		if (length>MTU)		/* long/exception packet */
		{
			if (length<MinTU) offset-=3;
			arcpacket->hardheader.offset1=0;
			arcpacket->hardheader.offset2=offset;
		}
		else			/* short packet */
		{
			arcpacket->hardheader.offset1=(offset-=256);
		}
		
		BUGLVL(D_DURING)
			printk("arcnet: length=%Xh, offset=%Xh, offset1=%Xh, offset2=%Xh\n",
				length,offset,arcpacket->hardheader.offset1,
				arcpacket->hardheader.offset2);
		
		arcsoft=&arcpacket->raw[offset];
		arcsoft[0]=ARC_P_MS_TCPIP;
		arcsoft++;
		
		/* copy the packet into ARCnet shmem
		 *  - the first bytes of ClientData header are skipped
		 */
		BUGLVL(D_DURING) printk("arcnet: ready to memcpy\n");
		
		memcpy(arcsoft,skb->data,skb->len);
			
		BUGLVL(D_DURING)
			printk("arcnet: transmitting packet to station %02Xh (%d bytes)\n",
				daddr,length);
				
		BUGLVL(D_TX)
		{
			int countx,county;
			
			printk("arcnet: packet dump [tx] follows:");

	 		for (county=0; county<16+(length>=240)*16; county++)
			{
				printk("\n[%04X] ",county*16);
				for (countx=0; countx<16; countx++)
					printk("%02X ",
						arcpacket->raw[county*16+countx]);
			}
			
			printk("\n");
		}

	#ifdef VERIFY_ACK
		lp->outgoing.lastload_dest=daddr;
	#endif
		lp->txready=lp->txbuf;	/* packet is ready for sending */

		arcnetA_go_tx(dev);
		dev->trans_start = jiffies;
	}

	dev_kfree_skb(skb,FREE_WRITE);

	return 0;
}


/****************************************************************************
 *                                                                          *
 * Interrupt handler                                                        *
 *                                                                          *
 ****************************************************************************/


/* The typical workload of the driver:  Handle the network interface
 * interrupts.  This doesn't do much right now except call arcnet_inthandler,
 * which takes different parameters but is sometimes called from other places
 * as well.
 */
static void
arcnet_interrupt(int irq,struct pt_regs *regs)
{
	struct device *dev = (struct device *)(irq2dev_map[irq]);

	if (dev==NULL || !dev->start)
	{
		BUGLVL(D_EXTRA)
			printk("arcnet: irq %d for unknown device.\n", irq);
		return;
	}

	arcnet_inthandler(dev);
}


/* The actual interrupt handler routine - handle various IRQ's generated
 * by the card.
 */
static void
arcnet_inthandler(struct device *dev)
{	
	struct arcnet_local *lp;
	int ioaddr, status, boguscount = 3, didsomething;
	
	if (dev->interrupt)
		printk("arcnet: DRIVER PROBLEM!  Nested arcnet interrupts!\n");
	
	dev->interrupt = 1;

	ioaddr = dev->base_addr;
	lp = (struct arcnet_local *)dev->priv;

	outb(0,INTMASK);
	sti();
	
	BUGLVL(D_DURING)
		printk("arcnet: in net_interrupt (status=%Xh)\n",inb(STATUS));

	do
	{
		status = inb(STATUS);
		didsomething=0;
	
		if (!dev->start)
		{
			BUGLVL(D_DURING)
				printk("arcnet: ARCnet not yet initialized.  irq ignored. (status=%Xh)\n",
					status);
			if (!(status&NORXflag))
				outb(NORXflag|RECON_flag,INTMASK);

			dev->interrupt=0;
			return;
		}
	
		/* RESET flag was enabled - card is resetting and if RX
		 * is disabled, it's NOT because we just got a packet.
		 */
		if (status & RESETflag)
		{
			outb(CFLAGScmd|RESETclear,COMMAND);
			BUGLVL(D_INIT)
				printk("arcnet: reset irq (status=%Xh)\n",
					status);
		}
#ifdef DETECT_RECONFIGS
		if (status & RECONflag)
		{
			outb(CFLAGScmd|CONFIGclear,COMMAND);
			BUGLVL(D_EXTRA)
				printk("arcnet: Network reconfiguration detected (status=%Xh)\n",
					status);
			lp->stats.tx_carrier_errors++;
		}
#endif

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

		/* it can only be an xmit-done irq if we're xmitting :) */
		if (status&TXFREEflag && !lp->in_txhandler && lp->sending)
		{
			struct Outgoing *out=&(lp->outgoing);
			
			lp->in_txhandler++;
			lp->sending--;
			
			BUGLVL(D_DURING)
				printk("arcnet: TX IRQ (stat=%Xh, numsegs=%d, segnum=%d, skb=%ph)\n",
					status,out->numsegs,out->segnum,out->skb);
					
#ifdef VERIFY_ACK
			if (!(status&TXACKflag))
			{
				if (lp->outgoing.lasttrans_dest != 0)
				{
					BUGLVL(D_NORMAL)
						printk("arcnet: transmit was not acknowledged! (status=%Xh, dest=%d)\n",
							status,
							lp->outgoing.lasttrans_dest);
					lp->stats.tx_errors++;
				}
				else
				{
					BUGLVL(D_DURING)
						printk("arcnet: broadcast was not acknowledged; that's normal (status=%Xh, dest=%d)\n",
							status,
							lp->outgoing.lasttrans_dest);
				}
			}
#endif
					
			/* send packet if there is one */
			if (lp->txready)
			{
				arcnetA_go_tx(dev);
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
				if (!lp->txready && IF_TBUSY)
				{
					TBUSY=0;
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
				arcnetA_continue_tx(dev);
			if (lp->txready && !lp->sending)
				arcnetA_go_tx(dev);

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
				if (!lp->txready && IF_TBUSY)
				{
					TBUSY=0;
					mark_bh(NET_BH);
				}
			}
			didsomething++;
			
			lp->in_txhandler--;
		}

	} while (--boguscount && didsomething);

	BUGLVL(D_DURING)
		printk("arcnet: net_interrupt complete (status=%Xh)\n\n",
			inb(STATUS));

	if (dev->start && lp->sending )
		outb(NORXflag|TXFREEflag|RECON_flag,INTMASK);
	else
		outb(NORXflag|RECON_flag,INTMASK);

	dev->interrupt=0;
}


/****************************************************************************
 *                                                                          *
 * Receiver routines                                                        *
 *                                                                          *
 ****************************************************************************/


/* A packet has arrived; grab it from the buffers and possibly unsplit it.
 * This is a generic packet receiver that calls arcnet??_rx depending on the
 * protocol ID found.
 */
static void
arcnet_rx(struct device *dev,int recbuf)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int ioaddr = dev->base_addr;
	union ArcPacket *arcpacket=
		(union ArcPacket *)(dev->mem_start+recbuf*512);
	u_char *arcsoft;
	short length,offset;
	u_char daddr,saddr;

	saddr=arcpacket->hardheader.source;
	daddr=arcpacket->hardheader.destination;
	
	/* if source is 0, it's a "used" packet! */
	if (saddr==0)
	{
		printk("arcnet: discarding old packet. (status=%Xh)\n",
			inb(STATUS));
		lp->stats.rx_errors++;
		return;
	}
	arcpacket->hardheader.source=0;
	
	if (arcpacket->hardheader.offset1) /* Normal Packet */
	{
		offset=arcpacket->hardheader.offset1;
		arcsoft=&arcpacket->raw[offset];
		length=256-offset;
	}
	else		/* ExtendedPacket or ExceptionPacket */
	{
		offset=arcpacket->hardheader.offset2;
		arcsoft=&arcpacket->raw[offset];

		length=512-offset;
	}
	
       	BUGLVL(D_DURING)
		printk("arcnet: received packet from %02Xh to %02Xh (%d bytes)\n",
       			saddr,daddr,length);

	/* call the right receiver for the protocol */
	switch (arcsoft[0])
	{
	case ARC_P_IP:
	case ARC_P_ARP:
	case ARC_P_RARP:
	case ARC_P_IPX:
		arcnetA_rx(dev,(struct ClientData*)arcsoft,
			length,saddr,daddr);
		break;
	case ARC_P_MS_TCPIP:
		arcnetW_rx(dev,arcsoft,length,saddr,daddr);
		break;		
	default:
		printk("arcnet: received unknown protocol %d (%Xh)\n",
			arcsoft[0],arcsoft[0]);
		break;
	}

        BUGLVL(D_RX)
	{
        	int countx,county;
        	
        	printk("arcnet: rx packet dump follows:");

       		for (county=0; county<16+(length>240)*16; county++)
       		{
       			printk("\n[%04X] ",county*16);
       			for (countx=0; countx<16; countx++)
       				printk("%02X ",
       					arcpacket->raw[county*16+countx]);
       		}
       		
       		printk("\n");
       	}


	/* If any worth-while packets have been received, a mark_bh(NET_BH)
	 * has been done by netif_rx and Linux will handle them after we
	 * return.
	 */
}


/* Packet receiver for "standard" RFC1201-style packets
 */
static void
arcnetA_rx(struct device *dev,struct ClientData *arcsoft,
	int length,u_char saddr, u_char daddr)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	struct sk_buff *skb;
	struct ClientData *soft;
	
	BUGLVL(D_DURING)
		printk("arcnet: it's an RFC1201 packet (length=%d)\n",
			length);
			
	arcsoft=(struct ClientData *)((u_char *)arcsoft-EXTRA_CLIENTDATA);
	length+=EXTRA_CLIENTDATA;

	if (arcsoft->split_flag==0xFF)  /* Exception Packet */
	{
		BUGLVL(D_DURING)
			printk("arcnet: compensating for exception packet\n");

		/* skip over 4-byte junkola */
		arcsoft=(struct ClientData *)
			((u_char *)arcsoft + 4);
		length-=4;
	}
	
	if (!arcsoft->split_flag)		/* not split */
	{
		struct Incoming *in=&lp->incoming[saddr];

		BUGLVL(D_RX) printk("arcnet: incoming is not split (splitflag=%d)\n",
			arcsoft->split_flag);
			
		if (in->skb)	/* already assembling one! */
        	{
        		BUGLVL(D_EXTRA) printk("arcnet: aborting assembly (seq=%d) for unsplit packet (splitflag=%d, seq=%d)\n",
        			in->sequence,arcsoft->split_flag,
        			arcsoft->sequence);
        		kfree_skb(in->skb,FREE_WRITE);
			lp->stats.tx_dropped++;
        		lp->stats.rx_errors++;
        		in->skb=NULL;
        	}
        	
        	in->sequence=arcsoft->sequence;

         	skb = alloc_skb(length, GFP_ATOMIC);
         	if (skb == NULL) {
         		printk("arcnet: Memory squeeze, dropping packet.\n");
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
         	
         	/* ARP packets have problems when sent from DOS.
         	 * source address is always 0 on some systems!  So we take
         	 * the hardware source addr (which is impossible to fumble)
         	 * and insert it ourselves.
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
         				BUGLVL(D_EXTRA)
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
         			lp->stats.rx_frame_errors++;
         		}
         	}
         	
		BUGLVL(D_SKB)
		{
	            short i;
	               	for( i=0; i< skb->len; i++)
               		{
                       		if( i%16 == 0 ) printk("\n[%04hX] ",i);
                       		printk("%02hX ",((unsigned char*)skb->data)[i]);
               		}
               		printk("\n");
            	}

		skb->protocol=arcnetA_type_trans(skb,dev);

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
			BUGLVL(D_EXTRA) printk("arcnet: wrong seq number, aborting assembly (expected=%d, seq=%d, splitflag=%d)\n",	
				in->sequence,arcsoft->sequence,
				arcsoft->split_flag);
	        	kfree_skb(in->skb,FREE_WRITE);
	        	in->skb=NULL;
			lp->stats.tx_dropped++;
	        	lp->stats.rx_fifo_errors++;
	        	in->lastpacket=in->numpackets=0;
	        }
	          
	        if (arcsoft->split_flag & 1)	/* first packet in split */
	        {
	        	BUGLVL(D_RX) printk("arcnet: brand new splitpacket (splitflag=%d)\n",
	        		arcsoft->split_flag);
	        	if (in->skb)	/* already assembling one! */
	        	{
	        		BUGLVL(D_EXTRA) printk("arcnet: aborting previous (seq=%d) assembly (splitflag=%d, seq=%d)\n",
	        			in->sequence,arcsoft->split_flag,
	        			arcsoft->sequence);
				lp->stats.tx_dropped++;
	        		lp->stats.rx_over_errors++;
	        		kfree_skb(in->skb,FREE_WRITE);
	        	}

			in->sequence=arcsoft->sequence;
	        	in->numpackets=((unsigned)arcsoft->split_flag>>1)+2;
	        	in->lastpacket=1;
	        	
	        	if (in->numpackets>16)
	        	{
	        		BUGLVL(D_EXTRA) printk("arcnet: incoming packet more than 16 segments; dropping. (splitflag=%d)\n",
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
				BUGLVL(D_EXTRA) printk("arcnet: can't continue split without starting first! (splitflag=%d, seq=%d)\n",
					arcsoft->split_flag,arcsoft->sequence);
				lp->stats.rx_errors++;
				return;
			}

	        	in->lastpacket++;
	        	if (packetnum!=in->lastpacket) /* not the right flag! */
	        	{
				/* harmless duplicate? ignore. */
				if (packetnum<=in->lastpacket-1)
				{
					BUGLVL(D_EXTRA) printk("arcnet: duplicate splitpacket ignored! (splitflag=%d)\n",
						arcsoft->split_flag);
					return;
				}
				
				/* "bad" duplicate, kill reassembly */
				BUGLVL(D_EXTRA) printk("arcnet: out-of-order splitpacket, reassembly (seq=%d) aborted (splitflag=%d, seq=%d)\n",	
					in->sequence,arcsoft->split_flag,
					arcsoft->sequence);
	        		kfree_skb(in->skb,FREE_WRITE);
	        		in->skb=NULL;
				lp->stats.tx_dropped++;
	        		lp->stats.rx_fifo_errors++;
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
         	
         	/* are we done? */
         	if (in->lastpacket == in->numpackets)
         	{
         		if (!skb || !in->skb)
         			printk("arcnet: ?!? done reassembling packet, no skb? (skb=%ph, in->skb=%ph)\n",
         				skb,in->skb);
         		in->skb=NULL;
         		in->lastpacket=in->numpackets=0;
			
			BUGLVL(D_SKB)
			{
	        	    short i;
	        	       	for( i=0; i< skb->len; i++)
               			{
                	       		if( i%16 == 0 ) printk("\n[%04hX] ",i);
                	       		printk("%02hX ",((unsigned char*)skb->data)[i]);
               			}
               			printk("\n");
            		}

			skb->protocol=arcnetA_type_trans(skb,dev);

	         	netif_rx(skb);
        	 	lp->stats.rx_packets++;
        	}
         }
}


/* Packet receiver for non-standard Windows-style packets
 */
static void
arcnetW_rx(struct device *dev,u_char *arcsoft,
	int length,u_char saddr, u_char daddr)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	struct sk_buff *skb;
	
	BUGLVL(D_DURING)
		printk("arcnet: it's a Windows packet (length=%d)\n",
			length);

       	skb = alloc_skb(length, GFP_ATOMIC);
       	if (skb == NULL) {
       		printk("arcnet: Memory squeeze, dropping packet.\n");
       		lp->stats.rx_dropped++;
       		return;
       	}
       	
       	skb->len = length;
       	skb->dev = lp->wdev;
       	
       	memcpy(skb->data,(u_char *)arcsoft+1,length-1);

        BUGLVL(D_SKB)
        {
		short i;
		printk("arcnet: rx skb dump follows:\n");
                for(i=0; i<skb->len; i++)
                {
			if (i%16==0)
				printk("\n[%04hX] ",i);
			else
                        	printk("%02hX ",((u_char *)skb->data)[i]);
		}
                printk("\n");
        }
        
	skb->protocol=eth_type_trans(skb,dev);
        
        netif_rx(skb);
        lp->stats.rx_packets++;
}


/****************************************************************************
 *                                                                          *
 * Miscellaneous routines                                                   *
 *                                                                          *
 ****************************************************************************/


/* Get the current statistics.	This may be called with the card open or
 * closed.
 */

static struct enet_statistics *
arcnet_get_stats(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

	return &lp->stats;
}

/* Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, clear multicast list
 * num_addrs > 0	Multicast mode, receive normal and MC packets, and do
 *			best-effort filtering.
 */
static void
set_multicast_list(struct device *dev, int num_addrs, void *addrs)
{
#if 0	  /* no promiscuous mode at all on most ARCnet models */
	struct arcnet_local *lp=(struct arcnet_local *)(dev->priv);

	short ioaddr = dev->base_addr;
	if (num_addrs) {
		outw(69, ioaddr);		/* Enable promiscuous mode */
	} else
		outw(99, ioaddr);		/* Disable promiscuous mode, use normal mode */
#endif
}

/* Create the ARCnet ClientData header for an arbitrary protocol layer
 *
 * saddr=NULL	means use device source address (always will anyway)
 * daddr=NULL	means leave destination address (eg unresolved arp)
 */
int arcnetA_header(struct sk_buff *skb,struct device *dev,unsigned short type,
		void *daddr,void *saddr,unsigned len)
{
	struct ClientData *head = (struct ClientData *)
		skb_push(skb,dev->hard_header_len);
/*	struct arcnet_local *lp=(struct arcnet_local *)(dev->priv);*/

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

	/*
	 * Set the source hardware address.
	 *
	 * This is pretty pointless for most purposes, but it can help
	 * in debugging.  saddr is stored in the ClientData header and
	 * removed before sending the packet (since ARCnet does not allow
	 * us to change the source address in the actual packet sent)
	 */
	if(saddr)
		head->saddr=((u_char*)saddr)[0];
	else
		head->saddr=((u_char*)(dev->dev_addr))[0];

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
	head->sequence=0;	/* so are sequence numbers */

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


/* Rebuild the ARCnet ClientData header. This is called after an ARP
 * (or in future other address resolution) has completed on this
 * sk_buff. We now let ARP fill in the other fields.
 */
int arcnetA_rebuild_header(void *buff,struct device *dev,unsigned long dst,
		struct sk_buff *skb)
{
	struct ClientData *head = (struct ClientData *)buff;

	/*
	 * Only ARP and IP are currently supported
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
	 * Try and get ARP to resolve the header.
	 */
#ifdef CONFIG_INET	 
	return arp_find(&(head->daddr), dst, dev, dev->pa_addr, skb)? 1 : 0;
#else
	return 0;	
#endif	
}
		
/* Determine a packet's protocol ID.
 *
 * With ARCnet we have to convert everything to Ethernet-style stuff.
 */
unsigned short arcnetA_type_trans(struct sk_buff *skb,struct device *dev)
{
	struct ClientData *head = (struct ClientData *) skb->data;
	struct arcnet_local *lp=(struct arcnet_local *) (dev->priv);

	/* Pull off the arcnet header. */
	skb->mac.raw=skb->data;
	skb_pull(skb,dev->hard_header_len);
	
	if (head->daddr==0)
		skb->pkt_type=PACKET_BROADCAST;
	else if (dev->flags&IFF_PROMISC)
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
	case ARC_P_ATALK:   return htons(ETH_P_ATALK); /* untested appletalk */
	case ARC_P_LANSOFT: /* don't understand.  fall through. */
	default:
		BUGLVL(D_EXTRA)
			printk("arcnet: received packet of unknown protocol id %d (%Xh)\n",
				head->protocol_id,head->protocol_id);
		lp->stats.rx_frame_errors++;
		return 0;
	}

	return htons(ETH_P_IP);
}



/****************************************************************************
 *                                                                          *
 * Kernel Loadable Module Support                                           *
 *                                                                          *
 ****************************************************************************/


#ifdef MODULE
char kernel_version[] = UTS_RELEASE;
static struct device thiscard = {
  "       ",/* if blank, device name inserted by /linux/drivers/net/net_init.c */
  0, 0, 0, 0,
  0, 0,  /* I/O address, IRQ */
  0, 0, 0, NULL, arcnet_probe };
	
	
int io=0x0;	/* <--- EDIT THESE LINES FOR YOUR CONFIGURATION */
int irqnum=0;	/* or use the insmod io= irqnum= shmem= options */
int shmem=0;
int num=0;	/* number of device (ie for 0 for arc0, 1 for arc1...) */

int
init_module(void)
{
	sprintf(thiscard.name,"arc%d",num);

	thiscard.base_addr=io;

	thiscard.irq=irqnum;
	if (thiscard.irq==2) thiscard.irq=9;

	if (shmem)
	{
		thiscard.mem_start=shmem;
		thiscard.mem_end=thiscard.mem_start+512*4-1;
		thiscard.rmem_start=thiscard.mem_start+512*0;
		thiscard.rmem_end=thiscard.mem_start+512*2-1;
	}

	if (register_netdev(&thiscard) != 0)
		return -EIO;
	return 0;
}

void
cleanup_module(void)
{
	if (MOD_IN_USE)
	{
		printk("%s: device busy, remove delayed\n",thiscard.name);
	}
	else
	{
		if (thiscard.start) arcnet_close(&thiscard);
		if (thiscard.irq) free_irq(thiscard.irq);
		if (thiscard.base_addr) release_region(thiscard.base_addr,
						ARCNET_TOTAL_SIZE);
		unregister_netdev(&thiscard);
	}
}

#endif /* MODULE */



/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c arcnet.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 8
 * End:
 */

