/*********************************************************************
 *                
 * Filename:      toshoboe.c
 * Version:       0.1
 * Description:   Driver for the Toshiba OBOE (or type-O or 700 or 701)
 *                FIR Chipset. 
 * Status:        Experimental.
 * Author:        James McKenzie <james@fishsoup.dhs.org>
 * Created at:    Sat May 8  12:35:27 1999
 * 
 *     Copyright (c) 1999 James McKenzie, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither James McKenzie nor Cambridge University admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *
 *     Applicable Models : Libretto 100CT. and many more
 *     Toshiba refers to this chip as the type-O IR port.
 *
 ********************************************************************/

/* This driver is experimental, I have only three ir devices */
/* an olivetti notebook which doesn't have FIR, a toshiba libretto, and */
/* an hp printer, this works fine at 4MBPS with my HP printer */

static char *rcsid = "$Id: toshoboe.c,v 1.5 1999/05/12 12:24:39 root Exp root $";

/* 
 * $Log: toshoboe.c,v $
 * Revision 1.5  1999/05/12 12:24:39  root
 * *** empty log message ***
 *
 * Revision 1.4  1999/05/12 11:55:08  root
 * *** empty log message ***
 *
 * Revision 1.3  1999/05/09 01:33:12  root
 * *** empty log message ***
 *
 * Revision 1.2  1999/05/09 01:30:38  root
 * *** empty log message ***
 *
 * Revision 1.1  1999/05/09 01:25:04  root
 * Initial revision
 * 
 */

/* Define this to have only one frame in the XMIT or RECV queue */
/* Toshiba's drivers do this, but it disables back to back tansfers */
/* I think that the chip may have some problems certainly, I have */
/* seen it jump over tasks in the taskfile->xmit with this turned on */
#define ONETASK

/* To adjust the number of tasks in use edit toshoboe.h */

/* Define this to enable FIR and MIR support */
#define ENABLE_FAST

/* Number of ports this driver can support, you also need to edit dev_self below */
#define NSELFS 4

/* Size of IO window */
#define CHIP_IO_EXTENT	0x1f

/* Transmit and receive buffer sizes, adjust at your peril */
#define RX_BUF_SZ 	4196
#define TX_BUF_SZ	4196

/* No user servicable parts below here */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/system.h>
#include <asm/io.h>

#include <net/irda/wrapper.h>
#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/irda_device.h>

#include <net/irda/toshoboe.h>

static char *driver_name = "toshoboe";

static struct toshoboe_cb *dev_self[NSELFS + 1] =
{NULL, NULL, NULL, NULL, NULL};

/* Shutdown the chip and point the taskfile reg somewhere else */
static void 
toshoboe_stopchip (struct toshoboe_cb *self)
{
  DEBUG (4, __FUNCTION__ "()\n");

  outb_p (0x0e, OBOE_REG_11);

  outb_p (0x00, OBOE_RST);
  outb_p (0x3f, OBOE_TFP2);     /*Write the taskfile address */
  outb_p (0xff, OBOE_TFP1);
  outb_p (0xff, OBOE_TFP0);
  outb_p (0x0f, OBOE_REG_1B);
  outb_p (0xff, OBOE_REG_1A);
  outb_p (0x00, OBOE_ISR);      /*FIXME: should i do this to disbale ints */
  outb_p (0x80, OBOE_RST);
  outb_p (0xe, OBOE_LOCK);
}

