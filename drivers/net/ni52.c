/* 
 * net-3-driver for the NI5210 card (i82586 Ethernet chip)
 *
 * This is an extension to the Linux operating system, and is covered by the
 * same Gnu Public License that covers that work.
 * 
 * Alphacode 0.51 (94/08/19) for Linux 1.1.47 (or later)
 * Copyrights (c) 1994 by Michael Hipp (mhipp@student.uni-tuebingen.de)
 *    [feel free to mail ....]
 *
 * CAN YOU PLEASE REPORT ME YOUR PERFORMANCE EXPERIENCES !!.
 *
 * autoprobe for: base_addr: 0x300,0x280,0x360,0x320,0x340
 *                mem_start: 0xd0000,0xd4000,0xd8000 (8K and 16K)
 *
 * sources:
 *   skeleton.c from Donald Becker
 *
 * I have also done a look in the following sources: (mail me if you need them)
 *   crynwr-packet-driver by Russ Nelson
 *   Garret A. Wollman's (fourth) i82586-driver for BSD
 *   (before getting an i82596 manual, the existing drivers helped
 *    me a lot to understand this tricky chip.)
 *
 * Known Bugs:
 *   The internal sysbus seems to be slow. So we often lose packets because of
 *   overruns while receiving from a fast remote host. 
 *   This can slow down TCP connections. Maybe the newer ni5210 cards are better.
 */

/*
 * 19.Aug.94: changed request_irq() parameter (MH)
 * 
 * 20.July.94: removed cleanup bugs, removed a 16K-mem-probe-bug (MH)
 *
 * 19.July.94: lotsa cleanups .. (MH)
 *
 * 17.July.94: some patches ... verified to run with 1.1.29 (MH)
 *
 * 4.July.94: patches for Linux 1.1.24  (MH)
 *
 * 26.March.94: patches for Linux 1.0 and iomem-auto-probe (MH)
 *
 * 30.Sep.93: Added nop-chain .. driver now runs with only one Xmit-Buff, too (MH)
 */
 
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/bitops.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "ni52.h"

#define DEBUG   /* debug on */

/*
#define DEBUG1
#define DEBUG2
#define DEBUG3
*/

#define SYSBUSVAL 1

#define ni_attn586()  {outb(0,dev->base_addr+NI52_ATTENTION);}
#define ni_reset586() {outb(0,dev->base_addr+NI52_RESET);}

#define make32(ptr16) (p->memtop + (short) (ptr16) )
#define make24(ptr32) ((char *) (ptr32) - p->base)
#define make16(ptr32) ((unsigned short) ((unsigned long) (ptr32) - (unsigned long) p->memtop ))

/******************* how to calc the buffers *****************************

IMPORTANT NOTE: if you configure only one NUM_XMIT_BUFFS, do also a 
---------------   #define ONLY_ONE_XMIT_BUF
                btw: it seems, that only the ONLY_ONE_XMIT_BUF Mode is stable


sizeof(scp)=12; sizeof(scb)=16; sizeof(iscp)=8;
sizeof(scp)+sizeof(iscp)+sizeof(scb) = 36 = INIT
sizeof(rfd) = 24; sizeof(rbd) = 12; 
sizeof(tbd) = 8; sizeof(transmit_cmd) = 16;
sizeof(nop_cmd) = 8; 

examples:
---------

->cfg1: NUM_RECV_FRAMES=16, NUM_RECV_BUFFS=48, RECV_BUFF_SIZE=256, 
        NUM_XMIT_BUFFS=2 ,XMIT_BUFF_SIZE=1514

NUM_RECV_FRAMES * sizeof(rfd) = 384;
NUM_RECV_BUFFS * ( sizeof(rbd) + RECV_BUFF_SIZE) = 12864
NUM_XMIT_BUFFS * ( sizeof(tbd+transmit_cmd+nop_cmd) + XMIT_BUFF_SIZE) = 3092
INIT = 36
--------------------
16358   (36 bytes left!)

************************

->cfg2: NUM_RECV_FRAMES=9, NUM_RECV_BUFFS=18, RECV_BUFF_SIZE=256, 
        NUM_XMIT_BUFFS=2 ,XMIT_BUFF_SIZE=1514

NUM_RECV_FRAMES * sizeof(rfd) = 216
NUM_RECV_BUFFS * ( sizeof(rbd) + RECV_BUFF_SIZE) = 4824
NUM_XMIT_BUFFS * ( sizeof(tbd+transmit_cmd+nop_cmd) + XMIT_BUFF_SIZE) = 3092
INIT = 36
------------------
8180    (24 bytes left!)

->cfg3: NUM_RECV_FRAMES=7, NUM_RECV_BUFFS=24, RECV_BUFF_SIZE=256, 
        NUM_XMIT_BUFFS=1, XMIT_BUFF_SIZE=1514
        168  +  6432  +  1538  +  36  +  16 = 8190 

***************************************************************************/

