/*	$Id: arcnet.c,v 1.34 1997/11/09 11:04:55 mj Exp $

	Written 1994-1996 by Avery Pennarun,
	derived from skeleton.c by Donald Becker.

	**********************

	The original copyright was as follows:

	skeleton.c Written 1993 by Donald Becker.
	Copyright 1993 United States Government as represented by the
        Director, National Security Agency.  This software may only be used
        and distributed according to the terms of the GNU Public License as
        modified by SRC, incorporated herein by reference.

	**********************

	v3.02 (98/06/07)
	  - Use register_netdevice() instead of register_netdev() to create
	    new devices for RFC1051 and Ethernet encapsulation in arcnet_open.
	    Likewise for unregistering them later. This avoids the deadlock 
	    encountered because the original routines call rtnl_lock() when
	    it's already locked. [dw]

	v3.01 (98/04/17)
	  - Interrupt handler now also checks dev->[se]dev are non-NULL
	    to avoid crashes in interrupts during card init. [dw]

	v3.00 (97/11/09)
	  - Minor cleanup of debugging messages. [mj]

	v2.93 ALPHA (97/11/06)
	  - irq2dev mapping removed.
	  - Interrupt handler now checks whether dev->priv is non-null in order
	    to avoid crashes in interrupts which come during card init. [mj]

	v2.92 ALPHA (97/09/02)
	  - Code cleanup [Martin Mares <mj@atrey.karlin.mff.cuni.cz>]
	  - Better probing for the COM90xx chipset, although only as
	    a temporary solution until we implement adding of all found
	    devices at once. [mj]

	v2.91 ALPHA (97/08/19)
	  - Add counting of octets in/out.

	v2.90 ALPHA (97/08/08)
	  - Add support for kernel command line parsing so that chipset
	    drivers are usable when compiled in.

	v2.80 ALPHA (97/08/01)
	  - Split source into multiple files; generic arcnet support and
	    individual chipset drivers. <Dave@imladris.demon.co.uk>

	v2.61 ALPHA (97/07/30)  by David Woodhouse (Dave@imladris.demon.co.uk)
	                            for Nortel (Northern Telecom).
          - Added support for IO-mapped modes and for SMC COM20020 chipset.
	  - Fixed (avoided) race condition in send_packet routines which was
            discovered when the buffer copy routines got slow (?).
          - Fixed support for device naming at load time.
	  - Added backplane, clock and timeout options for COM20020.
	  - Added support for promiscuous mode.

	v2.60 ALPHA (96/11/23)
	  - Added patch from Vojtech Pavlik <vojtech@atrey.karlin.mff.cuni.cz>
	    and Martin Mares <mj@k332.feld.cvut.cz> to make the driver work
	    with the new Linux 2.1.x memory management.  I modified their
	    patch quite a bit though; bugs are my fault.  More changes should
	    be made to get eliminate any remaining phys_to_virt calls.
	  - Quietly ignore protocol id's 0, 1, 8, and 243.  Thanks to Jake
	    Messinger <jake@ams.com> for reporting these codes and their
	    meanings.
	  - Smarter shmem probe for cards with 4k mirrors. (does it work?)
	  - Initial support for RIM I type cards which use no I/O ports at
	    all.  To use this option, you need to compile with RIM_I_MODE
	    enabled.  Thanks to Kolja Waschk <kawk@yo.com> for explaining
	    RIM I programming to me.  Now, does my RIM I code actually
	    work?

	v2.56 (96/10/18)
	  - Turned arc0e/arc0s startup messages back on by default, as most
	    people will probably not notice the additional devices
	    otherwise.  This causes undue confusion.
	  - Fixed a tiny but noticeable bug in the packet debugging routines
	    (thanks Tomasz)

	The following has been SUMMARIZED.  The complete ChangeLog is
	available in the full Linux-ARCnet package at
		http://www.worldvisions.ca/~apenwarr/arcnet

	v2.50 (96/02/24)
	  - Massively improved autoprobe routines; they now work even as a
	    module.  Thanks to Vojtech Pavlik <Vojtech.Pavlik@st.mff.cuni.cz>
	    for his ideas and help in this area.
	  - Changed printk's around quite a lot.

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

         - Use cleaner "architecture-independent" shared memory access.
           This is half-done in ARCnet 2.60, but still uses some
           undocumented i386 stuff.  (We shouldn't call phys_to_virt,
           for example.)
	 - Allow use of RFC1051 or Ether devices without RFC1201.
	 - Keep separate stats for each device.
         - Support "arpless" mode like NetBSD does, and as recommended
           by the (obsoleted) RFC1051.
         - Smarter recovery from RECON-during-transmit conditions. (ie.
           retransmit immediately)
         - Add support for the new 1.3.x IP header cache, and other features.
         - Replace setting of debug level with the "metric" flag hack by
	   something that still exists. SIOCDEVPRIVATE is a good candidate, 
	   but it would require an extra user-level utility.

         - What about cards with shared memory that can be "turned off?"
           (or that have none at all, like the SMC PC500longboard)
           Does this work now, with IO_MAPPED_BUFFERS?

         - Autoconfigure PDI5xxPlus cards. (I now have a PDI508Plus to play
           with temporarily.)  Update: yes, the Pure Data config program
           for DOS works fine, but the PDI508Plus I have doesn't! :)
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
	 - The official ARCnet COM20020 data sheets.
	 - Information on some more obscure ARCnet controller chips, thanks
	   to the nice people at SMC.
	 - net/inet/eth.c (from kernel 1.1.50) for header-building info.
	 - Alternate Linux ARCnet source by V.Shergin <vsher@sao.stavropol.su>
	 - Textual information and more alternate source from Joachim Koenig
	 	<jojo@repas.de>
*/

