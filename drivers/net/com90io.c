/*	$Id: com90io.c,v 1.6 1997/11/09 11:04:59 mj Exp $

        Written 1997 by David Woodhouse <dwmw2@cam.ac.uk>

	Derived from the original arcnet.c,
	Written 1994-1996 by Avery Pennarun,
	which was in turn derived from skeleton.c by Donald Becker.

	**********************

	The original copyright of skeleton.c was as follows:

	skeleton.c Written 1993 by Donald Becker.
	Copyright 1993 United States Government as represented by the
        Director, National Security Agency.  This software may only be used
        and distributed according to the terms of the GNU Public License as
        modified by SRC, incorporated herein by reference.

	**********************

	For more details, see drivers/net/arcnet.c

	**********************
*/


#include <linux/module.h>
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


/* Internal function declarations */

static int arc90io_probe(struct device *dev);
static void arc90io_rx(struct device *dev,int recbuf);
static int arc90io_found(struct device *dev,int ioaddr,int airq);
static void arc90io_inthandler (struct device *dev);
static int arc90io_reset (struct device *dev, int reset_delay);
static void arc90io_setmask (struct device *dev, u_char mask);
static void arc90io_command (struct device *dev, u_char command);
static u_char arc90io_status (struct device *dev);
static void arc90io_prepare_tx(struct device *dev,u_char *hdr,int hdrlen,
       		      char *data,int length,int daddr,int exceptA, int offset);
static  void arc90io_openclose(int open);

static u_char get_buffer_byte (struct device *dev, unsigned offset);
static void put_buffer_byte (struct device *dev, unsigned offset, u_char datum);
static void get_whole_buffer (struct device *dev, unsigned offset, unsigned length, char *dest);
static void put_whole_buffer (struct device *dev, unsigned offset, unsigned length, char *dest);


/* Module parameters */

#ifdef MODULE
static int io=0x0;	/* <--- EDIT THESE LINES FOR YOUR CONFIGURATION */
static int irq=0;	/* or use the insmod io= irq= shmem= options */
static char *device;	/* use eg. device="arc1" to change name */

MODULE_PARM(io, "i");
MODULE_PARM(irq, "i");
MODULE_PARM(device, "s");
#else
__initfunc(void com90io_setup (char *str, int *ints));
extern struct device arcnet_devs[];
extern char arcnet_dev_names[][10];
extern int arcnet_num_devs;
#endif


/* Handy defines for ARCnet specific stuff */

/* The number of low I/O ports used by the card. */
#define ARCNET_TOTAL_SIZE 16

/* COM 9026 controller chip --> ARCnet register addresses */
#define _INTMASK (ioaddr+0)	/* writable */
#define _STATUS  (ioaddr+0)	/* readable */
#define _COMMAND (ioaddr+1)	/* writable, returns random vals on read (?) */
#define _RESET  (ioaddr+8)	/* software reset (on read) */
#define _MEMDATA  (ioaddr+12)            /* Data port for IO-mapped memory */
#define _ADDR_HI  (ioaddr+15)            /* Control registers for said */
#define _ADDR_LO  (ioaddr+14)
#define _CONFIG  (ioaddr+2)      /* Configuration register */

#define ARCSTATUS	inb(_STATUS)
#define ACOMMAND(cmd) outb((cmd),_COMMAND)
#define AINTMASK(msk)	outb((msk),_INTMASK)

#define ARCRESET	inb(_RESET)

#define SETCONF 	outb((lp->config),_CONFIG)


/****************************************************************************
 *                                                                          *
 * IO-mapped operation routines                                             *
 *                                                                          *
 ****************************************************************************/

u_char get_buffer_byte (struct device *dev, unsigned offset)
{
  int ioaddr=dev->base_addr;

  outb(offset >> 8,  _ADDR_HI);
  outb(offset & 0xff, _ADDR_LO);

  return inb(_MEMDATA);
}

void put_buffer_byte (struct device *dev, unsigned offset, u_char datum)
{
  int ioaddr=dev->base_addr;

  outb(offset >> 8,   _ADDR_HI);
  outb(offset & 0xff, _ADDR_LO);

  outb(datum, _MEMDATA);
}