/*Set the baud rate */
static void 
toshoboe_setbaud (struct toshoboe_cb *self, int baud)
{
  DEBUG (4, __FUNCTION__ "()\n");

  printk (KERN_WARNING "ToshOboe: seting baud to %d\n", baud);

  cli ();
  switch (baud)
    {
    case 2400:
      outb_p (OBOE_PMDL_SIR, OBOE_PMDL);
      outb_p (OBOE_SMDL_SIR, OBOE_SMDL);
      outb_p (0xbf, OBOE_UDIV);
      break;
    case 4800:
      outb_p (OBOE_PMDL_SIR, OBOE_PMDL);
      outb_p (OBOE_SMDL_SIR, OBOE_SMDL);
      outb_p (0x5f, OBOE_UDIV);
      break;
    case 9600:
      outb_p (OBOE_PMDL_SIR, OBOE_PMDL);
      outb_p (OBOE_SMDL_SIR, OBOE_SMDL);
      outb_p (0x2f, OBOE_UDIV);
      break;
    case 19200:
      outb_p (OBOE_PMDL_SIR, OBOE_PMDL);
      outb_p (OBOE_SMDL_SIR, OBOE_SMDL);
      outb_p (0x17, OBOE_UDIV);
      break;
    case 38400:
      outb_p (OBOE_PMDL_SIR, OBOE_PMDL);
      outb_p (OBOE_SMDL_SIR, OBOE_SMDL);
      outb_p (0xb, OBOE_UDIV);
      break;
    case 57600:
      outb_p (OBOE_PMDL_SIR, OBOE_PMDL);
      outb_p (OBOE_SMDL_SIR, OBOE_SMDL);
      outb_p (0x7, OBOE_UDIV);
      break;
    case 115200:
      outb_p (OBOE_PMDL_SIR, OBOE_PMDL);
      outb_p (OBOE_SMDL_SIR, OBOE_SMDL);
      outb_p (0x3, OBOE_UDIV);
      break;
    case 1152000:
      outb_p (OBOE_PMDL_MIR, OBOE_PMDL);
      outb_p (OBOE_SMDL_MIR, OBOE_SMDL);
      outb_p (0x1, OBOE_UDIV);
      break;
    case 4000000:
      outb_p (OBOE_PMDL_FIR, OBOE_PMDL);
      outb_p (OBOE_SMDL_FIR, OBOE_SMDL);
      outb_p (0x0, OBOE_UDIV);
      break;
    }

  sti ();

  outb_p (0x00, OBOE_RST);
  outb_p (0x80, OBOE_RST);
  outb_p (0x01, OBOE_REG_9);

}

/* Wake the chip up and get it looking at the taskfile */
static void 
toshoboe_startchip (struct toshoboe_cb *self)
{
  __u32 physaddr;

  DEBUG (4, __FUNCTION__ "()\n");


  outb_p (0, OBOE_LOCK);
  outb_p (0, OBOE_RST);
  outb_p (OBOE_NTR_VAL, OBOE_NTR);
  outb_p (0xf0, OBOE_REG_D);
  outb_p (0xff, OBOE_ISR);
  outb_p (0x0f, OBOE_REG_1A);
  outb_p (0xff, OBOE_REG_1B);


  physaddr = virt_to_bus (self->taskfile);

  outb_p ((physaddr >> 0x0a) & 0xff, OBOE_TFP0);
  outb_p ((physaddr >> 0x12) & 0xff, OBOE_TFP1);
  outb_p ((physaddr >> 0x1a) & 0x3f, OBOE_TFP2);

  outb_p (0x0e, OBOE_REG_11);
  outb_p (0x80, OBOE_RST);

  toshoboe_setbaud (self, 9600);

}

/*Let the chip look at memory */
static void 
toshoboe_enablebm (struct toshoboe_cb *self)
{
  DEBUG (4, __FUNCTION__ "()\n");
  pci_set_master (self->pdev);
}

/*Don't let the chip look at memory */
static void 
toshoboe_disablebm (struct toshoboe_cb *self)
{
  __u8 command;
  DEBUG (4, __FUNCTION__ "()\n");

  pci_read_config_byte (self->pdev, PCI_COMMAND, &command);
  command &= ~PCI_COMMAND_MASTER;
  pci_write_config_byte (self->pdev, PCI_COMMAND, command);

}

