/* arcnet.c
	Written 1994-95 by Avery Pennarun, derived from skeleton.c by Donald
	Becker.

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

	v2.20 ALPHA (95/11/12)
	  - Added a bit of protocol confusion to the arc0 code to allow
	    trxnet-compatible IPX support - and the use of all that new
	    Linux-IPX stuff (including lwared).  Just tell the ipx_interface
	    command that arc0 uses 802.3 (other protocols will probably also
	    work).
	  - Fixed lp->stats to update tx_packets on all devices, not just
	    arc0.  Also, changed a lot of the error counting to make more
	    sense.
	  - rx_packets on arc0 was being updated for each completely received
	    packet.  It now updates for each segment received, which makes
	    more sense. 
	  - Removed unneeded extra check of dev->start in arcnet_inthandler.
	  - Included someone else's fixes from kernel 1.3.39.  Does it still
	    work with kernel 1.2?
	  - Added a new define to disable PRINTING (not checking) of RECON
	    messages.
	  - Separated the duplicated error checking code from the various
	    arcnet??_send_packet routines into a new function.
	  - Cleaned up lock variables and ping-pong buffers a bit.  This
	    should mean more stability, fewer missing packets, and less ugly
	    debug messages.  Assuming it works.
	  - Changed the format of debug messages to always include the actual
	    device name instead of just "arcnet:".  Used more macros to
	    shorten debugging code even more.
	  - Finally squashed the "startup NULL pointer" bug.  irq2dev_map
	    wasn't being cleared to NULL when the driver was closed.
	  - Improved RECON checking; if a certain number of RECON messages
	    are received within one minute, a warning message is printed
	    to the effect of "cabling problem."  One-log-message-per-recon
	    now defaults to OFF.

	v2.12 ALPHA (95/10/27)
	  - Tried to improve skb handling and init code to fix problems with
	    the latest 1.3.x kernels. (We no longer use ether_setup except
	    in arc0e since they keep coming up with new and improved
	    incompatibilities for us.)

	v2.11 ALPHA (95/10/25)
	  - Removed superfluous sti() from arcnet_inthandler.
	  - "Cleaned up" (?) handling of dev->interrupt for multiple
	    devices.
	  - Place includes BEFORE configuration settings (solves some
	    problems with insmod and undefined symbols)
	  - Removed race condition in arcnet_open that could cause
	    packet reception to be disabled until after the first TX.
	  
	v2.10 ALPHA (95/09/10)
	  - Integrated Tomasz Motylewski's new RFC1051 compliant "arc0s"
	    ([S]imple [S]tandard?) device.  This should make Linux-ARCnet
	    work with the NetBSD/AmiTCP implementation of this older RFC,
	    via the arc0s device.
	  - Decided the current implementation of Multiprotocol ARCnet
	    involved way too much duplicated code, and tried to share things
	    a _bit_ more, at least.  This means, pretty much, that any
	    bugs in the arc0s module are now my fault :)
	  - Added a new ARCNET_DEBUG_MAX define that sets the highest
	    debug message level to be included in the driver.  This can
	    reduce the size of the object file, and probably increases
	    efficiency a bit.  I get a 0.1 ms speedup in my "ping" times if
	    I disable all debug messages.  Oh, wow.
	  - Fixed a long-standing annoyance with some of the power-up debug
	    messages.  ("IRQ for unknown device" was showing up too often)

	v2.00 (95/09/06)
	  - THIS IS ONLY A SUMMARY.  The complete changelog is available
	    from me upon request.

	  - ARCnet RECON messages are now detected and logged.  These occur
	    when a new computer is powered up on the network, or in a
	    constant stream when the network cable is broken.  Thanks to
	    Tomasz Motylewski for this.  You must have D_EXTRA enabled
	    if you want these messages sent to syslog, otherwise they will
	    only show up in the network statistics (/proc/net/dev).
	  - The TX Acknowledge flag is now checked, and a log message is sent
	    if a completed transmit is not ACK'd.  (I have yet to have this
	    happen to me.)
	  - Debug levels are now completely different.  See the README.
	  - Many code cleanups, with several no-longer-necessary and some
	    completely useless options removed.
	  - Multiprotocol support.  You can now use the "arc0e" device to
	    send "Ethernet-Encapsulation" packets, which are compatible with
	    Windows for Workgroups and LAN Manager, and possibly other
	    software.  See the README for more information.
	  - Documentation updates and improvements.
	  
	v1.02 (95/06/21)
          - A fix to make "exception" packets sent from Linux receivable
	    on other systems.  (The protocol_id byte was sometimes being set
	    incorrectly, and Linux wasn't checking it on receive so it
	    didn't show up)

	v1.01 (95/03/24)
	  - Fixed some IPX-related bugs. (Thanks to Tomasz Motylewski
            <motyl@tichy.ch.uj.edu.pl> for the patches to make arcnet work
            with dosemu!)
            
	v1.00 (95/02/15)
	  - Initial non-alpha release.
	
         
	TO DO:
	
         - Test in systems with NON-ARCnet network cards, just to see if
           autoprobe kills anything.  Currently, we do cause some NE2000's
           to die.  Autoprobe is also way too slow and verbose, particularly
           if there aren't any ARCnet cards in the system.  And why shouldn't
           it work as a module again?
         - Rewrite autoprobe.
         - Make sure RESET flag is cleared during an IRQ even if dev->start==0;
           mainly a portability fix.  This will confuse autoprobe a bit, so
           test that too.
         - What about cards with shared memory that can be "turned off?"
         - NFS mount freezes after several megabytes to SOSS for DOS. 
 	   unmount/remount fixes it.  Is this arcnet-specific?  I don't know.
         - Some newer ARCnets support promiscuous mode, supposedly.  If
           someone sends me information, I'll try to implement it.
         - Dump Linux 1.2 support and its ugly #ifdefs.
         - Add support for the new 1.3.x IP header cache features.
         - ATA protocol support?? 
         - Banyan VINES TCP/IP support??

           
	Sources:
	 - Crynwr arcnet.com/arcether.com packet drivers.
	 - arcnet.c v0.00 dated 1/1/94 and apparently by
	 	Donald Becker - it didn't work :)
	 - skeleton.c v0.05 dated 11/16/93 by Donald Becker
	 	(from Linux Kernel 1.1.45)
	 - The official ARCnet data sheets (!) thanks to Ken Cornetet
		<kcornete@nyx10.cs.du.edu>
	 - RFC's 1201 and 1051 - re: TCP/IP over ARCnet
	 - net/inet/eth.c (from kernel 1.1.50) for header-building info...
	 - Alternate Linux ARCnet source by V.Shergin <vsher@sao.stavropol.su>
	 - Textual information and more alternate source from Joachim Koenig
	 	<jojo@repas.de>
*/

static const char *version =
 "arcnet.c: v2.20 ALPHA 95/11/12 Avery Pennarun <apenwarr@foxnet.net>\n";

 

#include <linux/module.h>
#include <linux/config.h>
#include <linux/version.h>

/* are we Linux 1.2.x? */
#if LINUX_VERSION_CODE < 0x10300
#define LINUX12
#endif


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
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

/*#include <linux/config.h>*/		/* For CONFIG_INET */

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#ifdef LINUX12
#include "arp.h"
#else
#include <net/arp.h>
#endif

/**************************************************************************/

/* The card sends the reconfiguration signal when it loses the connection to
 * the rest of its network. It is a 'Hello, is anybody there?' cry.  This
 * usually happens when a new computer on the network is powered on or when
 * the cable is broken.
 *
 * Define DETECT_RECONFIGS if you want to detect network reconfigurations. 
 * They may be a real nuisance on a larger ARCnet network; if you are a
 * network administrator you probably would like to count them. 
 * Reconfigurations will be recorded in stats.tx_carrier_errors (the last
 * field of the /proc/net/dev file).
 *
 * Define SHOW_RECONFIGS if you really want to see a log message whenever
 * a RECON occurs.
 */
#define DETECT_RECONFIGS
#undef SHOW_RECONFIGS

/* RECON_THRESHOLD is the maximum number of RECON messages to receive within
 * one minute before printing a "cabling problem" warning.  You must have
 * DETECT_RECONFIGS enabled if you want to use this.  The default value
 * should be fine.
 *
 * After that, a "cabling restored" message will be printed on the next IRQ
 * if no RECON messages have been received for 10 seconds.
 *
 * Do not define RECON_THRESHOLD at all if you want to disable this feature.
 */
#define RECON_THRESHOLD 30

/* Define this if you want to make sure transmitted packets are "acknowledged"
 * by the destination host, as long as they're not to the broadcast address.
 *
 * That way, if one segment of a split packet doesn't get through, it can
 * be resent immediately rather than confusing the other end.
 *
 * Disable this to return to 1.02-style behaviour, if you have problems.
 */
#define VERIFY_ACK

/* Define this to the minimum "timeout" value.  If a transmit takes longer
 * than TX_TIMEOUT jiffies, Linux will abort the TX and retry.  On a large
 * network, or one with heavy network traffic, this timeout may need to be
 * increased.
 */
