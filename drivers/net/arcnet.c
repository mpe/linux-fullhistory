/* arcnet.c:
	Written 1994-1996 by Avery Pennarun,
	derived from skeleton.c by Donald Becker.

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
	
	v2.53 (96/06/06)
	  - arc0e and arc0s wouldn't initialize in newer kernels, which
	    don't like dev->open==NULL or dev->stop==NULL.
	
	v2.52 (96/04/20)
	  - Replaced more decimal node ID's with hex, for consistency.
	  - Changed a couple of printk debug levels.

	v2.51 (96/02/29)
	  - Inserted a rather important missing "volatile" in autoprobe.
	  - arc0e and arc0s are now options in drivers/net/Config.in.
	
	v2.50 (96/02/24)
	  - Increased IRQ autoprobe delay.  Thanks to Andrew J. Kroll for
	    noticing the problem, which seems to only affect certain cards.
	  - Errors reserving ports and IRQ's now clean up after themselves.
	  - We now replace the TESTvalue byte from unused shmem addresses.
	  - You can now use "irq=" as well as "irqnum=" to set the IRQ
	    during module autoprobe.  This doesn't seem to crash insmod
	    anymore. (?)
	  - You can now define the name of the ARCnet device more easily
	    when loading the module: insmod arcnet.o device=test1
	    A new option, CONFIG_ARCNET_ETHNAME, allows the kernel to
	    choose one of the standard eth?-style device names
	    automatically.  This currently only works as a module.
	  - printk's now try to make some use of the KERN_xxx priority level
	    macros (though they may not be perfect yet).
	  - Packet and skb dump loops are now separate functions.
	  - Removed D_INIT from default debug level, because I am (finally)
	    pretty confident that autoprobe shouldn't toast anyone's
	    computer.
	  - This version is no longer ALPHA.

	v2.41 ALPHA (96/02/10)
	  - Incorporated changes someone made to arcnet_setup in 1.3.60.
	  - Increased reset timeout to 3/10 of a second because some cards
	    are too slow.
	  - Removed a useless delay from autoprobe stage 4; I wonder
	    how that got there!  (oops)
	  - If FAST_IFCONFIG is defined, don't reset the card during
	    arcnet_open; rather, do it in arcnet_close but don't delay. 
	    This speeds up calls to ifconfig up/down.  (Thanks to Vojtech
	    for the idea to speed this up.)
	  - If FAST_PROBE is defined, don't bother to reset the card in
	    autoprobe Stage 5 when there is only one io port and one shmem
	    left in the list.  They "obviously" correspond to each other. 
	    Another good idea from Vojtech.

	v2.40 ALPHA (96/02/03)
	  - Checked the RECON flag last in the interrupt handler (when
	    enabled) so the flag will show up in "status" when reporting
	    other errors.  (I had a cabling problem that was hard to notice
	    until I saw the huge RECON count in /proc/net/dev.)
	  - Moved "IRQ for unknown device" message to D_DURING.
	  - "transmit timed out" and "not acknowledged" messages are
	    now D_EXTRA, because they very commonly happen when things
	    are working fine.  "transmit timed out" due to missed IRQ's
	    is still D_NORMAL, because it's probably a bug.
	  - "Transmit timed out" messages now include destination station id.
	  - The virtual arc0e and arc0s devices can now be disabled. 
	    Massive (but simple) code rearrangement to support this with
	    fewer ifdef's.
	  - SLOW_XMIT_COPY option so fast computers don't hog the bus.  It's
	    weird, but it works.
	  - Finally redesigned autoprobe after some discussion with Vojtech
	    Pavlik <Vojtech.Pavlik@st.mff.cuni.cz>.  It should be much safer
	    and more reliable now, I think.  We now probe several more io
	    ports and many more shmem addresses.
	  - Rearranged D_* debugging flags slightly.  Watch out!  There is
	    now a new flag, disabled by default, that will tell you the
	    reason a particular port or shmem is discarded from the list.

	v2.30 ALPHA (96/01/10)
	  - Abandoned support for Linux 1.2 to simplify coding and allow me
	    to take advantage of new features in 1.3.
	  - Updated cabling/jumpers documentation with MUCH information that
	    people have recently (and in some cases, not so recently) sent
	    me.  I don't mind writing documentation, but the jumpers
	    database is REALLY starting to get dull.
	  - Autoprobing works as a module now - but ONLY with my fix to
	    register_netdev and unregister_netdev.  It's also much faster,
	    and uses the "new-style" IRQ autoprobe routines.  Autoprobe is
	    still a horrible mess and will be cleaned up further as time
	    passes.
	    
	The following has been SUMMARIZED.  The complete ChangeLog is
	available in the full Linux-ARCnet package.
	
	v2.22 (95/12/08)
	  - Major cleanups, speedups, and better code-sharing.
	  - Eliminated/changed many useless/meaningless/scary debug messages
	    (and, in most cases, the bugs that caused them).
	  - Better IPX support.
	  - lp->stats updated properly.
	  - RECON checking now by default only prints a message if there are
	    excessive errors (ie. your cable is probably broken).
	  - New RFC1051-compliant "arc0s" virtual device by Tomasz
	    Motylewski.
	  - Excess debug messages can be compiled out to reduce code size.

	v2.00 (95/09/06)
	  - ARCnet RECON messages are now detected and logged as "carrier"
	    errors.
	  - The TXACK flag is now checked, and errors are logged.
	  - Debug levels are now completely different.  See the README.
	  - Massive code cleanups, with several no-longer-necessary and some
	    completely useless options removed.
	  - Multiprotocol support.  You can now use the "arc0e" device to
	    send "Ethernet-Encapsulation" packets, which are compatible with
	    Windows for Workgroups and LAN Manager, and possibly other
	    software.  See the README for more information.
	  
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
	
         
	TO DO: (semi-prioritized)
	
         - Smarter recovery from RECON-during-transmit conditions.
         - Make arcnetE_send_packet use arcnet_prepare_tx for loading the
           packet into ARCnet memory.
	 - Probe for multiple devices in one shot (trying to decide whether
	   to do it the "ugly" way or not).
         - Add support for the new 1.3.x IP header cache features.
	 - Debug level should be changed with a system call, not a hack to
	   the "metric" flag.
         - What about cards with shared memory that can be "turned off?"
           (or that have none at all, like the SMC PC500longboard)
         - Autoconfigure PDI5xxPlus cards. (I now have a PDI508Plus to play
           with temporarily.)  Update: yes, the Pure Data config program
           for DOS works fine, but the PDI508Plus I have doesn't! :)
         - Try to implement promiscuous (receive-all-packets) mode available
           on some newer cards with COM20020 and similar chips.  I don't have
           one, but SMC sent me the specs.
         - ATA protocol support?? 
         - VINES TCP/IP encapsulation?? (info needed)

           
	Sources:
	 - Crynwr arcnet.com/arcether.com packet drivers.
	 - arcnet.c v0.00 dated 1/1/94 and apparently by
	 	Donald Becker - it didn't work :)
	 - skeleton.c v0.05 dated 11/16/93 by Donald Becker
	 	(from Linux Kernel 1.1.45)
	 - RFC's 1201 and 1051 - re: TCP/IP over ARCnet
	 - The official ARCnet COM9026 data sheets (!) thanks to Ken
		Cornetet <kcornete@nyx10.cs.du.edu>
	 - Information on some more obscure ARCnet controller chips, thanks
	   to the nice people at SMC.
	 - net/inet/eth.c (from kernel 1.1.50) for header-building info.
	 - Alternate Linux ARCnet source by V.Shergin <vsher@sao.stavropol.su>
	 - Textual information and more alternate source from Joachim Koenig
	 	<jojo@repas.de>
*/

static const char *version =
 "arcnet.c: v2.53 96/06/06 Avery Pennarun <apenwarr@foxnet.net>\n";

 

#include <linux/module.h>
#include <linux/config.h>
#include <linux/version.h>

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
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <net/arp.h>

/**************************************************************************/

/* Normally, the ARCnet device needs to be assigned a name (default arc0). 
 * Ethernet devices have a function to automatically try eth0, eth1, etc
 * until a free name is found.  To name the ARCnet device using an "eth?"
 * device name, define this option.
 */
#undef CONFIG_ARCNET_ETHNAME