#undef ONE_AT_A_TIME_TX
#undef ONE_AT_A_TIME_RX

void get_whole_buffer (struct device *dev, unsigned offset, unsigned length, char *dest)
{
  int ioaddr=dev->base_addr;

  outb( (offset >> 8) | AUTOINCflag,   _ADDR_HI);
  outb( offset & 0xff,                 _ADDR_LO);

  while (length--)
#ifdef ONE_AT_A_TIME_RX
    *(dest++) = get_buffer_byte(dev,offset++);
#else
    *(dest++) = inb (_MEMDATA);
#endif
}

void put_whole_buffer (struct device *dev, unsigned offset, unsigned length, char *dest)
{
  int ioaddr=dev->base_addr;

  outb( (offset >> 8) | AUTOINCflag,   _ADDR_HI);
  outb( offset & 0xff,                 _ADDR_LO);

  while (length--)
#ifdef ONE_AT_A_TIME_TX
    put_buffer_byte(dev,offset++,*(dest++));
#else
    outb (*(dest++), _MEMDATA);
#endif
}


static const char *version =
 "com90io.c: v3.00 97/11/09 Avery Pennarun <apenwarr@worldvisions.ca> et al.\n";


/****************************************************************************
 *                                                                          *
 * Probe and initialization                                                 *
 *                                                                          *
 ****************************************************************************/

/* We cannot probe for an IO mapped card either, although we can check that
 * it's where we were told it was, and even autoirq
 */

__initfunc(int arc90io_probe(struct device *dev))
{
  int ioaddr=dev->base_addr,status,delayval;
  unsigned long airqmask;

  BUGLVL(D_NORMAL) printk(version);

  if (ioaddr<0x200)
    {
      BUGMSG(D_NORMAL,"No autoprobe for IO mapped cards; you "
	     "must specify the base address!\n");
      return -ENODEV;
    }

  if (check_region(ioaddr, ARCNET_TOTAL_SIZE))
    {
      BUGMSG(D_INIT_REASONS,"IO check_region %x-%x failed.\n",
	     ioaddr,ioaddr+ARCNET_TOTAL_SIZE-1);
      return -ENXIO;
    }

  if (ARCSTATUS == 0xFF)
    {
      BUGMSG(D_INIT_REASONS,"IO address %x empty\n",ioaddr);
      return -ENODEV;
    }

  ARCRESET;
  JIFFER(RESETtime);

  status=ARCSTATUS;

  if ((status & 0x9D)
      !=  (NORXflag|RECONflag|TXFREEflag|RESETflag))
    {
      BUGMSG(D_INIT_REASONS,"Status invalid (%Xh).\n",status);
      return -ENODEV;
    }

  BUGMSG(D_INIT_REASONS,"Status after reset: %X\n",status);

  ACOMMAND(CFLAGScmd|RESETclear|CONFIGclear);

  BUGMSG(D_INIT_REASONS,"Status after reset acknowledged: %X\n",status);

  status=ARCSTATUS;

  if (status & RESETflag)
    {
      BUGMSG(D_INIT_REASONS,"Eternal reset (status=%Xh)\n",status);
      return -ENODEV;
    }

  outb((0x16 | IOMAPflag) &~ENABLE16flag, _CONFIG);

  /* Read first loc'n of memory */

  outb(AUTOINCflag ,_ADDR_HI);
  outb(0,_ADDR_LO);

  if ((status=inb(_MEMDATA)) != 0xd1)
    {
      BUGMSG(D_INIT_REASONS,"Signature byte not found"
	     " (%Xh instead).\n", status);
      return -ENODEV;
    }

  if (!dev->irq)
    {
      /* if we do this, we're sure to get an IRQ since the
       * card has just reset and the NORXflag is on until
       * we tell it to start receiving.
       */

      airqmask = probe_irq_on();
      outb(NORXflag,_INTMASK);
      udelay(1);
      outb(0,_INTMASK);
      dev->irq = probe_irq_off(airqmask);

      if (dev->irq<=0)
	{
	  BUGMSG(D_INIT_REASONS,"Autoprobe IRQ failed\n");
	  return -ENODEV;
	}
    }

  return arc90io_found(dev,dev->base_addr,dev->irq);
}