#define TX_TIMEOUT 20

/* Define this if you want to make it easier to use the "call trace" when
 * a kernel NULL pointer assignment occurs.  Hopefully unnecessary, most of
 * the time.  It will make all the function names (and other things) show
 * up as kernel symbols.  (especially handy when using arcnet as a module)
 */
#undef static

/**************************************************************************/

/* New debugging bitflags: each option can be enabled individually.
 *
 * These can be set while the driver is running by typing:
 *	ifconfig arc0 down metric 1xxx HOSTNAME
 *		where 1xxx is 1000 + the debug level you want
 *		and HOSTNAME is your hostname/ip address
 * and then resetting your routes.
 *
 * Note: only debug flags included in the ARCNET_DEBUG_MAX define will
 *   actually be available.  GCC will (at least, GCC 2.7.0 will) notice
 *   lines using a BUGLVL not in ARCNET_DEBUG_MAX and automatically optimize
 *   them out.
 */
#define D_NORMAL	1	/* D_NORMAL  normal operational info	*/
#define	D_INIT		2	/* D_INIT    show init/probe messages	*/
#define D_EXTRA		4	/* D_EXTRA   extra information		*/
/* debug levels past this point give LOTS of output! */
#define D_DURING	8	/* D_DURING  during normal use (irq's)	*/
#define D_TX		16	/* D_TX	     show tx packets		*/
#define D_RX		32	/* D_RX	     show rx packets		*/
#define D_SKB		64	/* D_SKB     dump skb's			*/

#ifndef ARCNET_DEBUG_MAX
#define ARCNET_DEBUG_MAX (~0)		/* enable ALL debug messages */
/*#define ARCNET_DEBUG_MAX (D_NORMAL|D_INIT|D_EXTRA) */
/*#define ARCNET_DEBUG_MAX 0	*/	/* enable NO debug messages */
#endif

#ifndef ARCNET_DEBUG
#define ARCNET_DEBUG (D_NORMAL|D_INIT|D_EXTRA)
/*#define ARCNET_DEBUG (D_NORMAL|D_INIT)*/
#endif
int arcnet_debug = ARCNET_DEBUG;

/* macros to simplify debug checking */
#define BUGLVL(x) if ((ARCNET_DEBUG_MAX)&arcnet_debug&(x))
#define BUGMSG(x,msg,args...) BUGLVL(x) printk("%6s: " msg, dev->name , ## args);

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

/* Some useful multiprotocol macros */
#define TBUSY lp->adev->tbusy \
		=lp->edev->tbusy \
		=lp->sdev->tbusy
#define IF_TBUSY (lp->adev->tbusy \
		|| lp->edev->tbusy \
		|| lp->sdev->tbusy)

#define INTERRUPT lp->adev->interrupt \
		=lp->edev->interrupt \
		=lp->sdev->interrupt
#define IF_INTERRUPT (lp->adev->interrupt \
		|| lp->edev->interrupt \
		|| lp->sdev->interrupt)

#define START lp->adev->start \
		=lp->edev->start \
		=lp->sdev->start
		

/* The number of low I/O ports used by the ethercard. */
#define ARCNET_TOTAL_SIZE	16


/* Handy defines for ARCnet specific stuff */
	/* COM 9026 controller chip --> ARCnet register addresses */
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

	/* Starts receiving packets into recbuf.
	 */
#define EnableReceiver()	outb(RXcmd|(recbuf<<3)|RXbcasts,COMMAND)

	/* RFC1201 Protocol ID's */
#define ARC_P_IP	212		/* 0xD4 */
#define ARC_P_ARP	213		/* 0xD5 */
#define ARC_P_RARP	214		/* 0xD6 */
#define ARC_P_IPX	250		/* 0xFA */

	/* Old RFC1051 Protocol ID's */
#define ARC_P_IP_RFC1051 240		/* 0xF0 */
#define ARC_P_ARP_RFC1051 241		/* 0xF1 */

	/* MS LanMan/WfWg protocol */
#define ARC_P_ETHER	0xE8

	/* Unsupported/indirectly supported protocols */
#define ARC_P_LANSOFT	251		/* 0xFB - what is this? */
#define ARC_P_ATALK	0xDD

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

	/* the "client data" header - RFC1201 information
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


	/* the "client data" header - RFC1051 information
	 * this also screws up if it's not an even number of bytes
	 * <sigh again>
	 */
struct S_ClientData
{
	/* data that's NOT part of real packet - we MUST get rid of it before
	 * actually sending!!
	 */
	u_char  saddr,		/* Source address - needed for IPX */
		daddr,		/* Destination address */
		junk;		/* padding to make an even length */
	
	/* data that IS part of real packet */
	u_char	protocol_id;	/* ARC_P_IP, ARC_P_ARP, etc */
};
#define S_EXTRA_CLIENTDATA (sizeof(struct S_ClientData)-1)


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

#if defined(DETECT_RECONFIGS) && defined(RECON_THRESHOLD)
	time_t first_recon,	/* time of "first" RECON message to count */
		last_recon;	/* time of most recent RECON */
	int num_recons,		/* number of RECONs between first and last. */
	    network_down;	/* do we think the network is down? */
#endif
	
	struct timer_list timer; /* the timer interrupt struct */
	struct Incoming incoming[256];	/* one from each address */
	struct Outgoing outgoing; /* packet currently being sent */
	
	struct device *adev,	/* RFC1201 protocol device */
		 	*edev,	/* Ethernet-Encap device */
		 	*sdev;	/* RFC1051 protocol device */
};


/* Index to functions, as function prototypes. */
extern int arcnet_probe(struct device *dev);
#ifndef MODULE
static int arcnet_memprobe(struct device *dev,u_char *addr);
static int arcnet_ioprobe(struct device *dev, short ioaddr);
#endif
static void arcnet_setup(struct device *dev);
static int arcnetE_init(struct device *dev);
static int arcnetS_init(struct device *dev);

static int arcnet_open(struct device *dev);
static int arcnet_close(struct device *dev);
static int arcnet_reset(struct device *dev);

static int arcnet_send_packet_bad(struct sk_buff *skb,struct device *dev);
static int arcnetA_send_packet(struct sk_buff *skb, struct device *dev);
static int arcnetE_send_packet(struct sk_buff *skb, struct device *dev);
static int arcnetS_send_packet(struct sk_buff *skb, struct device *dev);
static void arcnetA_continue_tx(struct device *dev);
static void arcnetAS_prepare_tx(struct device *dev,u_char *hdr,int hdrlen,
		char *data,int length,int daddr,int exceptA);
static int arcnet_go_tx(struct device *dev,int enable_irq);

static void arcnet_interrupt(int irq,struct pt_regs *regs);
static void arcnet_inthandler(struct device *dev);

static void arcnet_rx(struct device *dev,int recbuf);
static void arcnetA_rx(struct device *dev,u_char *buf,
	int length,u_char saddr, u_char daddr);
static void arcnetE_rx(struct device *dev,u_char *arcsoft,
	int length,u_char saddr, u_char daddr);
static void arcnetS_rx(struct device *dev,u_char *buf,
	int length,u_char saddr, u_char daddr);

static struct enet_statistics *arcnet_get_stats(struct device *dev);
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);

	/* functions for header/arp/etc building */
#ifdef LINUX12
int arcnetA_header(unsigned char *buff,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len,
		struct sk_buff *skb);
int arcnetS_header(unsigned char *buff,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len,
		struct sk_buff *skb);
#else
int arcnetA_header(struct sk_buff *skb,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len);
int arcnetS_header(struct sk_buff *skb,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len);
#endif
int arcnetA_rebuild_header(void *eth,struct device *dev,unsigned long raddr,
		struct sk_buff *skb);
int arcnetS_rebuild_header(void *eth,struct device *dev,unsigned long raddr,
		struct sk_buff *skb);
unsigned short arcnetA_type_trans(struct sk_buff *skb,struct device *dev);
unsigned short arcnetS_type_trans(struct sk_buff *skb,struct device *dev);

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

	printk(version);
	
#if 1
	BUGLVL(D_NORMAL)
	{
		printk("arcnet: ***\n");
		printk("arcnet: * Read README.arcnet for important release notes!\n");
		printk("arcnet: *\n");
		printk("arcnet: * This is an ALPHA version!  (Last stable release: v2.00)  E-mail me if\n");
		printk("arcnet: * you have any questions, comments, or bug reports.\n");
		printk("arcnet: ***\n");
	}
#else
	BUGLVL(D_INIT)
	{
		printk("arcnet: ***\n");
		printk("arcnet: * Read README.arcnet for important release notes!\n");
		printk("arcnet: *\n");
		printk("arcnet: * This version should be stable, but please e-mail\n");
		printk("arcnet: * me if you have any questions or comments.\n");
		printk("arcnet: ***\n");
	}