/* On a fast computer, the buffer copy from memory to the ARCnet card during
 * a transmit can hog the bus just a little too long.  SLOW_XMIT_COPY
 * replaces the fast memcpy() with a slower for() loop that seems to solve
 * my problems with ftape.
 *
 * Probably a better solution would be to use memcpy_toio (more portable
 * anyway) and modify that routine to support REALLY_SLOW_IO-style
 * defines; ARCnet probably is not the only driver that can screw up an
 * ftape DMA transfer.
 *
 * Turn this off if you don't have timing-sensitive DMA (ie. a tape drive)
 * and would like the little bit of speed back.  It's on by default because
 * - trust me - it's very difficult to figure out that you need it!
 */
#define SLOW_XMIT_COPY

/* The card sends the reconfiguration signal when it loses the connection to
 * the rest of its network. It is a 'Hello, is anybody there?' cry.  This
 * usually happens when a new computer on the network is powered on or when
 * the cable is broken.
 *
 * Define DETECT_RECONFIGS if you want to detect network reconfigurations. 
 * Recons may be a real nuisance on a larger ARCnet network; if you are a
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

/* Define this to the minimum "timeout" value.  If a transmit takes longer
 * than TX_TIMEOUT jiffies, Linux will abort the TX and retry.  On a large
 * network, or one with heavy network traffic, this timeout may need to be
 * increased.  The larger it is, though, the longer it will be between
 * necessary transmits - don't set this too large.
 */
#define TX_TIMEOUT 20

/* Define this to speed up the autoprobe by assuming if only one io port and
 * shmem are left in the list at Stage 5, they must correspond to each
 * other.
 *
 * This is undefined by default because it might not always be true, and the
 * extra check makes the autoprobe even more careful.  Speed demons can turn
 * it on - I think it should be fine if you only have one ARCnet card
 * installed.
 *
 * If no ARCnet cards are installed, this delay never happens anyway and thus
 * the option has no effect.
 */
#undef FAST_PROBE

/* Define this to speed up "ifconfig up" by moving the card reset command
 * around.  This is a new option in 2.41 ALPHA.  If it causes problems,
 * undefine this to get the old behaviour; then send me email, because if
 * there are no problems, this option will go away very soon.
 */
#define FAST_IFCONFIG

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
 * An ioctl() should be used for this instead, someday.
 *
 * Note: only debug flags included in the ARCNET_DEBUG_MAX define will
 *   actually be available.  GCC will (at least, GCC 2.7.0 will) notice
 *   lines using a BUGLVL not in ARCNET_DEBUG_MAX and automatically optimize
 *   them out.
 */
#define D_NORMAL	1	/* important operational info		*/
#define D_EXTRA		2	/* useful, but non-vital information	*/
#define	D_INIT		4	/* show init/probe messages		*/
#define D_INIT_REASONS	8	/* show reasons for discarding probes	*/
/* debug levels below give LOTS of output during normal operation! */
#define D_DURING	16	/* trace operations (including irq's)	*/
#define D_TX		32	/* show tx packets			*/
#define D_RX		64	/* show rx packets			*/
#define D_SKB		128	/* show skb's				*/

#ifndef ARCNET_DEBUG_MAX
#define ARCNET_DEBUG_MAX (~0)		/* enable ALL debug messages	 */
/*#define ARCNET_DEBUG_MAX (D_NORMAL|D_EXTRA|D_INIT|D_INIT_REASONS) 	 */
/*#define ARCNET_DEBUG_MAX 0	*/	/* enable NO messages (bad idea) */
#endif

#ifndef ARCNET_DEBUG
#define ARCNET_DEBUG (D_NORMAL|D_EXTRA)
#endif
int arcnet_debug = ARCNET_DEBUG;

/* macros to simplify debug checking */
#define BUGLVL(x) if ((ARCNET_DEBUG_MAX)&arcnet_debug&(x))
#define BUGMSG2(x,msg,args...) BUGLVL(x) printk(msg, ## args)
#define BUGMSG(x,msg,args...) BUGMSG2(x,"%s%6s: " msg, \
            x==D_NORMAL	? KERN_WARNING : \
      x<=D_INIT_REASONS	? KERN_INFO    : KERN_DEBUG , \
	dev->name , ## args)

/* Some useful multiprotocol macros.  The idea here is that GCC will
 * optimize away multiple tests or assignments to lp->adev.  Relying on this
 * results in the cleanest mess possible.
 */
#define ADEV lp->adev
 
#ifdef CONFIG_ARCNET_ETH
 #define EDEV lp->edev
#else
 #define EDEV lp->adev
#endif

#ifdef CONFIG_ARCNET_1051
 #define SDEV lp->sdev
#else
 #define SDEV lp->adev
#endif

#define TBUSY ADEV->tbusy=EDEV->tbusy=SDEV->tbusy
#define IF_TBUSY (ADEV->tbusy||EDEV->tbusy||SDEV->tbusy)

#define INTERRUPT ADEV->interrupt=EDEV->interrupt=SDEV->interrupt
#define IF_INTERRUPT (ADEV->interrupt||EDEV->interrupt||SDEV->interrupt)

#define START ADEV->start=EDEV->start=SDEV->start


/* The number of low I/O ports used by the ethercard. */
#define ARCNET_TOTAL_SIZE	16


/* Handy defines for ARCnet specific stuff */
	/* COM 9026 controller chip --> ARCnet register addresses */
#define INTMASK	(ioaddr+0)		/* writable */
#define STATUS	(ioaddr+0)		/* readable */
#define COMMAND (ioaddr+1)	/* writable, returns random vals on read (?) */
#define RESET  (ioaddr+8)		/* software reset writable */

#define SETMASK outb(lp->intmask,INTMASK);

	/* Time needed to reset the card - in jiffies.  This works on my SMC
	 * PC100.  I can't find a reference that tells me just how long I
	 * should wait.
	 */
#define RESETtime (HZ * 3 / 10)		/* reset */

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
#define RES1flag        0x20            /* reserved - usually set by jumper */
#define RES2flag        0x40            /* reserved - usually set by jumper */
#define NORXflag        0x80            /* receiver inhibited */

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
#define ARC_P_NOVELL_EC	236		/* 0xEC */

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
};


/* Information that needs to be kept for each board. */
struct arcnet_local {
	struct enet_statistics stats;
	u_short sequence;	/* sequence number (incs with each packet) */
	u_char stationid,	/* our 8-bit station address */
		recbuf,		/* receive buffer # (0 or 1) */
		txbuf,		/* transmit buffer # (2 or 3) */
		txready,	/* buffer where a packet is ready to send */
		intmask;	/* current value of INTMASK register */
	short intx,		/* in TX routine? */
	      in_txhandler,	/* in TX_IRQ handler? */
	      sending,		/* transmit in progress? */
	      lastload_dest,	/* can last loaded packet be acked? */
	      lasttrans_dest;	/* can last TX'd packet be acked? */

#if defined(DETECT_RECONFIGS) && defined(RECON_THRESHOLD)
	time_t first_recon,	/* time of "first" RECON message to count */
		last_recon;	/* time of most recent RECON */
	int num_recons,		/* number of RECONs between first and last. */
	    network_down;	/* do we think the network is down? */
#endif
	
	struct timer_list timer; /* the timer interrupt struct */
	struct Incoming incoming[256];	/* one from each address */
	struct Outgoing outgoing; /* packet currently being sent */
	
	struct device *adev;	/* RFC1201 protocol device */

#ifdef CONFIG_ARCNET_ETH
	struct device *edev;	/* Ethernet-Encap device */
#endif

#ifdef CONFIG_ARCNET_1051
	struct device *sdev;	/* RFC1051 protocol device */
#endif
};


/* Index to functions, as function prototypes. */

#if ARCNET_DEBUG_MAX & D_SKB
static void arcnet_dump_skb(struct device *dev,struct sk_buff *skb,
	char *desc);
#else
#	define arcnet_dump_skb(dev,skb,desc) ;
#endif

#if (ARCNET_DEBUG_MAX & D_RX) || (ARCNET_DEBUG_MAX & D_TX)
static void arcnet_dump_packet(struct device *dev,u_char *buffer,int ext,
	char *desc);
#else
#	define arcnet_dump_packet(dev,buffer,ext,desc) ;
#endif

extern int arcnet_probe(struct device *dev);
static int arcnet_found(struct device *dev,int port,int airq,u_long shmem);

static void arcnet_setup(struct device *dev);
static int arcnet_open(struct device *dev);
static int arcnet_close(struct device *dev);
static int arcnet_reset(struct device *dev,int reset_delay);

