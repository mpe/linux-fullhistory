/*
 * ni6510 (am7990 'lance' chip) driver for Linux-net-3 by MH
 * Alphacode v0.51 (96/02/20) for 1.3.66 (or later)
 *
 * copyright (c) 1994,1995,1996 by M.Hipp
 *
 * This is an extension to the Linux operating system, and is covered by the
 * same Gnu Public License that covers the Linux-kernel.
 *
 * comments/bugs/suggestions can be sent to:
 *   Michael Hipp
 *   email: Michael.Hipp@student.uni-tuebingen.de
 *
 * sources:
 *   some things are from the 'ni6510-packet-driver for dos by Russ Nelson'
 *   and from the original drivers by D.Becker
 *
 * known problems:
 *   on some PCI boards (including my own) the card/board/ISA-bridge has
 *   problems with bus master DMA. This results in lotsa overruns.
 *   It may help to '#define RCV_PARANOIA_CHECK'
 *   or just play with your BIOS options to optimize ISA-DMA access.
 *
 * credits:
 *   thanx to Jason Sullivan for sending me a ni6510 card!
 *
 * simple performance test:
 *   8.1 seconds for getting a 8MB file via FTP -> near 1MB/s
 */

/*
 * 96.Feb.19: fixed a few bugs .. cleanups .. tested for 1.3.66
 *            hopefully no more 16MB limit
 *
 * 95.Nov.18: multicast tweaked (AC).
 *
 * 94.Aug.22: changes in xmit_intr (ack more than one xmitted-packet), ni65_send_packet (p->lock) (MH)
 *
 * 94,July.16: fixed bugs in recv_skb and skb-alloc stuff  (MH)
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
#include <asm/dma.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "ni65.h"

/*
 * the current setting allows max. performance
 * for 'RCV_PARANOIA_CHECK' read the 'known problems' part in 
 * the header of this file
 */
#define RCV_VIA_SKB
#undef RCV_PARANOIA_CHECK
#define XMT_VIA_SKB

/*
 * a few card specific defines
 */
#define NI65_TOTAL_SIZE    16
#define NI65_ADDR0 0x02
#define NI65_ADDR1 0x07
#define NI65_ADDR2 0x01
#define NI65_ID0   0x00
#define NI65_ID1   0x55

#define PORT dev->base_addr

/*
 * buffer configuration
 */
#define RMDNUM 8
#define RMDNUMMASK 0x60000000 /* log2(RMDNUM)<<29 */
#define TMDNUM 4
#define TMDNUMMASK 0x40000000 /* log2(TMDNUM)<<29 */

#define R_BUF_SIZE 1536
#define T_BUF_SIZE 1536

/*
 * lance register defines
 */
#define L_DATAREG 0x00
#define L_ADDRREG 0x02

#define L_RESET   0x04
#define L_CONFIG  0x05
#define L_EBASE   0x08

/* 
 * to access the am7990-regs, you have to write
 * reg-number into L_ADDRREG, then you can access it using L_DATAREG
 */
#define CSR0 0x00
#define CSR1 0x01
#define CSR2 0x02
#define CSR3 0x03

#define writereg(val,reg) {outw(reg,PORT+L_ADDRREG);inw(PORT+L_ADDRREG); \
                           outw(val,PORT+L_DATAREG);inw(PORT+L_DATAREG);}
#define readreg(reg) (outw(reg,PORT+L_ADDRREG),inw(PORT+L_ADDRREG),\
                       inw(PORT+L_DATAREG))
#define writedatareg(val) {outw(val,PORT+L_DATAREG);inw(PORT+L_DATAREG);}

static int  ni65_probe1(struct device **dev,int);
static void ni65_interrupt(int irq, void * dev_id, struct pt_regs *regs);
static void ni65_recv_intr(struct device *dev,int);
static void ni65_xmit_intr(struct device *dev,int);
static int  ni65_open(struct device *dev);
static int  ni65_am7990_reinit(struct device *dev);
static int  ni65_send_packet(struct sk_buff *skb, struct device *dev);
static int  ni65_close(struct device *dev);
static struct enet_statistics *ni65_get_stats(struct device *);
static void set_multicast_list(struct device *dev);