/*setup the taskfile */
static void 
toshoboe_initbuffs (struct toshoboe_cb *self)
{
  int i;

  DEBUG (4, __FUNCTION__ "()\n");

  cli ();

  for (i = 0; i < TX_SLOTS; ++i)
    {
      self->taskfile->xmit[i].len = 0;
      self->taskfile->xmit[i].control = 0x00;
      self->taskfile->xmit[i].buffer = virt_to_bus (self->xmit_bufs[i]);
    }

  for (i = 0; i < RX_SLOTS; ++i)
    {
      self->taskfile->recv[i].len = 0;
      self->taskfile->recv[i].control = 0x83;
      self->taskfile->recv[i].buffer = virt_to_bus (self->recv_bufs[i]);
    }

  sti ();
}


/*Transmit something */
static int 
toshoboe_hard_xmit (struct sk_buff *skb, struct device *dev)
{
  struct irda_device *idev;
  struct toshoboe_cb *self;
  int mtt, len;

  idev = (struct irda_device *) dev->priv;
  ASSERT (idev != NULL, return 0;);
  ASSERT (idev->magic == IRDA_DEVICE_MAGIC, return 0;);

  self = idev->priv;
  ASSERT (self != NULL, return 0;);


#ifdef ONETASK
  if (self->txpending)
    return -EBUSY;

  self->txs = inb_p (OBOE_XMTT) - OBOE_XMTT_OFFSET;

  self->txs &= 0x3f;

#endif

  if (self->taskfile->xmit[self->txs].control)
    return -EBUSY;


  if (inb_p (OBOE_RST) & OBOE_RST_WRAP)
    {
      len = async_wrap_skb (skb, self->xmit_bufs[self->txs], TX_BUF_SZ);
    }
  else
    {
      len = skb->len;
      memcpy (self->xmit_bufs[self->txs], skb->data, len);
    }
  self->taskfile->xmit[self->txs].len = len & 0x0fff;



  outb_p (0, OBOE_RST);
  outb_p (0x1e, OBOE_REG_11);

  self->taskfile->xmit[self->txs].control = 0x84;

  mtt = irda_get_mtt (skb);
  if (mtt)
    udelay (mtt);

  self->txpending++;

  /*FIXME: ask about tbusy,media_busy stuff, for the moment */
  /*tbusy means can't queue any more */
#ifndef ONETASK
  if (self->txpending == TX_SLOTS)
    {
#else
  {
#endif
    if (irda_lock ((void *) &dev->tbusy) == FALSE)
      return -EBUSY;
  }

  outb_p (0x80, OBOE_RST);
  outb_p (1, OBOE_REG_9);

  self->txs++;
  self->txs %= TX_SLOTS;

  dev_kfree_skb (skb);

  return 0;
}

/*interrupt handler */
static void 
toshoboe_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
  struct irda_device *idev = (struct irda_device *) dev_id;
  struct toshoboe_cb *self;
  __u8 irqstat;
  struct sk_buff *skb;

  if (idev == NULL)
    {
      printk (KERN_WARNING "%s: irq %d for unknown device.\n",
              driver_name, irq);
      return;
    }

  self = idev->priv;

  if (!self)
    return;

  DEBUG (4, __FUNCTION__ "()\n");

  irqstat = inb_p (OBOE_ISR);

/* woz it us */
  if (!(irqstat & 0xf8))
    return;

  outb_p (irqstat, OBOE_ISR);   /*Acknologede it */


/* Txdone */
  if (irqstat & OBOE_ISR_TXDONE)
    {
      self->txpending--;

      idev->stats.tx_packets++;

      idev->media_busy = FALSE;
      idev->netdev.tbusy = 0;

      mark_bh (NET_BH);
    }

  if (irqstat & OBOE_ISR_RXDONE)
    {

#ifdef ONETASK
      self->rxs = inb_p (OBOE_RCVT);
      self->rxs += (RX_SLOTS - 1);
      self->rxs %= RX_SLOTS;
#else
      while (self->taskfile->recv[self->rxs].control == 0)
#endif
        {
          int len = self->taskfile->recv[self->rxs].len;
	
	  if (len>2) len-=2;

          skb = dev_alloc_skb (len + 1);
          if (skb)
            {
              skb_reserve (skb, 1);

              skb_put (skb, len);
              memcpy (skb->data, self->recv_bufs[self->rxs], len);

              idev->stats.rx_packets++;
              skb->dev = &idev->netdev;
              skb->mac.raw = skb->data;
              skb->protocol = htons (ETH_P_IRDA);
            }
          else
            {
              printk (KERN_INFO __FUNCTION__
                      "(), memory squeeze, dropping frame.\n");
            }



          self->taskfile->recv[self->rxs].control = 0x83;
          self->taskfile->recv[self->rxs].len = 0x0;

          self->rxs++;
          self->rxs %= RX_SLOTS;

          if (skb)
            netif_rx (skb);

        }

    }

  if (irqstat & OBOE_ISR_20)
    {
      printk (KERN_WARNING "Oboe_irq: 20\n");
    }
  if (irqstat & OBOE_ISR_10)
    {
      printk (KERN_WARNING "Oboe_irq: 10\n");
    }
  if (irqstat & 0x8)
    {
      /*FIXME: I think this is a TX or RX error of some sort */

      idev->stats.tx_errors++;
      idev->stats.rx_errors++;

    }


}