static int arcnet_send_packet_bad(struct sk_buff *skb,struct device *dev);
static int arcnetA_send_packet(struct sk_buff *skb, struct device *dev);
static void arcnetA_continue_tx(struct device *dev);
static void arcnetAS_prepare_tx(struct device *dev,u_char *hdr,int hdrlen,
		char *data,int length,int daddr,int exceptA);
static int arcnet_go_tx(struct device *dev,int enable_irq);

static void arcnet_interrupt(int irq,void *dev_id,struct pt_regs *regs);
static void arcnet_inthandler(struct device *dev);

static void arcnet_rx(struct device *dev,int recbuf);
static void arcnetA_rx(struct device *dev,u_char *buf,
	int length,u_char saddr, u_char daddr);

static struct enet_statistics *arcnet_get_stats(struct device *dev);

int arcnetA_header(struct sk_buff *skb,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len);
int arcnetA_rebuild_header(void *eth,struct device *dev,unsigned long raddr,
		struct sk_buff *skb);
unsigned short arcnetA_type_trans(struct sk_buff *skb,struct device *dev);

#ifdef CONFIG_ARCNET_ETH
	/* functions specific to Ethernet-Encap */
static int arcnetE_init(struct device *dev);
static int arcnetE_open_close(struct device *dev);
static int arcnetE_send_packet(struct sk_buff *skb, struct device *dev);
static void arcnetE_rx(struct device *dev,u_char *arcsoft,
	int length,u_char saddr, u_char daddr);
#endif

#ifdef CONFIG_ARCNET_1051
	/* functions specific to RFC1051 */
static int arcnetS_init(struct device *dev);
static int arcnetS_open_close(struct device *dev);
static int arcnetS_send_packet(struct sk_buff *skb, struct device *dev);
static void arcnetS_rx(struct device *dev,u_char *buf,
	int length,u_char saddr, u_char daddr);
int arcnetS_header(struct sk_buff *skb,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len);
int arcnetS_rebuild_header(void *eth,struct device *dev,unsigned long raddr,
		struct sk_buff *skb);
unsigned short arcnetS_type_trans(struct sk_buff *skb,struct device *dev);
#endif

#ifdef MODULE
int  init_module(void);
void cleanup_module(void);
#endif

#define tx_done(dev) 1

#define JIFFER(time) for (delayval=jiffies+time; jiffies<delayval;) ;


/****************************************************************************
 *                                                                          *
 * Packet dumps for debugging                                               *
 *                                                                          *
 ****************************************************************************/

/* Dump the contents of an sk_buff
 */ 
#if ARCNET_DEBUG_MAX & D_SKB
void arcnet_dump_skb(struct device *dev,struct sk_buff *skb,char *desc)
{
	int i;
	long flags;
	
	save_flags(flags);
	cli();
	printk(KERN_DEBUG "%6s: skb dump (%s) follows:",dev->name,desc);
        for(i=0; i<skb->len; i++)
        {
		if (i%16==0)
			printk("\n" KERN_DEBUG "[%04X] ",i);
		else
                       	printk("%02X ",((u_char *)skb->data)[i]);
	}
        printk("\n");
        restore_flags(flags);
}
#endif

/* Dump the contents of an ARCnet buffer
 */
#if (ARCNET_DEBUG_MAX & D_RX) || (ARCNET_DEBUG_MAX & D_TX)
void arcnet_dump_packet(struct device *dev,u_char *buffer,int ext,char *desc)
{
	int i;
	long flags;
	
	save_flags(flags);
	cli();
	printk(KERN_DEBUG "%6s: packet dump (%s) follows:",dev->name,desc);
	for (i=0; i<256+(ext!=0)*256; i++)
	{
		if (i%16==0)
			printk("\n" KERN_DEBUG "[%04X] ",i);
		else
			printk("%02X ",buffer[i]);
	}
	printk("\n");
	restore_flags(flags);
}
#endif 

/****************************************************************************
 *                                                                          *
 * Probe and initialization                                                 *
 *                                                                          *
 ****************************************************************************/
 
/* Check for an ARCnet network adaptor, and return '0' if one exists.
 *  If dev->base_addr == 0, probe all likely locations.
 *  If dev->base_addr == 1, always return failure.
 *  If dev->base_addr == 2, allocate space for the device and return success
 *  			    (detachable devices only).
 *
 * NOTE: the list of possible ports/shmems is static, so it is retained
 * across calls to arcnet_probe.  So, if more than one ARCnet probe is made,
 * values that were discarded once will not even be tried again.
 */