struct priv 
{
  struct rmd rmdhead[RMDNUM];
  struct tmd tmdhead[TMDNUM];
  struct init_block ib; 
  int rmdnum;
  int tmdnum,tmdlast;
#ifdef RCV_VIA_SKB
  struct sk_buff *recv_skb[RMDNUM];
#else
  void *recvbounce[RMDNUM];
#endif
#ifdef XMT_VIA_SKB
  struct sk_buff *tmd_skb[TMDNUM];
#endif
  void *tmdbounce[TMDNUM];
  int lock,xmit_queued;
  struct enet_statistics stats;
}; 

static int irqtab[] = { 9,12,15,5 }; /* irq config-translate */
static int dmatab[] = { 0,3,5,6 };   /* dma config-translate */
static int debuglevel = 0;

/*
 * open (most done by init) 
 */
static int ni65_open(struct device *dev)
{
  if(ni65_am7990_reinit(dev))
  {
    dev->tbusy     = 0;
    dev->interrupt = 0;
    dev->start     = 1;
    return 0;
  }
  else
  {
    dev->start = 0;
    return -EAGAIN;
  }
}

static int ni65_close(struct device *dev)
{
  outw(0,PORT+L_RESET); /* that's the hard way */
  dev->tbusy = 1;
  dev->start = 0;
  return 0; 
}

/* 
 * Probe The Card (not the lance-chip) 
 * and set hardaddress
 */ 
int ni65_probe(struct device *dev)
{
  int *port;
  static int ports[] = {0x300,0x320,0x340,0x360, 0};

  if(dev) {
    int base_addr = dev->base_addr;
    if (base_addr > 0x1ff)          /* Check a single specified location. */
       return ni65_probe1(&dev, base_addr);
    else if (base_addr > 0)         /* Don't probe at all. */
       return -ENXIO;
    dev->base_addr = base_addr;
  }

  for (port = ports; *port; port++) 
  {
    int ioaddr = *port;

    if (check_region(ioaddr, NI65_TOTAL_SIZE))
       continue;
    if( !(inb(ioaddr+L_EBASE+6) == NI65_ID0) || 
        !(inb(ioaddr+L_EBASE+7) == NI65_ID1) )
       continue;
    if (ni65_probe1(&dev, ioaddr) == 0)
       return 0;
  }

  return -ENODEV;
}

int ni65_init(void)
{
        ni65_probe(NULL);
        return 0;
}