#endif

	BUGMSG(D_INIT,"given: base %lXh, IRQ %Xh, shmem %lXh\n",
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
			BUGMSG(D_INIT,"Skipping %Xh because of check_region...\n",
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
	BUGMSG(D_INIT,"ioprobe okay!  Waiting for reset...\n");
	JIFFER(100);

	/* okay, now we have to find the shared memory area. */
	BUGMSG(D_INIT,"starting memory probe, given %lXh\n",
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
		printk("%6s: loadable modules can't autoprobe!\n",dev->name);
		printk("%6s:  try using io=, irqnum=, and shmem= on the insmod line.\n",
			dev->name);
		printk("%6s:  you may also need num= to change the device name. (ie. num=1 for arc1)\n",
			dev->name);
		return ENODEV;
	}
#endif
	/* now reserve the irq... */
	{	
		int irqval = request_irq(dev->irq, &arcnet_interrupt, 0,
			"arcnet");
		if (irqval) {
			printk("%6s: unable to get IRQ %d (irqval=%d).\n",
				dev->name,dev->irq, irqval);
			return EAGAIN;
		 }
	 }
	 
	/* Grab the region so we can find another board if autoIRQ fails. */
	request_region(dev->base_addr, ARCNET_TOTAL_SIZE,"arcnet");
	
	printk("%6s: ARCnet card found at %03lXh, IRQ %d, ShMem at %lXh.\n", 
		dev->name, dev->base_addr, dev->irq, dev->mem_start);

	/* Initialize the device structure. */
	dev->priv = kmalloc(sizeof(struct arcnet_local), GFP_KERNEL);
	if (dev->priv == NULL)
		return -ENOMEM;
	memset(dev->priv, 0, sizeof(struct arcnet_local));
	lp=(struct arcnet_local *)(dev->priv);

	dev->open=arcnet_open;
	dev->stop=arcnet_close;
	dev->hard_start_xmit=arcnetA_send_packet;
	dev->get_stats=arcnet_get_stats;
#ifdef HAVE_MULTICAST
	dev->set_multicast_list = &set_multicast_list;
#endif

	/* Fill in the fields of the device structure with generic
	 * values.
	 */
	arcnet_setup(dev);
	
	/* And now fill particular fields with arcnet values */
	dev->mtu=1500; /* completely arbitrary - agrees with ether, though */
	dev->hard_header_len=sizeof(struct ClientData);
	BUGMSG(D_DURING,"ClientData header size is %d.\n",
		sizeof(struct ClientData));
	BUGMSG(D_DURING,"HardHeader size is %d.\n",
		sizeof(struct HardHeader));

		/* since we strip EXTRA_CLIENTDATA bytes off before sending,
		 * we let Linux add that many bytes to the packet data...
		 */
	
	BUGMSG(D_INIT,"arcnet_probe: resetting card.\n");
	arcnet_reset(dev);
	JIFFER(50);
	BUGMSG(D_NORMAL,"We appear to be station %d (%02Xh)\n",
			lp->arcnum,lp->arcnum);
	if (lp->arcnum==0)
		printk("%6s: WARNING!  Station address 0 is reserved for broadcasts!\n",
			dev->name);
	if (lp->arcnum==255)
		printk("%6s: WARNING!  Station address 255 may confuse DOS networking programs!\n",
			dev->name);
	dev->dev_addr[0]=lp->arcnum;
	lp->sequence=1;
	lp->recbuf=0;

	dev->hard_header=arcnetA_header;
	dev->rebuild_header=arcnetA_rebuild_header;
	
	#ifdef LINUX12
	dev->type_trans=arcnetA_type_trans;
	#endif

	return 0;
}

#ifndef MODULE

int arcnet_ioprobe(struct device *dev, short ioaddr)
{
	int delayval,airq;

	BUGMSG(D_INIT,"probing address %Xh\n",ioaddr);
	BUGMSG(D_INIT," status1=%Xh\n",inb(STATUS));

		
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
		BUGMSG(D_INIT," probe failed.  Status port empty.\n");
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
			BUGLVL(D_INIT," probe failed.  never-changing command port (%02Xh).\n",
				initval);
			return ENODEV;
		}
	}
