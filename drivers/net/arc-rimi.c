/*	$Id: arc-rimi.c,v 1.5 1997/11/09 11:04:57 mj Exp $

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

/**************************************************************************/

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
 * Turn this on if you have timing-sensitive DMA (ie. a tape drive) and
 * would like to sacrifice a little bit of network speed to reduce tape
 * write retries or some related problem.
 */
#undef SLOW_XMIT_COPY


/* Internal function declarations */

static int arcrimi_probe(struct device *dev);
static void arcrimi_rx(struct device *dev,int recbuf);
static int arcrimi_found(struct device *dev,int ioaddr,int airq,u_long shmem);
static void arcrimi_inthandler (struct device *dev);
static int arcrimi_reset (struct device *dev, int reset_delay);
static void arcrimi_setmask (struct device *dev, u_char mask);
static void arcrimi_command (struct device *dev, u_char command);
static u_char arcrimi_status (struct device *dev);
static void arcrimi_prepare_tx(struct device *dev,u_char *hdr,int hdrlen,
       	       char *data,int length,int daddr,int exceptA, int offset);
static  void arcrimi_openclose(int open);


/* Module parameters */

#ifdef MODULE
static int shmem=0x0;	/* <--- EDIT THESE LINES FOR YOUR CONFIGURATION */
static int irq=0;	/* or use the insmod io= irq= shmem= options */
static char *device;	/* use eg. device="arc1" to change name */
static int node=0;	/* you must specify the node ID for RIM I cards */

MODULE_PARM(shmem, "i");
MODULE_PARM(irq, "i");
MODULE_PARM(device, "s");
MODULE_PARM (node, "i");
#else
__initfunc(void arcrimi_setup (char *str, int *ints));
extern struct device arcnet_devs[];
extern char arcnet_dev_names[][10];
extern int arcnet_num_devs;
#endif

/* Handy defines for ARCnet specific stuff */

/* COM 9026 controller chip --> ARCnet register addresses */
#define _INTMASK (ioaddr+0)	/* writable */
#define _STATUS  (ioaddr+0)	/* readable */
#define _COMMAND (ioaddr+1)	/* writable, returns random vals on read (?) */
#define _RESET  (ioaddr+8)	/* software reset (on read) */
#define _MEMDATA  (ioaddr+12)            /* Data port for IO-mapped memory */
#define _ADDR_HI  (ioaddr+15)            /* Control registers for said */
#define _ADDR_LO  (ioaddr+14)
#define _CONFIG  (ioaddr+2)      /* Configuration register */

#define RDDATAflag      0x00     /* Next access is a read/~write */

#define ARCSTATUS	readb(_STATUS)
#define ACOMMAND(cmd) writeb((cmd),_COMMAND)
#define ARCRESET	writeb(TESTvalue,ioaddr-0x800)	/* fake reset */
#define AINTMASK(msk)	writeb((msk),_INTMASK)
#define SETCONF	writeb(lp->config,_CONFIG)

static const char *version =
"arc-rimi.c: v3.00 97/11/09 Avery Pennarun <apenwarr@worldvisions.ca> et al.\n";

/****************************************************************************
 *                                                                          *
 * Probe and initialization                                                 *
 *                                                                          *
 ****************************************************************************/


/* We cannot probe for a RIM I card; one reason is I don't know how to reset
 * them.  In fact, we can't even get their node ID automatically.  So, we
 * need to be passed a specific shmem address, IRQ, and node ID.
 */
__initfunc(int arcrimi_probe(struct device *dev))
{
  BUGLVL(D_NORMAL) printk(version);
  BUGMSG(D_NORMAL,"Given: node %02Xh, shmem %lXh, irq %d\n",
	 dev->dev_addr[0],dev->mem_start,dev->irq);

  if (dev->mem_start<=0 || dev->irq<=0)
    {
      BUGMSG(D_NORMAL,"No autoprobe for RIM I; you "
	     "must specify the shmem and irq!\n");
      return -ENODEV;
    }

  if (dev->dev_addr[0]==0)
    {
      BUGMSG(D_NORMAL,"You need to specify your card's station "
	     "ID!\n");
      return -ENODEV;
    }

  return arcrimi_found(dev,dev->dev_addr[0],dev->irq,dev->mem_start);
}