static int ni65_probe1(struct device **dev1,int ioaddr)
{
  int i;
  unsigned char *ptr;
  struct priv *p; 
  struct device *dev = *dev1;

  if(inb(ioaddr+L_EBASE+0) != NI65_ADDR0 || inb(ioaddr+L_EBASE+1) != NI65_ADDR1 
      || inb(ioaddr+L_EBASE+2) != NI65_ADDR2)
  {
    printk("%s: wrong Hardaddress \n",dev ? dev->name : "ni6510" );
    return -ENODEV;
  }

  if(!dev) {
    dev = init_etherdev(0,0);
    *dev1 = dev;
  }
  dev->base_addr = ioaddr;

  for(i=0;i<6;i++)
    dev->dev_addr[i] = inb(PORT+L_EBASE+i);

  if(dev->irq == 0) 
    dev->irq = irqtab[(inw(PORT+L_CONFIG)>>2)&3];
  if(dev->dma == 0)  
    dev->dma = dmatab[inw(PORT+L_CONFIG)&3];

  printk("%s: %s found at %#3lx, IRQ %d DMA %d.\n", dev->name,
           "ni6510", dev->base_addr, dev->irq,dev->dma);

  {        
    int irqval = request_irq(dev->irq, &ni65_interrupt,0,"ni6510",NULL);
    if (irqval) {
      printk ("%s: unable to get IRQ %d (irqval=%d).\n", 
                dev->name,dev->irq, irqval);
      return -EAGAIN;
    }
    if(request_dma(dev->dma, "ni6510") != 0)
    {
      printk("%s: Can't request dma-channel %d\n",dev->name,(int) dev->dma);
      free_irq(dev->irq,NULL);
      return -EAGAIN;
    }
  }
  irq2dev_map[dev->irq] = dev;

  /* 
   * Grab the region so we can find another board if autoIRQ fails. 
   */
  request_region(ioaddr,NI65_TOTAL_SIZE,"ni6510");

  dev->open               = ni65_open;
  dev->stop               = ni65_close;
  dev->hard_start_xmit    = ni65_send_packet;
  dev->get_stats          = ni65_get_stats;
  dev->set_multicast_list = set_multicast_list;

  ether_setup(dev);

  dev->interrupt      = 0;
  dev->tbusy          = 0;
  dev->start          = 0;

  /* 
   * we need 8-aligned memory ..
   */
  ptr = kmalloc(sizeof(struct priv)+8,GFP_KERNEL|GFP_DMA);
  if(!ptr)
    return -ENOMEM;
  ptr = (unsigned char *) (((unsigned long) ptr + 7) & ~0x7);
  if( (unsigned long) ptr + sizeof(struct priv) > 0x1000000) {
    printk("%s: Can't alloc buffer in lower 16MB!\n",dev->name);
    return -EAGAIN;
  }
  p = dev->priv = (struct priv *) ptr;
  memset((char *) dev->priv,0,sizeof(struct priv));

  for(i=0;i<TMDNUM;i++)
  {
    if( (ptr = kmalloc(T_BUF_SIZE,GFP_KERNEL | GFP_DMA )) == NULL) {
      printk("%s: Can't alloc Xmit-Mem.\n",dev->name);
      return -ENOMEM;
    }
    if( (unsigned long) (ptr+T_BUF_SIZE) > 0x1000000) {
      printk("%s: Can't alloc Xmit-Mem in lower 16MB!\n",dev->name);
      return -EAGAIN;
    }
    p->tmdbounce[i] = ptr;
#ifdef XMT_VIA_SKB
    p->tmd_skb[i] = NULL;
#endif
  }

#ifdef RCV_VIA_SKB
   for(i=0;i<RMDNUM;i++)
   {
     struct sk_buff *skb;
     if( !(skb = dev_alloc_skb(R_BUF_SIZE+2)) ) {
       printk("%s: unable to alloc recv-mem\n",dev->name);
       return -ENOMEM;
     }
     skb->dev = dev;
     skb_reserve(skb,2);
     skb_put(skb,R_BUF_SIZE);	/* grab the whole space .. (not necessary) */
     if( (unsigned long) (skb->data + R_BUF_SIZE) > 0x1000000 ) {
       printk("%s: unable to alloc receive-memory in lower 16MB!\n",dev->name);
       return -EAGAIN;
     }
     p->recv_skb[i] = skb;
   }
#else
   for(i=0;i<RMDNUM;i++)
   {
     if( !(p->recvbounce[i] = kmalloc(R_BUF_SIZE,GFP_KERNEL | GFP_DMA )) ) {
       printk("%s: unable to alloc recv-mem\n",dev->name);
       return -ENOMEM;
     }
     if( (unsigned long) p->recvbounce[i] + R_BUF_SIZE > 0x1000000 ) {
       printk("%s: unable to alloc receive-memory in lower 16MB!\n",dev->name);
       return -EAGAIN;
     }
   }
#endif

  return 0; /* we've found everything */
}

/* 
 * init lance (write init-values .. init-buffers) (open-helper)
 */