/* Change the baud rate */
static void 
toshoboe_change_speed (struct irda_device *idev, int speed)
{
  struct toshoboe_cb *self;
  DEBUG (4, __FUNCTION__ "()\n");

  ASSERT (idev != NULL, return;);
  ASSERT (idev->magic == IRDA_DEVICE_MAGIC, return;);

  self = idev->priv;
  ASSERT (self != NULL, return;);

  idev->io.baudrate = speed;

  toshoboe_setbaud (self, speed);

}


/* Check all xmit_tasks finished */
static void 
toshoboe_wait_until_sent (struct irda_device *idev)
{
  struct toshoboe_cb *self;
  int i;

  DEBUG (4, __FUNCTION__ "()\n");

  ASSERT (idev != NULL, return;);
  ASSERT (idev->magic == IRDA_DEVICE_MAGIC, return;);

  self = idev->priv;
  ASSERT (self != NULL, return;);

  for (i = 0; i < TX_SLOTS; ++i)
    {
      while (self->taskfile->xmit[i].control)
        {
          current->state = TASK_INTERRUPTIBLE;
          schedule_timeout (6);
        }
    }

}

static int 
toshoboe_is_receiving (struct irda_device *idev)
{
  DEBUG (4, __FUNCTION__ "()\n");

/*FIXME Can't tell! */
  return (FALSE);
}


static int 
toshoboe_net_init (struct device *dev)
{
  DEBUG (4, __FUNCTION__ "()\n");

  /* Setup to be a normal IrDA network device driver */
  irda_device_setup (dev);

  /* Insert overrides below this line! */
  return 0;
}




static int 
toshoboe_net_open (struct device *dev)
{
  struct irda_device *idev;
  struct toshoboe_cb *self;

  DEBUG (4, __FUNCTION__ "()\n");

  ASSERT (dev != NULL, return -1;);
  idev = (struct irda_device *) dev->priv;

  ASSERT (idev != NULL, return 0;);
  ASSERT (idev->magic == IRDA_DEVICE_MAGIC, return 0;);

  self = idev->priv;
  ASSERT (self != NULL, return 0;);

  if (request_irq (idev->io.irq, toshoboe_interrupt,
                   SA_SHIRQ | SA_INTERRUPT, idev->name, (void *) idev))
    {

      return -EAGAIN;
    }

  toshoboe_initbuffs (self);
  toshoboe_enablebm (self);
  toshoboe_startchip (self);


  cli ();

  /*FIXME: need to test this carefully to check which one */
  /*of the two possible startup logics the chip uses */
  /*although it won't make any difference if no-one xmits durining init */
  /*and none what soever if using ONETASK */

  self->rxs = inb_p (OBOE_RCVT);
  self->txs = inb_p (OBOE_XMTT) - OBOE_XMTT_OFFSET;

#if 0
  self->rxs = 0;
  self->txs = 0;
#endif
#if 0
  self->rxs = RX_SLOTS - 1;
  self->txs = 0;
#endif


  self->txpending = 0;

  sti ();


  dev->tbusy = 0;
  dev->interrupt = 0;
  dev->start = 1;

  MOD_INC_USE_COUNT;

  return 0;

}