#if 0
/* config-1 for 16Kram card */
#  define NUM_RECV_FRAMES 16	/* number of frames to allow for receive */
#  define NUM_RECV_BUFFS 48	/* number of buffers to allocate */
#  define RECV_BUFF_SIZE 256	/* size of each buffer, POWER OF 2 & EVEN*/
#  define XMIT_BUFF_SIZE 1514	/* length of transmit buffer (EVEN) */
#  define NUM_XMIT_BUFFS 2	/* number of Xmit-Buffs */
#elif 0
/* config-2 for 8Kram card */
#  define NUM_RECV_FRAMES 9
#  define NUM_RECV_BUFFS 18
#  define RECV_BUFF_SIZE 256
#  define XMIT_BUFF_SIZE 1514
#  define NUM_XMIT_BUFFS 2
#elif 1
/*
 * config-3 for 8Kram card  ___use_this_config____ seems to be stable
 */
#  define NUM_RECV_FRAMES 7
#  define NUM_RECV_BUFFS 24
#  define RECV_BUFF_SIZE 256
#  define XMIT_BUFF_SIZE 1514
#  define NUM_XMIT_BUFFS 1
#  define ONLY_ONE_XMIT_BUF 
#  define NO_NOPCOMMANDS
#elif 0
/*
 * cfg-4 for 16K, ONLY_ONE_XMIT_BUF
 */
#  define NUM_RECV_FRAMES 20
#  define NUM_RECV_BUFFS 27
#  define RECV_BUFF_SIZE 512
#  define XMIT_BUFF_SIZE 1514
#  define NUM_XMIT_BUFFS 1
#  define ONLY_ONE_XMIT_BUF
#else
#  define NUM_RECV_FRAMES 4
#  define NUM_RECV_BUFFS 4
#  define RECV_BUFF_SIZE 1536
#  define XMIT_BUFF_SIZE 1536
#  define NUM_XMIT_BUFFS 1
#  define ONLY_ONE_XMIT_BUF
#  define NO_NOPCOMMANDS
#endif

#define DELAY(x) {int i=jiffies; \
                  if(loops_per_sec == 1) \
                     while(i+(x)>jiffies); \
                  else \
                     __delay((loops_per_sec>>5)*x); \
                 }

extern void autoirq_setup(int waittime);
extern int autoirq_report(int waittime);
extern void *irq2dev_map[16];

#ifndef HAVE_PORTRESERVE
#define check_region(ioaddr, size) 		0
#define	register_iomem(ioaddr, size,name);		do ; while (0)
#endif

#define NI52_TOTAL_SIZE 16
#define NI52_ADDR0 0x02
#define NI52_ADDR1 0x07
#define NI52_ADDR2 0x01

static int     ni52_probe1(struct device *dev,int ioaddr);
static void    ni52_interrupt(int reg_ptr);
static int     ni52_open(struct device *dev);
static int     ni52_close(struct device *dev);
static int     ni52_send_packet(struct sk_buff *,struct device *);
static struct enet_statistics *ni52_get_stats(struct device *dev);
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);

/* helper-functions */
static int     init586(struct device *dev);
static int     check586(struct device *dev,char *where,unsigned size);
static void    alloc586(struct device *dev);
static void    startrecv586(struct device *dev);
static void   *alloc_rfa(struct device *dev,void *ptr);
static void    ni52_rcv_int(struct device *dev);
static void    ni52_xmt_int(struct device *dev);
static void    ni52_rnr_int(struct device *dev);

struct priv
{
  struct enet_statistics stats;
  unsigned long base;
  char *memtop,*max_cbuff32,*min_cbuff32,*max_cbuff24;
  volatile struct rbd_struct  *rbd_last;
  volatile struct rfd_struct  *rfd_last,*rfd_top,*rfd_first;
  volatile struct scp_struct  *scp;  /* volatile is important */
  volatile struct iscp_struct *iscp; /* volatile is important */
  volatile struct scb_struct  *scb;  /* volatile is important */
  volatile struct tbd_struct  *xmit_buffs[NUM_XMIT_BUFFS];
  volatile struct transmit_cmd_struct *xmit_cmds[NUM_XMIT_BUFFS];
#ifdef ONLY_ONE_XMIT_BUF
  volatile struct nop_cmd_struct *nop_cmds[2];
#else
  volatile struct nop_cmd_struct *nop_cmds[NUM_XMIT_BUFFS];
#endif
  volatile int    nop_point;
  volatile char  *xmit_cbuffs[NUM_XMIT_BUFFS];
  volatile int    xmit_count,xmit_last;
};


/**********************************************
 * close device 
 */

static int ni52_close(struct device *dev)
{
  free_irq(dev->irq);
  irq2dev_map[dev->irq] = 0;

  ni_reset586(); /* the hard way to stop the receiver */

  dev->start = 0;
  dev->tbusy = 0;

  return 0;
}

/**********************************************
 * open device 
 */

static int ni52_open(struct device *dev)
{
  alloc586(dev);
  init586(dev);  
  startrecv586(dev);

  if(request_irq(dev->irq, &ni52_interrupt,0,"ni52")) 
  {    
    ni_reset586();
    return -EAGAIN;
  }  
  irq2dev_map[dev->irq] = dev;

  dev->interrupt = 0;
  dev->tbusy = 0;
  dev->start = 1;

  return 0; /* most done by init */
}

/**********************************************
 * Check to see if there's an 82586 out there. 
 */