int arcnet_probe(struct device *dev)
{
	static int init_once = 0;
	static int ports[(0x3f0 - 0x200) / 16 + 1];
	static u_long shmems[(0xFF800 - 0xA0000) / 2048 + 1];
	static int numports=sizeof(ports)/sizeof(ports[0]),
	    	   numshmems=sizeof(shmems)/sizeof(shmems[0]);

	int count,status,delayval,ioaddr,numprint,airq,retval=-ENODEV,
		openparen=0;
	unsigned long airqmask;
	int *port;
	u_long *shmem;
	
	if (!init_once)
	{
		for (count=0x200; count<=0x3f0; count+=16)
			ports[(count-0x200)/16] = count;
		for (count=0xA0000; count<=0xFF800; count+=2048)
			shmems[(count-0xA0000)/2048] = count;
	}
	else
		init_once=1;

	BUGLVL(D_NORMAL) printk(version);

	BUGMSG(D_DURING,"space used for probe buffers: %d+%d=%d bytes\n",
		sizeof(ports),sizeof(shmems),
		sizeof(ports)+sizeof(shmems));

	
#if 0
	BUGLVL(D_EXTRA)
	{
		printk("arcnet: ***\n");
		printk("arcnet: * Read arcnet.txt for important release notes!\n");
		printk("arcnet: *\n");
		printk("arcnet: * This is an ALPHA version!  (Last stable release: v2.22)  E-mail me if\n");
		printk("arcnet: * you have any questions, comments, or bug reports.\n");
		printk("arcnet: ***\n");
	}
#endif

	BUGMSG(D_INIT,"given: base %lXh, IRQ %d, shmem %lXh\n",
			dev->base_addr,dev->irq,dev->mem_start);

	if (dev->base_addr > 0x1ff)	/* Check a single specified port */
	{
		ports[0]=dev->base_addr;
		numports=1;
	}
	else if (dev->base_addr > 0)	/* Don't probe at all. */
		return -ENXIO;
		
	if (dev->mem_start)
	{
		shmems[0]=dev->mem_start;
		numshmems=1;
	}
	

	/* Stage 1: abandon any reserved ports, or ones with status==0xFF
	 * (empty), and reset any others by reading the reset port.
	 */
	BUGMSG(D_INIT,"Stage 1: ");
	numprint=0;
	for (port = &ports[0]; port-ports<numports; port++)
	{
		numprint++;
		if (numprint>8)
		{
			BUGMSG2(D_INIT,"\n");
			BUGMSG(D_INIT,"Stage 1: ");
			numprint=1;
		}
		BUGMSG2(D_INIT,"%Xh ",*port);
		
		ioaddr=*port;
		
		if (check_region(*port, ARCNET_TOTAL_SIZE))
		{
			BUGMSG2(D_INIT_REASONS,"(check_region)\n");
			BUGMSG(D_INIT_REASONS,"Stage 1: ");
			BUGLVL(D_INIT_REASONS) numprint=0;
			*port=ports[numports-1];
			numports--;
			port--;
			continue;
		}
		
		if (inb(STATUS) == 0xFF)
		{
			BUGMSG2(D_INIT_REASONS,"(empty)\n");
			BUGMSG(D_INIT_REASONS,"Stage 1: ");
			BUGLVL(D_INIT_REASONS) numprint=0;
			*port=ports[numports-1];
			numports--;
			port--;
			continue;
		}
		
		inb(RESET);	/* begin resetting card */

		BUGMSG2(D_INIT_REASONS,"\n");
		BUGMSG(D_INIT_REASONS,"Stage 1: ");
		BUGLVL(D_INIT_REASONS) numprint=0;
	}
	BUGMSG2(D_INIT,"\n");
	
	if (!numports)
	{
		BUGMSG(D_NORMAL,"Stage 1: No ARCnet cards found.\n");
		return -ENODEV;
	}
	

	/* Stage 2: we have now reset any possible ARCnet cards, so we can't
	 * do anything until they finish.  If D_INIT, print the list of
	 * cards that are left.
	 */
	BUGMSG(D_INIT,"Stage 2: ");
	numprint=0;
	for (port = &ports[0]; port-ports<numports; port++)
	{
		numprint++;
		if (numprint>8)
		{
			BUGMSG2(D_INIT,"\n");
			BUGMSG(D_INIT,"Stage 2: ");
			numprint=1;
		}
		BUGMSG2(D_INIT,"%Xh ",*port);
	}
	BUGMSG2(D_INIT,"\n");
	JIFFER(RESETtime);
	

	/* Stage 3: abandon any shmem addresses that don't have the signature
	 * 0xD1 byte in the right place, or are read-only.
	 */
	BUGMSG(D_INIT,"Stage 3: ");
	numprint=0;
	for (shmem = &shmems[0]; shmem-shmems<numshmems; shmem++)
	{
		volatile u_char *cptr;
	
		numprint++;
		if (numprint>8)
		{
			BUGMSG2(D_INIT,"\n");
			BUGMSG(D_INIT,"Stage 3: ");
			numprint=1;
		}
		BUGMSG2(D_INIT,"%lXh ",*shmem);
		
		cptr=(u_char *)(*shmem);
		
		if (*cptr != TESTvalue)
		{
			BUGMSG2(D_INIT_REASONS,"(mem=%02Xh, not %02Xh)\n",
				*cptr,TESTvalue);
			BUGMSG(D_INIT_REASONS,"Stage 3: ");
			BUGLVL(D_INIT_REASONS) numprint=0;
			*shmem=shmems[numshmems-1];
			numshmems--;
			shmem--;
			continue;
		}
		
		/* By writing 0x42 to the TESTvalue location, we also make
		 * sure no "mirror" shmem areas show up - if they occur
		 * in another pass through this loop, they will be discarded
		 * because *cptr != TESTvalue.
		 */
		*cptr=0x42;
		if (*cptr != 0x42)
		{
			BUGMSG2(D_INIT_REASONS,"(read only)\n");
			BUGMSG(D_INIT_REASONS,"Stage 3: ");
			*shmem=shmems[numshmems-1];
			numshmems--;
			shmem--;
			continue;
		}
		
		BUGMSG2(D_INIT_REASONS,"\n");
		BUGMSG(D_INIT_REASONS,"Stage 3: ");
		BUGLVL(D_INIT_REASONS) numprint=0;
	}
	BUGMSG2(D_INIT,"\n");

	if (!numshmems)
	{
		BUGMSG(D_NORMAL,"Stage 3: No ARCnet cards found.\n");
		return -ENODEV;
	}

	/* Stage 4: something of a dummy, to report the shmems that are
	 * still possible after stage 3.
	 */
	BUGMSG(D_INIT,"Stage 4: ");
	numprint=0;
	for (shmem = &shmems[0]; shmem-shmems<numshmems; shmem++)
	{
		numprint++;
		if (numprint>8)
		{
			BUGMSG2(D_INIT,"\n");
			BUGMSG(D_INIT,"Stage 4: ");
			numprint=1;
		}
		BUGMSG2(D_INIT,"%lXh ",*shmem);
	}
	BUGMSG2(D_INIT,"\n");
	

	/* Stage 5: for any ports that have the correct status, can disable
	 * the RESET flag, and (if no irq is given) generate an autoirq,
	 * register an ARCnet device.
	 *
	 * Currently, we can only register one device per probe, so quit
	 * after the first one is found.
	 */
	BUGMSG(D_INIT,"Stage 5: ");
	numprint=0;
	for (port = &ports[0]; port-ports<numports; port++)
	{
		numprint++;
		if (numprint>8)
		{
			BUGMSG2(D_INIT,"\n");
			BUGMSG(D_INIT,"Stage 5: ");
			numprint=1;
		}
		BUGMSG2(D_INIT,"%Xh ",*port);
		
		ioaddr=*port;
		status=inb(STATUS);
		
		if ((status & 0x9F)
			!= (NORXflag|RECONflag|TXFREEflag|RESETflag))
		{
			BUGMSG2(D_INIT_REASONS,"(status=%Xh)\n",status);
			BUGMSG(D_INIT_REASONS,"Stage 5: ");
			BUGLVL(D_INIT_REASONS) numprint=0;
			*port=ports[numports-1];
			numports--;
			port--;
			continue;
		}

		outb(CFLAGScmd|RESETclear|CONFIGclear,COMMAND);
		status=inb(STATUS);
		if (status & RESETflag)
		{
			BUGMSG2(D_INIT_REASONS," (eternal reset, status=%Xh)\n",
					status);
			BUGMSG(D_INIT_REASONS,"Stage 5: ");
			BUGLVL(D_INIT_REASONS) numprint=0;
			*port=ports[numports-1];
			numports--;
			port--;
			continue;
		}

		/* skip this completely if an IRQ was given, because maybe
		 * we're on a machine that locks during autoirq!
		 */
		if (!dev->irq)
		{
			/* if we do this, we're sure to get an IRQ since the
			 * card has just reset and the NORXflag is on until
			 * we tell it to start receiving.
			 */
			airqmask = probe_irq_on();
			outb(NORXflag,INTMASK);
			udelay(1);
			outb(0,INTMASK);
			airq = probe_irq_off(airqmask);
	
			if (airq<=0)
			{
				BUGMSG2(D_INIT_REASONS,"(airq=%d)\n",airq);
				BUGMSG(D_INIT_REASONS,"Stage 5: ");
				BUGLVL(D_INIT_REASONS) numprint=0;
				*port=ports[numports-1];
				numports--;
				port--;
				continue;
			}
		}
		else
		{
			airq=dev->irq;
		}
		
		BUGMSG2(D_INIT,"(%d,", airq);
		openparen=1;
		
		/* Everything seems okay.  But which shmem, if any, puts
		 * back its signature byte when the card is reset?
		 *
		 * If there are multiple cards installed, there might be
		 * multiple shmems still in the list.
		 */
#ifdef FAST_PROBE
		if (numports>1 || numshmems>1)
		{
			inb(RESET);
			JIFFER(RESETtime);
		}
		else
		{
			/* just one shmem and port, assume they match */
			*(u_char *)(shmems[0]) = TESTvalue;
		}
#else
		inb(RESET);
		JIFFER(RESETtime);
#endif


		for (shmem = &shmems[0]; shmem-shmems<numshmems; shmem++)
		{
			u_char *cptr;
			cptr=(u_char *)(*shmem);
			
			if (*cptr == TESTvalue)	/* found one */
			{
				BUGMSG2(D_INIT,"%lXh)\n", *shmem);
				openparen=0;

				/* register the card */
				retval=arcnet_found(dev,*port,airq,*shmem);
				if (retval) openparen=0;

				/* remove shmem from the list */
				*shmem=shmems[numshmems-1];
				numshmems--;
				
				break;
			}
			else
			{
				BUGMSG2(D_INIT_REASONS,"%Xh-", *cptr);
			}
		}

		if (openparen)
		{
			BUGMSG2(D_INIT,"no matching shmem)\n");
			BUGMSG(D_INIT_REASONS,"Stage 5: ");
			BUGLVL(D_INIT_REASONS) numprint=0;
		}

		*port=ports[numports-1];
		numports--;
		port--;

		if (!retval) break;
	}
	BUGMSG(D_INIT_REASONS,"\n");

	/* Now put back TESTvalue on all leftover shmems.
	 */
	for (shmem = &shmems[0]; shmem-shmems<numshmems; shmem++)
		*(u_char *)(*shmem) = TESTvalue;
		
	if (retval) BUGMSG(D_NORMAL,"Stage 5: No ARCnet cards found.\n");
	return retval;
}

/* Set up the struct device associated with this card.  Called after
 * probing succeeds.
 */