static int ni65_am7990_reinit(struct device *dev)
{
   int i;
   struct priv *p = (struct priv *) dev->priv;

   p->lock = 0;
   p->xmit_queued = 0;

   disable_dma(dev->dma); /* I've never worked with dma, but we do it like the packetdriver */
   set_dma_mode(dev->dma,DMA_MODE_CASCADE);
   enable_dma(dev->dma); 

   outw(0,PORT+L_RESET); /* first: reset the card */
   if(inw(PORT+L_DATAREG) != 0x4)
   {
     printk(KERN_ERR "%s: can't RESET ni6510 card: %04x\n",dev->name,(int) inw(PORT+L_DATAREG));
     disable_dma(dev->dma);
     free_dma(dev->dma);
     free_irq(dev->irq, NULL);
     return 0;
   }

   p->tmdnum = 0; p->tmdlast = 0;
   for(i=0;i<TMDNUM;i++)
   {
     struct tmd *tmdp = p->tmdhead + i;
#ifdef XMT_VIA_SKB
     if(p->tmd_skb[i]) {
       dev_kfree_skb(p->tmd_skb[i],FREE_WRITE);
       p->tmd_skb[i] = NULL;
     }
#endif
     tmdp->u.buffer = 0x0;
     tmdp->u.s.status = XMIT_START | XMIT_END;
     tmdp->blen = tmdp->status2 = 0;
   }

   p->rmdnum = 0;
   for(i=0;i<RMDNUM;i++)
   {
     struct rmd *rmdp = p->rmdhead + i;
#ifdef RCV_VIA_SKB
     rmdp->u.buffer = (unsigned long) p->recv_skb[i]->data;
#else
     rmdp->u.buffer = (unsigned long) p->recvbounce[i];
#endif
     rmdp->blen = -(R_BUF_SIZE-8);
     rmdp->mlen = 0;
     rmdp->u.s.status = RCV_OWN;
   }
   
   for(i=0;i<6;i++)
     p->ib.eaddr[i] = dev->dev_addr[i];

   for(i=0;i<8;i++)
     p->ib.filter[i] = 0x0;
   p->ib.mode = 0x0;

   if(dev->flags & IFF_PROMISC) {
     p->ib.mode = M_PROM;
   }
   else if(dev->mc_count || dev->flags & IFF_ALLMULTI) {
     for(i=0;i<8;i++)
       p->ib.filter[i] = 0xff;
   }

   p->ib.trp = (unsigned long) p->tmdhead | TMDNUMMASK;
   p->ib.rrp = (unsigned long) p->rmdhead | RMDNUMMASK;

   writereg(0,CSR3);  /* busmaster/no word-swap */
   writereg((unsigned short) (((unsigned long) &(p->ib)) & 0xffff),CSR1);
   writereg((unsigned short) (((unsigned long) &(p->ib))>>16),CSR2);

   writereg(CSR0_INIT,CSR0); /* this changes L_ADDRREG to CSR0 */

  /*
   * NOW, WE WILL NEVER CHANGE THE L_ADDRREG, CSR0 IS ALWAYS SELECTED 
   */

    for(i=0;i<32;i++)
    {
      __delay((loops_per_sec>>8)); /* wait a while */
      if(inw(PORT+L_DATAREG) & CSR0_IDON) 
        break; /* init ok ? */
    }
    if(i == 32) 
    {
      printk(KERN_ERR "%s: can't init am7990/lance, status: %04x\n",dev->name,(int) inw(PORT+L_DATAREG));
      disable_dma(dev->dma);
      free_dma(dev->dma);
      free_irq(dev->irq, NULL);
      return 0; /* false */
    } 

    writedatareg(CSR0_CLRALL | CSR0_INEA | CSR0_STRT); /* start lance , enable interrupts */

    return 1; /* OK */
}
 
/* 
 * interrupt handler  
 */
static void ni65_interrupt(int irq, void * dev_id, struct pt_regs * regs)
{
  int csr0;
  struct device *dev = (struct device *) irq2dev_map[irq];
  int bcnt = 32;

  if (dev == NULL) {
    printk (KERN_ERR "ni65_interrupt(): irq %d for unknown device.\n", irq);
    return;
  }

  dev->interrupt = 1;

  while(--bcnt) {

    csr0 = inw(PORT+L_DATAREG);
    writedatareg(csr0 & CSR0_CLRALL); /* ack interrupts, disable int. */

    if(!(csr0 & (CSR0_ERR | CSR0_RINT | CSR0_TINT)))
      break;

    if(csr0 & CSR0_ERR)
    {
      struct priv *p = (struct priv *) dev->priv;

      if(csr0 & CSR0_BABL)
        p->stats.tx_errors++;
      if(csr0 & CSR0_MISS)
        p->stats.rx_errors++;
      if(csr0 & CSR0_MERR) {
        writedatareg(CSR0_STOP);
        writedatareg(CSR0_STRT);
      }
    }
    if(csr0 & CSR0_RINT) /* RECV-int? */
      ni65_recv_intr(dev,csr0);
    if(csr0 & CSR0_TINT) /* XMIT-int? */
      ni65_xmit_intr(dev,csr0);
  }

#ifdef RCV_PARANOIA_CHECK
{
  struct priv *p = (struct priv *) dev->priv;
  int i,f=0;
  for(i=0;i<RMDNUM;i++) {
     struct rmd *rmdp = p->rmdhead + ((p->rmdnum - i - 1) & (RMDNUM-1));
     if(! (rmdp->u.s.status & RCV_OWN) )
        f = 1;
     else if(f)
        break;
  }

  if(i < RMDNUM) {
    p->rmdnum = (p->rmdnum + 8 - i) & (RMDNUM - 1);
    printk(KERN_ERR "%s: Ooops, receive ring corrupted\n",dev->name);

    ni65_recv_intr(dev,csr0);
  }
}
#endif

  if(csr0 & (CSR0_RXON | CSR0_TXON) != (CSR0_RXON | CSR0_TXON) ) {
    writedatareg(CSR0_STOP);
    writedatareg(CSR0_STRT | CSR0_INEA);
  }
  else
    writedatareg(CSR0_INEA);
  dev->interrupt = 0;

  return;
}