/* Set up the struct device associated with this card.  Called after
 * probing succeeds.
 */
__initfunc(int arcrimi_found(struct device *dev,int node,int airq, u_long shmem))
{
  struct arcnet_local *lp;
  u_long first_mirror,last_mirror;
  int mirror_size;

  /* reserve the irq */
  if (request_irq(airq,&arcnet_interrupt,0,"arcnet (RIM I)",dev))
    {
      BUGMSG(D_NORMAL,"Can't get IRQ %d!\n",airq);
      return -ENODEV;
    }
  dev->irq=airq;

  dev->base_addr=0;
  writeb(TESTvalue,shmem);
  writeb(node,shmem+1);	/* actually the node ID */

  /* find the real shared memory start/end points, including mirrors */
#define BUFFER_SIZE (512)
#define MIRROR_SIZE (BUFFER_SIZE*4)

  /* guess the actual size of one "memory mirror" - the number of
   * bytes between copies of the shared memory.  On most cards, it's
   * 2k (or there are no mirrors at all) but on some, it's 4k.
   */
  mirror_size=MIRROR_SIZE;
  if (readb(shmem)==TESTvalue
      && readb(shmem-mirror_size)!=TESTvalue
      && readb(shmem-2*mirror_size)==TESTvalue)
    mirror_size*=2;

  first_mirror=last_mirror=shmem;
  while (readb(first_mirror)==TESTvalue) first_mirror-=mirror_size;
  first_mirror+=mirror_size;

  while (readb(last_mirror)==TESTvalue) last_mirror+=mirror_size;
  last_mirror-=mirror_size;

  dev->mem_start=first_mirror;
  dev->mem_end=last_mirror+MIRROR_SIZE-1;
  dev->rmem_start=dev->mem_start+BUFFER_SIZE*0;
  dev->rmem_end=dev->mem_start+BUFFER_SIZE*2-1;

  /* Initialize the rest of the device structure. */

  dev->priv = kmalloc(sizeof(struct arcnet_local), GFP_KERNEL);
  if (dev->priv == NULL)
    {
      free_irq(airq,dev);
      return -ENOMEM;
    }
  memset(dev->priv,0,sizeof(struct arcnet_local));
  lp=(struct arcnet_local *)(dev->priv);
  lp->card_type = ARC_RIM_I;
  lp->card_type_str = "RIM I";
  lp->arcnet_reset=arcrimi_reset;
  lp->asetmask=arcrimi_setmask;
  lp->astatus=arcrimi_status;
  lp->acommand=arcrimi_command;
  lp->openclose_device=arcrimi_openclose;
  lp->prepare_tx=arcrimi_prepare_tx;
  lp->inthandler=arcrimi_inthandler;

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

  /* get and check the station ID from offset 1 in shmem */
  lp->stationid = readb(first_mirror+1);

  if (lp->stationid==0)
    BUGMSG(D_NORMAL,"WARNING!  Station address 00 is reserved "
	   "for broadcasts!\n");
  else if (lp->stationid==255)
    BUGMSG(D_NORMAL,"WARNING!  Station address FF may confuse "
	   "DOS networking programs!\n");
  dev->dev_addr[0]=lp->stationid;

  BUGMSG(D_NORMAL,"ARCnet RIM I: station %02Xh found at IRQ %d, "
	 "ShMem %lXh (%ld*%d bytes).\n",
	 lp->stationid,
	 dev->irq, dev->mem_start,
	 (dev->mem_end-dev->mem_start+1)/mirror_size,mirror_size);

  return 0;
}


/* Do a hardware reset on the card, and set up necessary registers.
 *
 * This should be called as little as possible, because it disrupts the
 * token on the network (causes a RECON) and requires a significant delay.
 *
 * However, it does make sure the card is in a defined state.
 */