int arcnet_found(struct device *dev,int port,int airq, u_long shmem)
{
	u_char *first_mirror,*last_mirror;
	struct arcnet_local *lp;
	
	/* reserve the I/O region */
	request_region(port,ARCNET_TOTAL_SIZE,"arcnet");
	dev->base_addr=port;

	/* reserve the irq */
	if (request_irq(airq,&arcnet_interrupt,0,"arcnet",NULL))
	{
		BUGMSG(D_NORMAL,"Can't get IRQ %d!\n",airq);
		release_region(port,ARCNET_TOTAL_SIZE);
		return -ENODEV;
	}
	irq2dev_map[airq]=dev;
	dev->irq=airq;
	
	/* find the real shared memory start/end points, including mirrors */
	
	#define BUFFER_SIZE (512)
	#define MIRROR_SIZE (BUFFER_SIZE*4)
	
	first_mirror=last_mirror=(u_char *)shmem;
	while (*first_mirror==TESTvalue) first_mirror-=MIRROR_SIZE;
	first_mirror+=MIRROR_SIZE;

	while (*last_mirror==TESTvalue) last_mirror+=MIRROR_SIZE;
	last_mirror-=MIRROR_SIZE;

	dev->mem_start=(u_long)first_mirror;
	dev->mem_end=(u_long)last_mirror+MIRROR_SIZE-1;
	dev->rmem_start=dev->mem_start+BUFFER_SIZE*0;
	dev->rmem_end=dev->mem_start+BUFFER_SIZE*2-1;
	 
	/* Initialize the rest of the device structure. */
	
	dev->priv = kmalloc(sizeof(struct arcnet_local), GFP_KERNEL);
	if (dev->priv == NULL)
	{
		irq2dev_map[airq] = NULL;
		free_irq(airq,NULL);
		release_region(port,ARCNET_TOTAL_SIZE);
		return -ENOMEM;
	}
	memset(dev->priv,0,sizeof(struct arcnet_local));
	lp=(struct arcnet_local *)(dev->priv);
	
	dev->open=arcnet_open;
	dev->stop=arcnet_close;
	dev->hard_start_xmit=arcnetA_send_packet;
	dev->get_stats=arcnet_get_stats;
	/*dev->set_multicast_list = &set_multicast_list;*/

	/* Fill in the fields of the device structure with generic
	 * values.
	 */
	arcnet_setup(dev);
	
	/* And now fill particular fields with arcnet values */
	dev->mtu=1500; /* completely arbitrary - agrees with ether, though */
	dev->hard_header_len=sizeof(struct ClientData);
	dev->hard_header=arcnetA_header;
	dev->rebuild_header=arcnetA_rebuild_header;
	lp->sequence=1;
	lp->recbuf=0;

	BUGMSG(D_DURING,"ClientData header size is %d.\n",
		sizeof(struct ClientData));
	BUGMSG(D_DURING,"HardHeader size is %d.\n",
		sizeof(struct HardHeader));

	/* get and check the station ID from offset 1 in shmem */
	lp->stationid = first_mirror[1];
	if (lp->stationid==0)
		BUGMSG(D_NORMAL,"WARNING!  Station address 00 is reserved "
			"for broadcasts!\n");
	else if (lp->stationid==255)
		BUGMSG(D_NORMAL,"WARNING!  Station address FF may confuse "
			"DOS networking programs!\n");
	dev->dev_addr[0]=lp->stationid;

	BUGMSG(D_NORMAL,"ARCnet station %02Xh found at %03lXh, IRQ %d, "
		"ShMem %lXh (%ld bytes).\n",
		lp->stationid,
		dev->base_addr,dev->irq,dev->mem_start,
		dev->mem_end-dev->mem_start+1);
		
	return 0;
}


/* Do a hardware reset on the card, and set up necessary registers.
 *
 * This should be called as little as possible, because it disrupts the
 * token on the network (causes a RECON) and requires a significant delay.
 *
 * However, it does make sure the card is in a defined state.
 */