/* Set up the struct device associated with this card.  Called after
 * probing succeeds.
 */
__initfunc(int arc90io_found(struct device *dev,int ioaddr,int airq))
{
  struct arcnet_local *lp;

  /* reserve the irq */
  if (request_irq(airq,&arcnet_interrupt,0,"arcnet (COM90xx-IO)",dev))
    {
      BUGMSG(D_NORMAL,"Can't get IRQ %d!\n",airq);
      return -ENODEV;
    }
  dev->irq=airq;

  /* reserve the I/O region - guaranteed to work by check_region */
  request_region(ioaddr,ARCNET_TOTAL_SIZE,"arcnet (COM90xx-IO)");
  dev->base_addr=ioaddr;

  dev->mem_start=dev->mem_end=dev->rmem_start=dev->rmem_end=(long)NULL;

  /* Initialize the rest of the device structure. */

  dev->priv = kmalloc(sizeof(struct arcnet_local), GFP_KERNEL);
  if (dev->priv == NULL)
    {
      free_irq(airq,dev);
      release_region(ioaddr,ARCNET_TOTAL_SIZE);
      return -ENOMEM;
    }

  memset(dev->priv,0,sizeof(struct arcnet_local));
  lp=(struct arcnet_local *)(dev->priv);
  lp->card_type = ARC_90xx_IO;
  lp->card_type_str = "COM 90xx (IO)";

  lp->arcnet_reset=arc90io_reset;
  lp->asetmask=arc90io_setmask;
  lp->astatus=arc90io_status;
  lp->acommand=arc90io_command;
  lp->openclose_device=arc90io_openclose;
  lp->prepare_tx=arc90io_prepare_tx;
  lp->inthandler=arc90io_inthandler;

  /* Fill in the fields of the device structure with generic
   * values.
   */
  arcnet_setup(dev);

  /* And now fill particular fields with arcnet values */
  dev->mtu=1500; /* completely arbitrary - agrees with ether, though */
  dev->hard_header_len=sizeof(struct ClientData);
  lp->sequence=1;
  lp->recbuf=0;

  BUGMSG(D_DURING,"ClientData header size is %d.\n",
	 sizeof(struct ClientData));
  BUGMSG(D_DURING,"HardHeader size is %d.\n",
	 sizeof(struct archdr));

  lp->config = (0x16 | IOMAPflag) & ~ENABLE16flag;
  SETCONF;

  /* get and check the station ID from offset 1 in shmem */

  lp->stationid = get_buffer_byte(dev,1);

  if (!lp->stationid)
    BUGMSG(D_NORMAL,"WARNING!  Station address 00 is reserved "
	   "for broadcasts!\n");
  else if (lp->stationid==255)
    BUGMSG(D_NORMAL,"WARNING!  Station address FF may confuse "
	   "DOS networking programs!\n");
  dev->dev_addr[0]=lp->stationid;

  BUGMSG(D_NORMAL,"ARCnet COM90xx in IO-mapped mode:  "
	 "station %02Xh found at %03lXh, IRQ %d.\n",
	 lp->stationid,
	 dev->base_addr,dev->irq);

  return 0;
}


/****************************************************************************
 *                                                                          *
 * Utility routines                                                         *
 *                                                                          *
 ****************************************************************************/

/* Do a hardware reset on the card, and set up necessary registers.
 *
 * This should be called as little as possible, because it disrupts the
 * token on the network (causes a RECON) and requires a significant delay.
 *
 * However, it does make sure the card is in a defined state.
 */