/*
 * We have received an Xmit-Interrupt ..
 * send a new packet if necessary
 */
static void ni65_xmit_intr(struct device *dev,int csr0)
{
  struct priv *p = (struct priv *) dev->priv;

  while(p->xmit_queued)
  {
    struct tmd *tmdp = p->tmdhead + p->tmdlast;
    int tmdstat = tmdp->u.s.status;

    if(tmdstat & XMIT_OWN)
      break;

#ifdef XMT_VIA_SKB
    if(p->tmd_skb[p->tmdlast]) {
       dev_kfree_skb(p->tmd_skb[p->tmdlast],FREE_WRITE);
       p->tmd_skb[p->tmdlast] = NULL;
    }
#endif

    if(tmdstat & XMIT_ERR)
    {
#if 0
      if(tmdp->status2 & XMIT_TDRMASK && debuglevel > 3)
        printk(KERN_ERR "%s: tdr-problems (e.g. no resistor)\n",dev->name);
#endif
     /* checking some errors */
      if(tmdp->status2 & XMIT_RTRY)
        p->stats.tx_aborted_errors++;
      if(tmdp->status2 & XMIT_LCAR)
        p->stats.tx_carrier_errors++;
      if(tmdp->status2 & (XMIT_BUFF | XMIT_UFLO )) {
        p->stats.tx_fifo_errors++;
        writedatareg(CSR0_STOP);
        writedatareg(CSR0_STRT);
        if(debuglevel > 1)
          printk(KERN_ERR "%s: Xmit FIFO/BUFF error\n",dev->name);
      }
      if(debuglevel > 2)
        printk(KERN_ERR "%s: xmit-error: %04x %02x-%04x\n",dev->name,csr0,(int) tmdstat,(int) tmdp->status2);
      p->stats.tx_errors++;
      tmdp->status2 = 0;
    }
    else
      p->stats.tx_packets++;

    p->tmdlast = (p->tmdlast + 1) & (TMDNUM-1);
    if(p->tmdlast == p->tmdnum)
      p->xmit_queued = 0;
  }
  dev->tbusy = 0;
  mark_bh(NET_BH);
}

/*
 * We have received a packet
 */

static void ni65_recv_intr(struct device *dev,int csr0)
{
  struct rmd *rmdp; 
  int rmdstat,len;
  struct priv *p = (struct priv *) dev->priv;

  rmdp = p->rmdhead + p->rmdnum;
  while(!( (rmdstat = rmdp->u.s.status) & RCV_OWN))
  {
    if( (rmdstat & (RCV_START | RCV_END | RCV_ERR)) != (RCV_START | RCV_END) ) /* error or oversized? */ 
    {
      if(!(rmdstat & RCV_ERR)) {
        if(rmdstat & RCV_START)
        {
          p->stats.rx_length_errors++;
          printk(KERN_ERR "%s: recv, packet too long: %d\n",dev->name,rmdp->mlen & 0x0fff);
        }
      }
      else {
        printk(KERN_ERR "%s: receive-error: %04x, lance-status: %04x/%04x\n",
                dev->name,(int) rmdstat,csr0,(int) inw(PORT+L_DATAREG) );
        if(rmdstat & RCV_FRAM)
          p->stats.rx_frame_errors++;
        if(rmdstat & RCV_OFLO)
          p->stats.rx_over_errors++;
        if(rmdstat & (RCV_OFLO | RCV_BUF_ERR) ) { 
          writedatareg(CSR0_STOP);
          writedatareg(CSR0_STRT);
          if(debuglevel > 1)
            printk(KERN_ERR "%s: Rcv FIFO/BUFF error.\n",dev->name);
        }
        if(rmdstat & RCV_CRC)  p->stats.rx_crc_errors++;
      }
      rmdp->u.s.status = RCV_OWN; /* change owner */
      p->stats.rx_errors++;
    }
    else if( (len = (rmdp->mlen & 0x0fff) - 4) >= 60)
    {
#ifdef RCV_VIA_SKB
      struct sk_buff *skb = dev_alloc_skb(R_BUF_SIZE+2);
#else
      struct sk_buff *skb = dev_alloc_skb(len+2);
#endif
      if(skb)
      {
        skb_reserve(skb,2);
	skb->dev = dev;
#ifdef RCV_VIA_SKB
        if( (unsigned long) (skb->data + R_BUF_SIZE) > 0x1000000) {
          skb_put(skb,len);
          eth_copy_and_sum(skb, (unsigned char *)(p->recv_skb[p->rmdnum]->data),len,0);
        }
        else {
          struct sk_buff *skb1 = p->recv_skb[p->rmdnum];
          skb_put(skb,R_BUF_SIZE);
          p->recv_skb[p->rmdnum] = skb;
          rmdp->u.buffer = (unsigned long) skb->data;
          skb = skb1;
          skb_trim(skb,len);
        }
#else
        skb_put(skb,len);
        eth_copy_and_sum(skb, (unsigned char *) p->recvbounce[p->rmdnum],len,0);
#endif
        rmdp->u.s.status = RCV_OWN;
        p->stats.rx_packets++;
        skb->protocol=eth_type_trans(skb,dev);
        netif_rx(skb);
      }
      else
      {
        rmdp->u.s.status = RCV_OWN;
        printk(KERN_ERR "%s: can't alloc new sk_buff\n",dev->name);
        p->stats.rx_dropped++;
      }
    }
    else {
      rmdp->u.s.status = RCV_OWN;
      printk(KERN_INFO "%s: received runt packet\n",dev->name);
      p->stats.rx_errors++;
    }
    p->rmdnum = (p->rmdnum + 1) & (RMDNUM-1);
    rmdp = p->rmdhead + p->rmdnum;
  }
}

