/* ibmtr.c:  A shared-memory IBM Token Ring 16/4 driver for linux */
/*
	Written 1993 by Mark Swanson and Peter De Schrijver.
	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	This device driver should work with Any IBM Token Ring Card that does
   not use DMA.

	I used Donald Becker's (becker@super.org) device driver work
	as a base for most of my initial work.
*/

/*
   Changes by Peter De Schrijver (Peter.Deschrijver@linux.cc.kuleuven.ac.be) :
	
	+ changed name to ibmtr.c in anticipation of other tr boards.
	+ changed reset code and adapter open code.
	+ added SAP open code.
	+ a first attempt to write interrupt, transmit and receive routines.

   Changes by David W. Morris (dwm@shell.portal.com) :
     941003 dwm: - Restructure tok_probe for multiple adapters, devices
                 - Add comments, misc reorg for clarity
                 - Flatten interrupt handler levels

   Changes by Farzad Farid (farzy@zen.via.ecp.fr)
   and Pascal Andre (andre@chimay.via.ecp.fr) (March 9 1995) :
        - multi ring support clean up
        - RFC1042 compliance enhanced

   Changes by Pascal Andre (andre@chimay.via.ecp.fr) (September 7 1995) :
        - bug correction in tr_tx
        - removed redundant information display
        - some code reworking

   Chagnes by Steve Kipisz (bungy@ibm.net or kipisz@vnet.ibm.com)
                           (January 18 1996):
        - swapped WWOR and WWCR in ibmtr.h
        - moved some init code from tok_probe into trdev_init.  The
          PCMCIA code can call trdev_init to complete initializing
          the driver.
	- added -DPCMCIA to support PCMCIA
	- detecting PCMCIA Card Removal in interrupt handler.  if
	  ISRP is FF, then a PCMCIA card has been removed

   Warnings !!!!!!!!!!!!!!
      This driver is only partially sanitized for support of multiple
      adapters.  It will almost definately fail if more than one
      active adapter is identified.
*/
	
#ifdef PCMCIA
#define MODULE
#endif

#include <linux/module.h>

#ifdef PCMCIA
#undef MODULE
#endif

#define NO_AUTODETECT 1
#undef NO_AUTODETECT
#undef ENABLE_PAGING

#define FALSE 0
#define TRUE (!FALSE)

/* changes the output format of driver initialisation */
#define TR_NEWFORMAT	1

/* some 95 OS send many non UI frame; this allow removing the warning */
#define TR_FILTERNONUI	1

/* version and credits */
static const char *version = "ibmtr.c: v1.3.24 8/7/94 Peter De Schrijver and Mark Swanson\n"
"   modified 10/3/94 DW Morris, 3/9/95 F Farid and P Andre, 9/7/95 P Andre\n";

static char pcchannelid[]={0x05, 0x00, 0x04, 0x09,
			 0x04, 0x03, 0x04, 0x0f,
			 0x03, 0x06, 0x03, 0x01,
			 0x03, 0x01, 0x03, 0x00,
			 0x03, 0x09, 0x03, 0x09,
			 0x03, 0x00, 0x02, 0x00};
static char mcchannelid[]={0x04, 0x0d, 0x04, 0x01,
			 0x05, 0x02, 0x05, 0x03,
			 0x03, 0x06, 0x03, 0x03,
			 0x05, 0x08, 0x03, 0x04,
			 0x03, 0x05, 0x03, 0x01,
			 0x03, 0x08, 0x02, 0x00};

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/in.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/trdevice.h>
#include <stddef.h>
#include "ibmtr.h"


#define DPRINTK(format, args...) printk("%s: " format, dev->name , ## args)
#define DPRINTD(format, args...) DummyCall("%s: " format, dev->name , ## args)

#ifdef TR_NEWFORMAT
/* this allows displaying full adapter information */
const char *channel_def[] = { "ISA", "MCA", "ISA P&P" };
char *adapter_def(char type)
{
       switch (type)
       {
               case 0xF : return "Adapter/A";
               case 0xE : return "16/4 Adapter/II";
               default : return "adapter";
       };
};
#endif

#if 0
struct tok_info tok_info1;   /*  WARNING: this area must be replicated
                                 or 'malloced' to support > 1 adapter */

static struct wait_queue *wait_for_tok_int=NULL, *wait_for_reset;
void (*do_tok_int)(struct device *dev)=NULL;
#endif

#ifndef TR_NEWFORMAT
unsigned char ibmtr_debug_trace=1;  /*  Patch or otherwise alter to
                                         control tokenring tracing.  */
#else
unsigned char ibmtr_debug_trace=0;
#endif
#define TRC_INIT 0x01              /*  Trace initialization & PROBEs */
#define TRC_INITV 0x02             /*  verbose init trace points     */

static short TokBaseAddrs[]={MMIOStartLocP, /* Addr-s to scan */
                             MMIOStartLocA};


int tok_probe(struct device *dev);
unsigned char get_sram_size(struct tok_info *adapt_info);

static int  tok_init_card(struct device *dev);
int trdev_init(struct device *dev);
void tok_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static void initial_tok_int(struct device *dev);

static void open_sap(unsigned char type,struct device *dev);
void tok_open_adapter(unsigned long dev_addr);
static void tr_rx(struct device *dev);
static void tr_tx(struct device *dev);

static int tok_open(struct device *dev);
static int tok_close(struct device *dev);
static int tok_send_packet(struct sk_buff *skb, struct device *dev);
static struct enet_statistics * tok_get_stats(struct device *dev);

static struct timer_list tr_timer={NULL,NULL,0,0L,tok_open_adapter};

int DummyCallCount=0;

/*  This routine combined with the #DEFINE DPRINTD serves
    to workaround the gcc apparent bug.   in tr_tx() */

static void DummyCall(const char * fmt,...) {DummyCallCount++;return;}

static void PrtChanID(char *pcid, short stride) {
  short i, j;
  for (i=0,j=0;i<24;i++,j=j+stride) printk("%1x",((int) pcid[j])&0x0f);
  printk("\n");
}

/* tok_probe():  Routine specified in the network device structure
          to probe for an IBM Token Ring Adapter.  Routine outline:
          I.  Interrogate hardware to determine if an adapter exists
              and what the speeds and feeds are
         II.  Setup data structures to control execution based upon
              adapter characteristics.
         III. Initialize adapter operation
     We expect tok_probe to be called once for each device entry
     which references it.
 */