int arc90io_reset(struct device *dev,int reset_delay)
{
  struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
  short ioaddr=dev->base_addr;
  int delayval,recbuf=lp->recbuf;

  if (reset_delay==3)
    {
      ARCRESET;
      return 0;
    }

  /* no IRQ's, please! */
  lp->intmask=0;
  SETMASK;

  BUGMSG(D_INIT,"Resetting %s (status=%Xh)\n",
	 dev->name,ARCSTATUS);

  /* Set the thing to IO-mapped, 8-bit  mode */
  lp->config = (0x1C|IOMAPflag) & ~ENABLE16flag;
  SETCONF;

  if (reset_delay)
    {
      /* reset the card */
      ARCRESET;
      JIFFER(RESETtime);
    }

  ACOMMAND(CFLAGScmd|RESETclear); /* clear flags & end reset */
  ACOMMAND(CFLAGScmd|CONFIGclear);

  /* verify that the ARCnet signature byte is present */

  if (get_buffer_byte(dev,0) != TESTvalue)
    {
      BUGMSG(D_NORMAL,"reset failed: TESTvalue not present.\n");
      return 1;
    }

  /* clear out status variables */
  recbuf=lp->recbuf=0;
  lp->txbuf=2;

  /* enable extended (512-byte) packets */
  ACOMMAND(CONFIGcmd|EXTconf);

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


static void arc90io_openclose(int open)
{
  if (open)
    MOD_INC_USE_COUNT;
  else
    MOD_DEC_USE_COUNT;
}


static void arc90io_setmask(struct device *dev, u_char mask)
{
  short ioaddr=dev->base_addr;

  AINTMASK(mask);
}

static u_char arc90io_status(struct device *dev)
{
  short ioaddr=dev->base_addr;

  return ARCSTATUS;
}

static void arc90io_command(struct device *dev, u_char cmd)
{
  short ioaddr=dev->base_addr;

  ACOMMAND(cmd);
}


/* The actual interrupt handler routine - handle various IRQ's generated
 * by the card.
 */
static void
arc90io_inthandler(struct device *dev)
{
  struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
  int ioaddr=dev->base_addr, status, boguscount = 3, didsomething;

  AINTMASK(0);

  BUGMSG(D_DURING,"in arc90io_inthandler (status=%Xh, intmask=%Xh)\n",
	 ARCSTATUS,lp->intmask);

  do
    {
      status = ARCSTATUS;
      didsomething=0;

      /* RESET flag was enabled - card is resetting and if RX
       * is disabled, it's NOT because we just got a packet.
       */
      if (status & RESETflag)
	{
	  BUGMSG(D_NORMAL,"spurious reset (status=%Xh)\n",
		 status);
	  arc90io_reset(dev,0);

	  /* all other flag values are just garbage */
	  break;
	}

      /* RX is inhibited - we must have received something. */
      if (status & lp->intmask & NORXflag)
	{
	  int recbuf=lp->recbuf=!lp->recbuf;
	  int oldaddr=0;

	  BUGMSG(D_DURING,"receive irq (status=%Xh)\n",
		 status);

	  /* enable receive of our next packet */
	  EnableReceiver();

	  if (lp->intx)
	    oldaddr=(inb(_ADDR_HI)<<8) | inb(_ADDR_LO);


	  /* Got a packet. */
	  arc90io_rx(dev,!recbuf);


	  if (lp->intx)
	    {
	      outb( (oldaddr >> 8),  _ADDR_HI);
	      outb( oldaddr & 0xff, _ADDR_LO);
	    }

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
		     ARCSTATUS,lp->intx);
	      lp->in_txhandler--;
	      continue;
	    }

	  if (!lp->outgoing.skb)
	    {
	      BUGMSG(D_DURING,"TX IRQ done: no split to continue.\n");

	      /* inform upper layers */
	      if (!lp->txready) arcnet_tx_done(dev, lp);
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
		{
		  lp->stats.tx_bytes += out->skb->len;
		  dev_kfree_skb(out->skb);
		}
	      out->skb=NULL;

	      /* inform upper layers */
	      if (!lp->txready) arcnet_tx_done(dev, lp);
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
	  ACOMMAND(CFLAGScmd|CONFIGclear);
	  lp->stats.tx_carrier_errors++;

#ifdef SHOW_RECONFIGS
	  BUGMSG(D_NORMAL,"Network reconfiguration detected"
		 " (status=%Xh, config=%X)\n",
		 status,lp->config);
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
	}
      else if (lp->network_down && jiffies-lp->last_recon > HZ*10)
	{
	  if (lp->network_down)
	    BUGMSG(D_NORMAL,"cabling restored?\n");
	  lp->first_recon=lp->last_recon=0;
	  lp->num_recons=lp->network_down=0;

	  BUGMSG(D_DURING,"not recon: clearing counters anyway.\n");
#endif
	}
#endif /* DETECT_RECONFIGS */
    } while (--boguscount && didsomething);

  BUGMSG(D_DURING,"net_interrupt complete (status=%Xh, count=%d)\n",
	 ARCSTATUS,boguscount);
  BUGMSG(D_DURING,"\n");

  SETMASK;	/* put back interrupt mask */
}