static const char *version =
 "arcnet.c: v3.02 98/06/07 Avery Pennarun <apenwarr@worldvisions.ca> et al.\n";

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
#include <linux/init.h>

#include <linux/if_arcnet.h>
#include <linux/arcdevice.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <net/arp.h>

/* Define this if you want to make it easier to use the "call trace" when
 * a kernel NULL pointer assignment occurs.  Hopefully unnecessary, most of
 * the time.  It will make all the function names (and other things) show
 * up as kernel symbols.  (especially handy when using arcnet as a module)
 */
#undef static

/**************************************************************************/

/* These are now provided by the chipset driver. There's a performance
 * overhead in using them.
 */

#define AINTMASK(x) ((*lp->asetmask)(dev, x))
#define ARCSTATUS ((*lp->astatus)(dev))
#define ACOMMAND(x) ((*lp->acommand)(dev, x))

int arcnet_debug=ARCNET_DEBUG;

/* Exported function prototypes */

#ifdef MODULE
int  init_module(void);
void cleanup_module(void);
#else
void arcnet_init(void);
static int init_module(void);
#ifdef CONFIG_ARCNET_COM90xx
extern char com90xx_explicit;
extern int arc90xx_probe(struct device *dev);
#endif
#endif

void arcnet_tx_done(struct device *dev, struct arcnet_local *lp);
void arcnet_use_count (int open);
void arcnet_setup(struct device *dev);
void arcnet_makename(char *device);
void arcnetA_continue_tx(struct device *dev);
int arcnet_go_tx(struct device *dev,int enable_irq);
void arcnet_interrupt(int irq,void *dev_id,struct pt_regs *regs);
void arcnet_rx(struct arcnet_local *lp, u_char *arcsoft, short length, int saddr, int daddr);

EXPORT_SYMBOL(arcnet_debug);
EXPORT_SYMBOL(arcnet_tx_done);
EXPORT_SYMBOL(arcnet_use_count);
EXPORT_SYMBOL(arcnet_setup);
EXPORT_SYMBOL(arcnet_makename);
EXPORT_SYMBOL(arcnetA_continue_tx);
EXPORT_SYMBOL(arcnet_go_tx);
EXPORT_SYMBOL(arcnet_interrupt);
EXPORT_SYMBOL(arcnet_rx);

#if ARCNET_DEBUG_MAX & D_SKB
void arcnet_dump_skb(struct device *dev,struct sk_buff *skb,
	char *desc);
EXPORT_SYMBOL(arcnet_dump_skb);
#else
#	define arcnet_dump_skb(dev,skb,desc) ;
#endif

#if (ARCNET_DEBUG_MAX & D_RX) || (ARCNET_DEBUG_MAX & D_TX)
void arcnet_dump_packet(struct device *dev,u_char *buffer,int ext,
	char *desc);
EXPORT_SYMBOL(arcnet_dump_packet);
#else
#	define arcnet_dump_packet(dev,buffer,ext,desc) ;
#endif

/* Internal function prototypes */