static int 
toshoboe_net_close (struct device *dev)
{
  struct irda_device *idev;
  struct toshoboe_cb *self;

  DEBUG (4, __FUNCTION__ "()\n");

  ASSERT (dev != NULL, return -1;);
  idev = (struct irda_device *) dev->priv;

  ASSERT (idev != NULL, return 0;);
  ASSERT (idev->magic == IRDA_DEVICE_MAGIC, return 0;);

  dev->tbusy = 1;
  dev->start = 0;


  self = idev->priv;

  ASSERT (self != NULL, return 0;);

  free_irq (idev->io.irq, (void *) idev);

  toshoboe_stopchip (self);
  toshoboe_disablebm (self);

  MOD_DEC_USE_COUNT;

  return 0;

}



#ifdef MODULE

static int 
toshoboe_close (struct irda_device *idev)
{
  struct toshoboe_cb *self;
  int i;

  DEBUG (4, __FUNCTION__ "()\n");

  ASSERT (idev != NULL, return -1;);
  ASSERT (idev->magic == IRDA_DEVICE_MAGIC, return -1;);

  self = idev->priv;

  ASSERT (self != NULL, return -1;);

  toshoboe_stopchip (self);

  release_region (idev->io.iobase, idev->io.io_ext);


  for (i = 0; i < TX_SLOTS; ++i)
    {
      kfree (self->xmit_bufs[i]);
      self->xmit_bufs[i] = NULL;
    }

  for (i = 0; i < RX_SLOTS; ++i)
    {
      kfree (self->recv_bufs[i]);
      self->recv_bufs[i] = NULL;
    }


  kfree (self->taskfilebuf);
  self->taskfilebuf = NULL;
  self->taskfile = NULL;


  irda_device_close (idev);

  return (0);

}

#endif