static int check586(struct device *dev,char *where,unsigned size)
{
  struct priv *p = (struct priv *) dev->priv;
  char *iscp_addrs[2];
  int i;

  p->base = (unsigned long) where + size - 0x01000000;
  p->memtop = where + size;
  p->scp = (struct scp_struct *)(p->base + SCP_DEFAULT_ADDRESS);
  memset((char *)p->scp,0, sizeof(struct scp_struct));
  p->scp->sysbus = SYSBUSVAL;        /* 1 = 8Bit-Bus */
  
  iscp_addrs[0] = where;
  iscp_addrs[1]= (char *) p->scp - sizeof(struct iscp_struct);

  for(i=0;i<2;i++)
  {
    p->iscp = (struct iscp_struct *) iscp_addrs[i];
    memset((char *)p->iscp,0, sizeof(struct iscp_struct));

    p->scp->iscp = make24(p->iscp);
    p->iscp->busy = 1;

    ni_reset586();
    ni_attn586();
    DELAY(2);	/* wait a while... */

    if(p->iscp->busy)
      return 0;
  }
  return 1;
}

/******************************************************************
 * set iscp at the right place, called by ni52_probe1 and open586. 
 */

void alloc586(struct device *dev)
{
  struct priv *p =  (struct priv *) dev->priv; 

  p->scp  = (struct scp_struct *)  (p->base + SCP_DEFAULT_ADDRESS);
  p->scb  = (struct scb_struct *)  (dev->mem_start);
  p->iscp = (struct iscp_struct *) ((char *)p->scp - sizeof(struct iscp_struct));

  memset((char *) p->iscp,0,sizeof(struct iscp_struct));
  memset((char *) p->scp ,0,sizeof(struct scp_struct));

  p->scp->iscp = make24(p->iscp);
  p->scp->sysbus = SYSBUSVAL;
  p->iscp->scb_offset = make16(p->scb);

  p->iscp->busy = 1;
  ni_reset586();
  ni_attn586();

#ifdef DEBUG
  DELAY(2); 

  if(p->iscp->busy)
    printk("%s: Init-Problems (alloc).\n",dev->name);
#endif

  memset((char *)p->scb,0,sizeof(struct scb_struct));
}

/**********************************************
 * probe the ni5210-card
 */

int ni52_probe(struct device *dev)
{
  int *port, ports[] = {0x300, 0x280, 0x360 , 0x320 , 0x340, 0};
  int base_addr = dev->base_addr;

  if (base_addr > 0x1ff)		/* Check a single specified location. */
    if( (inb(base_addr+NI52_MAGIC1) == NI52_MAGICVAL1) &&
        (inb(base_addr+NI52_MAGIC2) == NI52_MAGICVAL2))
      return ni52_probe1(dev, base_addr);
  else if (base_addr > 0)		/* Don't probe at all. */
    return ENXIO;

  for (port = ports; *port; port++) {
    int ioaddr = *port;
    if (check_region(ioaddr, NI52_TOTAL_SIZE))
      continue;
    if( !(inb(ioaddr+NI52_MAGIC1) == NI52_MAGICVAL1) || 
        !(inb(ioaddr+NI52_MAGIC2) == NI52_MAGICVAL2))
      continue;

    dev->base_addr = ioaddr;
    if (ni52_probe1(dev, ioaddr) == 0)
      return 0;
  }

  dev->base_addr = base_addr;
  return ENODEV;
}

static int ni52_probe1(struct device *dev,int ioaddr)
{
  long memaddrs[] = { 0xd0000,0xd2000,0xd4000,0xd6000,0xd8000, 0 };
  int i,size;

  for(i=0;i<ETH_ALEN;i++)
    dev->dev_addr[i] = inb(dev->base_addr+i);

  if(dev->dev_addr[0] != NI52_ADDR0 || dev->dev_addr[1] != NI52_ADDR1
                                    || dev->dev_addr[2] != NI52_ADDR2)
    return ENODEV;

  printk("%s: Ni52 found at %#3x, ",dev->name,dev->base_addr);

  register_iomem(ioaddr,NI52_TOTAL_SIZE,"ni52");

  dev->priv = (void *) kmalloc(sizeof(struct priv),GFP_KERNEL); 
                                  /* warning: we don't free it on errors */
  memset((char *) dev->priv,0,sizeof(struct priv));

  /* 
   * check (or search) IO-Memory, 8K and 16K
   */
  if(dev->mem_start != 0) /* no auto-mem-probe */
  {
    size = 0x4000;
    if(!check586(dev,(char *) dev->mem_start,size)) {
      size = 0x2000;
      if(!check586(dev,(char *) dev->mem_start,size)) {
        printk("?memprobe, Can't find memory at 0x%lx!\n",dev->mem_start);
        return ENODEV;
      }
    }
  }
  else  
  {
    for(i=0;;i++)
    {
      if(!memaddrs[i]) {
        printk("?memprobe, Can't find io-memory!\n");
        return ENODEV;
      }
      dev->mem_start = memaddrs[i];
      size = 0x2000;
      if(check586(dev,(char *)dev->mem_start,size)) /* 8K-check */
        break;
      size = 0x4000;
      if(check586(dev,(char *)dev->mem_start,size)) /* 16K-check */
        break;
    }
  }

  ((struct priv *) (dev->priv))->base =  dev->mem_start + size - 0x01000000;
  alloc586(dev);

  printk("Memaddr: 0x%lx, Memsize: %d, ",dev->mem_start,size);

  if(dev->irq < 2)
  {
    autoirq_setup(0);
    ni_reset586();
    ni_attn586();
    if(!(dev->irq = autoirq_report(2)))
    {
      printk("?autoirq, Failed to detect IRQ line!\n"); 
      return 1;
    }
  }
  else if(dev->irq == 2) 
    dev->irq = 9;

  printk("IRQ %d.\n",dev->irq);

  dev->open            = &ni52_open;
  dev->stop            = &ni52_close;
  dev->get_stats       = &ni52_get_stats;
  dev->hard_start_xmit = &ni52_send_packet;
  dev->set_multicast_list = &set_multicast_list;

  dev->if_port 	       = 0;

  ether_setup(dev);

  dev->tbusy = 0;
  dev->interrupt = 0;
  dev->start = 0;
  
  return 0;
}