int tok_probe(struct device *dev)
{

  unsigned char segment=0, intr=0, irq=0, i=0, j=0,
                cardpresent=NOTOK,temp=0;
  unsigned char *t_mmio=0;
  short PIOaddr=0, iAddr;
  struct tok_info *ti=0;
  static struct tok_info *badti=0;  /* if fail after kmalloc, reuse */

  static unsigned char Shared_Ram_Base = IBMTR_SHARED_RAM_BASE;

  /*  this is the major adapter probe loop.  For each call to tok_probe,
      we try each remaining entry in TokBaseAddrs[] as a possible
      adapter.  Once an entry is rejected or assigned, we zero it to
      avoid duplicate use or worthless trial for the tok probe call*/

  for (iAddr=0;
       iAddr < (sizeof(TokBaseAddrs)/sizeof(short))&&PIOaddr==0;
       iAddr++) { char *tchanid, *cd_chanid, ctemp;
    PIOaddr=TokBaseAddrs[iAddr];  /* address to try           */
    TokBaseAddrs[iAddr] = 0;      /* (and marked already used */
    if (PIOaddr == 0) continue;   /* already tried this addr */
    if ( check_region(PIOaddr,TR_IO_EXTENT) ) { /* Make sure PIO address not
						   already assigned elsewhere
						   before we muck with I/O
						   addresses */
      if (ibmtr_debug_trace & TRC_INIT)
	DPRINTK("check_region(%4hx,%d) failed.\n",PIOaddr, TR_IO_EXTENT);
      PIOaddr = 0; continue; /* clear to flag fail and try next */
    }
           /*  Query the adapter PIO base port which will return
               indication of where MMIO was placed (per tech ref
               this assignment is done by BIOS - what is rational for
               where it is?).  We also have a coded interrupt address.*/

    segment = inb(PIOaddr);
    if (segment < 0x40 || segment > 0xe0) { /* out of range values
                            so we will assume non-existant IO dev */
      PIOaddr = 0; continue; /* clear to flag fail and try next */
    }

                 /*  Compute the linear base address of the MMIO area
                     as LINUX doesn't care about segments          */
    t_mmio=(char *) (((segment & 0xfc) << 11) + 0x80000);
    intr = segment & 0x03;   /* low bits is coded interrupt # */
    if (ibmtr_debug_trace & TRC_INIT)
      DPRINTK("PIOaddr: %4hx seg/intr: %2x mmio base: %p intr: %d\n",
              PIOaddr, (int) segment,t_mmio,(int) intr);
    /*  Now we will compare expected 'channelid' strings with
        what we is there to learn of ISA/MCA or not TR card */
        /*  !!!WARNING:!!!! It seems pretty silly to blunder ahead
            w/o verification that the mmio address we have found
            is valid storage -- perhaps this is tolerable for current
            hardware state??? */
    cd_chanid = (char *)(CHANNEL_ID + t_mmio);  /* for efficiency */
    tchanid=pcchannelid; cardpresent=TR_ISA;  /* try ISA ? */
                       /* suboptimize knowing first byte different */
    ctemp = (* cd_chanid) & 0x0f;
    if (  ctemp != *tchanid) { /* NOT ISA card, try MCA */
      tchanid=mcchannelid; cardpresent=TR_MCA;
      if (  ctemp != *tchanid)  /* Neither ISA nor MCA */
        cardpresent=NOTOK;
    }
    if (cardpresent != NOTOK) { /* know presumed type, try rest of ID */
      for (i=2,j=1; i<=46; i=i+2,j++) {
        if ( (cd_chanid[i] & 0x0f) != tchanid[j]) {
          cardpresent=NOTOK;   /* match failed, not TR card */
          break;
        }
      }
    }

    /* If we have an ISA board check for the ISA P&P version, as it has
       different IRQ settings */
    if (cardpresent == TR_ISA && (*(AIPFID + t_mmio)==0x0e))
	cardpresent=TR_ISAPNP;

    if (cardpresent == NOTOK) { /* "channel_id" did not match, report */
      if (ibmtr_debug_trace & TRC_INIT) {
        DPRINTK("Channel ID string not found for PIOaddr: %4hx\n",
                PIOaddr);
        DPRINTK("Expected for ISA: ");  PrtChanID(pcchannelid,1);
        DPRINTK("           found: ");  PrtChanID(cd_chanid,2);
        DPRINTK("Expected for MCA: ");  PrtChanID(mcchannelid,1);
      }
      PIOaddr = 0;  /* all to know not found yet */
      continue;
    }

   /* !!!! we could tighten validation by checking the HW Address
      against the 1-s complement..  Move the get HW logic to here */

  }

  /*   The search loop has either completed with a presumed TR adapter
       or none found.  Check situation ... march on if possible */

  if (PIOaddr == 0) { /* failed to find a valid TR adapter */
    if (ibmtr_debug_trace & TRC_INIT)
         DPRINTK("Unable to assign adapter to device.\n");
    return ENODEV;
  }

  /*?? Now, allocate some of the pl0 buffers for this driver.. */

  if (!badti) {
    ti = (struct tok_info *)kmalloc(sizeof(struct tok_info), GFP_KERNEL);
    if (ti == NULL)
          return -ENOMEM;
  } else {
    ti = badti; badti = NULL;
  }/*?? dev->priv = kmalloc(sizeof(struct net_local), GFP_KERNEL); */

  memset(ti,0,sizeof(struct tok_info));

  ti->mmio= t_mmio;

  dev->priv = ti;     /* this seems like the logical use of the
                         field ... lets try some empirical tests
                         using the token-info structure -- that
                         should fit with out future hope of multiple
                         adapter support as well /dwm   */

  switch (cardpresent) {
    case TR_ISA:
      if (intr==0) irq=9; /* irq2 really is irq9 */
      if (intr==1) irq=3;
      if (intr==2) irq=6;
      if (intr==3) irq=7;
      ti->global_int_enable=GLOBAL_INT_ENABLE+((irq==9) ? 2 : irq);
      ti->sram=NULL;
#ifndef TR_NEWFORMAT
      DPRINTK("ti->global_int_enable: %04X\n",ti->global_int_enable);
#endif
      break;
    case TR_MCA:
      if (intr==0) irq=9;
      if (intr==1) irq=3;
      if (intr==2) irq=10;
      if (intr==3) irq=11;
      ti->global_int_enable=0;
      ti->sram=(unsigned char *)((inb(PIOaddr+ADAPTRESETREL) & 0xfe)
                                  << 12);
      break;
    case TR_ISAPNP:
      if (intr==0) irq=9;
      if (intr==1) irq=3;
      if (intr==2) irq=10;
      if (intr==3) irq=11;
      while(!(ti->mmio + ACA_OFFSET + ACA_RW + RRR_EVEN));
      ti->sram=(unsigned char *)((unsigned long)(*(ti->mmio + ACA_OFFSET + ACA_RW + RRR_EVEN)) <<12);
      ti->global_int_enable=PIOaddr+ADAPTINTREL;
      break;
      
  }

  if (ibmtr_debug_trace & TRC_INIT) { /* just report int */
    DPRINTK("irq=%d",irq);
    if (ibmtr_debug_trace & TRC_INITV) { /* full chat in verbose only */
      DPRINTK(", ti->mmio=%p",ti->mmio);
      printk(", segment=%02X",segment);
    }
    printk(".\n");
  }

  /* Get hw address of token ring card */
#ifndef TR_NEWFORMAT 
  DPRINTK("hw address: ");
#endif
  j=0;
  for (i=0; i<0x18; i=i+2) {
    temp = *(char *)((ulong)AIP + (ulong)i + ti->mmio) & 0x0f; /* Tech ref states must do this */
#ifndef TR_NEWFORMAT
    printk("%1X",ti->hw_address[j]=temp);
#else
    ti->hw_address[j]=temp;
#endif
    if(j&1)
      dev->dev_addr[(j/2)]=ti->hw_address[j]+(ti->hw_address[j-1]<<4);
    ++j;
  }
#ifndef TR_NEWFORMAT
  printk("\n");
#endif


  /* get Adapter type:  'F' = Adapter/A, 'E' = 16/4 Adapter II,...*/
  ti->adapter_type = *(char *)(ti->mmio + AIPADAPTYPE);

  /* get Data Rate:  F=4Mb, E=16Mb, D=4Mb & 16Mb ?? */
  ti->data_rate = *(char *)(ti->mmio + AIPDATARATE);

  /* Get Early Token Release support?: F=no, E=4Mb, D=16Mb, C=4&16Mb */
  ti->token_release = *(char *)(ti->mmio + AIPEARLYTOKEN);

  /* How much shared RAM is on adapter ? */
  ti->avail_shared_ram = get_sram_size(ti);

  /* We need to set or do a bunch of work here based on previous results.. */
  /* Support paging?  What sizes?:  F=no, E=16k, D=32k, C=16 & 32k */
  ti->shared_ram_paging = *(char *)(ti->mmio + AIPSHRAMPAGE);

  /* Available DHB  4Mb size:   F=2048, E=4096, D=4464 */
  ti->dhb_size4mb = *(char *) (ti->mmio + AIP4MBDHB);

  /* Available DHB 16Mb size:  F=2048, E=4096, D=8192, C=16384, B=17960 */
  ti->dhb_size16mb = *(char *)(ti->mmio + AIP16MBDHB);

#ifndef TR_NEWFORMAT
  DPRINTK("atype=%x, drate=%x, trel=%x, asram=%dK, srp=%x, dhb(4mb=%x, 16mb=%x)\n",ti->adapter_type,
	  ti->data_rate, ti->token_release, ti->avail_shared_ram/2, ti->shared_ram_paging,
	  ti->dhb_size4mb, ti->dhb_size16mb);
#endif

  /* We must figure out how much shared memory space this adapter
     will occupy so that if there are two adapters we can fit both
     in.  Given a choice, we will limit this adapter to 32K.  The
     maximum space will will use for two adapters is 64K so if the
     adapter we are working on demands 64K (it also doesn't support
     paging), then only one adapter can be supported.  */

  /* determine how much of total RAM is mapped into PC space */
  ti->mapped_ram_size=1<<(((*(unsigned char *)
                       (ti->mmio+ ACA_OFFSET + ACA_RW + RRR_ODD))>>2)+4);
  ti->page_mask=0;
  if (ti->shared_ram_paging == 0xf) { /* No paging in adapter */
    ti->mapped_ram_size = ti->avail_shared_ram;
  } else {
#ifdef ENABLE_PAGING
    unsigned char pg_size;
#endif

#ifndef TR_NEWFORMAT
    DPRINTK("shared ram page size: %dK\n",ti->mapped_ram_size/2);
#endif
#ifdef ENABLE_PAGING
    switch(ti->shared_ram_paging) {
      case 0xf:
        break;
      case 0xe:
	ti->page_mask=(ti->mapped_ram_size==32) ? 0xc0 : 0;
	pg_size=32;   /* 16KB page size */
	break;
      case 0xd:
	ti->page_mask=(ti->mapped_ram_size==64) ? 0x80 : 0;
	pg_size=64;   /* 32KB page size */
	break;
      case 0xc:
	ti->page_mask=(ti->mapped_ram_size==32) ? 0xc0 : 0;
	ti->page_mask=(ti->mapped_ram_size==64) ? 0x80 : 0;
	DPRINTK("Dual size shared RAM page (code=0xC), don't support it!\n");
	/* nb/dwm: I did this because RRR (3,2) bits are documented as
	 * R/O and I can't find how to select which page size
	 * Also, the above conditional statement sequence is invalid
	 *       as page_mask will always be set by the second stmt
	 */
	badti=ti;
	break;
      default:
	DPRINTK("Unknown shared ram paging info %01X\n",ti->shared_ram_paging);
	badti=ti;    /* bail out if bad code */
	break;
    }
    if(ti->page_mask) {
      if(pg_size > ti->mapped_ram_size) {
	DPRINTK("Page size (%d) > mapped ram window (%d), can't page.\n",
		pg_size, ti->mapped_ram_size);
	ti->page_mask = 0;    /* reset paging */
      } else {
	ti->mapped_ram_size=ti->avail_shared_ram; /****** ?????????? *******/
	DPRINTK("Shared RAM paging enabled. Page size : %uK\n",((ti->page_mask^ 0xff)+1)>>2);
      }
    }
#else
#endif
  }

  if (cardpresent==TR_ISA) { /* finish figuring the shared RAM address */
    static unsigned char ram_bndry_mask[]={0xfe, 0xfc, 0xf8, 0xf0};
    unsigned char new_base, rrr_32, chk_base, rbm;
    rrr_32 = (*(unsigned char *)
                       (ti->mmio+ ACA_OFFSET + ACA_RW + RRR_ODD))>>2;
    rbm = ram_bndry_mask[rrr_32];
    new_base = (Shared_Ram_Base + (~rbm)) & rbm; /* up to boundary */
    chk_base = new_base + (ti->mapped_ram_size>>3);
    if (chk_base > (IBMTR_SHARED_RAM_BASE+IBMTR_SHARED_RAM_SIZE)) {
      DPRINTK("Shared RAM for this adapter (%05x) exceeds driver"
              " limit (%05x), adapter not started.\n",
              chk_base<<12, (IBMTR_SHARED_RAM_BASE+
                              IBMTR_SHARED_RAM_SIZE)<<12);
      badti=ti;
    } else {  /* seems cool, record what we have figured out */
      ti->sram_base = new_base;
      Shared_Ram_Base = new_base;
    }
  }

  /* dwm: irq and other final setup moved here so if we find other
        unrecognized values OR shared ram conflicts, we can still
        bail out in a rather benign fashion.    */

  if (badti) return ENODEV;
#ifndef TR_NEWFORMAT
  DPRINTK("Using %dK shared RAM\n",ti->mapped_ram_size/2);
#endif

  if (request_irq (dev->irq = irq, &tok_interrupt,0,"IBM TR", NULL) != 0) {
    DPRINTK("Could not grab irq %d.  Halting Token Ring driver.\n",irq);
    badti = ti;    /*  keep track of unused tok_info */
    return ENODEV;
  }
  irq2dev_map[irq]=dev;

  /*?? Now, allocate some of the PIO PORTs for this driver.. */
  request_region(PIOaddr,TR_IO_EXTENT,"ibmtr");  /* record PIOaddr range
						    as busy */
#ifndef TR_NEWFORMAT
   DPRINTK("%s",version); /* As we have passed card identification,
                             let the world know we're here! */
#else
  printk("%s",version);
  DPRINTK("%s %s found using irq %d, PIOaddr %4hx, %dK shared RAM.\n",
       channel_def[cardpresent-1], adapter_def(ti->adapter_type), irq,
        PIOaddr, ti->mapped_ram_size/2);
  DPRINTK("Hardware address : %02X:%02X:%02X:%02X:%02X:%02X\n",
       dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
        dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
#endif

  dev->base_addr=PIOaddr; /* set the value for device */

  trdev_init(dev);
  tok_init_card(dev);

  return 0;  /* Return 0 to indicate we have found a Token Ring card. */
}

/* query the adapter for the size of shared RAM  */

unsigned char get_sram_size(struct tok_info *adapt_info) {

	unsigned char avail_sram_code;
	static unsigned char size_code[]={ 0,16,32,64,127,128 };

           /*  Adapter gives
                'F' -- use RRR bits 3,2
                'E' -- 8kb   'D' -- 16kb
                'C' -- 32kb  'A' -- 64KB
                'B' - 64KB less 512 bytes at top
                      (WARNING ... must zero top bytes in INIT */

	avail_sram_code=0xf-*(adapt_info->mmio + AIPAVAILSHRAM);
	if(avail_sram_code)
		return size_code[avail_sram_code];
	else  /* for code 'F', must compute size from RRR(3,2) bits */

		return 1<<(((*(unsigned char *)
              (adapt_info->mmio+ ACA_OFFSET + ACA_RW + RRR_ODD))>>2)+4);
}


int trdev_init(struct device *dev)
{
  struct tok_info *ti=(struct tok_info *)dev->priv;

  ti->open_status=CLOSED;

  dev->init=tok_init_card;
  dev->open=tok_open;
  dev->stop=tok_close;
  dev->hard_start_xmit=tok_send_packet;
  dev->get_stats = NULL;
  dev->get_stats = tok_get_stats;
  dev->set_multicast_list = NULL;
  tr_setup(dev);

  return 0;
}


static int tok_open(struct device *dev) {

   struct tok_info *ti=(struct tok_info *)dev->priv;

   if(ti->open_status==CLOSED) {
      tok_init_card(dev);
   }

	if(ti->open_status==IN_PROGRESS) {
      sleep_on(&ti->wait_for_reset);
	}
	
   if(ti->open_status==SUCCES) {
      dev->tbusy=0;
      dev->interrupt=0;
      dev->start=1;
      /*  NEED to see smem size *AND* reset high 512 bytes if
          needed */

      MOD_INC_USE_COUNT;

      return 0;
   }
	else 
      return -EAGAIN;

}

static int tok_close(struct device *dev) {

  struct tok_info *ti=(struct tok_info *) dev->priv;

	struct srb_close_adapter *close_adapter=(struct srb_close_adapter *)ti->srb;

	close_adapter->command=DIR_CLOSE_ADAPTER;
	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD)=CMD_IN_SRB;

   ti->open_status=CLOSED;

	sleep_on(&ti->wait_for_tok_int);		

	if(close_adapter->ret_code)
		DPRINTK("close adapter failed: %02X\n",close_adapter->ret_code);
	
	MOD_DEC_USE_COUNT;

	return 0;
}