int arcnet_reset(struct device *dev,int reset_delay)
{
	struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
	short ioaddr=dev->base_addr;
	int delayval,recbuf=lp->recbuf;
	u_char *cardmem;
	
	/* no IRQ's, please! */
	lp->intmask=0;
	SETMASK;
	
	BUGMSG(D_INIT,"Resetting %s (status=%Xh)\n",
			dev->name,inb(STATUS));

	if (reset_delay)
	{
		/* reset the card */
		inb(RESET);
		JIFFER(RESETtime);
	}

	outb(CFLAGScmd|RESETclear, COMMAND); /* clear flags & end reset */
	outb(CFLAGScmd|CONFIGclear,COMMAND);

	/* verify that the ARCnet signature byte is present */
	cardmem = (u_char *) dev->mem_start;
	if (cardmem[0] != TESTvalue)
	{
		BUGMSG(D_NORMAL,"reset failed: TESTvalue not present.\n");
		return 1;
	}
	
	/* clear out status variables */
	recbuf=lp->recbuf=0;
	lp->txbuf=2;

	/* enable extended (512-byte) packets */
	outb(CONFIGcmd|EXTconf,COMMAND);
	
#ifndef SLOW_XMIT_COPY
	/* clean out all the memory to make debugging make more sense :) */
	BUGLVL(D_DURING)
		memset((void *)dev->mem_start,0x42,2048);
#endif
	
	/* and enable receive of our first packet to the first buffer */
	EnableReceiver();

	/* re-enable interrupts */
	lp->intmask|=NORXflag;
#ifdef DETECT_RECONFIGS
	lp->intmask|=RECONflag;
#endif
	SETMASK;
	
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

	dev->broadcast[0]	= 0x00;	/* for us, broadcasts are address 0 */
	dev->addr_len		= 1;
	dev->type		= ARPHRD_ARCNET;
	dev->tx_queue_len	= 30;	/* fairly long queue - arcnet is
					 * quite speedy.
					 */

	/* New-style flags. */
	dev->flags		= IFF_BROADCAST;
	dev->family		= AF_INET;
	dev->pa_addr		= 0;
	dev->pa_brdaddr 	= 0;
	dev->pa_mask		= 0;
	dev->pa_alen		= 4;
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
	int ioaddr=dev->base_addr;
	
	if (dev->metric>=1000)
	{
		arcnet_debug=dev->metric-1000;
		printk(KERN_INFO "%6s: debug level set to %d\n",dev->name,arcnet_debug);
		dev->metric=1;
	}

	BUGMSG(D_INIT,"arcnet_open: resetting card.\n");
	
#ifdef FAST_IFCONFIG
	/* try to put the card in a defined state - if it fails the first
	 * time, actually reset it.
	 */
	if (arcnet_reset(dev,0) && arcnet_reset(dev,1))
		return -ENODEV;
#else
	/* reset the card twice in case something goes wrong the first time.
	 */
	if (arcnet_reset(dev,1) && arcnet_reset(dev,1))
		return -ENODEV;
#endif
	
	dev->tbusy=0;
	dev->interrupt=0;
	lp->intx=0;
	lp->in_txhandler=0;
	
	/* The RFC1201 driver is the default - just store */
	lp->adev=dev;
	BUGMSG(D_EXTRA,"ARCnet RFC1201 protocol initialized.\n");

#ifdef CONFIG_ARCNET_ETH	
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
#else
	BUGMSG(D_EXTRA,"Ethernet-Encap protocol not available (disabled).\n");
#endif

#ifdef CONFIG_ARCNET_1051
	/* Initialize the RFC1051-encap protocol driver */
	lp->sdev=(struct device *)kmalloc(sizeof(struct device),GFP_KERNEL);
	memcpy(lp->sdev,dev,sizeof(struct device));
	lp->sdev->name=(char *)kmalloc(10,GFP_KERNEL);
	sprintf(lp->sdev->name,"%ss",dev->name);
	lp->sdev->init=arcnetS_init;
	register_netdev(lp->sdev);
#else
	BUGMSG(D_EXTRA,"RFC1051 protocol not available (disabled).\n");
#endif

	/* we're started */
	START=1;
	
	/* make sure we're ready to receive IRQ's.
	 * arcnet_reset sets this for us, but if we receive one before
	 * START is set to 1, it could be ignored.  So, we turn IRQ's
	 * off, then on again to clean out the IRQ controller.
	 */
	outb(0,INTMASK);
	udelay(1);	/* give it time to set the mask before
			 * we reset it again. (may not even be
			 * necessary)
			 */
	SETMASK;
	
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

	/* Shut down the card */
#ifdef FAST_IFCONFIG
	inb(RESET);	/* reset IRQ won't run if START=0 */
#else
	lp->intmask=0;
	SETMASK;	/* no IRQ's (except RESET, of course) */
	outb(NOTXcmd,COMMAND);  /* stop transmit */
	outb(NORXcmd,COMMAND);	/* disable receive */
#endif

	/* reset more flags */
	INTERRUPT=0;
	
	/* do NOT free lp->adev!!  It's static! */
	lp->adev=NULL;
	
#ifdef CONFIG_ARCNET_ETH
	/* free the ethernet-encap protocol device */
	lp->edev->priv=NULL;
	dev_close(lp->edev);
	unregister_netdev(lp->edev);
	kfree(lp->edev->name);
	kfree(lp->edev);
	lp->edev=NULL;
#endif

#ifdef CONFIG_ARCNET_1051
	/* free the RFC1051-encap protocol device */
	lp->sdev->priv=NULL;
	dev_close(lp->sdev);
	unregister_netdev(lp->sdev);
	kfree(lp->sdev->name);
	kfree(lp->sdev);
	lp->sdev=NULL;
#endif

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

		lp->intmask &= ~TXFREEflag;
		SETMASK;
		
		if (status&TXFREEflag)	/* transmit _DID_ finish */
		{
			BUGMSG(D_NORMAL,"tx timeout - missed IRQ? (status=%Xh, ticks=%d, mask=%Xh, dest=%02Xh)\n",
					status,tickssofar,lp->intmask,lp->lasttrans_dest);
			lp->stats.tx_errors++;
		}
		else
		{
			BUGMSG(D_EXTRA,"tx timed out (status=%Xh, tickssofar=%d, intmask=%Xh, dest=%02Xh)\n",
					status,tickssofar,lp->intmask,lp->lasttrans_dest);
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

		return 1;
	}

	/* If some higher layer thinks we've missed a tx-done interrupt
	   we are passed NULL. Caution: dev_tint() handles the cli()/sti()
	   itself. */
	if (skb == NULL) {
		BUGMSG(D_NORMAL,"tx passed null skb (status=%Xh, inTX=%d, tickssofar=%ld)\n",
			inb(STATUS),lp->intx,jiffies-dev->trans_start);
		lp->stats.tx_errors++;
		dev_tint(dev);
		return 0;
	}
	
	if (lp->txready)	/* transmit already in progress! */
	{
		BUGMSG(D_NORMAL,"trying to start new packet while busy! (status=%Xh)\n",
			inb(STATUS));
		lp->intmask &= ~TXFREEflag;
		SETMASK;
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
            BUGMSG(D_NORMAL,"transmitter called with busy bit set! (status=%Xh, inTX=%d, tickssofar=%ld)\n",
            		inb(STATUS),lp->intx,jiffies-dev->trans_start);
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
		
	BUGLVL(D_SKB) arcnet_dump_skb(dev,skb,"tx");

	out->hdr->sequence=(lp->sequence++);
	
	/* fits in one packet? */
	if (out->length-EXTRA_CLIENTDATA<=XMTU)
	{
		BUGMSG(D_DURING,"not splitting %d-byte packet. (split_flag=%d)\n",
				out->length,out->hdr->split_flag);
		if (out->hdr->split_flag)
			BUGMSG(D_NORMAL,"short packet has split_flag set?! (split_flag=%d)\n",
				out->hdr->split_flag);
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
	
	/* make sure we didn't ignore a TX IRQ while we were in here */
	lp->intmask |= TXFREEflag;
	SETMASK;

	return 0;
}


/* After an RFC1201 split packet has been set up, this function calls
 * arcnetAS_prepare_tx to load the next segment into the card.  This function
 * does NOT automatically call arcnet_go_tx.
 */
static void arcnetA_continue_tx(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr,maxsegsize=XMTU-4;
	struct Outgoing *out=&(lp->outgoing);
	
	BUGMSG(D_DURING,"continue_tx called (status=%Xh, intx=%d, intxh=%d, intmask=%Xh\n",
		inb(STATUS),lp->intx,lp->in_txhandler,lp->intmask);
	
	if (lp->txready)
	{
		BUGMSG(D_NORMAL,"continue_tx: called with packet in buffer!\n");
		return;
	}
	
	if (out->segnum>=out->numsegs)
	{
		BUGMSG(D_NORMAL,"continue_tx: building segment %d of %d!\n",
			out->segnum+1,out->numsegs);
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
	
#ifdef SLOW_XMIT_COPY
	char *iptr,*iend,*optr;
#endif
	
	lp->txbuf=lp->txbuf^1;	/* XOR with 1 to alternate between 2 and 3 */
	
	length+=hdrlen;

	BUGMSG(D_TX,"arcnetAS_prep_tx: hdr:%ph, length:%d, data:%ph\n",
			hdr,length,data);

#ifndef SLOW_XMIT_COPY
	/* clean out the page to make debugging make more sense :) */
	BUGLVL(D_DURING)
		memset((void *)dev->mem_start+lp->txbuf*512,0x42,512);
#endif

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
#ifdef SLOW_XMIT_COPY
	for (iptr=data,iend=iptr+length-hdrlen,optr=(char *)arcsoft+hdrlen;
		iptr<iend; iptr++,optr++)
	{
		*optr=*iptr;
		/*udelay(5);*/
	}
#else
	memcpy((u_char*)arcsoft+hdrlen,
		data,length-hdrlen);
#endif
		
	BUGMSG(D_DURING,"transmitting packet to station %02Xh (%d bytes)\n",
			daddr,length);
			
	BUGLVL(D_TX) arcnet_dump_packet(dev,arcpacket->raw,length>MTU,"tx");
	lp->lastload_dest=daddr;
	lp->txready=lp->txbuf;	/* packet is ready for sending */
}

/* Actually start transmitting a packet that was placed in the card's
 * buffer by arcnetAS_prepare_tx.  Returns 1 if a Tx is really started.
 *
 * This should probably always be called with the INTMASK register set to 0,
 * so go_tx is not called recursively.
 *
 * The enable_irq flag determines whether to actually write INTMASK value
 * to the card; TXFREEflag is always OR'ed into the memory variable either
 * way.
 */
static int
arcnet_go_tx(struct device *dev,int enable_irq)
{
	struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr;

	BUGMSG(D_DURING,"go_tx: status=%Xh, intmask=%Xh, txready=%d, sending=%d\n",
		inb(STATUS),lp->intmask,lp->txready,lp->sending);

	if (lp->sending || !lp->txready)
	{
		if (enable_irq && lp->sending)
		{
			lp->intmask |= TXFREEflag;
			SETMASK;
		}
		return 0;
	}
	
	/* start sending */
	outb(TXcmd|(lp->txready<<3),COMMAND);

	lp->stats.tx_packets++;
	lp->txready=0;
	lp->sending++;

	lp->lasttrans_dest=lp->lastload_dest;
	lp->lastload_dest=0;

	lp->intmask |= TXFREEflag;

	if (enable_irq)	SETMASK;

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
arcnet_interrupt(int irq,void *dev_id,struct pt_regs *regs)
{
	struct device *dev = (struct device *)(irq2dev_map[irq]);
	int ioaddr;
	
	if (dev==NULL)
	{
		BUGLVL(D_DURING)
			printk(KERN_DEBUG "arcnet: irq %d for unknown device.\n", irq);
		return;
	}
	
	BUGMSG(D_DURING,"in arcnet_interrupt\n");

	/* RESET flag was enabled - if !dev->start, we must clear it right
	 * away (but nothing else) since inthandler() is never called.
	 */
	ioaddr=dev->base_addr;
	if (!dev->start)
	{
		if (inb(STATUS) & RESETflag)
			outb(CFLAGScmd|RESETclear, COMMAND);
		return;
	}

	/* Call the "real" interrupt handler. */
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
		BUGMSG(D_NORMAL,"DRIVER PROBLEM!  Nested arcnet interrupts!\n");
		return;	/* don't even try. */
	}
	
	outb(0,INTMASK);
	INTERRUPT = 1;

	BUGMSG(D_DURING,"in arcnet_inthandler (status=%Xh, intmask=%Xh)\n",
		inb(STATUS),lp->intmask);

	do
	{
		status = inb(STATUS);
		didsomething=0;
	

		/* RESET flag was enabled - card is resetting and if RX
		 * is disabled, it's NOT because we just got a packet.
		 */
		if (status & RESETflag)
		{
			BUGMSG(D_NORMAL,"spurious reset (status=%Xh)\n",
					status);
			arcnet_reset(dev,0);
			
			/* all other flag values are just garbage */
			break;
		}
		
		
		/* RX is inhibited - we must have received something. */
		if (status & lp->intmask & NORXflag)
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
		/*if (status&TXFREEflag && !lp->in_txhandler && lp->sending)*/
		if (status & lp->intmask & TXFREEflag)
		{
			struct Outgoing *out=&(lp->outgoing);
			int was_sending=lp->sending;
			
			lp->intmask &= ~TXFREEflag;

			lp->in_txhandler++;
			if (was_sending) lp->sending--;

			BUGMSG(D_DURING,"TX IRQ (stat=%Xh, numsegs=%d, segnum=%d, skb=%ph)\n",
					status,out->numsegs,out->segnum,out->skb);
					
			if (was_sending && !(status&TXACKflag))
			{
				if (lp->lasttrans_dest != 0)
				{
					BUGMSG(D_EXTRA,"transmit was not acknowledged! (status=%Xh, dest=%02Xh)\n",
						status,lp->lasttrans_dest);
					lp->stats.tx_errors++;
					lp->stats.tx_carrier_errors++;
				}
				else
				{
					BUGMSG(D_DURING,"broadcast was not acknowledged; that's normal (status=%Xh, dest=%02Xh)\n",
							status,
							lp->lasttrans_dest);
				}
			}

			/* send packet if there is one */
			arcnet_go_tx(dev,0);
			didsomething++;
			
			if (lp->intx)
			{
				BUGMSG(D_DURING,"TXDONE while intx! (status=%Xh, intx=%d)\n",
					inb(STATUS),lp->intx);
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

#ifdef DETECT_RECONFIGS
		if (status & (lp->intmask) & RECONflag)
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
					BUGMSG(D_NORMAL,"reconfiguration detected: cabling restored?\n");
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
					BUGMSG(D_NORMAL,"many reconfigurations detected: cabling problem?\n");
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
				BUGMSG(D_NORMAL,"cabling restored?\n");
			lp->first_recon=lp->last_recon=0;
			lp->num_recons=lp->network_down=0;
			
			BUGMSG(D_DURING,"not recon: clearing counters anyway.\n");
		}
		#endif
#endif /* DETECT_RECONFIGS */
	} while (--boguscount && didsomething);

	BUGMSG(D_DURING,"net_interrupt complete (status=%Xh, count=%d)\n",
			inb(STATUS),boguscount);
	BUGMSG(D_DURING,"\n");

	SETMASK;	/* put back interrupt mask */

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
		BUGMSG(D_NORMAL,"discarding old packet. (status=%Xh)\n",
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
	
       	BUGMSG(D_DURING,"received packet from %02Xh to %02Xh (%d bytes)\n",
       			saddr,daddr,length);

	/* call the right receiver for the protocol */
	switch (arcsoft[0])
	{
	case ARC_P_IP:
	case ARC_P_ARP:
	case ARC_P_RARP:
	case ARC_P_IPX:
	case ARC_P_NOVELL_EC:
		arcnetA_rx(lp->adev,arcsoft,length,saddr,daddr);
		break;
#ifdef CONFIG_ARCNET_ETH
	case ARC_P_ETHER:
		arcnetE_rx(lp->edev,arcsoft,length,saddr,daddr);
		break;
#endif
#ifdef CONFIG_ARCNET_1051
	case ARC_P_IP_RFC1051:
	case ARC_P_ARP_RFC1051:
		arcnetS_rx(lp->sdev,arcsoft,length,saddr,daddr);
		break;
#endif
	case ARC_P_LANSOFT: /* don't understand.  fall through. */
	default:
		BUGMSG(D_NORMAL,"received unknown protocol %d (%Xh) from station %d.\n",
			arcsoft[0],arcsoft[0],saddr);
		lp->stats.rx_errors++;
		lp->stats.rx_crc_errors++;
		break;
	}

        BUGLVL(D_RX) arcnet_dump_packet(dev,arcpacket->raw,length>240,"rx");


#ifndef SLOW_XMIT_COPY
	/* clean out the page to make debugging make more sense :) */
	BUGLVL(D_DURING)
		memset((void *)arcpacket->raw,0x42,512);
#endif


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
        		BUGMSG(D_NORMAL,"aborting assembly (seq=%d) for unsplit packet (splitflag=%d, seq=%d)\n",
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
         		BUGMSG(D_NORMAL,"Memory squeeze, dropping packet.\n");
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
         			BUGMSG(D_NORMAL,"funny-shaped ARP packet. (%Xh, %Xh)\n",
         				arp->ar_hln,arp->ar_pln);
         			lp->stats.rx_errors++;
         			lp->stats.rx_crc_errors++;
         		}
         	}
         	
		BUGLVL(D_SKB) arcnet_dump_skb(dev,skb,"rx");

		skb->protocol=arcnetA_type_trans(skb,dev);

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
			BUGMSG(D_NORMAL,"wrong seq number (saddr=%d, expected=%d, seq=%d, splitflag=%d)\n",
				saddr,in->sequence,arcsoft->sequence,
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
	        		BUGMSG(D_NORMAL,"aborting previous (seq=%d) assembly (splitflag=%d, seq=%d)\n",
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
	        		BUGMSG(D_NORMAL,"incoming packet more than 16 segments; dropping. (splitflag=%d)\n",
	        			arcsoft->split_flag);
	        		lp->stats.rx_errors++;
	        		lp->stats.rx_length_errors++;
	        		return;
	        	}
	        
                	in->skb=skb=alloc_skb(508*in->numpackets
                			+ sizeof(struct ClientData),
	                		GFP_ATOMIC);
                	if (skb == NULL) {
                		BUGMSG(D_NORMAL,"(split) memory squeeze, dropping packet.\n");
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
				BUGMSG(D_NORMAL,"can't continue split without starting first! (splitflag=%d, seq=%d)\n",
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
					BUGMSG(D_NORMAL,"duplicate splitpacket ignored! (splitflag=%d)\n",
						arcsoft->split_flag);
					lp->stats.rx_errors++;
					lp->stats.rx_frame_errors++;
					return;
				}
				
				/* "bad" duplicate, kill reassembly */
				BUGMSG(D_NORMAL,"out-of-order splitpacket, reassembly (seq=%d) aborted (splitflag=%d, seq=%d)\n",	
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
         			BUGMSG(D_NORMAL,"?!? done reassembling packet, no skb? (skb=%ph, in->skb=%ph)\n",
         				skb,in->skb);
         		}
         		else
         		{
	         		in->skb=NULL;
        	 		in->lastpacket=in->numpackets=0;
			
				BUGLVL(D_SKB) arcnet_dump_skb(dev,skb,"rx");
			
				skb->protocol=arcnetA_type_trans(skb,dev);
				
	         		netif_rx(skb);
	         	}
        	}
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

#if 0
/* Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, clear multicast list
 * num_addrs > 0	Multicast mode, receive normal and MC packets, and do
 *			best-effort filtering.
 */
static void
set_multicast_list(struct device *dev)
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
#endif

/* Create the ARCnet ClientData header for an arbitrary protocol layer
 *
 * saddr=NULL	means use device source address (always will anyway)
 * daddr=NULL	means leave destination address (eg unresolved arp)
 */
int arcnetA_header(struct sk_buff *skb,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len)
{
	struct ClientData *head = (struct ClientData *)
		skb_push(skb,dev->hard_header_len);
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
		BUGMSG(D_NORMAL,"I don't understand protocol %d (%Xh)\n",
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
	struct arcnet_local *lp=(struct arcnet_local *)(dev->priv);
	int status;

	/*
	 * Only ARP and IP are currently supported
	 */
	 
	if(head->protocol_id != ARC_P_IP) 
	{
		BUGMSG(D_NORMAL,"I don't understand protocol type %d (%Xh) addresses!\n",
			head->protocol_id,head->protocol_id);
		lp->stats.tx_errors++;
		lp->stats.tx_aborted_errors++;
		head->daddr=0;
		/*memcpy(eth->h_source, dev->dev_addr, dev->addr_len);*/
		return 0;
	}

	/*
	 * Try to get ARP to resolve the header.
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


/* Determine a packet's protocol ID.
 *
 * With ARCnet we have to convert everything to Ethernet-style stuff.
 */
unsigned short arcnetA_type_trans(struct sk_buff *skb,struct device *dev)
{
	struct ClientData *head;
	struct arcnet_local *lp=(struct arcnet_local *) (dev->priv);

	/* Pull off the arcnet header. */
	skb->mac.raw=skb->data;
	skb_pull(skb,dev->hard_header_len);
	head=(struct ClientData *)skb->mac.raw;
	
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

	case ARC_P_IPX:
	case ARC_P_NOVELL_EC:
		return htons(ETH_P_802_3);
	default:
		BUGMSG(D_NORMAL,"received packet of unknown protocol id %d (%Xh)\n",
				head->protocol_id,head->protocol_id);
		lp->stats.rx_errors++;
		lp->stats.rx_crc_errors++;
		return 0;
	}

	return htons(ETH_P_IP);
}


#ifdef CONFIG_ARCNET_ETH
/****************************************************************************
 *                                                                          *
 * Ethernet-Encap Support                                                   *
 *                                                                          *
 ****************************************************************************/

/* Initialize the arc0e device.
 */
static int arcnetE_init(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

	ether_setup(dev); /* we're emulating ether here, not ARCnet */
	dev->dev_addr[0]=0;
	dev->dev_addr[5]=lp->stationid;
	dev->mtu=512-sizeof(struct HardHeader)-dev->hard_header_len-1;
	dev->open=arcnetE_open_close;
	dev->stop=arcnetE_open_close;
	dev->hard_start_xmit=arcnetE_send_packet;

	BUGMSG(D_EXTRA,"ARCnet Ethernet-Encap protocol initialized.\n");
			
	return 0;
}


/* Bring up/down the arc0e device - we don't actually have to do anything,
 * since our parent arc0 handles the card I/O itself.
 */
static int arcnetE_open_close(struct device *dev)
{
	return 0;
}


/* Called by the kernel in order to transmit an ethernet-type packet.
 */
static int
arcnetE_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
	int ioaddr=dev->base_addr,bad;
	union ArcPacket *arcpacket = 
		(union ArcPacket *)(dev->mem_start+512*(lp->txbuf^1));
	u_char *arcsoft,daddr;
	short offset,length=skb->len+1;

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
		BUGMSG(D_NORMAL,"MTU must be <= 493 for ethernet encap (length=%d).\n",
			length);
		BUGMSG(D_NORMAL,"transmit aborted.\n");

		dev_kfree_skb(skb,FREE_WRITE);
		lp->intx--;
		return 0;
	}
		
	BUGMSG(D_DURING,"starting tx sequence...\n");

	lp->txbuf=lp->txbuf^1; /* XOR with 1 to alternate btw 2 & 3 */

#ifndef SLOW_XMIT_COPY
	/* clean out the page to make debugging make more sense :) */
	BUGLVL(D_DURING)
		memset((void *)dev->mem_start+lp->txbuf*512,0x42,512);
#endif

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
				
	BUGLVL(D_TX) arcnet_dump_packet(dev,arcpacket->raw,length>=240,"tx");

	lp->lastload_dest=daddr;
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
	
	/* make sure we didn't ignore a TX IRQ while we were in here */
	lp->intmask |= TXFREEflag;
	SETMASK;

	return 0;
}


/* Packet receiver for ethernet-encap packets.
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
       		BUGMSG(D_NORMAL,"Memory squeeze, dropping packet.\n");
       		lp->stats.rx_dropped++;
       		return;
       	}
       	
       	skb->len = length;
       	skb->dev = dev;
       	
       	memcpy(skb->data,(u_char *)arcsoft+1,length-1);

        BUGLVL(D_SKB) arcnet_dump_skb(dev,skb,"rx");

	skb->protocol=eth_type_trans(skb,dev);
        
        netif_rx(skb);
}

#endif /* CONFIG_ARCNET_ETH */

#ifdef CONFIG_ARCNET_1051
/****************************************************************************
 *                                                                          *
 * RFC1051 Support                                                          *
 *                                                                          *
 ****************************************************************************/

/* Initialize the arc0s device.
 */
static int arcnetS_init(struct device *dev)
{
	struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

	arcnet_setup(dev);
       
	/* And now fill particular fields with arcnet values */
	dev->dev_addr[0]=lp->stationid;
	dev->hard_header_len=sizeof(struct S_ClientData);
	dev->mtu=512-sizeof(struct HardHeader)-dev->hard_header_len
		+ S_EXTRA_CLIENTDATA;
	dev->open=arcnetS_open_close;
	dev->stop=arcnetS_open_close;
	dev->hard_start_xmit=arcnetS_send_packet;
	dev->hard_header=arcnetS_header;
	dev->rebuild_header=arcnetS_rebuild_header;
	BUGMSG(D_EXTRA,"ARCnet RFC1051 (NetBSD, AmiTCP) protocol initialized.\n");

	return 0;
}


/* Bring up/down the arc0s device - we don't actually have to do anything,
 * since our parent arc0 handles the card I/O itself.
 */
static int arcnetS_open_close(struct device *dev)
{
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
	
	bad=arcnet_send_packet_bad(skb,dev);
	if (bad)
	{
		lp->intx--;
		return bad;
	}
	
	TBUSY=1;

	length = 1 < skb->len ? skb->len : 1;

	BUGLVL(D_SKB) arcnet_dump_skb(dev,skb,"tx");

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
		BUGMSG(D_NORMAL,"packet too long (length=%d)\n",
			length);
		dev_kfree_skb(skb,FREE_WRITE);
		lp->stats.tx_dropped++;
		TBUSY=0;
		mark_bh(NET_BH);	
	}

	dev->trans_start=jiffies;
	lp->intx--;

	/* make sure we didn't ignore a TX IRQ while we were in here */
	lp->intmask |= TXFREEflag;
	SETMASK;

	return 0;
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
         		BUGMSG(D_NORMAL,"Memory squeeze, dropping packet.\n");
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
         	
		BUGLVL(D_SKB) arcnet_dump_skb(dev,skb,"rx");

		skb->protocol=arcnetS_type_trans(skb,dev);

         	netif_rx(skb);
         }
}


/* Create the ARCnet ClientData header for an arbitrary protocol layer
 *
 * saddr=NULL	means use device source address (always will anyway)
 * daddr=NULL	means leave destination address (eg unresolved arp)
 */
int arcnetS_header(struct sk_buff *skb,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len)
{
	struct S_ClientData *head = (struct S_ClientData *)
		skb_push(skb,dev->hard_header_len);
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
		BUGMSG(D_NORMAL,"I don't understand protocol %d (%Xh)\n",
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
		BUGMSG(D_NORMAL,"I don't understand protocol type %d (%Xh) addresses!\n",
			head->protocol_id,head->protocol_id);
		lp->stats.tx_errors++;
		lp->stats.tx_aborted_errors++;
		head->daddr=0;
		/*memcpy(eth->h_source, dev->dev_addr, dev->addr_len);*/
		return 0;
	}

	/*
	 * Try to get ARP to resolve the header.
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
unsigned short arcnetS_type_trans(struct sk_buff *skb,struct device *dev)
{
	struct S_ClientData *head;
	struct arcnet_local *lp=(struct arcnet_local *) (dev->priv);

	/* Pull off the arcnet header. */
	skb->mac.raw=skb->data;
	skb_pull(skb,dev->hard_header_len);
	head=(struct S_ClientData *)skb->mac.raw;
	
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
	case ARC_P_ATALK:   return htons(ETH_P_ATALK); /* untested appletalk */
	default:
		BUGMSG(D_NORMAL,"received packet of unknown protocol id %d (%Xh)\n",
			head->protocol_id,head->protocol_id);
		lp->stats.rx_errors++;
		lp->stats.rx_crc_errors++;
		return 0;
	}

	return htons(ETH_P_IP);
}

#endif	/* CONFIG_ARCNET_1051 */

/****************************************************************************
 *                                                                          *
 * Kernel Loadable Module Support                                           *
 *                                                                          *
 ****************************************************************************/


#ifdef MODULE

static char devicename[9] = "";
static struct device thiscard = {
  devicename, /* device name is inserted by linux/drivers/net/net_init.c */
  0, 0, 0, 0,
  0, 0,  /* I/O address, IRQ */
  0, 0, 0, NULL, arcnet_probe
};
	
	
static int io=0x0;	/* <--- EDIT THESE LINES FOR YOUR CONFIGURATION */
static int irqnum=0;	/* or use the insmod io= irqnum= shmem= options */
static int irq=0;
static int shmem=0;
static char *device = NULL;

int
init_module(void)
{
	if (device)
		strcpy(thiscard.name,device);
#ifndef CONFIG_ARCNET_ETHNAME
	else if (!thiscard.name[0]) strcpy(thiscard.name,"arc0");
#endif

	thiscard.base_addr=io;
	
	if (irq) irqnum=irq;

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
	int ioaddr=thiscard.base_addr;

	if (thiscard.start) arcnet_close(&thiscard);

	/* Flush TX and disable RX */
	if (ioaddr)
	{
		outb(0,INTMASK);	/* disable IRQ's */
		outb(NOTXcmd,COMMAND);  /* stop transmit */
		outb(NORXcmd,COMMAND);	/* disable receive */
	}

	if (thiscard.irq)
	{
		irq2dev_map[thiscard.irq] = NULL;
		free_irq(thiscard.irq,NULL);
	}
	
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