/********************************************** 
 * init the chip (ni52-interrupt should be disabled?!)
 * needs a correct 'allocated' memory
 */

static int init586(struct device *dev)
{
  void *ptr;
  unsigned long s;
  int i,result=0;
  struct priv *p = (struct priv *) dev->priv;
  volatile struct configure_cmd_struct  *cfg_cmd;
  volatile struct iasetup_cmd_struct *ias_cmd;
  volatile struct tdr_cmd_struct *tdr_cmd;

  ptr = (void *) ((char *)p->scb + sizeof(struct scb_struct));

  cfg_cmd = (struct configure_cmd_struct *)ptr; /* configure-command */
 
  cfg_cmd->byte_cnt     = 0x04; /* number of cfg bytes */
  cfg_cmd->fifo         = 0xc8;    /* fifo-limit (8=tx:32/rx:64) | monitor */
  cfg_cmd->sav_bf       = 0x40; /* hold or discard bad recv frames (bit 7) */
  cfg_cmd->adr_len      = 0x2e; /* addr_len |!src_insert |pre-len |loopback */
  cfg_cmd->cmd_status   = 0;
  cfg_cmd->cmd_cmd      = CMD_CONFIGURE | CMD_LAST;
  cfg_cmd->cmd_link     = 0xffff;

  p->scb->cbl_offset = make16(cfg_cmd);

  p->scb->cmd = CUC_START; /* cmd.-unit start */
  ni_attn586();
 
  s = jiffies; /* warning: only active with interrupts on !! */
  while(!(cfg_cmd->cmd_status & STAT_COMPL)) 
    if(jiffies-s > 30) break;

  if((cfg_cmd->cmd_status & (STAT_OK|STAT_COMPL)) != (STAT_COMPL|STAT_OK))
  {
    printk("%s (ni52): configure command failed: %x\n",dev->name,cfg_cmd->cmd_status);
    return 1; 
  }

    /*
     * individual address setup
     */
  ias_cmd = (struct iasetup_cmd_struct *)ptr;

  ias_cmd->cmd_status = 0;
  ias_cmd->cmd_cmd    = CMD_IASETUP | CMD_LAST;
  ias_cmd->cmd_link   = 0xffff;

  memcpy((char *)&ias_cmd->iaddr,(char *) dev->dev_addr,ETH_ALEN);

  p->scb->cbl_offset = make16(ias_cmd);

  p->scb->cmd = CUC_START; /* cmd.-unit start */
  ni_attn586();

  s = jiffies;
  while(!(ias_cmd->cmd_status & STAT_COMPL)) 
    if(jiffies-s > 30) break;

  if((ias_cmd->cmd_status & (STAT_OK|STAT_COMPL)) != (STAT_OK|STAT_COMPL)) {
    printk("%s (ni52): individual address setup command failed: %04x\n",dev->name,ias_cmd->cmd_status);
    return 1; 
  }

   /* 
    * TDR, wire check .. e.g. no resistor e.t.c 
    */
  tdr_cmd = (struct tdr_cmd_struct *)ptr;

  tdr_cmd->cmd_status  = 0;
  tdr_cmd->cmd_cmd     = CMD_TDR | CMD_LAST;
  tdr_cmd->cmd_link    = 0xffff;
  tdr_cmd->status      = 0;

  p->scb->cbl_offset = make16(tdr_cmd);

  p->scb->cmd = CUC_START; /* cmd.-unit start */
  ni_attn586();

  s = jiffies; 
  while(!(tdr_cmd->cmd_status & STAT_COMPL))
    if(jiffies - s > 30) {
      printk("%s: Problems while running the TDR.\n",dev->name);
      result = 1;
    }

  if(!result)
  {
    DELAY(2); /* wait for result */
    result = tdr_cmd->status;

    p->scb->cmd = p->scb->status & STAT_MASK;
    ni_attn586(); /* ack the interrupts */

    if(result & TDR_LNK_OK) ;
    else if(result & TDR_XCVR_PRB)
      printk("%s: TDR: Transceiver problem!\n",dev->name);
    else if(result & TDR_ET_OPN)
      printk("%s: TDR: No correct termination %d clocks away.\n",dev->name,result & TDR_TIMEMASK);
    else if(result & TDR_ET_SRT) 
    {
      if (result & TDR_TIMEMASK) /* time == 0 -> strange :-) */
        printk("%s: TDR: Detected a short circuit %d clocks away.\n",dev->name,result & TDR_TIMEMASK);
    }
    else
      printk("%s: TDR: Unknown status %04x\n",dev->name,result);
  }
 
   /* 
    * ack interrupts 
    */
  p->scb->cmd = p->scb->status & STAT_MASK;
  ni_attn586();

   /*
    * alloc nop/xmit-cmds
    */
#ifdef ONLY_ONE_XMIT_BUF
  for(i=0;i<2;i++)
  {
    p->nop_cmds[i] = (struct nop_cmd_struct *)ptr;
    p->nop_cmds[i]->cmd_cmd    = 0;
    p->nop_cmds[i]->cmd_status = 0;
    p->nop_cmds[i]->cmd_link   = make16((p->nop_cmds[i]));
    ptr += sizeof(struct nop_cmd_struct);
  }
  p->xmit_cmds[0] = (struct transmit_cmd_struct *)ptr; /* transmit cmd/buff 0 */
  ptr += sizeof(struct transmit_cmd_struct);
#else
  for(i=0;i<NUM_XMIT_BUFFS;i++)
  {
    p->nop_cmds[i] = (struct nop_cmd_struct *)ptr;
    p->nop_cmds[i]->cmd_cmd    = 0;
    p->nop_cmds[i]->cmd_status = 0;
    p->nop_cmds[i]->cmd_link   = make16((p->nop_cmds[i]));
    ptr += sizeof(struct nop_cmd_struct);
    p->xmit_cmds[i] = (struct transmit_cmd_struct *)ptr; /* transmit cmd/buff 0 */
    ptr += sizeof(struct transmit_cmd_struct);
  }
#endif

  ptr = alloc_rfa(dev,(void *)ptr); /* init receive-frame-area */ 

   /*
    * alloc xmit-buffs 
    */
  for(i=0;i<NUM_XMIT_BUFFS;i++)
  {
    p->xmit_cbuffs[i] = (char *)ptr; /* char-buffs */
    ptr += XMIT_BUFF_SIZE;
    p->xmit_buffs[i] = (struct tbd_struct *)ptr; /* TBD */
    ptr += sizeof(struct tbd_struct);
    if((void *)ptr > (void *)p->iscp) 
    {
      printk("%s: not enough shared-mem for your configuration!\n",dev->name);
      return 1;
    }   
    memset((char *)(p->xmit_cmds[i]) ,0, sizeof(struct transmit_cmd_struct));
    memset((char *)(p->xmit_buffs[i]),0, sizeof(struct tbd_struct));
    p->xmit_cmds[i]->cmd_status = STAT_COMPL;
    p->xmit_cmds[i]->tbd_offset = make16((p->xmit_buffs[i]));
    p->xmit_buffs[i]->next = 0xffff;
    p->xmit_buffs[i]->buffer = make24((p->xmit_cbuffs[i]));
  }
  
  p->xmit_count = 0; 
  p->xmit_last  = 0;
#ifndef NO_NOPCOMMANDS
  p->nop_point  = 0;
#endif

   /*
    * 'start transmitter' (nop-loop)
    */
#ifndef NO_NOPCOMMANDS
  p->scb->cbl_offset = make16(p->nop_cmds[0]);
  p->scb->cmd = CUC_START;
  ni_attn586();
  while(p->scb->cmd);
#else
/*
  p->nop_cmds[0]->cmd_link = make16(p->nop_cmds[1]);
  p->nop_cmds[1]->cmd_link = make16(p->xmit_cmds[0]);
*/
  p->xmit_cmds[0]->cmd_link = 0xffff;
  p->xmit_cmds[0]->cmd_cmd  = CMD_XMIT | CMD_LAST | CMD_INT;
#endif

  return 0;
}