static int 
toshoboe_open (struct pci_dev *pci_dev)
{
  struct toshoboe_cb *self;
  struct irda_device *idev;
  int i = 0;
  int ok=0;


  DEBUG (4, __FUNCTION__ "()\n");

  while (dev_self[i])
    i++;

  if (i == NSELFS)
    {
      printk (KERN_ERR "Oboe: No more instances available");
      return -ENOMEM;
    }

  self = kmalloc (sizeof (struct toshoboe_cb), GFP_KERNEL);

  if (self == NULL)
    {
      printk (KERN_ERR "IrDA: Can't allocate memory for "
              "IrDA control block!\n");
      return -ENOMEM;
    }

  memset (self, 0, sizeof (struct toshoboe_cb));


  dev_self[i] = self;

  self->pdev = pci_dev;
  self->base = pci_dev->resource[0].start;

  idev = &self->idev;

  /*Setup idev */

  idev->io.iobase = self->base;
  idev->io.irq = pci_dev->irq;
  idev->io.io_ext = CHIP_IO_EXTENT;

  /* Lock the port that we need */
  i = check_region (idev->io.iobase, idev->io.io_ext);
  if (i < 0)
    {
      DEBUG (0, __FUNCTION__ "(), can't get iobase of 0x%03x\n",
             idev->io.iobase);

      dev_self[i] = NULL;
      kfree (self);

      return -ENODEV;
    }

  request_region (idev->io.iobase, idev->io.io_ext, driver_name);

  irda_init_max_qos_capabilies (&idev->qos);

  idev->qos.baud_rate.bits = IR_2400 | /*IR_4800 | */ IR_9600 | IR_19200 |
    IR_115200;
#ifdef ENABLE_FAST
 idev->qos.baud_rate.bits|= IR_576000 | IR_1152000 | (IR_4000000 << 8);
#endif

  idev->qos.min_turn_time.bits = 0xff;  /*FIXME: what does this do? */

  irda_qos_bits_to_value (&idev->qos);

  idev->flags = IFF_SIR | IFF_DMA | IFF_PIO;

#ifdef ENABLE_FAST
  idev->flags |= IFF_FIR;
#endif

  /* These aren't much use as we need to have a whole panoply of
   * buffers running */

  idev->rx_buff.flags = 0;
  idev->tx_buff.flags = 0;
  idev->rx_buff.truesize = 0;
  idev->rx_buff.truesize = 0;

  idev->change_speed = toshoboe_change_speed;
  idev->wait_until_sent = toshoboe_wait_until_sent;
  idev->is_receiving = toshoboe_is_receiving;

  idev->netdev.init = toshoboe_net_init;
  idev->netdev.hard_start_xmit = toshoboe_hard_xmit;
  idev->netdev.open = toshoboe_net_open;
  idev->netdev.stop = toshoboe_net_close;


  /* Now setup the endless buffers we need */

  self->txs = 0;
  self->rxs = 0;

  self->taskfilebuf = kmalloc (OBOE_TASK_BUF_LEN, GFP_KERNEL | GFP_DMA);
  if (!self->taskfilebuf) {
	printk(KERN_ERR "toshoboe: kmalloc for DMA failed()\n");
	kfree(self);
	return -ENOMEM;
  }


  memset (self->taskfilebuf, 0, OBOE_TASK_BUF_LEN);

  /*We need to align the taskfile on a taskfile size boundary */
  {
    __u32 addr;

    addr = (__u32) self->taskfilebuf;
    addr &= ~(sizeof (struct OboeTaskFile) - 1);
    addr += sizeof (struct OboeTaskFile);

    self->taskfile = (struct OboeTaskFile *) addr;
  }

  for (i = 0; i < TX_SLOTS; ++i)
    {
      self->xmit_bufs[i] = kmalloc (TX_BUF_SZ, GFP_KERNEL | GFP_DMA);
      if (self->xmit_bufs[i]) ok++;
    }

  for (i = 0; i < RX_SLOTS; ++i)
    {
      self->recv_bufs[i] = kmalloc (TX_BUF_SZ, GFP_KERNEL | GFP_DMA);
      if (self->recv_bufs[i]) ok++;
    }

  if (ok!=RX_SLOTS+TX_SLOTS) {
	printk(KERN_ERR  "toshoboe: kmalloc for buffers failed()\n");


  for (i = 0; i < TX_SLOTS; ++i) if (self->xmit_bufs[i]) kfree(self->xmit_bufs[i]);
  for (i = 0; i < RX_SLOTS; ++i) if (self->recv_bufs[i]) kfree(self->recv_bufs[i]);

	kfree(self);
	return -ENOMEM;

  }


  irda_device_open (idev, driver_name, self);

  printk (KERN_WARNING "ToshOboe: Using ");
#ifdef ONETASK
  printk ("single");
#else
  printk ("multiple");
#endif
  printk (" tasks, version %s\n", rcsid);

  return (0);
}

int __init toshoboe_init (void)
{
  struct pci_dev *pci_dev = NULL;
  int found = 0;

  do
    {
      pci_dev = pci_find_device (PCI_VENDOR_ID_TOSHIBA,
                                 PCI_DEVICE_ID_FIR701, pci_dev);
      if (pci_dev)
        {
          printk (KERN_WARNING "ToshOboe: Found 701 chip at 0x%0lx irq %d\n",
                  pci_dev->resource[0],
                  pci_dev->irq);

          if (!toshoboe_open (pci_dev))
            found++;
        }

    }
  while (pci_dev);

  if (found)
    return 0;

  return -ENODEV;
}

#ifdef MODULE

static void 
toshoboe_cleanup (void)
{
  int i;

  DEBUG (4, __FUNCTION__ "()\n");

  for (i = 0; i < 4; i++)
    {
      if (dev_self[i])
        toshoboe_close (&(dev_self[i]->idev));
    }
}



int 
init_module (void)
{
  return toshoboe_init ();
}


void 
cleanup_module (void)
{
  toshoboe_cleanup ();
}


#endif