static int arcnet_open(struct device *dev);
static int arcnet_close(struct device *dev);
static int arcnetA_header(struct sk_buff *skb,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len);
static int arcnetA_rebuild_header(struct sk_buff *skb);
static int arcnet_send_packet_bad(struct sk_buff *skb,struct device *dev);
static int arcnetA_send_packet(struct sk_buff *skb, struct device *dev);
static void arcnetA_rx(struct device *dev,u_char *buf,
	int length,u_char saddr, u_char daddr);
static struct net_device_stats *arcnet_get_stats(struct device *dev);
static unsigned short arcnetA_type_trans(struct sk_buff *skb,
					 struct device *dev);


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
static int arcnetS_header(struct sk_buff *skb,struct device *dev,
		unsigned short type,void *daddr,void *saddr,unsigned len);
static int arcnetS_rebuild_header(struct sk_buff *skb);
static unsigned short arcnetS_type_trans(struct sk_buff *skb,struct device *dev);
#endif


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
		printk("%02X ",buffer[i]);
	}
	printk("\n");
	restore_flags(flags);
}
#endif


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
	dev_init_buffers(dev);

	dev->broadcast[0]	= 0x00;	/* for us, broadcasts are address 0 */
	dev->addr_len		= 1;
	dev->type		= ARPHRD_ARCNET;
	dev->tx_queue_len	= 30;

	/* New-style flags. */
	dev->flags		= IFF_BROADCAST;

	/* Put in this stuff here, so we don't have to export the symbols
	 * to the chipset drivers.
	 */

  	dev->open=arcnet_open;
	dev->stop=arcnet_close;
	dev->hard_start_xmit=arcnetA_send_packet;
	dev->get_stats=arcnet_get_stats;
	dev->hard_header=arcnetA_header;
	dev->rebuild_header=arcnetA_rebuild_header;
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

  /*  if (dev->metric>=1000)
   *  {
   *  arcnet_debug=dev->metric-1000;
   *  printk(KERN_INFO "%6s: debug level set to %d\n",dev->name,arcnet_debug);
   *  dev->metric=1;
   *}
   */
  BUGMSG(D_INIT,"arcnet_open: resetting card.\n");

  /* try to put the card in a defined state - if it fails the first
   * time, actually reset it.
   */
  if ((*lp->arcnet_reset)(dev,0) && (*lp->arcnet_reset)(dev,1))
    return -ENODEV;

  dev->tbusy=0;
  dev->interrupt=0;
  lp->intx=0;
  lp->in_txhandler=0;

  /* The RFC1201 driver is the default - just store */
  lp->adev=dev;

  /* we're started */
  dev->start=1;

#ifdef CONFIG_ARCNET_ETH
  /* Initialize the ethernet-encap protocol driver */
  lp->edev=(struct device *)kmalloc(sizeof(struct device),GFP_KERNEL);
  if (lp->edev == NULL)
    return -ENOMEM;
  memcpy(lp->edev,dev,sizeof(struct device));
  lp->edev->type=ARPHRD_ETHER;
  lp->edev->name=(char *)kmalloc(10,GFP_KERNEL);
  if (lp->edev->name == NULL) {
    kfree(lp->edev);
    lp->edev = NULL;
    return -ENOMEM;
  }
  sprintf(lp->edev->name,"%se",dev->name);
  lp->edev->init=arcnetE_init;
  register_netdevice(lp->edev);
#endif

#ifdef CONFIG_ARCNET_1051
  /* Initialize the RFC1051-encap protocol driver */
  lp->sdev=(struct device *)kmalloc(sizeof(struct device),GFP_KERNEL);
  memcpy(lp->sdev,dev,sizeof(struct device));
  lp->sdev->name=(char *)kmalloc(10,GFP_KERNEL);
  sprintf(lp->sdev->name,"%ss",dev->name);
  lp->sdev->init=arcnetS_init;
  register_netdevice(lp->sdev);
#endif

  /* Enable TX if we need to */
  if (lp->en_dis_able_TX)
    (*lp->en_dis_able_TX)(dev, 1);

  /* make sure we're ready to receive IRQ's.
   * arcnet_reset sets this for us, but if we receive one before
   * START is set to 1, it could be ignored.  So, we turn IRQ's
   * off, then on again to clean out the IRQ controller.
   */

  AINTMASK(0);
  udelay(1);	/* give it time to set the mask before
		 * we reset it again. (may not even be
		 * necessary)
		 */
  SETMASK;

  /* Let it increase its use count */
  (*lp->openclose_device)(1);

  return 0;
}