/******************************************************
 * This is a helper routine for ni52_nr_int() and init586(). 
 * It sets up the Receive Frame Area (RFA).
 */

static void *alloc_rfa(struct device *dev,void *ptr) 
{
  volatile struct rfd_struct *rfd = (struct rfd_struct *)ptr;
  volatile struct rbd_struct *rbd;
  int i;
  struct priv *p = (struct priv *) dev->priv;

  memset((char *) rfd,0,sizeof(struct rfd_struct)*NUM_RECV_FRAMES);
  p->rfd_first = rfd;

  for(i = 0; i < NUM_RECV_FRAMES; i++)
    rfd[i].next = make16(rfd + (i+1) % NUM_RECV_FRAMES);
  rfd[NUM_RECV_FRAMES-1].last = RFD_LAST; /* set EOL (no RU suspend) */

  ptr = (char *) (rfd + NUM_RECV_FRAMES);

  rbd = (struct rbd_struct *) ptr;
  ptr += sizeof(struct rbd_struct)*NUM_RECV_BUFFS;

   /* clr descriptors */
  memset((char *) rbd,0,sizeof(struct rbd_struct)*NUM_RECV_BUFFS);

  p->min_cbuff32 = ptr;
  for(i=0;i<NUM_RECV_BUFFS;i++)
  {
    rbd[i].next = make16((rbd + (i+1) % NUM_RECV_BUFFS));
    rbd[i].size = RECV_BUFF_SIZE;
    rbd[i].buffer = make24(ptr);
    ptr += RECV_BUFF_SIZE;
  }
  rbd[NUM_RECV_BUFFS-1].size |= RBD_LAST; /* set eol */
  p->max_cbuff32 = ptr;
  p->max_cbuff24 = make24(p->max_cbuff32);
 
  p->rfd_top  = p->rfd_first;
  p->rfd_last = p->rfd_first + NUM_RECV_FRAMES - 1;

  p->rbd_last = rbd + NUM_RECV_BUFFS - 1;
 
  p->scb->rfa_offset		= make16(p->rfd_first);
  p->rfd_first->rbd_offset	= make16(rbd);

  return ptr;
}