/* A packet has arrived; grab it from the buffers and pass it to the generic
 * arcnet_rx routing to deal with it.
 */

static void
arc90io_rx(struct device *dev,int recbuf)
{
  struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
  int ioaddr=dev->base_addr;
  union ArcPacket packetbuf;
  union ArcPacket *arcpacket=&packetbuf;
  u_char *arcsoft;
  short length,offset;
  u_char daddr,saddr;

  lp->stats.rx_packets++;

  get_whole_buffer(dev,recbuf*512,4,(char *)arcpacket);

  saddr=arcpacket->hardheader.source;

  /* if source is 0, it's a "used" packet! */
  if (saddr==0)
    {
      BUGMSG(D_NORMAL,"discarding old packet. (status=%Xh)\n",
	     ARCSTATUS);
      lp->stats.rx_errors++;
      return;
    }
  /* Set source address to zero to mark it as old */

  put_buffer_byte(dev,recbuf*512,0);

  arcpacket->hardheader.source=0;

  daddr=arcpacket->hardheader.destination;

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

  get_whole_buffer(dev,recbuf*512+offset,length,(char *)arcpacket+offset);

  arcnet_rx(lp, arcsoft, length, saddr, daddr);

  BUGLVL(D_RX) arcnet_dump_packet(lp->adev,arcpacket->raw,length>240,"rx");
}


/* Given an skb, copy a packet into the ARCnet buffers for later transmission
 * by arcnet_go_tx.
 */
static void
arc90io_prepare_tx(struct device *dev,u_char *hdr,int hdrlen,
		    char *data,int length,int daddr,int exceptA, int offset)
{
  struct arcnet_local *lp = (struct arcnet_local *)dev->priv;

  lp->txbuf=lp->txbuf^1;	/* XOR with 1 to alternate between 2 and 3 */

  length+=hdrlen;

  BUGMSG(D_TX,"arcnetAS_prep_tx: hdr:%ph, length:%d, data:%ph\n",
	 hdr,length,data);

  put_buffer_byte(dev, lp->txbuf*512+1, daddr);

  /* load packet into shared memory */
  if (length<=MTU)	/* Normal (256-byte) Packet */
    put_buffer_byte(dev, lp->txbuf*512+2, offset=offset?offset:256-length);

  else if (length>=MinTU || offset)	/* Extended (512-byte) Packet */
    {
      put_buffer_byte(dev, lp->txbuf*512+2, 0);
      put_buffer_byte(dev, lp->txbuf*512+3, offset=offset?offset:512-length);
    }
  else if (exceptA)		/* RFC1201 Exception Packet */
    {
      put_buffer_byte(dev, lp->txbuf*512+2, 0);
      put_buffer_byte(dev, lp->txbuf*512+3, offset=512-length-4);

      /* exception-specific stuff - these four bytes
       * make the packet long enough to fit in a 512-byte
       * frame.
       */

      put_buffer_byte(dev, lp->txbuf*512+offset,hdr[0]);
      put_whole_buffer(dev, lp->txbuf*512+offset+1,3,"\377\377\377");
      offset+=4;
    }
  else				/* "other" Exception packet */
    {
      /* RFC1051 - set 4 trailing bytes to 0 */

      put_whole_buffer(dev,lp->txbuf*512+508,4,"\0\0\0\0");

      /* now round up to MinTU */
      put_buffer_byte(dev, lp->txbuf*512+2, 0);
      put_buffer_byte(dev, lp->txbuf*512+3, offset=512-MinTU);
    }

  /* copy the packet into ARCnet shmem
   *  - the first bytes of ClientData header are skipped
   */

  put_whole_buffer(dev, 512*lp->txbuf+offset, hdrlen,(u_char *)hdr);
  put_whole_buffer(dev, 512*lp->txbuf+offset+hdrlen,length-hdrlen,data);

  BUGMSG(D_DURING,"transmitting packet to station %02Xh (%d bytes)\n",
	 daddr,length);

  lp->lastload_dest=daddr;
  lp->txready=lp->txbuf;	/* packet is ready for sending */
}