void tok_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{

	unsigned char status;
  struct tok_info *ti;
/*   int irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2); */
   struct device *dev = (struct device *)(irq2dev_map[irq]);
#if 0
   DPRINTK("Int from tok_driver, dev : %p\n",dev);
#endif
  ti=(struct tok_info *) dev->priv;

  switch (ti->do_tok_int) {
    case NOT_FIRST:

        /*  Begin the regular interrupt handler HERE inline to avoid
            the extra levels of logic and call depth for the
            original solution.   */


	dev->interrupt=1;

	/* Disable interrupts till processing is finished */
	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_EVEN)=(~INT_ENABLE);

	/* Reset interrupt for ISA boards */
	if(ti->global_int_enable)
		outb(0,ti->global_int_enable);
	
	status=*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RW + ISRP_ODD);

#ifdef PCMCIA
	/* Check if the PCMCIA card was pulled. */
	if (status == 0xFF)
	{
	   DPRINTK("PCMCIA card removed.\n");
	   dev->interrupt = 0;
	   return;
	}

	/* Check ISRP EVEN too. */
	if ( *(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RW + ISRP_EVEN) == 0xFF)
	{
	   DPRINTK("PCMCIA card removed.\n");
	   dev->interrupt = 0;
	   return;
	}
#endif

	if(status & ADAP_CHK_INT) {
		int i;
                unsigned char *check_reason=ti->sram + ntohs(*(unsigned short *)(ti->mmio + ACA_OFFSET + ACA_RW +WWCR_EVEN));

		DPRINTK("adapter check interrupt\n");

		DPRINTK("8 reason bytes follow: ");
		for(i=0;i<	8;i++,check_reason++)
			printk("%02X ",*check_reason);	
		printk("\n");

		*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD)=(~ADAP_CHK_INT);
		*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET  + ISRP_EVEN)=INT_ENABLE;
		dev->interrupt=0;
	}	

	else if((*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RW + ISRP_EVEN)) & (TCR_INT + ERR_INT + ACCESS_INT)) {

		DPRINTK("adapter error: ISRP_EVEN : %02x\n",
				*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RW + ISRP_EVEN));
				
		*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_EVEN)=~(TCR_INT + ERR_INT + ACCESS_INT);
		*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET  + ISRP_EVEN)=INT_ENABLE;
		dev->interrupt=0;
	}
					
	else if(status & (SRB_RESP_INT + ASB_FREE_INT + ARB_CMD_INT + SSB_RESP_INT)) {

		if(status & SRB_RESP_INT) {
			switch(*ti->srb) {
				case XMIT_DIR_FRAME: {
					struct srb_xmit *xmit=(struct srb_xmit *)(ti->srb);
					if(xmit->ret_code!=0xff) {
						DPRINTK("error on xmit_dir_frame request: %02X\n",xmit->ret_code);
						if(ti->current_skb) {
							dev_kfree_skb(ti->current_skb, FREE_WRITE);
							ti->current_skb=NULL;
						}
						dev->tbusy=0;
					}												
				}
				break;
				
				case XMIT_UI_FRAME: {
					struct srb_xmit *xmit=(struct srb_xmit *)(ti->srb);
					if(xmit->ret_code!=0xff) {
						DPRINTK("error on xmit_ui_frame request: %02X\n",xmit->ret_code);
						if(ti->current_skb) {
							dev_kfree_skb(ti->current_skb, FREE_WRITE);
							ti->current_skb=NULL;
						}
						dev->tbusy=0;
					}												
				}
				break;
	
				case DIR_OPEN_ADAPTER: {
					struct srb_open_response *open_response=(struct srb_open_response *)(ti->init_srb);

					ti->srb=ti->sram+ntohs(open_response->srb_addr);
					ti->ssb=ti->sram+ntohs(open_response->ssb_addr);
					ti->arb=ti->sram+ntohs(open_response->arb_addr);
					ti->asb=ti->sram+ntohs(open_response->asb_addr);
					ti->current_skb=NULL;

					if(open_response->ret_code==7) {
						if(!ti->auto_ringspeedsave && (open_response->error_code==0x24)) {
							DPRINTK("open failed: Adapter speed must match ring speed if Automatic Ring Speed Save is disabled\n");
							ti->open_status=FAILURE;
							wake_up(&ti->wait_for_reset);
						}
						else if(open_response->error_code==0x24) 
							DPRINTK("retrying open to adjust to ring speed\n");

						else if((open_response->error_code==0x2d) && ti->auto_ringspeedsave)
							DPRINTK("No signal detected for Auto Speed Detection\n");
						else DPRINTK("Unrecoverable error: error code = %02X\n",open_response->error_code);
					}
					else if(!open_response->ret_code) {
#ifndef TR_NEWFORMAT
						DPRINTK("board opened...\n");
#else
						DPRINTK("Adapter initialized and opened.\n");
#endif
						*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD)=~(SRB_RESP_INT);
						*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRA_ODD)=~(CMD_IN_SRB);
						open_sap(EXTENDED_SAP,dev);
/* YdW probably hates me */
						goto skip_reset;
					}
					else {	
						DPRINTK("open failed: ret_code = %02X, retrying\n",open_response->ret_code);
					}
					if(ti->open_status!=FAILURE) {
						tr_timer.expires=jiffies+TR_RETRY_INTERVAL;
						tr_timer.data=(unsigned long)dev;
						tr_timer.next=tr_timer.prev=NULL;
						add_timer(&tr_timer);
					}
				}
				break;

				case DIR_CLOSE_ADAPTER:
					wake_up(&ti->wait_for_tok_int);
					break;
				case DLC_OPEN_SAP: {
					struct dlc_open_sap *open_sap=(struct dlc_open_sap *)ti->srb;
					if(open_sap->ret_code) {
						DPRINTK("open_sap failed: ret_code = %02X,retrying\n",open_sap->ret_code);
						tr_timer.expires=jiffies+TR_RETRY_INTERVAL;
						tr_timer.data=(unsigned long)dev;
						tr_timer.next=tr_timer.prev=NULL;
						add_timer(&tr_timer);
					}
					else {
						ti->exsap_station_id=open_sap->station_id;
						ti->open_status=SUCCES; /* TR adapter is now available */
						wake_up(&ti->wait_for_reset);
					}
				}
				break;

				case DIR_INTERRUPT:
				case DIR_MOD_OPEN_PARAMS:	
				case DIR_SET_GRP_ADDR:
				case DIR_SET_FUNC_ADDR:
				case DLC_CLOSE_SAP: {
					struct srb_interrupt *intr=(struct srb_interrupt *)(ti->srb);
					if(intr->ret_code)
						DPRINTK("error on %02X: %02X\n",intr->command,intr->ret_code);	
				}
				break;

				case DIR_READ_LOG: {
					struct srb_read_log *read_log=(struct srb_read_log *)(ti->srb);
					if(read_log->ret_code)
						DPRINTK("error on dir_read_log: %02X\n",read_log->ret_code);
					else {
						DPRINTK("Line errors %02X, Internal errors %02X, Burst errors %02X\n",
                          read_log->line_errors,read_log->internal_errors,read_log->burst_errors);
						DPRINTK("A/C errors %02X, Abort delimiters %02X, Lost frames %02X\n",
                          read_log->A_C_errors,read_log->abort_delimiters,read_log->lost_frames);
						DPRINTK("Receive congestion count %02X, Frame copied errors %02X, Frequency errors %02X\n",
                          read_log->recv_congest_count,read_log->frame_copied_errors,read_log->frequency_errors);
						DPRINTK("Token errors %02X\n",read_log->token_errors);
					}
					dev->tbusy=0;
				}
				break;

				default:
					DPRINTK("Unknown command %02X encountered\n",*(ti->srb));
			}
			*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRA_ODD)=~(CMD_IN_SRB);
			*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD)=~(SRB_RESP_INT);