int arcrimi_reset(struct device *dev,int reset_delay)
{
  struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
  short ioaddr=dev->mem_start + 0x800;
  int recbuf=lp->recbuf;

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

  ACOMMAND(CFLAGScmd|RESETclear); /* clear flags & end reset */
  ACOMMAND(CFLAGScmd|CONFIGclear);

  /* clear out status variables */
  recbuf=lp->recbuf=0;
  lp->txbuf=2;

  /* enable extended (512-byte) packets */
  ACOMMAND(CONFIGcmd|EXTconf);

#ifndef SLOW_XMIT_COPY
  /* clean out all the memory to make debugging make more sense :) */
  BUGLVL(D_DURING)
    memset_io(dev->mem_start,0x42,2048);
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


static void arcrimi_openclose(int open)
{
  if (open)
    MOD_INC_USE_COUNT;
  else
    MOD_DEC_USE_COUNT;
}

static void arcrimi_setmask(struct device *dev, u_char mask)
{
  int ioaddr=dev->mem_start+0x800;

  AINTMASK(mask);
}

static u_char arcrimi_status(struct device *dev)
{
  int ioaddr=dev->mem_start+0x800;

  return ARCSTATUS;
}

static void arcrimi_command(struct device *dev, u_char cmd)
{
  int ioaddr=dev->mem_start+0x800;

  ACOMMAND(cmd);
}


/* The actual interrupt handler routine - handle various IRQ's generated
 * by the card.
 */
static void
arcrimi_inthandler(struct device *dev)
{
  struct arcnet_local *lp=(struct arcnet_local *)dev->priv;
  int ioaddr=dev->mem_start+0x800, status, boguscount = 3, didsomething;

  AINTMASK(0);

  BUGMSG(D_DURING,"in arcrimi_inthandler (status=%Xh, intmask=%Xh)\n",
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
	  arcrimi_reset(dev,0);

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
	  arcrimi_rx(dev,!recbuf);

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
arcrimi_rx(struct device *dev,int recbuf)
{
  struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
  int ioaddr=dev->mem_start+0x800;
  union ArcPacket *arcpacket=
    (union ArcPacket *)phys_to_virt(dev->mem_start+recbuf*512);
  u_char *arcsoft;
  short length,offset;
  u_char daddr,saddr;

  lp->stats.rx_packets++;

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

  arcnet_rx(lp, arcsoft, length, saddr, daddr);

  BUGLVL(D_RX) arcnet_dump_packet(lp->adev,arcpacket->raw,length>240,"rx");

#ifndef SLOW_XMIT_COPY
  /* clean out the page to make debugging make more sense :) */
  BUGLVL(D_DURING)
    memset((void *)arcpacket->raw,0x42,512);
#endif
}


/* Given an skb, copy a packet into the ARCnet buffers for later transmission
 * by arcnet_go_tx.
 */
static void
arcrimi_prepare_tx(struct device *dev,u_char *hdr,int hdrlen,
		   char *data,int length,int daddr,int exceptA, int offset)
{
  struct arcnet_local *lp = (struct arcnet_local *)dev->priv;
  union ArcPacket *arcpacket =
    (union ArcPacket *)phys_to_virt(dev->mem_start+512*(lp->txbuf^1));

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
    memset_io(dev->mem_start+lp->txbuf*512,0x42,512);
#endif

  arcpacket->hardheader.destination=daddr;

  /* load packet into shared memory */
  if (length<=MTU)	/* Normal (256-byte) Packet */
    arcpacket->hardheader.offset1=offset=offset?offset:256-length;

  else if (length>=MinTU || offset)	/* Extended (512-byte) Packet */
    {
      arcpacket->hardheader.offset1=0;
      arcpacket->hardheader.offset2=offset=offset?offset:512-length;
    }
  else if (exceptA)		/* RFC1201 Exception Packet */
    {
      arcpacket->hardheader.offset1=0;
      arcpacket->hardheader.offset2=offset=512-length-4;

      /* exception-specific stuff - these four bytes
       * make the packet long enough to fit in a 512-byte
       * frame.
       */

      arcpacket->raw[offset+0]=hdr[0];
      arcpacket->raw[offset+1]=0xFF; /* FF flag */
      arcpacket->raw[offset+2]=0xFF; /* FF padding */
      arcpacket->raw[offset+3]=0xFF; /* FF padding */
      offset+=4;
    }
  else				/* "other" Exception packet */
    {
      /* RFC1051 - set 4 trailing bytes to 0 */
      memset(&arcpacket->raw[508],0,4);

      /* now round up to MinTU */
      arcpacket->hardheader.offset1=0;
      arcpacket->hardheader.offset2=offset=512-MinTU;
    }


  /* copy the packet into ARCnet shmem
   *  - the first bytes of ClientData header are skipped
   */

  memcpy((u_char*)arcpacket+offset, (u_char*)hdr,hdrlen);
#ifdef SLOW_XMIT_COPY
  for (iptr=data,iend=iptr+length-hdrlen,optr=(char *)arcpacket+offset+hdrlen;
       iptr<iend; iptr++,optr++)
    {
      *optr=*iptr;
      /*udelay(5);*/
    }
#else
  memcpy((u_char*)arcpacket+offset+hdrlen, data,length-hdrlen);
#endif

  BUGMSG(D_DURING,"transmitting packet to station %02Xh (%d bytes)\n",
	 daddr,length);

  BUGLVL(D_TX) arcnet_dump_packet(dev,arcpacket->raw,length>MTU,"tx");

  lp->lastload_dest=daddr;
  lp->txready=lp->txbuf;	/* packet is ready for sending */
}


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
  0, 0, 0, NULL, arcrimi_probe
};


int init_module(void)
{
  struct device *dev=&thiscard;
  if (device)
    strcpy(dev->name,device);
  else arcnet_makename(dev->name);

  if (node && node != 0xff)
    dev->dev_addr[0]=node;

  dev->irq=irq;
  if (dev->irq==2) dev->irq=9;

  if (shmem)
    {
      dev->mem_start=shmem;
      dev->mem_end=thiscard.mem_start+512*4-1;
      dev->rmem_start=thiscard.mem_start+512*0;
      dev->rmem_end=thiscard.mem_start+512*2-1;
    }

  if (register_netdev(dev) != 0)
    return -EIO;
  arcnet_use_count(1);
  return 0;
}

void cleanup_module(void)
{
  struct device *dev=&thiscard;
  int ioaddr=dev->mem_start;

  if (dev->start) (*dev->stop)(dev);

  /* Flush TX and disable RX */
  if (ioaddr)
    {
      AINTMASK(0);		/* disable IRQ's */
      ACOMMAND(NOTXcmd);	/* stop transmit */
      ACOMMAND(NORXcmd);	/* disable receive */
    }

  if (dev->irq)
    {
      free_irq(dev->irq,dev);
    }

  unregister_netdev(dev);
  kfree(dev->priv);
  dev->priv = NULL;
  arcnet_use_count(0);
}

#else

__initfunc(void arcrimi_setup (char *str, int *ints))
{
  struct device *dev;

  if (arcnet_num_devs == MAX_ARCNET_DEVS)
    {
      printk("ARCnet RIM I: Too many ARCnet devices registered (max %d).\n",
	     MAX_ARCNET_DEVS);
      return;
    }

  dev=&arcnet_devs[arcnet_num_devs];

  if (ints[0] < 3)
    {
      printk("ARCnet RIM I: You must give address, IRQ and node ID.\n");
      return;
    }

  dev->init=arcrimi_probe;

  switch(ints[0])
    {
    case 4: /* ERROR */
      printk("ARCnet RIM I: Too many arguments.\n");

    case 3: /* Node ID */
      dev->dev_addr[0]=(u_char)ints[3];

    case 2: /* IRQ */
      dev->irq=ints[2];

    case 1: /* Mem address */
      dev->mem_start=ints[1];
    }

  dev->name = (char *)&arcnet_dev_names[arcnet_num_devs];

  if (str)
    strncpy(dev->name, str, 9);

  arcnet_num_devs++;
}

#endif /* MODULE */