/**************************************************
 * Interrupt Handler ...
 */

static void ni52_interrupt(int reg_ptr)
{
  struct device *dev = (struct device *) irq2dev_map[-((struct pt_regs *)reg_ptr)->orig_eax-2];
  unsigned short stat;
  int pd = 0;
  struct priv *p;

#ifdef DEBUG2
  printk("(1)");
#endif

  if (dev == NULL) {
    printk ("ni52-interrupt: irq %d for unknown device.\n",(int) -(((struct pt_regs *)reg_ptr)->orig_eax+2));
    return;
  }
  p = (struct priv *) dev->priv;

  if(dev->interrupt)
  {
    printk("(ni52-I)");
    return;
  }

  dev->interrupt = 1;

  while((stat=p->scb->status & STAT_MASK))
  {
    p->scb->cmd = stat;
    ni_attn586(); /* ack inter. */

    if(pd) 
      printk("ni52-%04x/%04x-",(int) stat,(int) p->scb->status); /* debug */

    if(stat & (STAT_FR | STAT_RNR)) 
      ni52_rcv_int(dev);

    if(stat & STAT_CX) 
      ni52_xmt_int(dev);

#ifndef NO_NOPCOMMANDS
    if(stat & STAT_CNA)
#else
    if( (stat & STAT_CNA) && !(stat & STAT_CX) )
#endif
      printk("%s: oops! CU has left active state. stat: %04x/%04x.\n",dev->name,(int) stat,(int) p->scb->status);

    if(stat & STAT_RNR)
    {
      printk("%s: rnr: %04x/%04x.\n",dev->name,(int) stat,(int) p->scb->status);
      ni52_rnr_int(dev); 
      pd = 1; /* local debug on */
    }

#ifdef DEBUG2
    pd++;
#endif

    while(p->scb->cmd)
    {
      int i; /* wait for ack. (ni52_xmt_int can be faster than ack!!) */
      for(i=0;i<200;i++);
    }
  }

#ifdef DEBUG
  {
    static int old_ovr=0;
    int l;
    if((l = p->scb->ovrn_errs - old_ovr))
    {
      if(l > 0)
        p->stats.rx_over_errors += l;
      else
        old_ovr=0;
    }
  }
#endif

#ifdef DEBUG2
  printk("(2)");
#endif

  dev->interrupt = 0;
}

/*******************************************************
 * receive-interrupt
 */

static void ni52_rcv_int(struct device *dev)
{
  int status;
  unsigned short totlen,pnt;
  struct sk_buff *skb;
  struct rbd_struct *rbd,*rbd_first;
  struct priv *p = (struct priv *) dev->priv;

  for(;(status = p->rfd_top->status) & STAT_COMPL;)
  {
      rbd = rbd_first = (struct rbd_struct *) make32(p->rfd_top->rbd_offset);

#ifdef DEBUG1
      {
        struct rbd_struct *rbd1 = rbd;
        if(rbd1==p->rbd_last)
          printk("L");
        printk("S:%04x/%x/%02x >",(int) rbd1->status,(int) rbd1->size>>12,(int)((unsigned long) rbd1 & 0xff));
        rbd1 = (struct rbd_struct *) make32(rbd1->next);
        for(;rbd1 != rbd_first;rbd1 = (struct rbd_struct *)  make32(rbd1->next))
        {
          if(rbd1 == p->rbd_last)
            printk("L:");
          printk("%04x/%x-",(int) rbd1->status>>12,(int) rbd1->size>>12);
        }
        printk("< ");
      }
      {
        struct rfd_struct *rfd1 = p->rfd_top;
        if(rfd1==p->rfd_last)
          printk("L");
        printk("S:%04x/%x/%02x >",(int) rfd1->status,(int) rfd1->last>>12,(int)((unsigned long) rfd1 & 0xff));
        rfd1 = (struct rfd_struct *) make32(rfd1->next);
        for(;rfd1 != p->rfd_top;rfd1 = (struct rfd_struct *)  make32(rfd1->next))
        {
          if(rfd1 == p->rfd_last)
            printk("L:");
          printk("%x/%x-",(int) rfd1->status>>12,(int) rfd1->last>>12);
        }
        printk("<\n");
      }
#endif

      p->rfd_top->status = 0;
      p->rfd_top->last = RFD_LAST;
      p->rfd_last->last = 0;        /* delete RFD_LAST, no RU suspend */
      p->rfd_last = p->rfd_top;
      p->rfd_top = (struct rfd_struct *) make32(p->rfd_top->next);

      if(status & RFD_ERRMASK)
        printk("%s: RFD-Error ... status: %04x.\n",dev->name,status);

      if(status & STAT_OK)
      {
        for(totlen=0; !(rbd->status & RBD_LAST); rbd=(struct rbd_struct *) make32(rbd->next)) {
          totlen += RECV_BUFF_SIZE;
          rbd->status = 0;
        }
        totlen += rbd->status & RBD_MASK;
        rbd->status = 0;
        
        skb = (struct sk_buff *) alloc_skb(totlen, GFP_ATOMIC);

        if (skb != NULL) /* copy header */
        {
          skb->len = totlen;
          skb->dev = dev;

          if(rbd->buffer < rbd_first->buffer)
          {
            pnt = p->max_cbuff24 - rbd_first->buffer;
            memcpy( (char *) skb->data,p->max_cbuff32-pnt,pnt);
            memcpy( (char *) skb->data+pnt,p->min_cbuff32,totlen-pnt);
          }
          else
            memcpy( (char *) skb->data,(char *) p->base+(unsigned long) rbd_first->buffer, totlen);

          rbd->size |= RBD_LAST;
          p->rbd_last->size &= ~RBD_LAST;
          p->rbd_last = rbd;

          netif_rx(skb);
          p->stats.rx_packets++;
        }
        else
        {
          rbd->size |= RBD_LAST;
          p->rbd_last->size &= ~RBD_LAST;
          p->rbd_last = rbd;
        }
      }
      else /* frame !(ok), only with 'save-bad-frames' */
      {
        printk("%s: oops! rfd-error-status: %04x\n",dev->name,status);
        p->stats.rx_errors++;
      }
  }
}