skip_reset:
		}

		if(status & ASB_FREE_INT) {
			switch(*ti->asb) {
				case REC_DATA:
				case XMIT_UI_FRAME:
				case XMIT_DIR_FRAME:
					if(*(ti->asb+2)!=0xff)
						DPRINTK("ASB error %02X in cmd %02X\n",	*(ti->asb+2),*(ti->asb));
					break;
				default:
					DPRINTK("unknown command in asb %02X\n",*ti->asb);
			}
			*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD)=~(ASB_FREE_INT);
		}

		if(status & ARB_CMD_INT) {
			switch(*ti->arb) {
				case DLC_STATUS: {
					struct arb_dlc_status *dlc_status=(struct arb_dlc_status *)(ti->arb);			
					DPRINTK("DLC_STATUS new status: %02X on station %02X\n",ntohs(dlc_status->status),ntohs(dlc_status->station_id));
				}
				break;

				case REC_DATA:
					tr_rx(dev);
					break;

				case RING_STAT_CHANGE: {
					struct arb_ring_stat_change *ring_stat_change=(struct arb_ring_stat_change *)(ti->arb);
					unsigned short ring_status=ntohs(ring_stat_change->ring_status);

					if(ring_status & (SIGNAL_LOSS + LOBE_FAULT)) {
						DPRINTK("Signal loss/Lobe fault\n");
						DPRINTK("We try to reopen the adapter.\n");	
						tr_timer.expires=jiffies+TR_RETRY_INTERVAL;
						tr_timer.data=(unsigned long)dev;
						tr_timer.next=tr_timer.prev=NULL;
						add_timer(&tr_timer);
					} else if (ring_status & (HARD_ERROR + XMIT_BEACON +
								  AUTO_REMOVAL + REMOVE_RECV + RING_RECOVER))
						DPRINTK("New ring status: %02X\n",ring_status);

					if(ring_status & LOG_OVERFLOW) {
						*(ti->srb)=DIR_READ_LOG;
						*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN)=INT_ENABLE;
						*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD)=CMD_IN_SRB;
						dev->tbusy=1; /* really srb busy... */
					}
				}
				break;

				case XMIT_DATA_REQ:
					tr_tx(dev);
					break;

				default:
					DPRINTK("Unknown command %02X in arb\n",*(ti->arb));
					break;
			}
			*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD)=~(ARB_CMD_INT);
			*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD)=ARB_FREE;
		}

		if(status & SSB_RESP_INT) {
			switch(*ti->ssb) {
				case XMIT_DIR_FRAME:
				case XMIT_UI_FRAME:
					if(*(ti->ssb+2))
						DPRINTK("xmit ret_code: %02X xmit error code: %02X\n",*(ti->ssb+2),*(ti->ssb+6));		
					else
						ti->tr_stats.tx_packets++;
					break;
				
				case XMIT_XID_CMD:
					DPRINTK("xmit xid ret_code: %02X\n",*(ti->ssb+2));
				
				default:
					DPRINTK("Unknown command %02X in ssb\n",*(ti->ssb));
			}
			*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD)=~(SSB_RESP_INT);
			*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD)=SSB_FREE;
		}
	}	

	dev->interrupt=0;
	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN)=INT_ENABLE;

	return;

      break;
    case FIRST_INT:
      initial_tok_int(dev);
      break;
    default:
      DPRINTK("Unexpected interrupt from tr adapter\n");
  }

}