/* The inverse routine to arcnet_open - shuts down the card.
 */
static int
arcnet_close(struct device *dev)
{
  struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

  if (test_and_set_bit(0, (int *)&dev->tbusy))
    BUGMSG(D_NORMAL, "arcnet_close: tbusy already set!\n");

  dev->start=0;
#ifdef CONFIG_ARCNET_1051
  lp->sdev->tbusy=1;
  lp->sdev->start=0;
#endif
#ifdef CONFIG_ARCNET_ETH
  lp->edev->tbusy=1;
  lp->edev->start=0;
#endif

  /* Shut down the card */

  /* Disable TX if we need to */
  if (lp->en_dis_able_TX)
    (*lp->en_dis_able_TX)(dev, 0);

  (*lp->arcnet_reset)(dev, 3);	/* reset IRQ won't run if START=0 */
#if 0
  lp->intmask=0;
  SETMASK;		/* no IRQ's (except RESET, of course) */
  ACOMMAND(NOTXcmd);	/* stop transmit */
  ACOMMAND(NORXcmd);	/* disable receive */
#endif

  /* reset more flags */
  dev->interrupt=0;
#ifdef CONFIG_ARCNET_ETH
  lp->edev->interrupt=0;
#endif
#ifdef CONFIG_ARCNET_1051
  lp->sdev->interrupt=0;
#endif

  /* do NOT free lp->adev!!  It's static! */
  lp->adev=NULL;

#ifdef CONFIG_ARCNET_ETH
  /* free the ethernet-encap protocol device */
  lp->edev->priv=NULL;
  unregister_netdevice(lp->edev);
  kfree(lp->edev->name);
  kfree(lp->edev);
  lp->edev=NULL;
#endif

#ifdef CONFIG_ARCNET_1051
  /* free the RFC1051-encap protocol device */
  lp->sdev->priv=NULL;
  unregister_netdevice(lp->sdev);
  kfree(lp->sdev->name);
  kfree(lp->sdev);
  lp->sdev=NULL;
#endif

  /* Update the statistics here. (not necessary in ARCnet) */

  /* Decrease the use count */
  (*lp->openclose_device)(0);

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

  BUGMSG(D_DURING,"transmit requested (status=%Xh, inTX=%d)\n",
	 ARCSTATUS,lp->intx);

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

  if (test_bit(0, (int *)&dev->tbusy))
    {
      /* If we get here, some higher level has decided we are broken.
	 There should really be a "kick me" function call instead. */
      int tickssofar = jiffies - dev->trans_start;

      int status=ARCSTATUS;

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

	  ACOMMAND(NOTXcmd);
	}

      if (lp->outgoing.skb)
	{
	  dev_kfree_skb(lp->outgoing.skb);
	  lp->stats.tx_dropped++;
	}
      lp->outgoing.skb=NULL;

#ifdef CONFIG_ARCNET_ETH
      lp->edev->tbusy=0;
#endif
#ifdef CONFIG_ARCNET_1051
      lp->sdev->tbusy=0;
#endif
      if (!test_and_clear_bit(0,(int *)&dev->tbusy))
	BUGMSG(D_EXTRA, "after timing out, tbusy was clear!\n");

      lp->txready=0;
      lp->sending=0;

      return 1;
    }

  if (lp->txready)	/* transmit already in progress! */
    {
      BUGMSG(D_NORMAL,"trying to start new packet while busy! (status=%Xh)\n",
	     ARCSTATUS);
      lp->intmask &= ~TXFREEflag;
      SETMASK;
      ACOMMAND(NOTXcmd); /* abort current send */
      (*lp->inthandler)(dev); /* fake an interrupt */
      lp->stats.tx_errors++;
      lp->stats.tx_fifo_errors++;
      lp->txready=0;	/* we definitely need this line! */

      return 1;
    }

  /* Block a timer-based transmit from overlapping.  This could better be
     done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
  if (test_and_set_bit(0, (int *)&lp->adev->tbusy))
    {
      BUGMSG(D_NORMAL,"transmitter called with busy bit set! (status=%Xh, inTX=%d, tickssofar=%ld)\n",
	     ARCSTATUS,lp->intx,jiffies-dev->trans_start);
      lp->stats.tx_errors++;
      lp->stats.tx_fifo_errors++;
      return -EBUSY;
    }
#ifdef CONFIG_ARCNET_1051
  lp->sdev->tbusy=1;
#endif
#ifdef CONFIG_ARCNET_ETH
  lp->edev->tbusy=1;
#endif

  return 0;
}


/* Called by the kernel in order to transmit a packet.
 */