/*
 * kick xmitter ..
 */
static int ni65_send_packet(struct sk_buff *skb, struct device *dev)
{
  struct priv *p = (struct priv *) dev->priv;

  if(dev->tbusy)
  {
    int tickssofar = jiffies - dev->trans_start;
    if (tickssofar < 5)
      return 1;

    printk(KERN_ERR "%s: xmitter timed out, try to restart!\n",dev->name);
    ni65_am7990_reinit(dev);
    dev->tbusy=0;
    dev->trans_start = jiffies;
  }

  if(skb == NULL) {
    dev_tint(dev);
    return 0;
  }

  if (skb->len <= 0)
    return 0;

  if (set_bit(0, (void*)&dev->tbusy) != 0) {
     printk(KERN_ERR "%s: Transmitter access conflict.\n", dev->name);
     return 1;
  }
  if (set_bit(0, (void*)&p->lock)) {
	printk(KERN_ERR "%s: Queue was locked.\n", dev->name);
	return 1;
  }

  {
    short len = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
    struct tmd *tmdp = p->tmdhead + p->tmdnum;
    long flags;

#ifdef XMT_VIA_SKB
    if( (unsigned long) (skb->data + skb->len) > 0x1000000) {
#endif
      tmdp->u.buffer = (unsigned long ) p->tmdbounce[p->tmdnum]; 
      memcpy((char *) tmdp->u.buffer,(char *)skb->data,
               (skb->len > T_BUF_SIZE) ? T_BUF_SIZE : skb->len);
      dev_kfree_skb (skb, FREE_WRITE);
#ifdef XMT_VIA_SKB
    }
    else {
      tmdp->u.buffer = (unsigned long) skb->data;
      p->tmd_skb[p->tmdnum] = skb;
    }
#endif
    tmdp->blen = -len;

    save_flags(flags);
    cli();

    tmdp->u.s.status = XMIT_OWN | XMIT_START | XMIT_END;
    writedatareg(CSR0_TDMD | CSR0_INEA); /* enable xmit & interrupt */

    p->xmit_queued = 1;
    p->tmdnum = (p->tmdnum + 1) & (TMDNUM-1);

    dev->tbusy = (p->tmdnum == p->tmdlast) ? 1 : 0;
    p->lock = 0;
    dev->trans_start = jiffies;

    restore_flags(flags);
  }

  return 0;
}

static struct enet_statistics *ni65_get_stats(struct device *dev)
{
  return &((struct priv *) dev->priv)->stats;
}

static void set_multicast_list(struct device *dev)
{
	if(!ni65_am7990_reinit(dev))
		printk(KERN_ERR "%s: Can't switch card into MC mode!\n",dev->name);
	dev->tbusy = 0;
}

/*
 * END of ni65.c 
 */