static void initial_tok_int(struct device *dev) {

#ifndef TR_NEWFORMAT
  int i;
#endif
  unsigned char *encoded_addr;
  struct tok_info *ti;

  ti=(struct tok_info *) dev->priv;

	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_EVEN)=(~INT_ENABLE);
	
	/* Reset interrupt for ISA boards */
	if(ti->global_int_enable)
		outb(0,ti->global_int_enable);

	ti->do_tok_int=NOT_FIRST;

#ifndef TR_NEWFORMAT
	DPRINTK("Initial tok int received\n");
#endif

	if(!ti->sram) {  /* we assign the address for ISA devices */
			/* set RRR even to D000 for shared ram address */
		*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RW + RRR_EVEN)=
		                       ti->sram_base;
		ti->sram=(char *)(ti->sram_base << 12);
	}
	ti->init_srb=ti->sram+ntohs(*(unsigned short *)(ti->mmio+ ACA_OFFSET + WRBR_EVEN));
	SET_PAGE(ntohs(*(unsigned short *)(ti->mmio+ ACA_OFFSET + WRBR_EVEN)));

#if 0
	DPRINTK("init_srb(%p):",ti->init_srb);
	for(i=0;i<17;i++)
		printk("%02X ",*(ti->init_srb+i));
	printk("\n");