static int
arcnetA_send_packet(struct sk_buff *skb, struct device *dev)
{
  struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
  int bad,oldmask=0;
  struct Outgoing *out=&(lp->outgoing);

  lp->intx++;

  oldmask |= lp->intmask;
  lp->intmask=0;
  SETMASK;

  bad=arcnet_send_packet_bad(skb,dev);
  if (bad)
    {
      lp->intx--;
      lp->intmask=oldmask;
      SETMASK;
      return bad;
    }

  /* arcnet_send_packet_pad has already set tbusy - don't bother here. */

  lp->intmask = oldmask & ~TXFREEflag;
  SETMASK;

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
      (*lp->prepare_tx)(dev,
			((char *)out->hdr)+EXTRA_CLIENTDATA,
			sizeof(struct ClientData)-EXTRA_CLIENTDATA,
			((char *)skb->data)+sizeof(struct ClientData),
			out->length-sizeof(struct ClientData),
			out->hdr->daddr,1,0);

      /* done right away */
      lp->stats.tx_bytes += out->skb->len;
      dev_kfree_skb(out->skb);
      out->skb=NULL;

      if (arcnet_go_tx(dev,1))
	{
	  /* inform upper layers */
	  arcnet_tx_done(dev, lp);
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
	    {
	      lp->stats.tx_bytes += skb->len;
	      dev_kfree_skb(out->skb);
	    }
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
void arcnetA_continue_tx(struct device *dev)
{
  struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
  int maxsegsize=XMTU-4;
  struct Outgoing *out=&(lp->outgoing);

  BUGMSG(D_DURING,"continue_tx called (status=%Xh, intx=%d, intxh=%d, intmask=%Xh\n",
	 ARCSTATUS,lp->intx,lp->in_txhandler,lp->intmask);

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

  (*lp->prepare_tx)(dev,((char *)out->hdr)+EXTRA_CLIENTDATA,
		    sizeof(struct ClientData)-EXTRA_CLIENTDATA,
		    out->data,out->seglen,out->hdr->daddr,1,0);

  out->dataleft-=out->seglen;
  out->data+=out->seglen;
  out->segnum++;
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
int arcnet_go_tx(struct device *dev,int enable_irq)
{
	struct arcnet_local *lp=(struct arcnet_local *)dev->priv;

	BUGMSG(D_DURING,"go_tx: status=%Xh, intmask=%Xh, txready=%d, sending=%d\n",
		ARCSTATUS,lp->intmask,lp->txready,lp->sending);

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
	ACOMMAND(TXcmd|(lp->txready<<3));

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
 * interrupts. Establish which device needs attention, and call the correct
 * chipset interrupt handler.
 */
void
arcnet_interrupt(int irq,void *dev_id,struct pt_regs *regs)
{
	struct device *dev = dev_id;
	struct arcnet_local *lp;

	if (dev==NULL)
	  {
	    BUGMSG(D_DURING, "arcnet: irq %d for unknown device.\n", irq);
	    return;
	  }

	BUGMSG(D_DURING,"in arcnet_interrupt\n");

	lp=(struct arcnet_local *)dev->priv;
	if (!lp)
	  {
	    BUGMSG(D_DURING, "arcnet: irq ignored.\n");
	    return;
	  }

	/* RESET flag was enabled - if !dev->start, we must clear it right
	 * away (but nothing else) since inthandler() is never called.
	 */

	if (!dev->start)
	  {
	    if (ARCSTATUS & RESETflag)
	      ACOMMAND(CFLAGScmd|RESETclear);
	    return;
	  }


	if (test_and_set_bit(0, (int *)&dev->interrupt))
	  {
	    BUGMSG(D_NORMAL,"DRIVER PROBLEM!  Nested arcnet interrupts!\n");
	    return;	/* don't even try. */
	  }
#ifdef CONFIG_ARCNET_1051
	if (lp->sdev)
	  lp->sdev->interrupt=1;
#endif
#ifdef CONFIG_ARCNET_ETH
	if (lp->edev)
	  lp->edev->interrupt=1;
#endif

	/* Call the "real" interrupt handler. */
	(*lp->inthandler)(dev);

#ifdef CONFIG_ARCNET_ETH
	if (lp->edev)
	  lp->edev->interrupt=0;
#endif
#ifdef CONFIG_ARCNET_1051
	if (lp->sdev)
	  lp->sdev->interrupt=0;
#endif
	if (!test_and_clear_bit(0, (int *)&dev->interrupt))
	  BUGMSG(D_NORMAL, "Someone cleared our dev->interrupt flag!\n");

}

void arcnet_tx_done(struct device *dev, struct arcnet_local *lp)
{
  if (dev->tbusy)
    {
#ifdef CONFIG_ARCNET_ETH
      lp->edev->tbusy=0;
#endif
#ifdef CONFIG_ARCNET_1051
      lp->sdev->tbusy=0;
#endif
      if (!test_and_clear_bit(0, (int *)&dev->tbusy))
	BUGMSG(D_NORMAL, "In arcnet_tx_done: Someone cleared our dev->tbusy"
	       " flag!\n");

      mark_bh(NET_BH);
    }
}


/****************************************************************************
 *                                                                          *
 * Receiver routines                                                        *
 *                                                                          *
 ****************************************************************************/

/*
 * This is a generic packet receiver that calls arcnet??_rx depending on the
 * protocol ID found.
 */

void arcnet_rx(struct arcnet_local *lp, u_char *arcsoft, short length, int saddr, int daddr)
{
  struct device *dev=lp->adev;

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
    case ARC_P_DATAPOINT_BOOT:
    case ARC_P_DATAPOINT_MOUNT:
      break;
    case ARC_P_POWERLAN_BEACON:
    case ARC_P_POWERLAN_BEACON2:
      break;
    case ARC_P_LANSOFT: /* don't understand.  fall through. */
    default:
      BUGMSG(D_EXTRA,"received unknown protocol %d (%Xh) from station %d.\n",
	     arcsoft[0],arcsoft[0],saddr);
      lp->stats.rx_errors++;
      lp->stats.rx_crc_errors++;
      break;
    }

  /* If any worth-while packets have been received, a mark_bh(NET_BH)
   * has been done by netif_rx and Linux will handle them after we
   * return.
   */


}



/* Packet receiver for "standard" RFC1201-style packets
 */
static void
arcnetA_rx(struct device *dev,u_char *buf,
	   int length, u_char saddr, u_char daddr)
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
	  lp->aborted_seq=arcsoft->sequence;
	  kfree_skb(in->skb);
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

      skb_put(skb,length);
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

      lp->stats.rx_bytes += skb->len;
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
	  BUGMSG(D_EXTRA,"wrong seq number (saddr=%d, expected=%d, seq=%d, splitflag=%d)\n",
		 saddr,in->sequence,arcsoft->sequence,
		 arcsoft->split_flag);
	  kfree_skb(in->skb);
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
	      kfree_skb(in->skb);
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
	    BUGMSG(D_NORMAL,"(split) memory squeeze, dropping packet.\n");
	    lp->stats.rx_dropped++;
	    return;
	  }

	  soft=(struct ClientData *)skb->data;

	  skb_put(skb,sizeof(struct ClientData));
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
	      if (lp->aborted_seq != arcsoft->sequence)
		{
		  BUGMSG(D_EXTRA,"can't continue split without starting first! (splitflag=%d, seq=%d, aborted=%d)\n",
			 arcsoft->split_flag,arcsoft->sequence, lp->aborted_seq);
		  lp->stats.rx_errors++;
		  lp->stats.rx_missed_errors++;
		}
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
	      lp->aborted_seq=arcsoft->sequence;
	      kfree_skb(in->skb);
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
      skb_put(skb,length-sizeof(struct ClientData));

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

	      lp->stats.rx_bytes += skb->len;
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

static struct net_device_stats *arcnet_get_stats(struct device *dev)
{
  struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

  return &lp->stats;
}


/* Create the ARCnet ClientData header for an arbitrary protocol layer
 *
 * saddr=NULL	means use device source address (always will anyway)
 * daddr=NULL	means leave destination address (eg unresolved arp)
 */
static int arcnetA_header(struct sk_buff *skb,struct device *dev,
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
static int arcnetA_rebuild_header(struct sk_buff *skb)
{
  struct ClientData *head = (struct ClientData *)skb->data;
  struct device *dev=skb->dev;
  struct arcnet_local *lp=(struct arcnet_local *)(dev->priv);
#ifdef CONFIG_INET
  int status;
#endif

  /*
   * Only ARP and IP are currently supported
   *
   * FIXME: Anyone want to spec IPv6 over ARCnet ?
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
  status=arp_find(&(head->daddr),skb)? 1 : 0;
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
static unsigned short arcnetA_type_trans(struct sk_buff *skb,struct device *dev)
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
  dev->mtu=512-sizeof(struct archdr)-dev->hard_header_len-1;
  dev->open=arcnetE_open_close;
  dev->stop=arcnetE_open_close;
  dev->hard_start_xmit=arcnetE_send_packet;

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
  int bad,oldmask=0;
  u_char daddr;
  short offset,length=skb->len+1;
  u_char proto=ARC_P_ETHER;

  lp->intx++;

  oldmask |= lp->intmask;
  lp->intmask=0;
  SETMASK;

  bad=arcnet_send_packet_bad(skb,dev);
  if (bad)
    {
      lp->intx--;
      lp->intmask=oldmask;
      SETMASK;
      return bad;
    }

  /* arcnet_send_packet_pad has already set tbusy - don't bother here. */

  lp->intmask=oldmask;
  SETMASK;

  if (length>XMTU)
    {
      BUGMSG(D_NORMAL,"MTU must be <= 493 for ethernet encap (length=%d).\n",
	     length);
      BUGMSG(D_NORMAL,"transmit aborted.\n");

      dev_kfree_skb(skb);
      lp->intx--;
      return 0;
    }

  BUGMSG(D_DURING,"starting tx sequence...\n");

  /* broadcasts have address FF:FF:FF:FF:FF:FF in etherspeak */
  if (((struct ethhdr*)(skb->data))->h_dest[0] == 0xFF)
    daddr=0;
  else
    daddr=((struct ethhdr*)(skb->data))->h_dest[5];

  /* load packet into shared memory */
  offset=512-length;
  if (length>MTU)		/* long/exception packet */
    {
      if (length<MinTU) offset-=3;
    }
  else			/* short packet */
    {
      offset-=256;
    }

  BUGMSG(D_DURING," length=%Xh, offset=%Xh\n",
	 length,offset);

  (*lp->prepare_tx)(dev, &proto, 1, skb->data, length-1, daddr, 0,
			   offset);

  dev_kfree_skb(skb);

  if (arcnet_go_tx(dev,1))
    {
      /* inform upper layers */
      arcnet_tx_done(lp->adev, lp);
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

       	skb_put(skb,length);

       	skb->dev = dev;

       	memcpy(skb->data,(u_char *)arcsoft+1,length-1);

        BUGLVL(D_SKB) arcnet_dump_skb(dev,skb,"rx");

	lp->stats.rx_bytes += skb->len;
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
  dev->mtu=512-sizeof(struct archdr)-dev->hard_header_len
    + S_EXTRA_CLIENTDATA;
  dev->open=arcnetS_open_close;
  dev->stop=arcnetS_open_close;
  dev->hard_start_xmit=arcnetS_send_packet;
  dev->hard_header=arcnetS_header;
  dev->rebuild_header=arcnetS_rebuild_header;

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
  int bad,length;
  struct S_ClientData *hdr=(struct S_ClientData *)skb->data;

  lp->intx++;

  bad=arcnet_send_packet_bad(skb,dev);
  if (bad)
    {
      lp->intx--;
      return bad;
    }

  /* arcnet_send_packet_pad has already set tbusy - don't bother here. */

  length = 1 < skb->len ? skb->len : 1;

  BUGLVL(D_SKB) arcnet_dump_skb(dev,skb,"tx");

  /* fits in one packet? */
  if (length-S_EXTRA_CLIENTDATA<=XMTU)
    {
      (*lp->prepare_tx)(dev,
			skb->data+S_EXTRA_CLIENTDATA,
			sizeof(struct S_ClientData)-S_EXTRA_CLIENTDATA,
			skb->data+sizeof(struct S_ClientData),
			length-sizeof(struct S_ClientData),
			hdr->daddr,0,0);

      /* done right away */
      dev_kfree_skb(skb);

      if (arcnet_go_tx(dev,1))
	{
	  /* inform upper layers */
	  arcnet_tx_done(lp->adev, lp);
	}
    }
  else			/* too big for one - not accepted */
    {
      BUGMSG(D_NORMAL,"packet too long (length=%d)\n",
	     length);
      dev_kfree_skb(skb);
      lp->stats.tx_dropped++;
      arcnet_tx_done(lp->adev, lp);
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
    skb_put(skb,length);

    memcpy((u_char *)soft + sizeof(struct S_ClientData) - S_EXTRA_CLIENTDATA,
	   (u_char *)arcsoft + sizeof(struct S_ClientData) -S_EXTRA_CLIENTDATA,
	   length - sizeof(struct S_ClientData) + S_EXTRA_CLIENTDATA);
    soft->protocol_id=arcsoft->protocol_id;
    soft->daddr=daddr;
    soft->saddr=saddr;
    skb->dev = dev;  /* is already lp->sdev */

    BUGLVL(D_SKB) arcnet_dump_skb(dev,skb,"rx");

    lp->stats.rx_bytes += skb->len;
    skb->protocol=arcnetS_type_trans(skb,dev);
    netif_rx(skb);
  }
}


/* Create the ARCnet ClientData header for an arbitrary protocol layer
 *
 * saddr=NULL	means use device source address (always will anyway)
 * daddr=NULL	means leave destination address (eg unresolved arp)
 */
static int arcnetS_header(struct sk_buff *skb,struct device *dev,
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
static int arcnetS_rebuild_header(struct sk_buff *skb)
{
  struct device *dev=skb->dev;
  struct S_ClientData *head = (struct S_ClientData *)skb->data;
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
  return arp_find(&(head->daddr),skb)? 1 : 0;
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

void cleanup_module(void)
{
  printk("Generic arcnet support removed.\n");
}

void arcnet_use_count(int open)
{
  if (open)
    MOD_INC_USE_COUNT;
  else
    MOD_DEC_USE_COUNT;
}

#else

void arcnet_use_count(int open)
{
}

struct device arcnet_devs[MAX_ARCNET_DEVS];
int arcnet_num_devs=0;
char arcnet_dev_names[MAX_ARCNET_DEVS][10];

__initfunc(void arcnet_init(void))
{
  int c;

  init_module();

  /* Don't register_netdev here. The chain hasn't been initialised. */

#ifdef CONFIG_ARCNET_COM90xx
  if ((!com90xx_explicit) && arcnet_num_devs < MAX_ARCNET_DEVS)
    {
      arcnet_devs[arcnet_num_devs].init=arc90xx_probe;
      arcnet_devs[arcnet_num_devs].name=
	(char *)&arcnet_dev_names[arcnet_num_devs];
      arcnet_num_devs++;
    }
#endif

  if (!arcnet_num_devs)
    {
      printk("Don't forget to load the chipset driver.\n");
      return;
    }

  /* Link into the device chain */

  /* Q: Should we put ourselves at the beginning or the end of the chain? */
  /* Probably the end, because we're not so fast, but... */

  for (c=0; c< (arcnet_num_devs-1); c++)
    arcnet_devs[c].next=&arcnet_devs[c+1];

  arcnet_devs[c].next=dev_base;
  dev_base=&arcnet_devs[0];

  /* Give names to those without them */

  for (c=0; c< arcnet_num_devs; c++)
    if (!arcnet_dev_names[c][0])
      arcnet_makename((char *)&arcnet_dev_names[c]);
}

#endif /* MODULE */


#ifdef MODULE
int init_module(void)
#else
__initfunc(static int init_module(void))
#endif
{
#ifdef ALPHA_WARNING
  BUGLVL(D_EXTRA)
    {
      printk("arcnet: ***\n");
      printk("arcnet: * Read arcnet.txt for important release notes!\n");
      printk("arcnet: *\n");
      printk("arcnet: * This is an ALPHA version!  (Last stable release: v2.56)  E-mail me if\n");
      printk("arcnet: * you have any questions, comments, or bug reports.\n");
      printk("arcnet: ***\n");
    }
#endif

  printk("%sAvailable protocols: ARCnet RFC1201"
#ifdef CONFIG_ARCNET_ETH
	 ", Ethernet-Encap"
#endif
#ifdef CONFIG_ARCNET_1051
	 ", ARCnet RFC1051"
#endif
#ifdef MODULE
	 ".\nDon't forget to load the chipset driver"
#endif
	".\n",version);
  return 0;
}


void arcnet_makename(char *device)
{
  struct device *dev;
  int arcnum;

  arcnum = 0;
  for (;;)
    {
      sprintf(device, "arc%d", arcnum);
      for (dev = dev_base; dev; dev=dev->next)
	if (dev->name != device && !strcmp(dev->name, device))
	  break;
      if (!dev)
	return;
      arcnum++;
    }
}