/**********************************************************
 * I never got this error , (which should occur if someone 
 * wants to blast your machine) so I couldn't debug it for now.
 * but we _try_ to fix the receiver not ready int.
 */

static void ni52_rnr_int(struct device *dev)
{
  struct priv *p = (struct priv *) dev->priv;

  p->stats.rx_errors++;

  while(p->scb->cmd);    /* wait for the last cmd */
  p->scb->cmd = RUC_ABORT;
  ni_attn586(); 
  while(p->scb->cmd);    /* wait for accept cmd. */

  alloc_rfa(dev,(char *)p->rfd_first);
  startrecv586(dev); /* restart */
}

/**********************************************************
 * handle xmit - interrupt
 */

static void ni52_xmt_int(struct device *dev)
{
  int status;
  struct priv *p = (struct priv *) dev->priv;

/*
  if(!(p->xmit_cmds[0]->cmd_status & STAT_COMPL))
    return;
*/

  if( (status=p->xmit_cmds[p->xmit_last]->cmd_status) & STAT_OK)
  {
    p->stats.tx_packets++;
    p->stats.collisions += (status & TCMD_MAXCOLLMASK);
    dev->tbusy = 0;
    mark_bh(NET_BH);
  }
  else 
  {
    p->stats.tx_errors++;
    if(status & TCMD_LATECOLL) {
      printk("%s: late collision detected.\n",dev->name);
      p->stats.collisions++;
    } 
    else if(status & TCMD_NOCARRIER) {
      p->stats.tx_carrier_errors++;
      printk("%s: no carrier detected.\n",dev->name);
    } 
    else if(status & TCMD_LOSTCTS) 
      printk("%s: loss of CTS detected.\n",dev->name);
    else if(status & TCMD_UNDERRUN) {
      printk("%s: DMA underrun detected.\n",dev->name);
    }
    else if(status & TCMD_MAXCOLL) {
      printk("%s: Max. collisions exceeded.\n",dev->name);
      p->stats.collisions += 16;
    } 
  }

#ifndef ONLY_ONE_XMIT_BUF
  if( (++p->xmit_last) == NUM_XMIT_BUFFS) 
    p->xmit_last = 0;
#endif

}

/***********************************************************
 * (re)start the receiver
 */ 

static void startrecv586(struct device *dev)
{
  struct priv *p = (struct priv *) dev->priv;

  p->scb->rfa_offset = make16(p->rfd_first);
  p->scb->cmd = RUC_START;
  ni_attn586(); /* start cmd. */
  while(p->scb->cmd); /* wait for accept cmd. (no timeout!!) */

  DELAY(2); /* isn't necess. */

  p->scb->cmd = p->scb->status & STAT_MASK;
  ni_attn586(); /* ack interr */
}

/******************************************************
 * send frame 
 */