#endif

#ifndef TR_NEWFORMAT		
	DPRINTK("srb_init_response->encoded_address: %04X\n",((struct srb_init_response *)ti->init_srb)->encoded_address);
	DPRINTK("ntohs(srb_init_response->encoded_address): %04X\n",ntohs(((struct srb_init_response *)ti->init_srb)->encoded_address));
#endif
	encoded_addr=(unsigned char *)(ti->sram + ntohs(((struct srb_init_response *)ti->init_srb)->encoded_address));

#ifndef TR_NEWFORMAT
	DPRINTK("encoded addr (%04X,%04X,%p): ",
			((struct srb_init_response *)ti->init_srb)->encoded_address,
			ntohs(((struct srb_init_response *)ti->init_srb)->encoded_address),	
			encoded_addr);
#else
       DPRINTK("Initial interrupt : shared RAM located at %p.\n", encoded_addr);
#endif
	ti->auto_ringspeedsave=((struct srb_init_response *)ti->init_srb)->init_status_2 & 0x4 ? TRUE : FALSE;

#ifndef TR_NEWFORMAT
	for(i=0;i<TR_ALEN;i++)
		printk("%02X%s",dev->dev_addr[i]=encoded_addr[i],(i==TR_ALEN-1) ? "" : ":" );
	printk("\n");
#endif

	tok_open_adapter((unsigned long)dev);

}

static int tok_init_card(struct device *dev) {

   struct tok_info *ti;
	short PIOaddr;
        PIOaddr = dev->base_addr;
        ti=(struct tok_info *) dev->priv;

      /* Special processing for first interrupt after reset */
	ti->do_tok_int=FIRST_INT;

	/* Reset adapter */

	dev->tbusy=1; /* nothing can be done before reset and open completed */

#ifdef ENABLE_PAGING
	if(ti->page_mask) {
		*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RW + SRPR_EVEN)=SRPR_ENABLE_PAGING;
	}
#endif

	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_EVEN)=~(INT_ENABLE);
#ifndef TR_NEWFORMAT
	DPRINTK("resetting card\n");
#endif
	outb(0,PIOaddr+ADAPTRESET);
        udelay(50000);
	outb(0,PIOaddr+ADAPTRESETREL);
#ifndef TR_NEWFORMAT
	DPRINTK("card reset\n");
#endif

        ti->open_status=IN_PROGRESS;

	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN)=INT_ENABLE;
        return 0;
}

static void open_sap(unsigned char type,struct device *dev) {

  struct tok_info *ti=(struct tok_info *) dev->priv;
	struct dlc_open_sap *open_sap=(struct dlc_open_sap *)ti->srb;

	SET_PAGE(ti->srb);
	memset(open_sap,0,sizeof(struct dlc_open_sap));

	open_sap->command=DLC_OPEN_SAP;
	open_sap->max_i_field=htons(MAX_I_FIELD);
	open_sap->sap_options=SAP_OPEN_IND_SAP | SAP_OPEN_PRIORITY;
	open_sap->station_count=SAP_OPEN_STATION_CNT;
	open_sap->sap_value=type;

	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD)=CMD_IN_SRB;

}