/****************************************************************************
 *                                                                          *
 * Kernel Loadable Module Support                                           *
 *                                                                          *
 ****************************************************************************/


#ifdef MODULE

static struct device *cards[16]={NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
			  NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};


int init_module(void)
{
  struct device *dev=cards[0];

  cards[0]=dev=(struct device *)kmalloc(sizeof(struct device), GFP_KERNEL);
  if (!dev)
    return -ENOMEM;

  memset(dev, 0, sizeof(struct device));

  dev->name=(char *)kmalloc(9, GFP_KERNEL);
  if (!dev->name)
    {
      kfree(dev);
      return -ENOMEM;
    }
  dev->init=arc90io_probe;

  if (device)
    strcpy(dev->name,device);
  else arcnet_makename(dev->name);

  dev->base_addr=io;
  dev->irq=irq;

  if (dev->irq==2) dev->irq=9;

  if (register_netdev(dev) != 0)
    return -EIO;

  /* Increase use count of arcnet.o */
  arcnet_use_count(1);
  return 0;
}

void cleanup_module(void)
{
  struct device *dev=cards[0];
  int ioaddr=dev->base_addr;

  if (dev->start) (*dev->stop)(dev);

  /* Flush TX and disable RX */
  if (ioaddr)
    {
      AINTMASK(0);		/* disable IRQ's */
      ACOMMAND(NOTXcmd);	/* stop transmit */
      ACOMMAND(NORXcmd);	/* disable receive */

      /* Set the thing back to MMAP mode, in case the old
	         driver is loaded later */
      outb( (inb(_CONFIG)&~IOMAPflag),_CONFIG);
    }

  if (dev->irq)
    {
      free_irq(dev->irq,dev);
    }

  if (dev->base_addr) release_region(dev->base_addr,ARCNET_TOTAL_SIZE);
  unregister_netdev(dev);
  kfree(dev->priv);
  dev->priv = NULL;

  /* Decrease use count of arcnet.o */
  arcnet_use_count(0);
}

#else

__initfunc(void com90io_setup (char *str, int *ints))
{
  struct device *dev;

  if (arcnet_num_devs == MAX_ARCNET_DEVS)
    {
      printk("com90xx IO-MAP: Too many ARCnet devices registered (max %d).\n",
	     MAX_ARCNET_DEVS);
      return;
    }

  dev=&arcnet_devs[arcnet_num_devs];

  if (ints[0] < 1)
    {
      printk("com90xx IO-MAP: You must give an IO address.\n");
      return;
    }

  dev->init=arc90io_probe;

  switch(ints[0])
    {
    case 3: /* ERROR */
      printk("com90xx IO-MAP: Too many arguments.\n");

    case 2: /* IRQ */
      dev->irq=ints[2];

    case 1: /* IO address */
      dev->base_addr=ints[1];
    }

  dev->name = (char *)&arcnet_dev_names[arcnet_num_devs];

  if (str)
    strncpy(dev->name, str, 9);

  arcnet_num_devs++;
}

#endif /* MODULE */