#endif

	BUGMSG(D_INIT," status2=%Xh\n",inb(STATUS));

	/* now we turn the reset bit off so we can IRQ next reset... */
	outb(CFLAGScmd|RESETclear|CONFIGclear,COMMAND);
	if (inb(STATUS) & RESETflag) /* reset flag STILL on */
	{
		BUGMSG(D_INIT," probe failed.  eternal reset flag1...(status=%Xh)\n",
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
		printk("%6s:  autoirq is %d\n", dev->name, airq);

	/* if there was no autoirq AND the user hasn't set any defaults,
	 * give up.
	 */
	if (!airq && !(dev->base_addr && dev->irq))
	{
		BUGMSG(D_INIT," probe failed.  no autoirq...\n");
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
		BUGMSG(D_INIT," probe failed.  eternal reset flag...(status=%Xh)\n",
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
		BUGMSG(D_NORMAL,"IRQ2 == IRQ9, don't worry.\n");
		dev->irq = 9;
	}

	BUGMSG(D_INIT,"irq and base address seem okay. (%lXh, IRQ %d)\n",
			dev->base_addr,dev->irq);
	return 0;
}


/* A memory probe that is called after the card is reset.
 * It checks for the official TESTvalue in byte 0 and makes sure the buffer
 * has certain characteristics of an ARCnet.
 */
int arcnet_memprobe(struct device *dev,u_char *addr)
{
	BUGMSG(D_INIT,"probing memory at %lXh\n",(u_long)addr);
		
	dev->mem_start=0;

	/* ARCnet memory byte 0 is TESTvalue */
	if (addr[0]!=TESTvalue)
	{
		BUGMSG(D_INIT," probe failed.  addr=%lXh, addr[0]=%Xh (not %Xh)\n",
				(unsigned long)addr,addr[0],TESTvalue);
		return ENODEV;
	}
	
	/* now verify the shared memory writability */
	addr[0]=0x42;
	if (addr[0]!=0x42)
	{
		BUGMSG(D_INIT," probe failed.  addr=%lXh, addr[0]=%Xh (not 42h)\n",
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


/* Do a hardware reset on the card.
 */
int arcnet_reset(struct device *dev)
{
	struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
	short ioaddr=dev->base_addr;
	int delayval,recbuf=lp->recbuf;
	u_char *cardmem;
	
	outb(0,INTMASK);	/* no IRQ's, please! */
	
	BUGMSG(D_INIT,"Resetting %s (status=%Xh)\n",
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
		BUGMSG(D_INIT,"reset failed: TESTvalue not present.\n");
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


/* Setup a struct device for ARCnet.  This should really be in net_init.c
 * but since there are three different ARCnet devices ANYWAY... <gargle>
 *
 * Actually, the whole idea of having all this kernel-dependent stuff (ie.
 * "new-style flags") setup per-net-device is kind of weird anyway.
 *
 * Intelligent defaults?!  Nah.
 */
void arcnet_setup(struct device *dev)
{
	int i;
	for (i=0; i<DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);

	dev->broadcast[0]=0x00;	/* broadcasts on ARCnet are address 0 */
	dev->addr_len=1;
	dev->type=ARPHRD_ARCNET;

	/* New-style flags. */
	dev->flags	= IFF_BROADCAST;
	dev->family	= AF_INET;
	dev->pa_addr	= 0;
	dev->pa_brdaddr = 0;
	dev->pa_mask	= 0;
	dev->pa_alen	= 4;
}


/* Initialize the arc0e device.
 */
static int arcnetE_init(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

	ether_setup(dev); /* we're emulating ether here, not ARCnet */
	dev->dev_addr[0]=0;
	dev->dev_addr[5]=lp->arcnum;
	dev->mtu=512-sizeof(struct HardHeader)-dev->hard_header_len-1;
	dev->open=NULL;
	dev->stop=NULL;
	dev->hard_start_xmit=arcnetE_send_packet;

	BUGMSG(D_EXTRA,"ARCnet Ethernet-Encap protocol initialized.\n");
			
	return 0;
}

/* Initialize the arc0s device.
 */
static int arcnetS_init(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

	arcnet_setup(dev);
       
	/* And now fill particular fields with arcnet values */
	dev->dev_addr[0]=lp->arcnum;
	dev->hard_header_len=sizeof(struct S_ClientData);
	dev->mtu=512-sizeof(struct HardHeader)-dev->hard_header_len
		+ S_EXTRA_CLIENTDATA;
	dev->open=NULL;
	dev->stop=NULL;
	dev->hard_start_xmit=arcnetS_send_packet;
	dev->hard_header=arcnetS_header;
	dev->rebuild_header=arcnetS_rebuild_header;
#ifdef LINUX12
	dev->type_trans=arcnetS_type_trans;
#endif
	BUGMSG(D_EXTRA,"ARCnet RFC1051 (NetBsd, AmiTCP) protocol initialized.\n");

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
	int ioaddr=dev->base_addr,delayval;
	
	if (dev->metric>=1000)
	{
		arcnet_debug=dev->metric-1000;
		printk("%6s: debug level set to %d\n",dev->name,arcnet_debug);
		dev->metric=1;
	}

	BUGLVL(D_EXTRA) printk(version);

	irq2dev_map[dev->irq] = dev;

	BUGMSG(D_EXTRA,"arcnet_open: resetting card.\n");
	
	/* try to reset - twice if it fails the first time */
	if (arcnet_reset(dev) && arcnet_reset(dev))
		return -ENODEV;
	
	dev->tbusy=0;
	dev->interrupt=0;
	lp->intx=0;
	lp->in_txhandler=0;
	
	/* The RFC1201 driver is the default - just store */
	lp->adev=dev;
	BUGMSG(D_EXTRA,"ARCnet RFC1201 protocol initialized.\n");
	
	/* Initialize the ethernet-encap protocol driver */
	lp->edev=(struct device *)kmalloc(sizeof(struct device),GFP_KERNEL);
	if (lp->edev == NULL)
		return -ENOMEM;
	memcpy(lp->edev,dev,sizeof(struct device));
	lp->edev->name=(char *)kmalloc(10,GFP_KERNEL);
	if (lp->edev->name == NULL) {
		kfree(lp->edev);
		lp->edev = NULL;
		return -ENOMEM;
	}
	sprintf(lp->edev->name,"%se",dev->name);
	lp->edev->init=arcnetE_init;
	register_netdev(lp->edev);

	/* Initialize the RFC1051-encap protocol driver */
	lp->sdev=(struct device *)kmalloc(sizeof(struct device),GFP_KERNEL);
	memcpy(lp->sdev,dev,sizeof(struct device));
	lp->sdev->name=(char *)kmalloc(10,GFP_KERNEL);
	sprintf(lp->sdev->name,"%ss",dev->name);
	lp->sdev->init=arcnetS_init;
	register_netdev(lp->sdev);

	/* we're started */
	START=1;
	
	/* make sure we're ready to receive IRQ's.
	 * arcnet_reset sets this for us, but if we receive one before
	 * START is set to 1, bad things happen.
	 */
	outb(0,INTMASK);
	JIFFER(ACKtime);
	outb(NORXflag|RECON_flag,INTMASK);
	
	MOD_INC_USE_COUNT;
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
	
	/* very important! */
	irq2dev_map[dev->irq] = NULL;

	
	/* Flush TX and disable RX */
	outb(0,INTMASK);	/* no IRQ's (except RESET, of course) */
	outb(NOTXcmd,COMMAND);  /* stop transmit */
	outb(NORXcmd,COMMAND);	/* disable receive */
	
	/* reset more flags */
	INTERRUPT=0;
	
	/* do NOT free lp->adev!!  It's static! */
	lp->adev=NULL;
	
	/* free the ethernet-encap protocol device */
	lp->edev->priv=NULL;
	dev_close(lp->edev);
	unregister_netdev(lp->edev);
	kfree(lp->edev->name);
	kfree(lp->edev);
	lp->edev=NULL;

	/* free the RFC1051-encap protocol device */
	lp->sdev->priv=NULL;
	dev_close(lp->sdev);
	unregister_netdev(lp->sdev);
	kfree(lp->sdev->name);
	kfree(lp->sdev);
	lp->sdev=NULL;

	/* Update the statistics here. (not necessary in ARCnet) */

	MOD_DEC_USE_COUNT;
	return 0;
}



/****************************************************************************
 *                                                                          *
 * Transmitter routines                                                     *
 *                                                                          *
 ****************************************************************************/

/* Generic error checking routine for arcnet??_send_packet
 */
static int
arcnet_send_packet_bad(struct sk_buff *skb, struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr;
	
	BUGMSG(D_DURING,"transmit requested (status=%Xh, inTX=%d)\n",
			inb(STATUS),lp->intx);
			
	if (lp->in_txhandler)
	{
		BUGMSG(D_NORMAL,"send_packet called while in txhandler!\n");
		lp->stats.tx_dropped++;
		return 1;
	}

	if (lp->intx>1)
	{
		BUGMSG(D_NORMAL,"send_packet called while intx!\n");
		lp->stats.tx_dropped++;
		return 1;
	}

	if (IF_TBUSY)
	{
		/* If we get here, some higher level has decided we are broken.
		   There should really be a "kick me" function call instead. */
		int tickssofar = jiffies - dev->trans_start;
		/*int recbuf=lp->recbuf;*/
		int status=inb(STATUS);
		
		if (tickssofar < TX_TIMEOUT) 
		{
			BUGMSG(D_DURING,"premature kickme! (status=%Xh ticks=%d o.skb=%ph numsegs=%d segnum=%d\n",
					status,tickssofar,lp->outgoing.skb,
					lp->outgoing.numsegs,
					lp->outgoing.segnum);
			return 1;
		}

		outb(0,INTMASK);

		if (status&TXFREEflag)	/* transmit _DID_ finish */
		{
			BUGMSG(D_EXTRA,"tx timed out - missed IRQ? (status=%Xh, inTX=%d, inTXh=%d, tickssofar=%d)\n",
					status,lp->intx,
					lp->in_txhandler,tickssofar);
			lp->stats.tx_errors++;
		}
		else
		{
			BUGMSG(D_NORMAL,"tx timed out (status=%Xh, inTX=%d, inTXh=%d, tickssofar=%d)\n",
					status,lp->intx,
					lp->in_txhandler,tickssofar);
			lp->stats.tx_errors++;
			lp->stats.tx_aborted_errors++;

			outb(NOTXcmd,COMMAND);
		}

		if (lp->outgoing.skb)
		{
			dev_kfree_skb(lp->outgoing.skb,FREE_WRITE);
			lp->stats.tx_dropped++;
		}
		lp->outgoing.skb=NULL;
		
		TBUSY=0;
		lp->txready=0;
		lp->sending=0;

		outb(NORXflag|RECON_flag,INTMASK);

		return 1;
	}

	/* If some higher layer thinks we've missed a tx-done interrupt
	   we are passed NULL. Caution: dev_tint() handles the cli()/sti()
	   itself. */
	if (skb == NULL) {
		printk("%6s: tx passed null skb (status=%Xh, inTX=%d, tickssofar=%ld)\n",
			dev->name,inb(STATUS),lp->intx,jiffies-dev->trans_start);
		lp->stats.tx_errors++;
		dev_tint(dev);
		return 0;
	}
	
	if (lp->txready)	/* transmit already in progress! */
	{
		printk("%6s: trying to start new packet while busy! (status=%Xh)\n",
			dev->name,inb(STATUS));
		outb(0,INTMASK);
		outb(NOTXcmd,COMMAND); /* abort current send */
		arcnet_inthandler(dev); /* fake an interrupt */
		lp->stats.tx_errors++;
		lp->stats.tx_fifo_errors++;
		lp->txready=0;	/* we definitely need this line! */
		
		return 1;
	}

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
        if (set_bit(0, (void*)&dev->tbusy) != 0)
        {
            printk("%6s: transmitter called with busy bit set! (status=%Xh, inTX=%d, tickssofar=%ld)\n",
            		dev->name,inb(STATUS),lp->intx,jiffies-dev->trans_start);
            lp->stats.tx_errors++;
            lp->stats.tx_fifo_errors++;
            return -EBUSY;
        }

	return 0;
}


/* Called by the kernel in order to transmit a packet.
 */
static int
arcnetA_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr,bad;
	struct Outgoing *out=&(lp->outgoing);

	BUGMSG(D_DURING,"transmit requested (status=%Xh, inTX=%d)\n",
			inb(STATUS),lp->intx);

	lp->intx++;
	
	bad=arcnet_send_packet_bad(skb,dev);
	if (bad)
	{
		lp->intx--;
		return bad;
	}
	
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
	
	/*arcnet_go_tx(dev);*/	/* Make sure buffers are clear */

	/* fits in one packet? */
	if (out->length-EXTRA_CLIENTDATA<=XMTU)
	{
		BUGMSG(D_DURING,"not splitting %d-byte packet. (split_flag=%d)\n",
				out->length,out->hdr->split_flag);
		BUGLVL(D_EXTRA) if (out->hdr->split_flag)
			printk("%6s: short packet has split_flag set?! (split_flag=%d)\n",
				dev->name,out->hdr->split_flag);
		out->numsegs=1;
		out->segnum=1;
		arcnetAS_prepare_tx(dev,
			((char *)out->hdr)+EXTRA_CLIENTDATA,
			sizeof(struct ClientData)-EXTRA_CLIENTDATA,
			((char *)skb->data)+sizeof(struct ClientData),
			out->length-sizeof(struct ClientData),
			out->hdr->daddr,1);

		/* done right away */
		dev_kfree_skb(out->skb,FREE_WRITE);
		out->skb=NULL;
					
		if (arcnet_go_tx(dev,1))
		{
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
			
		BUGMSG(D_TX,"packet (%d bytes) split into %d fragments:\n",
			out->length,out->numsegs);

		/* if a packet waiting, launch it */
		arcnet_go_tx(dev,1);

		if (!lp->txready)
		{
			/* prepare a packet, launch it and prepare
			 * another.
			 */
			arcnetA_continue_tx(dev);
			if (arcnet_go_tx(dev,1))
			{
				arcnetA_continue_tx(dev);
				arcnet_go_tx(dev,1);
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

	dev->trans_start=jiffies;
	lp->intx--;
	return 0;
}


/* Called by the kernel in order to transmit an ethernet-type packet.
 */
static int
arcnetE_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int bad;
	union ArcPacket *arcpacket = 
		(union ArcPacket *)(dev->mem_start+512*(lp->txbuf^1));
	u_char *arcsoft,daddr;
	short offset,length=skb->len+1;

	BUGMSG(D_DURING,"in arcnetE_send_packet (skb=%p)\n",skb);
		
	lp->intx++;
	
	bad=arcnet_send_packet_bad(skb,dev);
	if (bad)
	{
		lp->intx--;
		return bad;
	}

	TBUSY=1;
		
	if (length>XMTU)
	{
		printk("%6s: MTU must be <= 493 for ethernet encap (length=%d).\n",
			dev->name,length);
		printk("%6s: transmit aborted.\n",dev->name);

		dev_kfree_skb(skb,FREE_WRITE);
		return 0;
	}
		
	BUGMSG(D_DURING,"starting tx sequence...\n");

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
	
	BUGMSG(D_DURING," length=%Xh, offset=%Xh, offset1=%Xh, offset2=%Xh\n",
			length,offset,arcpacket->hardheader.offset1,
			arcpacket->hardheader.offset2);
	
	arcsoft=&arcpacket->raw[offset];
	arcsoft[0]=ARC_P_ETHER;
	arcsoft++;
		
	/* copy the packet into ARCnet shmem
	 *  - the first bytes of ClientData header are skipped
	 */
	BUGMSG(D_DURING,"ready to memcpy\n");
	
	memcpy(arcsoft,skb->data,skb->len);
		
	BUGMSG(D_DURING,"transmitting packet to station %02Xh (%d bytes)\n",
			daddr,length);
				
	BUGLVL(D_TX)
	{
		int countx,county;
			
		printk("%6s: packet dump [tx] follows:",dev->name);

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

	dev_kfree_skb(skb,FREE_WRITE);

	if (arcnet_go_tx(dev,1))
	{
		/* inform upper layers */
		TBUSY=0;
		mark_bh(NET_BH);
	}

	dev->trans_start=jiffies;
	lp->intx--;
	return 0;
}


/* Called by the kernel in order to transmit an RFC1051-type packet.
 */
static int
arcnetS_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr,bad,length;
	struct S_ClientData *hdr=(struct S_ClientData *)skb->data;

	lp->intx++;
	
	BUGMSG(D_DURING,"transmit requested (status=%Xh, inTX=%d)\n",
			inb(STATUS),lp->intx);

	bad=arcnet_send_packet_bad(skb,dev);
	if (bad)
	{
		lp->intx--;
		return bad;
	}
	
	TBUSY=1;

	length = 1 < skb->len ? skb->len : 1;

	BUGLVL(D_SKB)
	{
		short i;
		for(i=0; i<skb->len; i++)
		{
			if( i%16 == 0 ) printk("\n[%04hX] ",i);
			printk("%02hX ",((unsigned char*)skb->data)[i]);
		}
		printk("\n");
	}

	/*if (lp->txready && inb(STATUS)&TXFREEflag)
		arcnet_go_tx(dev);*/

	/* fits in one packet? */
	if (length-S_EXTRA_CLIENTDATA<=XMTU)
	{
		arcnetAS_prepare_tx(dev,
			skb->data+S_EXTRA_CLIENTDATA,
			sizeof(struct S_ClientData)-S_EXTRA_CLIENTDATA,
			skb->data+sizeof(struct S_ClientData),
			length-sizeof(struct S_ClientData),
			hdr->daddr,0);

		/* done right away */
		dev_kfree_skb(skb,FREE_WRITE);
				
		if (arcnet_go_tx(dev,1))
		{
			/* inform upper layers */
			TBUSY=0;
			mark_bh(NET_BH);
		}
	}
	else			/* too big for one - not accepted */
	{
		printk("arcnetS: packet too long (length=%d)\n",
			length);
		dev_kfree_skb(skb,FREE_WRITE);
		lp->stats.tx_dropped++;
		TBUSY=0;
		mark_bh(NET_BH);	
	}

	dev->trans_start=jiffies;
	lp->intx--;
	return 0;
}


/* After an RFC1201 split packet has been set up, this function calls
 * arcnetAS_prepare_tx to load the next segment into the card.  This function
 * does NOT automatically call arcnet_go_tx.
 */
static void arcnetA_continue_tx(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int maxsegsize=XMTU-4;
	struct Outgoing *out=&(lp->outgoing);
	
	if (lp->txready)
	{
		printk("%6s: continue_tx: called with packet in buffer!\n",
			dev->name);
		return;
	}

	if (out->segnum>=out->numsegs)
	{
		printk("%6s: continue_tx: building segment %d of %d!\n",
			dev->name,out->segnum+1,out->numsegs);
	}

	if (!out->segnum)	/* first packet */
		out->hdr->split_flag=((out->numsegs-2)<<1)+1;
	else
		out->hdr->split_flag=out->segnum<<1;

	out->seglen=maxsegsize;
	if (out->seglen>out->dataleft) out->seglen=out->dataleft;
			
	BUGMSG(D_TX,"building packet #%d (%d bytes) of %d (%d total), splitflag=%d\n",
		out->segnum+1,out->seglen,out->numsegs,
		out->length,out->hdr->split_flag);

	arcnetAS_prepare_tx(dev,((char *)out->hdr)+EXTRA_CLIENTDATA,
		sizeof(struct ClientData)-EXTRA_CLIENTDATA,
		out->data,out->seglen,out->hdr->daddr,1);
		
	out->dataleft-=out->seglen;
	out->data+=out->seglen;
	out->segnum++;
}


/* Given an skb, copy a packet into the ARCnet buffers for later transmission
 * by arcnet_go_tx.
 */
static void
arcnetAS_prepare_tx(struct device *dev,u_char *hdr,int hdrlen,
		char *data,int length,int daddr,int exceptA)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	struct ClientData *arcsoft;
	union ArcPacket *arcpacket = 
		(union ArcPacket *)(dev->mem_start+512*(lp->txbuf^1));
	int offset;
	
	lp->txbuf=lp->txbuf^1;	/* XOR with 1 to alternate between 2 and 3 */
	
	length+=hdrlen;

	BUGMSG(D_TX,"arcnetAS_prep_tx: hdr:%ph, length:%d, data:%ph\n",
			hdr,length,data);

	/* clean out the page to make debugging make more sense :) */
	BUGLVL(D_DURING)
		memset((void *)dev->mem_start+lp->txbuf*512,0x42,512);

	arcpacket->hardheader.destination=daddr;

	/* load packet into shared memory */
	if (length<=MTU)	/* Normal (256-byte) Packet */
	{
		arcpacket->hardheader.offset1=offset=256-length;
		arcsoft=(struct ClientData *)
			(&arcpacket->raw[offset]);
	}
	else if (length>=MinTU)	/* Extended (512-byte) Packet */
	{
		arcpacket->hardheader.offset1=0;
		arcpacket->hardheader.offset2=offset=512-length;
		
		arcsoft=(struct ClientData *)
			(&arcpacket->raw[offset]);
	}
	else if (exceptA)		/* RFC1201 Exception Packet */
	{
		arcpacket->hardheader.offset1=0;
		arcpacket->hardheader.offset2=offset=512-length-4;
		arcsoft=(struct ClientData *)
			(&arcpacket->raw[offset+4]);
		
		/* exception-specific stuff - these four bytes
		 * make the packet long enough to fit in a 512-byte
		 * frame.
		 */
		arcpacket->raw[offset+0]=hdr[0];
		arcpacket->raw[offset+1]=0xFF; /* FF flag */
			arcpacket->raw[offset+2]=0xFF; /* FF padding */
			arcpacket->raw[offset+3]=0xFF; /* FF padding */
	}
	else				/* "other" Exception packet */
	{
		/* RFC1051 - set 4 trailing bytes to 0 */
		memset(&arcpacket->raw[508],0,4);

		/* now round up to MinTU */
		arcpacket->hardheader.offset1=0;
		arcpacket->hardheader.offset2=offset=512-MinTU;
		arcsoft=(struct ClientData *)(&arcpacket->raw[offset]);
	}


	/* copy the packet into ARCnet shmem
	 *  - the first bytes of ClientData header are skipped
	 */
	memcpy((u_char*)arcsoft,
		(u_char*)hdr,hdrlen);
	memcpy((u_char*)arcsoft+hdrlen,
		data,length-hdrlen);
		
	BUGMSG(D_DURING,"transmitting packet to station %02Xh (%d bytes)\n",
			daddr,length);
			
	BUGLVL(D_TX)
	{
		int countx,county;
		
		printk("%6s: packet dump [tx] follows:",dev->name);

 		for (county=0; county<16+(length>MTU)*16; county++)
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
}

/* Actually start transmitting a packet that was placed in the card's
 * buffer by arcnetAS_prepare_tx.  Returns 1 if a Tx is really started.
 */
static int
arcnet_go_tx(struct device *dev,int enable_irq)
{
	struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr;

	BUGMSG(D_DURING,"go_tx: status=%Xh\n",
			inb(STATUS));
			
	outb(0,INTMASK);

	if (lp->sending || !lp->txready)
	{
		if (enable_irq)
		{
			if (lp->sending)
				outb(TXFREEflag|NORXflag|RECON_flag,INTMASK);
			else
				outb(NORXflag|RECON_flag,INTMASK);
		}
		return 0;
	}

	/* start sending */
	outb(TXcmd|(lp->txready<<3),COMMAND);

	lp->stats.tx_packets++;
	lp->txready=0;
	lp->sending++;

#ifdef VERIFY_ACK	
	lp->outgoing.lasttrans_dest=lp->outgoing.lastload_dest;
	lp->outgoing.lastload_dest=0;
#endif

	if (enable_irq)
		outb(TXFREEflag|NORXflag|RECON_flag,INTMASK);

	return 1;
}


/****************************************************************************
 *                                                                          *
 * Interrupt handler                                                        *
 *                                                                          *
 ****************************************************************************/


/* The typical workload of the driver:  Handle the network interface
 * interrupts.  This doesn't do much right now except call arcnet_inthandler,
 * which takes different parameters but may be called from other places
 * as well.
 */
static void
arcnet_interrupt(int irq,struct pt_regs *regs)
{
	struct device *dev = (struct device *)(irq2dev_map[irq]);

	if (dev==NULL)
	{
		BUGLVL(D_EXTRA)
			printk("arcnet: irq %d for unknown device.\n", irq);
		return;
	}
	
	if (!dev->start) return;

	arcnet_inthandler(dev);
}


/* The actual interrupt handler routine - handle various IRQ's generated
 * by the card.
 */
static void
arcnet_inthandler(struct device *dev)
{	
	struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr, status, boguscount = 3, didsomething;

	if (IF_INTERRUPT)
	{
		printk("%6s: DRIVER PROBLEM!  Nested arcnet interrupts!\n",
			dev->name);
		return;	/* don't even try. */
	}
	outb(0,INTMASK);
	INTERRUPT = 1;

	BUGMSG(D_DURING,"in net_interrupt (status=%Xh)\n",inb(STATUS));
		
#if 1 /* Whatever you do, don't set this to 0. */
	do
	{
		status = inb(STATUS);
		didsomething=0;
	
#if 0 /* no longer necessary - doing checking in arcnet_interrupt now */
		if (!dev->start)
		{
			BUGMSG(D_DURING,"ARCnet not yet initialized.  irq ignored. (status=%Xh)\n",
					status);
			if (!(status&NORXflag))
				outb(NORXflag|RECON_flag,INTMASK);

			/* using dev->interrupt here instead of INTERRUPT
			 * because if dev->start is 0, the other devices
			 * probably do not exist.
			 */
			dev->interrupt=0;
			return;
		}
#endif
	
		/* RESET flag was enabled - card is resetting and if RX
		 * is disabled, it's NOT because we just got a packet.
		 */
		if (status & RESETflag)
		{
			outb(CFLAGScmd|RESETclear,COMMAND);
			BUGMSG(D_INIT,"reset irq (status=%Xh)\n",
					status);
		}
#ifdef DETECT_RECONFIGS
		if (status & RECONflag)
		{
			outb(CFLAGScmd|CONFIGclear,COMMAND);
			lp->stats.tx_carrier_errors++;
			
			#ifdef SHOW_RECONFIGS
			BUGMSG(D_NORMAL,"Network reconfiguration detected (status=%Xh)\n",
					status);
			#endif /* SHOW_RECONFIGS */
			
			#ifdef RECON_THRESHOLD
			/* is the RECON info empty or old? */
			if (!lp->first_recon || !lp->last_recon || 
				jiffies-lp->last_recon > HZ*10)
			{
				if (lp->network_down)
					printk("%6s: reconfiguration detected: cabling restored?\n",
						dev->name);
				lp->first_recon=lp->last_recon=jiffies;
				lp->num_recons=lp->network_down=0;
				
				BUGMSG(D_DURING,"recon: clearing counters.\n");
			}
			else /* add to current RECON counter */
			{
				lp->last_recon=jiffies;
				lp->num_recons++;
				
				BUGMSG(D_DURING,"recon: counter=%d, time=%lds, net=%d\n",
					lp->num_recons,
					(lp->last_recon-lp->first_recon)/HZ,
					lp->network_down);

				/* if network is marked up;
				 * and first_recon and last_recon are 60+ sec
				 *   apart;
				 * and the average no. of recons counted is
				 *   > RECON_THRESHOLD/min;
				 * then print a warning message.
				 */
				if (!lp->network_down
				    && (lp->last_recon-lp->first_recon)<=HZ*60
				    && lp->num_recons >= RECON_THRESHOLD)
				{
					lp->network_down=1;
					printk("%6s: many reconfigurations detected: cabling problem?\n",
						dev->name);
				}
				else if (!lp->network_down
				    && lp->last_recon-lp->first_recon > HZ*60)
				{
					/* reset counters if we've gone for
					 * over a minute.
					 */
					lp->first_recon=lp->last_recon;
					lp->num_recons=1;
				}
			}
			#endif
		}
		#ifdef RECON_THRESHOLD
		else if (lp->network_down && jiffies-lp->last_recon > HZ*10)
		{
			if (lp->network_down)
				printk("%6s: cabling restored?\n",dev->name);
			lp->first_recon=lp->last_recon=0;
			lp->num_recons=lp->network_down=0;
			
			BUGMSG(D_DURING,"not recon: clearing counters anyway.\n");
		}
		#endif
#endif /* DETECT_RECONFIGS */

		/* RX is inhibited - we must have received something. */
		if (status & NORXflag)
		{
			int recbuf=lp->recbuf=!lp->recbuf;

			BUGMSG(D_DURING,"receive irq (status=%Xh)\n",
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
			
			BUGMSG(D_DURING,"TX IRQ (stat=%Xh, numsegs=%d, segnum=%d, skb=%ph)\n",
					status,out->numsegs,out->segnum,out->skb);
					
#ifdef VERIFY_ACK
			if (!(status&TXACKflag))
			{
				if (lp->outgoing.lasttrans_dest != 0)
				{
					printk("%6s: transmit was not acknowledged! (status=%Xh, dest=%d)\n",
						dev->name,status,
						lp->outgoing.lasttrans_dest);
					lp->stats.tx_errors++;
					lp->stats.tx_carrier_errors++;
				}
				else
				{
					BUGMSG(D_DURING,"broadcast was not acknowledged; that's normal (status=%Xh, dest=%d)\n",
							status,
							lp->outgoing.lasttrans_dest);
				}
			}
#endif
			/* send packet if there is one */
			arcnet_go_tx(dev,0);
			didsomething++;
			
			if (lp->intx)
			{
				lp->in_txhandler--;
				continue;
			}

			if (!lp->outgoing.skb)
			{
				BUGMSG(D_DURING,"TX IRQ done: no split to continue.\n");
				
				/* inform upper layers */
				if (!lp->txready && IF_TBUSY)
				{
					TBUSY=0;
					mark_bh(NET_BH);
				}
				
				lp->in_txhandler--;
				continue;
			}
			
			/* if more than one segment, and not all segments
			 * are done, then continue xmit.
			 */
			if (out->segnum<out->numsegs)
				arcnetA_continue_tx(dev);
			arcnet_go_tx(dev,0);

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
		else if (lp->txready && !lp->sending && !lp->intx)
		{
			BUGMSG(D_NORMAL,"recovery from silent TX (status=%Xh)\n",
						status);
			arcnet_go_tx(dev,0);
			didsomething++;
		}
	} while (--boguscount && didsomething);

	BUGMSG(D_DURING,"net_interrupt complete (status=%Xh, count=%d)\n\n",
			inb(STATUS),boguscount);

	if (dev->start && (lp->sending || (lp->txready && !lp->intx)))
		outb(NORXflag|TXFREEflag|RECON_flag,INTMASK);
	else
		outb(NORXflag|RECON_flag,INTMASK);

#else /* Disable everything */

	outb(RXcmd|(0<<3)|RXbcasts,COMMAND);
	outb(NORXflag,INTMASK);

#endif

	INTERRUPT=0;
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
	
	lp->stats.rx_packets++;

	saddr=arcpacket->hardheader.source;
	daddr=arcpacket->hardheader.destination;
	
	/* if source is 0, it's a "used" packet! */
	if (saddr==0)
	{
		printk("%6s: discarding old packet. (status=%Xh)\n",
			dev->name,inb(STATUS));
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
	
       	BUGMSG(D_DURING,"received packet from %02Xh to %02Xh (%d bytes)\n",
       			saddr,daddr,length);

	/* call the right receiver for the protocol */
	switch (arcsoft[0])
	{
	case ARC_P_IP:
	case ARC_P_ARP:
	case ARC_P_RARP:
	case ARC_P_IPX:
		arcnetA_rx(lp->adev,arcsoft,length,saddr,daddr);
		break;
	case ARC_P_ETHER:
		arcnetE_rx(lp->edev,arcsoft,length,saddr,daddr);
		break;		
	case ARC_P_IP_RFC1051:
	case ARC_P_ARP_RFC1051:
		arcnetS_rx(lp->sdev,arcsoft,length,saddr,daddr);
		break;
	default:
		printk("%6s: received unknown protocol %d (%Xh)\n",
			dev->name,arcsoft[0],arcsoft[0]);
		lp->stats.rx_errors++;
		lp->stats.rx_crc_errors++;
		break;
	}

        BUGLVL(D_RX)
	{
        	int countx,county;
        	
        	printk("%6s: packet dump [rx] follows:",dev->name);

       		for (county=0; county<16+(length>240)*16; county++)
       		{
       			printk("\n[%04X] ",county*16);
       			for (countx=0; countx<16; countx++)
       				printk("%02X ",
       					arcpacket->raw[county*16+countx]);
       		}
       		
       		printk("\n");
       	}


	/* clean out the page to make debugging make more sense :) */
	BUGLVL(D_DURING)
		memset((void *)arcpacket->raw,0x42,512);


	/* If any worth-while packets have been received, a mark_bh(NET_BH)
	 * has been done by netif_rx and Linux will handle them after we
	 * return.
	 */
}


/* Packet receiver for "standard" RFC1201-style packets
 */
static void
arcnetA_rx(struct device *dev,u_char *buf,
	int length,u_char saddr, u_char daddr)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	struct sk_buff *skb;
	struct ClientData *arcsoft,*soft;
	
	BUGMSG(D_DURING,"it's an RFC1201 packet (length=%d)\n",
			length);
			
	/* compensate for EXTRA_CLIENTDATA (which isn't actually in the
	 * packet)
	 */
	arcsoft=(struct ClientData *)(buf-EXTRA_CLIENTDATA);
	length+=EXTRA_CLIENTDATA;

	if (arcsoft->split_flag==0xFF)  /* Exception Packet */
	{
		BUGMSG(D_DURING,"compensating for exception packet\n");

		/* skip over 4-byte junkola */
		arcsoft=(struct ClientData *)
			((u_char *)arcsoft + 4);
		length-=4;
	}
	
	if (!arcsoft->split_flag)		/* not split */
	{
		struct Incoming *in=&lp->incoming[saddr];

		BUGMSG(D_RX,"incoming is not split (splitflag=%d)\n",
			arcsoft->split_flag);
			
		if (in->skb)	/* already assembling one! */
        	{
        		BUGMSG(D_EXTRA,"aborting assembly (seq=%d) for unsplit packet (splitflag=%d, seq=%d)\n",
        			in->sequence,arcsoft->split_flag,
        			arcsoft->sequence);
        		kfree_skb(in->skb,FREE_WRITE);
			lp->stats.rx_errors++;
        		lp->stats.rx_missed_errors++;
        		in->skb=NULL;
        	}
        	
        	in->sequence=arcsoft->sequence;

         	skb = alloc_skb(length, GFP_ATOMIC);
         	if (skb == NULL) {
         		printk("%6s: Memory squeeze, dropping packet.\n",
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
         				BUGMSG(D_EXTRA,"ARP source address was 00h, set to %02Xh.\n",
         						saddr);
         				lp->stats.rx_crc_errors++;
         				*cptr=saddr;
         			}
         			else
         			{
         				BUGMSG(D_DURING,"ARP source address (%Xh) is fine.\n",
         					*cptr);
         			}
         		}
         		else
         		{
         			printk("%6s: funny-shaped ARP packet. (%Xh, %Xh)\n",
         				dev->name,arp->ar_hln,arp->ar_pln);
         			lp->stats.rx_errors++;
         			lp->stats.rx_crc_errors++;
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

		#ifndef LINUX12
		skb->protocol=arcnetA_type_trans(skb,dev);
		#endif

         	netif_rx(skb);
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
	        
	        BUGMSG(D_RX,"packet is split (splitflag=%d, seq=%d)\n",
	        	arcsoft->split_flag,in->sequence);

		if (in->skb && in->sequence!=arcsoft->sequence)
		{
			BUGMSG(D_EXTRA,"wrong seq number, aborting assembly (expected=%d, seq=%d, splitflag=%d)\n",	
				in->sequence,arcsoft->sequence,
				arcsoft->split_flag);
	        	kfree_skb(in->skb,FREE_WRITE);
	        	in->skb=NULL;
			lp->stats.rx_errors++;
	        	lp->stats.rx_missed_errors++;
	        	in->lastpacket=in->numpackets=0;
	        }
	          
	        if (arcsoft->split_flag & 1)	/* first packet in split */
	        {
	        	BUGMSG(D_RX,"brand new splitpacket (splitflag=%d)\n",
	        		arcsoft->split_flag);
	        	if (in->skb)	/* already assembling one! */
	        	{
	        		BUGMSG(D_EXTRA,"aborting previous (seq=%d) assembly (splitflag=%d, seq=%d)\n",
	        			in->sequence,arcsoft->split_flag,
	        			arcsoft->sequence);
				lp->stats.rx_errors++;
	        		lp->stats.rx_missed_errors++;
	        		kfree_skb(in->skb,FREE_WRITE);
	        	}

			in->sequence=arcsoft->sequence;
	        	in->numpackets=((unsigned)arcsoft->split_flag>>1)+2;
	        	in->lastpacket=1;
	        	
	        	if (in->numpackets>16)
	        	{
	        		BUGMSG(D_EXTRA,"incoming packet more than 16 segments; dropping. (splitflag=%d)\n",
	        			arcsoft->split_flag);
	        		lp->stats.rx_errors++;
	        		lp->stats.rx_length_errors++;
	        		return;
	        	}
	        
                	in->skb=skb=alloc_skb(508*in->numpackets
                			+ sizeof(struct ClientData),
	                		GFP_ATOMIC);
                	if (skb == NULL) {
                		printk("%6s: (split) memory squeeze, dropping packet.\n", 
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
				BUGMSG(D_EXTRA,"can't continue split without starting first! (splitflag=%d, seq=%d)\n",
					arcsoft->split_flag,arcsoft->sequence);
				lp->stats.rx_errors++;
				lp->stats.rx_missed_errors++;
				return;
			}

	        	in->lastpacket++;
	        	if (packetnum!=in->lastpacket) /* not the right flag! */
	        	{
				/* harmless duplicate? ignore. */
				if (packetnum<=in->lastpacket-1)
				{
					BUGMSG(D_EXTRA,"duplicate splitpacket ignored! (splitflag=%d)\n",
						arcsoft->split_flag);
					lp->stats.rx_errors++;
					lp->stats.rx_frame_errors++;
					return;
				}
				
				/* "bad" duplicate, kill reassembly */
				BUGMSG(D_EXTRA,"out-of-order splitpacket, reassembly (seq=%d) aborted (splitflag=%d, seq=%d)\n",	
					in->sequence,arcsoft->split_flag,
					arcsoft->sequence);
	        		kfree_skb(in->skb,FREE_WRITE);
	        		in->skb=NULL;
				lp->stats.rx_errors++;
	        		lp->stats.rx_missed_errors++;
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
         		{
         			printk("%6s: ?!? done reassembling packet, no skb? (skb=%ph, in->skb=%ph)\n",
         				dev->name,skb,in->skb);
         		}
         		else
         		{
	         		in->skb=NULL;
        	 		in->lastpacket=in->numpackets=0;
			
				BUGLVL(D_SKB)
				{
		        	    short i;
		        	       	for(i=0; i<skb->len; i++)
               				{
                		       		if( i%16 == 0 ) printk("\n[%04hX] ",i);
                	       			printk("%02hX ",((unsigned char*)skb->data)[i]);
	               			}
        	       			printk("\n");
	            		}

			
				#ifndef LINUX12
				skb->protocol=arcnetA_type_trans(skb,dev);
				#endif
				
	         		netif_rx(skb);
	         	}
        	}
         }
}


/* Packet receiver for non-standard ethernet-style packets
 */
static void
arcnetE_rx(struct device *dev,u_char *arcsoft,
	int length,u_char saddr, u_char daddr)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	struct sk_buff *skb;
	
	BUGMSG(D_DURING,"it's an ethernet-encap packet (length=%d)\n",
			length);

       	skb = alloc_skb(length, GFP_ATOMIC);
       	if (skb == NULL) {
       		printk("%6s: Memory squeeze, dropping packet.\n",dev->name);
       		lp->stats.rx_dropped++;
       		return;
       	}
       	
       	skb->len = length;
       	skb->dev = dev;
       	
       	memcpy(skb->data,(u_char *)arcsoft+1,length-1);

        BUGLVL(D_SKB)
        {
		short i;
		printk("%6s: rx skb dump follows:\n",dev->name);
                for(i=0; i<skb->len; i++)
                {
			if (i%16==0)
				printk("\n[%04hX] ",i);
			else
                        	printk("%02hX ",((u_char *)skb->data)[i]);
		}
                printk("\n");
        }
        
	#ifndef LINUX12
	skb->protocol=eth_type_trans(skb,dev);
	#endif
        
        netif_rx(skb);
}

/* Packet receiver for RFC1051 packets; 
 */
static void
arcnetS_rx(struct device *dev,u_char *buf,
	int length,u_char saddr, u_char daddr)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	struct sk_buff *skb;
	struct S_ClientData *arcsoft,*soft;
	
	arcsoft=(struct S_ClientData *)(buf-S_EXTRA_CLIENTDATA);
	length+=S_EXTRA_CLIENTDATA;
	
	BUGMSG(D_DURING,"it's an RFC1051 packet (length=%d)\n",
			length);
			
	
			
	{    /* was "if not split" in A protocol, S is never split */	

         	skb = alloc_skb(length, GFP_ATOMIC);
         	if (skb == NULL) {
         		printk("arcnetS: Memory squeeze, dropping packet.\n");
         		lp->stats.rx_dropped++;
         		return;
         	}
         	soft=(struct S_ClientData *)skb->data;
		skb->len = length;
		memcpy((u_char *)soft + sizeof(struct S_ClientData)
				- S_EXTRA_CLIENTDATA,
			(u_char *)arcsoft + sizeof(struct S_ClientData)
				- S_EXTRA_CLIENTDATA, 
			length - sizeof(struct S_ClientData)
				+ S_EXTRA_CLIENTDATA);
		soft->protocol_id=arcsoft->protocol_id;
         	soft->daddr=daddr;
         	soft->saddr=saddr;
         	skb->dev = dev;  /* is already lp->sdev */
         	
		BUGLVL(D_SKB)
		{
	        	short i;
	               	for(i=0; i<skb->len; i++)
               		{
                       		if( i%16 == 0 ) printk("\n[%04hX] ",i);
                       		printk("%02hX ",((unsigned char*)skb->data)[i]);
               		}
               		printk("\n");
            	}

		#ifndef LINUX12
		skb->protocol=arcnetS_type_trans(skb,dev);
		#endif

         	netif_rx(skb);
         }
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
#if 0	  /* no promiscuous mode at all on most (all?) ARCnet models */
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
#ifdef LINUX12
int arcnetA_header(unsigned char *buff,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len,
		struct sk_buff *skb)
#else
int arcnetA_header(struct sk_buff *skb,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len)
#endif
{
	struct ClientData *head = (struct ClientData *)
#ifdef LINUX12
		buff;
#else
		skb_push(skb,dev->hard_header_len);
#endif
	struct arcnet_local *lp=(struct arcnet_local *)(dev->priv);

	BUGMSG(D_DURING,"create header from %d to %d; protocol %d (%Xh); size %u.\n",
			saddr ? *(u_char*)saddr : -1,
			daddr ? *(u_char*)daddr : -1,
			type,type,len);

	/* set the protocol ID according to RFC1201 */
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
	case ETH_P_802_3:
	case ETH_P_802_2:
		head->protocol_id=ARC_P_IPX;
		break;
	case ETH_P_ATALK:
		head->protocol_id=ARC_P_ATALK;
		break;
	default:
		printk("%6s: I don't understand protocol %d (%Xh)\n",
			dev->name,type,type);
		lp->stats.tx_errors++;
		lp->stats.tx_aborted_errors++;
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


/* Create the ARCnet ClientData header for an arbitrary protocol layer
 *
 * saddr=NULL	means use device source address (always will anyway)
 * daddr=NULL	means leave destination address (eg unresolved arp)
 */
#ifdef LINUX12
int arcnetS_header(unsigned char *buff,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len,
		struct sk_buff *skb)
#else
int arcnetS_header(struct sk_buff *skb,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len)
#endif
{
	struct S_ClientData *head = (struct S_ClientData *)
#ifdef LINUX12
		buff;
#else
		skb_push(skb,dev->hard_header_len);
#endif
	struct arcnet_local *lp=(struct arcnet_local *)(dev->priv);

	/* set the protocol ID according to RFC1051 */
	switch(type)
	{
	case ETH_P_IP:
		head->protocol_id=ARC_P_IP_RFC1051;
		BUGMSG(D_DURING,"S_header: IP_RFC1051 packet.\n");
		break;
	case ETH_P_ARP:
		head->protocol_id=ARC_P_ARP_RFC1051;
		BUGMSG(D_DURING,"S_header: ARP_RFC1051 packet.\n");
		break;
	default:
		printk("arcnetS: I don't understand protocol %d (%Xh)\n",
			type,type);
		lp->stats.tx_errors++;
		lp->stats.tx_aborted_errors++;
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
	struct arcnet_local *lp=(struct arcnet_local *)(dev->priv);
	int status;

	/*
	 * Only ARP and IP are currently supported
	 */
	 
	if(head->protocol_id != ARC_P_IP) 
	{
		printk("%6s: I don't understand protocol type %d (%Xh) addresses!\n",
			dev->name,head->protocol_id,head->protocol_id);
		lp->stats.tx_errors++;
		lp->stats.tx_aborted_errors++;
		head->daddr=0;
		/*memcpy(eth->h_source, dev->dev_addr, dev->addr_len);*/
		return 0;
	}

	/*
	 * Try and get ARP to resolve the header.
	 */
#ifdef CONFIG_INET	 
	BUGMSG(D_DURING,"rebuild header from %d to %d; protocol %Xh\n",
			head->saddr,head->daddr,head->protocol_id);
	status=arp_find(&(head->daddr), dst, dev, dev->pa_addr, skb)? 1 : 0;
	BUGMSG(D_DURING," rebuilt: from %d to %d; protocol %Xh\n",
			head->saddr,head->daddr,head->protocol_id);
	return status;
#else
	return 0;	
#endif	
}


int arcnetS_rebuild_header(void *buff,struct device *dev,unsigned long dst,
		struct sk_buff *skb)
{
	struct S_ClientData *head = (struct S_ClientData *)buff;
	struct arcnet_local *lp=(struct arcnet_local *)(dev->priv);

	/*
	 * Only ARP and IP are currently supported
	 */
	 
	if(head->protocol_id != ARC_P_IP_RFC1051) 
	{
		printk("arcnetS: I don't understand protocol type %d (%Xh) addresses!\n",
			head->protocol_id,head->protocol_id);
		lp->stats.tx_errors++;
		lp->stats.tx_aborted_errors++;
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
	struct ClientData *head;
	struct arcnet_local *lp=(struct arcnet_local *) (dev->priv);

#ifdef LINUX12
	head=(struct ClientData *)skb->data;
#else
	/* Pull off the arcnet header. */
	skb->mac.raw=skb->data;
	skb_pull(skb,dev->hard_header_len);
	head=(struct ClientData *)skb->mac.raw;
#endif
	
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
	case ARC_P_IPX:		return htons(ETH_P_802_3);
	case ARC_P_ATALK:   return htons(ETH_P_ATALK); /* untested appletalk */
	case ARC_P_LANSOFT: /* don't understand.  fall through. */
	default:
		BUGMSG(D_EXTRA,"received packet of unknown protocol id %d (%Xh)\n",
				head->protocol_id,head->protocol_id);
		lp->stats.rx_errors++;
		lp->stats.rx_crc_errors++;
		return 0;
	}

	return htons(ETH_P_IP);
}


unsigned short arcnetS_type_trans(struct sk_buff *skb,struct device *dev)
{
	struct S_ClientData *head;
	struct arcnet_local *lp=(struct arcnet_local *) (dev->priv);

#ifdef LINUX12
	head=(struct S_ClientData *)skb->data;
#else
	/* Pull off the arcnet header. */
	skb->mac.raw=skb->data;
	skb_pull(skb,dev->hard_header_len);
	head=(struct S_ClientData *)skb->mac.raw;
#endif
	
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
	case ARC_P_IP_RFC1051:	return htons(ETH_P_IP);
	case ARC_P_ARP_RFC1051:	return htons(ETH_P_ARP);
	default:
		BUGMSG(D_EXTRA,"received packet of unknown protocol id %d (%Xh)\n",
				head->protocol_id,head->protocol_id);
		lp->stats.rx_errors++;
		lp->stats.rx_crc_errors++;
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
static char devicename[9] = { 0, };
static struct device thiscard = {
  devicename, /* device name is inserted by linux/drivers/net/net_init.c */
  0, 0, 0, 0,
  0, 0,  /* I/O address, IRQ */
  0, 0, 0, NULL, arcnet_probe
};
	
	
static int io=0x0;	/* <--- EDIT THESE LINES FOR YOUR CONFIGURATION */
static int irqnum=0;	/* or use the insmod io= irqnum= shmem= options */
static int shmem=0;
static int num=0;	/* number of device (ie for 0 for arc0, 1 for arc1...) */

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
	if (thiscard.start) arcnet_close(&thiscard);
	if (thiscard.irq) free_irq(thiscard.irq);
	if (thiscard.base_addr) release_region(thiscard.base_addr,
						ARCNET_TOTAL_SIZE);
	unregister_netdev(&thiscard);
	kfree(thiscard.priv);
	thiscard.priv = NULL;
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