void tok_open_adapter(unsigned long dev_addr) {

	struct device *dev=(struct device *)dev_addr;
	struct dir_open_adapter *open_adapter;
  struct tok_info *ti;
  ti=(struct tok_info *) dev->priv;

#ifndef TR_NEWFORMAT
	DPRINTK("now opening the board...\n");
#endif

	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD)=~(SRB_RESP_INT);
	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_RESET + ISRA_ODD)=~(CMD_IN_SRB);

	open_adapter=(struct dir_open_adapter *)(ti->init_srb);
	memset(open_adapter,0,sizeof(struct dir_open_adapter));

	open_adapter->command=DIR_OPEN_ADAPTER;
	open_adapter->open_options=htons(OPEN_PASS_BCON_MAC);
	open_adapter->num_rcv_buf=htons(NUM_RCV_BUF);
	open_adapter->rcv_buf_len=htons(RCV_BUF_LEN);
	open_adapter->dhb_length=htons(DHB_LENGTH);
	open_adapter->num_dhb=NUM_DHB;
	open_adapter->dlc_max_sap=DLC_MAX_SAP;
	open_adapter->dlc_max_sta=DLC_MAX_STA;

	ti->srb=ti->init_srb; /* We use this one in the interrupt handler */

	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN)=INT_ENABLE;
	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD)=CMD_IN_SRB;

}

static void tr_tx(struct device *dev) {

  struct tok_info *ti=(struct tok_info *) dev->priv;
	struct asb_xmit_resp *xmit_resp=(struct asb_xmit_resp *)ti->asb;
	struct arb_xmit_req *xmit_req=(struct arb_xmit_req *)ti->arb;
	struct srb_xmit *xmit=(struct srb_xmit *)ti->srb;
	struct trh_hdr *trhdr=(struct trh_hdr *)ti->current_skb->data;
	unsigned int hdr_len;
	unsigned char *dhb;

/* */
  DPRINTD("ti=%p asb=(%p,%p) arb=(%p,%p) srb=(%p,%p)\n",
       ti , ti->asb, xmit_resp, ti->arb, xmit_req, ti->srb, xmit);
/* */

#if 0
DPRINTK("transmitting...\n");
#endif

	if(xmit_resp->ret_code!=0xff)  DPRINTK("ASB not free !!!\n");

                     /*  in providing the transmit interrupts,
                         is telling us it is ready for data and
                         providing a shared memory address for us
                         to stuff with data.  Here we compute the
                         effective address where we will place data.*/
  dhb=ti->sram+ntohs(xmit_req->dhb_address);

	xmit_resp->command=xmit->command;
	xmit_resp->station_id=xmit_req->station_id;
	xmit_resp->rsap_value=EXTENDED_SAP;
	xmit_resp->cmd_corr=xmit_req->cmd_corr;
	xmit_resp->ret_code=0;

	if((xmit->command==XMIT_XID_CMD) || (xmit->command==XMIT_TEST_CMD)) {
		xmit_resp->frame_length=htons(0x11);
		xmit_resp->hdr_length=0x0e;
		dhb[0]=AC;
		dhb[1]=LLC_FRAME;
		memset(dhb+2,(int)0x0ff,TR_ALEN);
		memset(dhb+2+TR_ALEN,0,TR_ALEN);
		*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET
		                                         + ISRA_ODD)=RESP_IN_ASB;
		return;
	}

     /* the token ring packet is copied from sk_buff to the adapter
        buffer identified in the command data received with the
        interrupt.  The sk_buff area was set up with a maximum
        sized route information field so here we must compress
        out the extra (all) rif fields.   */
     /* nb/dwm .... I re-arranged code here to avoid copy of extra
        bytes, ended up with fewer statements as well             */

     /*  TR arch. identifies if RIF present by high bit of source
         address.  So here we check if RIF present */
  if(!(trhdr->saddr[0] & 0x80)) {
    hdr_len=sizeof(struct trh_hdr)-18;
#if 0
DPRINTK(("hdr_length: %d, frame length: %ld\n",hdr_len,
      ti->current_skb->len-18));
#endif
  }   /*  TR packet includes RIF data ... preserve it */
  else {
    hdr_len=((ntohs(trhdr->rcf)
             & TR_RCF_LEN_MASK)>>8)+sizeof(struct trh_hdr)-18;
#if 0
/* rework the following if activated, hdr_len <> rif_len */
DPRINTK("rcf: %02X rif_len: %d\n", trhdr->rcf,wrk_len);
DPRINTK("hdr_length: %d, frame length: %ld\n",sizeof(struct trh_hdr)-18+hdr_len,
      ti->current_skb->len-18+hdr_len);
#endif
  }

  /* header length including rif is computed above, now move the data
     and set fields appropriately.     */

  memcpy(dhb,ti->current_skb->data,hdr_len);
  dhb+=hdr_len;
  xmit_resp->hdr_length= hdr_len;
  xmit_resp->frame_length=htons(ti->current_skb->len
                                -sizeof(struct trh_hdr)+hdr_len);

                  /*  now copy the actual packet data next to hdr */
  memcpy(dhb,ti->current_skb->data+sizeof(struct trh_hdr),
             ti->current_skb->len-sizeof(struct trh_hdr));

  *(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD)
             =RESP_IN_ASB;
  dev->tbusy=0;
  dev_kfree_skb(ti->current_skb,FREE_WRITE);
  ti->current_skb=NULL;
  mark_bh(NET_BH);
}

static void tr_rx(struct device *dev) {

  struct tok_info *ti=(struct tok_info *) dev->priv;

	struct arb_rec_req *rec_req=(struct arb_rec_req *)ti->arb;
	struct asb_rec *rec_resp=(struct asb_rec *)ti->asb;
	struct rec_buf *rbuffer;	
	struct trllc *llc;
	unsigned char *data;
	unsigned int rbuffer_len,lan_hdr_len;
	struct sk_buff *skb;

	rbuffer=(struct rec_buf *)(ti->sram+ntohs(rec_req->rec_buf_addr));

	if(rec_resp->ret_code!=0xff) DPRINTK("ASB not free !!!\n");

	rec_resp->command=REC_DATA;
	rec_resp->station_id=rec_req->station_id;
	rec_resp->rec_buf_addr=rec_req->rec_buf_addr;

	lan_hdr_len=rec_req->lan_hdr_len;

	llc=(struct trllc *)((unsigned char *)rbuffer+offsetof(struct rec_buf,data)+lan_hdr_len);

#if 0
DPRINTK("offsetof data: %02X lan_hdr_len: %02X\n",offsetof(struct rec_buf,data),lan_hdr_len);
DPRINTK("llc: %p rec_buf_addr: %04X ti->sram: %p\n",llc,ntohs(rec_req->rec_buf_addr),ti->sram);
DPRINTK("dsap: %02X, ssap: %02X, llc: %02X, protid: %02X%02X%02X, ethertype: %04X\n",
			llc->dsap,llc->ssap,llc->llc,llc->protid[0],llc->protid[1],llc->protid[2],llc->ethertype);
#endif

	if(llc->llc!=UI_CMD) {
#ifndef TR_FILTERNONUI		
		DPRINTK("non-UI frame arrived. dropped. llc= %02X\n",llc->llc);
#endif
		rec_resp->ret_code=DATA_LOST;
		ti->tr_stats.rx_dropped++;
		*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD)=RESP_IN_ASB;
		return;
	}