static int ni52_send_packet(struct sk_buff *skb, struct device *dev)
{
  int len;
#ifndef NO_NOPCOMMANDS
  int next_nop;
#endif
  struct priv *p = (struct priv *) dev->priv;

  if(dev->tbusy)
  {
    int tickssofar = jiffies - dev->trans_start;

    if (tickssofar < 30)
      return 1;

#ifdef DEBUG
    printk("%s: xmitter timed out, try to restart! stat: %04x\n",dev->name,p->scb->status);
    printk("%s: command-stats: %04x %04x\n",dev->name,p->xmit_cmds[0]->cmd_status,p->xmit_cmds[1]->cmd_status);
#endif

    ni52_close(dev);
    ni52_open(dev);
    dev->trans_start = jiffies;
  }

  if(skb == NULL)
  {
    dev_tint(dev);
    return 0;
  }

  if (skb->len <= 0)
    return 0;

  if (set_bit(0, (void*)&dev->tbusy) != 0)
     printk("%s: Transmitter access conflict.\n", dev->name);
  else
  {
    memcpy((char *)p->xmit_cbuffs[p->xmit_count],(char *)(skb->data),skb->len);
    len = (ETH_ZLEN < skb->len) ? skb->len : ETH_ZLEN;

#ifdef ONLY_ONE_XMIT_BUF  
#  ifdef NO_NOPCOMMANDS
    p->xmit_buffs[0]->size = TBD_LAST | len;
    p->xmit_cmds[0]->cmd_status = 0;
    p->scb->cbl_offset = make16(p->xmit_cmds[0]);
    p->scb->cmd = CUC_START;

    dev->trans_start = jiffies;
    ni_attn586();
    while(p->scb->cmd)
      for(len=0;len<256;len++);

  /*  DELAY(1); */ /* TEST;TEST;TEST */
#  else
    next_nop = (p->nop_point + 1) & 0x1;
    p->xmit_buffs[0]->size = TBD_LAST | len;

    p->xmit_cmds[0]->cmd_cmd    = CMD_XMIT | CMD_INT;
    p->xmit_cmds[0]->cmd_status = 0;
    p->xmit_cmds[0]->cmd_link   = p->nop_cmds[next_nop]->cmd_link 
                                = make16((p->nop_cmds[next_nop]));
    p->nop_cmds[next_nop]->cmd_status = 0;

    p->nop_cmds[p->nop_point]->cmd_link = make16((p->xmit_cmds[0]));
    dev->trans_start = jiffies;
    p->nop_point = next_nop;
#  endif
#else
    p->xmit_buffs[p->xmit_count]->size = TBD_LAST | len;
    if( (next_nop = p->xmit_count + 1) == NUM_XMIT_BUFFS ) 
      next_nop = 0;

    p->xmit_cmds[p->xmit_count]->cmd_cmd  = CMD_XMIT | CMD_INT;
    p->xmit_cmds[p->xmit_count]->cmd_status  = 0;
    p->xmit_cmds[p->xmit_count]->cmd_link = p->nop_cmds[next_nop]->cmd_link 
                                          = make16((p->nop_cmds[next_nop]));
    p->nop_cmds[next_nop]->cmd_status = 0;

    p->nop_cmds[p->xmit_count]->cmd_link = make16((p->xmit_cmds[p->xmit_count]));
    dev->trans_start = jiffies;
    p->xmit_count = next_nop;
  
    cli();
    if(p->xmit_count != p->xmit_last)
      dev->tbusy = 0;
    sti();
#endif
  }

  dev_kfree_skb(skb,FREE_WRITE);

  return 0;
}

static struct enet_statistics *ni52_get_stats(struct device *dev)
{
  struct priv *p = (struct priv *) dev->priv;
#ifdef DEBUG3
  printk("ni52: errs, crc %d, align %d, resource %d, ovrn %d.\n",(int) p->scb->crc_errs,(int) p->scb->aln_errs,(int) p->scb->rsc_errs,(int) p->scb->ovrn_errs); 
#endif
  return &p->stats;
}

static void set_multicast_list(struct device *dev, int num_addrs, void *addrs)
{
/*
  struct priv *p = (struct priv *) dev->priv;
  volatile struct configure_cmd_struct  *cfg_cmd;
*/

  if(!num_addrs)
    printk("%s: Currently, the Ni52 driver doesn't support promiscuous or multicast mode.\n",dev->name);

#if 0
  p->scb->cmd = CUC_SUSPEND;
  ni_attn586();
  while(p->scb->cmd);
  p->scb->cmd = RUC_SUSPEND; 
  ni_attn586();
  while(p->scb->cmd);

  cfg_cmd = (struct configure_cmd_struct *) p->xmit_cbuffs[0]; /* we're using a transmitcommand */
 
  cfg_cmd->cmd_status = 0;
  cfg_cmd->cmd_cmd    = CMD_CONFIGURE | CMD_LAST;
  cfg_cmd->cmd_link   = 0xffff;

  cfg_cmd->byte_cnt   = 0x0a; /* number of cfg bytes */
  cfg_cmd->fifo       = 0x08; /* fifo-limit (8=tx:32/rx:64) */
  cfg_cmd->sav_bf     = 0x40; /* hold or discard bad recv frames (bit 7) */
  cfg_cmd->adr_len    = 0x2e; /* addr_len |!src_insert |pre-len |loopback */
  cfg_cmd->priority   = 0x00;
  cfg_cmd->ifd        = 0x60;
  cfg_cmd->time_low   = 0x00;
  cfg_cmd->time_high  = 0xf2;
  cfg_cmd->promisc    = 0x01; /* promisc on */
  cfg_cmd->carr_coll  = 0x00;

  p->scb->cbl_offset = make16(cfg_cmd);

  p->scb->cmd = CUC_START; /* cmd.-unit start */
  ni_attn586();
  while(p->scb->cmd);

  p->scb->cbl_offset = p->nop_cmds[0]->cmd_link = make16(p->nop_cmds[0]);
  p->scb->cmd = CUC_START;
  ni_atthn586();
  while(p->scb->cmd);
  p->scb->cmd = RUC_RESUME;
  ni_atthn586();
  while(p->scb->cmd);
#endif

}