#if 0
	if((llc->dsap!=0xaa) || (llc->ssap!=0xaa)) {

	struct trh_hdr *trhdr=(struct trh_hdr *)((unsigned char *)rbuffer+offsetof(struct rec_buf,data));

DPRINTK("Probably non-IP frame received.\n");
DPRINTK("ssap: %02X dsap: %02X saddr: %02X:%02X:%02X:%02X:%02X:%02X daddr: %02X:%02X:%02X:%02X:%02X:%02X\n",
			llc->ssap,llc->dsap,trhdr->saddr[0],trhdr->saddr[1],trhdr->saddr[2],trhdr->saddr[3],trhdr->saddr[4],trhdr->saddr[5],
			trhdr->daddr[0],trhdr->daddr[1],trhdr->daddr[2],trhdr->daddr[3],trhdr->daddr[4],trhdr->daddr[5]);
	}
#endif


	if(!(skb=dev_alloc_skb(ntohs(rec_req->frame_len)-lan_hdr_len+sizeof(struct trh_hdr)))) {
		DPRINTK("out of memory. frame dropped.\n");	
		ti->tr_stats.rx_dropped++;
		rec_resp->ret_code=DATA_LOST;
		*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD)=RESP_IN_ASB;
		return;
	}

	skb_put(skb,ntohs(rec_req->frame_len)-lan_hdr_len+sizeof(struct trh_hdr));
	skb->dev=dev;

#if 0
DPRINTK("Now copying data...\n");
#endif


	data=skb->data;
	memcpy(data,&(rbuffer->data),lan_hdr_len);


	if(lan_hdr_len<sizeof(struct trh_hdr))
		memset(data+lan_hdr_len,0,sizeof(struct trh_hdr)-lan_hdr_len);

	data+=sizeof(struct trh_hdr);
	rbuffer_len=ntohs(rbuffer->buf_len)-lan_hdr_len;
#if 0
DPRINTK("rbuffer_len: %d, data: %p\n",rbuffer_len,data);
#endif
	memcpy(data,(unsigned char *)(&(rbuffer->data))+lan_hdr_len,rbuffer_len);
	data+=rbuffer_len;


	if(rbuffer->buf_ptr)
		for(rbuffer=(struct rec_buf *)(ti->sram+ntohs(rbuffer->buf_ptr)-2);
				memcpy(data,&(rbuffer->data),rbuffer_len=ntohs(rbuffer->buf_len)),rbuffer->buf_ptr;
				data+=rbuffer_len,rbuffer=(struct rec_buf *)(ti->sram+ntohs(rbuffer->buf_ptr)-2))
#if 0
	DPRINTK("buf_ptr: %d,data =%p\n",ntohs(rbuffer->buf_ptr),data);
#endif

	rec_resp->ret_code=0;

	*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD)=RESP_IN_ASB;

	ti->tr_stats.rx_packets++;

	skb->protocol=tr_type_trans(skb,dev);
	netif_rx(skb);

	return;
}

static int tok_send_packet(struct sk_buff *skb, struct device *dev) {

  struct tok_info *ti=(struct tok_info *) dev->priv;

#if 0
DPRINTK("tada: sending packet...\n");
#endif

	if (dev->tbusy) {
		int ticks_waited=jiffies - dev->trans_start;
		if(ticks_waited<TX_TIMEOUT)
			return 1;
		DPRINTK("Arrg. Transmitter busy for more than 50 msec. Donald resets adapter, but resetting\n \
the IBM tokenring adapter takes a long time. It might not even help when the\n \
ring is very busy, so we just wait a little longer and hope for the best.\n");		
		dev->trans_start+=5; /* we fake the transmission start time... */
		return 1;
	}

	/* Donald does this, so we do too. */

	if(skb==NULL) {
		dev_tint(dev);
		return 0;
	}

	if(set_bit(0,(void *)&dev->tbusy)!=0)
		DPRINTK("Transmitter access conflict\n");
	else {
		struct srb_xmit *xmit=(struct srb_xmit *)ti->srb;

		ti->current_skb=skb; /* save skb. We will need it when the adapter
                                  asks for the data */
		xmit->command=XMIT_UI_FRAME;
		xmit->station_id=ti->exsap_station_id;
		*(unsigned char *)(ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD)=CMD_IN_SRB;
		dev->trans_start=jiffies;
	}

	return 0;
}	

/* tok_get_stats():  Basically a scaffold routine which will return
         the address of the tr_statistics structure associated with
         this device -- the tr.... structure is a ethnet look-alike
         so at least for this iteration may suffice.   */

static struct enet_statistics * tok_get_stats(struct device *dev) {

  struct tok_info *toki;
  toki=(struct tok_info *) dev->priv;
  return (struct enet_statistics *) &toki->tr_stats;
}

#ifdef MODULE
static char devicename[9] = { 0, };
static struct device dev_ibmtr = {
	devicename, /* device name is inserted by linux/drivers/net/net_init.c */
	0, 0, 0, 0,
	0, 0,
	0, 0, 0, NULL, tok_probe };

static int io = 0xa20;

int init_module(void)
{
	if (io == 0)
		printk("ibmtr: You should not use auto-probing with insmod!\n");
	dev_ibmtr.base_addr = io;
	dev_ibmtr.irq       = 0;
	if (register_netdev(&dev_ibmtr) != 0) {
		printk("ibmtr: register_netdev() returned non-zero.\n");
		return -EIO;
	}
	return 0;
}

void
cleanup_module(void)
{
	unregister_netdev(&dev_ibmtr);

	/* If we don't do this, we can't re-insmod it later. */
	free_irq(dev_ibmtr.irq, NULL);
	irq2dev_map[dev_ibmtr.irq] = NULL;
	release_region(dev_ibmtr.base_addr, TR_IO_EXTENT);
}
#endif /* MODULE */
