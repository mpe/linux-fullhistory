/*
 * DLCI		Implementation of Frame Relay protocol for Linux, according to
 *		RFC 1490.  This generic device provides en/decapsulation for an
 *		underlying hardware driver.  Routes & IPs are assigned to these
 *		interfaces.  Requires 'dlcicfg' program to create usable 
 *		interfaces, the initial one, 'dlci' is for IOCTL use only.
 *
 * Version:	@(#)dlci.c	0.10	23 Mar 1996
 *
 * Author:	Mike McLagan <mike.mclagan@linux.org>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>

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
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/if_frad.h>

#include <net/sock.h>

static const char *devname = "dlci";
static const char *version = "DLCI driver v0.10, 23 Mar 1996, mike.mclagan@linux.org";

static struct device *open_dev[CONFIG_DLCI_COUNT];

static char *basename[16];

int dlci_init(struct device *dev);

/* allow FRAD's to register their name as a valid FRAD */
int register_frad(const char *name)
{
   int i;

   if (!name)
      return(-EINVAL);

   for (i=0;i<sizeof(basename) / sizeof(char *);i++)
   {
      if (!basename[i])
         break;

      /* take care of multiple registrations */
      if (strcmp(basename[i], name) == 0)
         return(0);
   }

   if (i == sizeof(basename) / sizeof(char *))
      return(-ENOMEM);

   basename[i] = (char *) name;

   return(0);
}

int unregister_frad(const char *name)
{
   int i;

   if (!name)
      return(-EINVAL);

   for (i=0;i<sizeof(basename) / sizeof(char *);i++)
      if (basename[i] && (strcmp(basename[i], name) == 0))
         break;

   if (i == sizeof(basename) / sizeof(char *))
      return(-EINVAL);

   basename[i] = NULL;

   return(0);
}

/* 
   these encapsulate the RFC 1490 requirements as well as 
   deal with packet transmission and reception, working with
   the upper network layers 
 */

static int dlci_header(struct sk_buff *skb, struct device *dev, 
                           unsigned short type, void *daddr, void *saddr, 
                           unsigned len)
{
   struct fradhdr    hdr;
   struct dlci_local *dlp;
   unsigned          hlen;
   char              *dest;

   dlp = dev->priv;

   hdr.control = FRAD_I_UI;
   switch(type)
   {
      case ETH_P_IP:
         hdr.pad = FRAD_P_IP;
         hlen = sizeof(hdr.control) + sizeof(hdr.pad);
         break;

      /* feel free to add other types, if necessary */

      default:
         hdr.pad = FRAD_P_PADDING;
         hdr.NLPID = FRAD_P_SNAP;
         memset(hdr.OUI, 0, sizeof(hdr.OUI));
         hdr.PID = type;
         hlen = sizeof(hdr);
         break;
   }

   dest = skb_push(skb, hlen);
   if (!dest)
      return(0);

   memcpy(dest, &hdr, hlen);

   return(hlen);
}

static void dlci_receive(struct sk_buff *skb, struct device *dev)
{
   struct dlci_local *dlp;
   struct fradhdr    *hdr;
   int               process, header;

   dlp = dev->priv;
   hdr = (struct fradhdr *) skb->data;
   process = 0;
   header = 0;
   skb->dev = dev;

   if (hdr->control != FRAD_I_UI)
   {
      printk(KERN_NOTICE "%s: Invalid header flag 0x%02X.\n", dev->name, hdr->control);
      dlp->stats.rx_errors++;
   }
   else
      switch(hdr->pad)
      {
         case FRAD_P_PADDING:
            if (hdr->NLPID != FRAD_P_SNAP)
            {
               printk(KERN_NOTICE "%s: Unsupported NLPID 0x%02X.\n", dev->name, hdr->NLPID);
               dlp->stats.rx_errors++;
               break;
            }
    
            if (hdr->OUI[0] + hdr->OUI[1] + hdr->OUI[2] != 0)
            {
               printk(KERN_NOTICE "%s: Unsupported organizationally unique identifier 0x%02X-%02X-%02X.\n", dev->name, hdr->OUI[0], hdr->OUI[1], hdr->OUI[2]);
               dlp->stats.rx_errors++;
               break;
            }

            /* at this point, it's an EtherType frame */
            header = sizeof(struct fradhdr);
            skb->protocol = htons(hdr->PID);
            process = 1;
            break;

         case FRAD_P_IP:
            header = sizeof(hdr->control) + sizeof(hdr->pad);
            skb->protocol = htons(ETH_P_IP);
            process = 1;
            break;

         case FRAD_P_SNAP:
         case FRAD_P_Q933:
         case FRAD_P_CLNP:
            printk(KERN_NOTICE "%s: Unsupported NLPID 0x%02X.\n", dev->name, hdr->pad);
            dlp->stats.rx_errors++;
            break;

         default:
            printk(KERN_NOTICE "%s: Invalid pad byte 0x%02X.\n", dev->name, hdr->pad);
            dlp->stats.rx_errors++;
            break;            
      }

   if (process)
   {
      /* we've set up the protocol, so discard the header */
      skb->mac.raw = skb->data; 
      skb_pull(skb, header);
      netif_rx(skb);
      dlp->stats.rx_packets++;
   }
   else
      dev_kfree_skb(skb, FREE_WRITE);
}

static int dlci_transmit(struct sk_buff *skb, struct device *dev)
{
   struct dlci_local *dlp;
   int               ret;

   ret = 0;

   if (!skb || !dev)
      return(0);

   if (dev->tbusy)
      return(1);

   dlp = dev->priv;

   if (set_bit(0, (void*)&dev->tbusy) != 0)
      printk(KERN_WARNING "%s: transmitter access conflict.\n", dev->name);
   else
   {
      ret = dlp->slave->hard_start_xmit(skb, dlp->slave); 
      if (ret)
         dlp->stats.tx_errors++;
      else 
         dlp->stats.tx_packets++;

      /* per Alan Cox, always return 0, let the slave free the packet */
      ret = 0;
      dev->tbusy = 0;
   }

   return(ret);
}

int dlci_add(struct dlci_add *new)
{
   struct device       *master, *slave;
   struct dlci_local   *dlp;
   struct frad_local   *flp;
   struct dlci_add     dlci;
   int                 err, i;
   char                buf[10];

   err = verify_area(VERIFY_READ, new, sizeof(*new));
   if (err)
      return(err);

   memcpy_fromfs(&dlci, new, sizeof(dlci));

   /* validate slave device */
   slave = dev_get(dlci.devname);
   if (!slave)
      return(-ENODEV);

   if (slave->type != ARPHRD_FRAD)
      return(-EINVAL);

   /* check for registration */
   for (i=0;i<sizeof(basename) / sizeof(char *); i++)
      if ((basename[i]) && 
          (strncmp(dlci.devname, basename[i], strlen(basename[i])) == 0) && 
          (strlen(dlci.devname) > strlen(basename[i])))
         break;

   if (i == sizeof(basename) / sizeof(char *))
      return(-EINVAL);

   /* check for too many open devices : should this be dynamic ? */
   for(i=0;i<CONFIG_DLCI_COUNT;i++)
      if (!open_dev[i])
         break;

   if (i == CONFIG_DLCI_COUNT)
      return(-ENOSPC);  /*  #### Alan: Comments on this?? */

   /* create device name */
   sprintf(buf, "%s%02i", devname, i);

   master = kmalloc(sizeof(*master), GFP_KERNEL);
   if (!master)
      return(-ENOMEM);

   memset(master, 0, sizeof(*master));
   master->name = kmalloc(strlen(buf), GFP_KERNEL);

   if (!master->name)
   {
      kfree(master);
      return(-ENOMEM);
   }

   strcpy(master->name, buf);
   master->init = dlci_init;
   master->flags = 0;

   err = register_netdev(master);
   if (err < 0)
   {
      kfree(master->name);
      kfree(master);
      return(err);
   }

   *(short *)(master->dev_addr) = dlci.dlci;

   dlp = (struct dlci_local *) master->priv;
   dlp->slave = slave;

   flp = slave->priv;
   err = flp ? (*flp->assoc)(slave, master) : -EINVAL;
   if (err < 0)
   {
      unregister_netdev(master);
      kfree(master->priv);
      kfree(master->name);
      kfree(master);
      return(err);
   }

   memcpy_tofs(new->devname, buf, strlen(buf) + 1);
   open_dev[i] = master;

   MOD_INC_USE_COUNT;

   return(0);
}

int dlci_del(struct device *master)
{
   struct dlci_local *dlp;
   struct frad_local *flp;
   struct device     *slave;
   int               i, err;

   if (master->start)
      return(-EBUSY);

   dlp = master->priv;
   slave = dlp->slave;
   flp = slave->priv;

   err = (*flp->deassoc)(slave, master);
   if (err)
      return(err);

   unregister_netdev(master);

   for(i=0;i<CONFIG_DLCI_COUNT;i++)
      if (master == open_dev[i])
         break;

   if (i<CONFIG_DLCI_COUNT)
      open_dev[i] = NULL;

   kfree(master->priv);
   kfree(master->name);
   kfree(master);

   MOD_DEC_USE_COUNT;

   return(0);
}

int dlci_config(struct device *dev, struct dlci_conf *conf, int get)
{
   struct dlci_conf  config;
   struct dlci_local *dlp;
   struct frad_local *flp;
   int               err;

   dlp = dev->priv;

   flp = dlp->slave->priv;

   if (!get)
   {
      memcpy_fromfs(&config, conf, sizeof(struct dlci_conf));
      if (config.flags & ~DLCI_VALID_FLAGS)
         return(-EINVAL);
      memcpy(&dlp->config, &config, sizeof(struct dlci_conf));
      dlp->configured = 1;
   }

   err = (*flp->dlci_conf)(dlp->slave, dev, get);
   if (err)
      return(err);

   if (get)
      memcpy_tofs(conf, &dlp->config, sizeof(struct dlci_conf));

   return(0);
}

int dlci_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
   struct dlci_local *dlp;

   if (!suser())
      return(-EPERM);

   dlp = dev->priv;

   switch(cmd)
   {
      case DLCI_GET_SLAVE:
         if (!*(short *)(dev->dev_addr))
            return(-EINVAL);

         strcpy(ifr->ifr_slave, dlp->slave->name);
         break;

      case DLCI_DEVADD:
         /* can only add on the primary device */
         if (*(short *)(dev->dev_addr))
            return(-EINVAL);

         return(dlci_add((struct dlci_add *) ifr->ifr_data));
         break;

      case DLCI_DEVDEL:
         /* can't delete the primary device */
         if (!*(short *)(dev->dev_addr))
            return(-EINVAL);

         if (dev->start)
            return(-EBUSY);

         return(dlci_del(dev));
         break;

      case DLCI_GET_CONF:
      case DLCI_SET_CONF:
         if (!*(short *)(dev->dev_addr))
            return(-EINVAL);

         return(dlci_config(dev, (struct dlci_conf *) ifr->ifr_data, cmd == DLCI_GET_CONF));
         break;

      default: 
         return(-EOPNOTSUPP);
   }
   return(0);
}

static int dlci_change_mtu(struct device *dev, int new_mtu)
{
   struct dlci_local *dlp;

   dlp = dev->priv;

   return((*dlp->slave->change_mtu)(dlp->slave, new_mtu));
}

static int dlci_open(struct device *dev)
{
   struct dlci_local *dlp;
   struct frad_local *flp;
   int               err;

   dlp = dev->priv;

   if (!*(short *)(dev->dev_addr))
      return(-EINVAL);

   if (!dlp->slave->start)
      return(-ENOTCONN);

   dev->flags = 0;
   dev->tbusy = 0;
   dev->interrupt = 0;
   dev->start = 1;

   flp = dlp->slave->priv;
   err = (*flp->activate)(dlp->slave, dev);
   if (err)
      return(err);

   return 0;
}

static int dlci_close(struct device *dev)
{
   struct dlci_local *dlp;
   struct frad_local *flp;
   int               err;

   dlp = dev->priv;

   flp = dlp->slave->priv;
   err = (*flp->deactivate)(dlp->slave, dev);

   dev->start = 0;

   return 0;
}

static struct enet_statistics *dlci_get_stats(struct device *dev)
{
   struct dlci_local *dlp;

   dlp = dev->priv;

   return(&dlp->stats);
}

int dlci_init(struct device *dev)
{
   struct dlci_local *dlp;
   int i;

   dev->priv = kmalloc(sizeof(struct dlci_local), GFP_KERNEL);
   if (!dev->priv)
      return(-ENOMEM);

   memset(dev->priv, 0, sizeof(struct dlci_local));
   dlp = dev->priv;

   dev->flags           = 0;
   dev->open            = dlci_open;
   dev->stop            = dlci_close;
   dev->do_ioctl        = dlci_ioctl;
   dev->hard_start_xmit = dlci_transmit;
   dev->hard_header     = dlci_header;
   dev->get_stats       = dlci_get_stats;
   dev->change_mtu	= dlci_change_mtu;

   dlp->receive         = dlci_receive;

   dev->type            = ARPHRD_DLCI;
   dev->family          = AF_INET;
   dev->hard_header_len = sizeof(struct fradhdr);
   dev->pa_alen         = sizeof(unsigned long);

   memset(dev->dev_addr, 0, sizeof(dev->dev_addr));

   for (i = 0; i < DEV_NUMBUFFS; i++) 
      skb_queue_head_init(&dev->buffs[i]);

   return(0);
}

int dlci_setup(void)
{
   int i;

   printk("%s.\n", version);
   
   for(i=0;i<CONFIG_DLCI_COUNT;i++)
      open_dev[i] = NULL;

   for(i=0;i<sizeof(basename) / sizeof(char *);i++)
      basename[i] = NULL;

   return(0);
}

#ifdef MODULE
static struct device dlci = {"dlci", 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, dlci_init, };

int init_module(void)
{
   dlci_setup();
   return(register_netdev(&dlci));
}

void cleanup_module(void)
{
   struct dlci_local   *dlp;
   struct frad_local   *flp;
   int                 i;

   unregister_netdev(&dlci);

   for(i=0;i<CONFIG_DLCI_COUNT;i++)
      if (open_dev[i])
      {
         dlp = open_dev[i]->priv;
         flp = open_dev[i]->slave->priv;

         if (open_dev[i]->start)
         {
            if (flp->deactivate)
               (*flp->deactivate)(open_dev[i]->slave, open_dev[i]);
            open_dev[i]->start = 0;
         }

         (*flp->deassoc)(open_dev[i]->slave, open_dev[i]);
         kfree(open_dev[i]->priv);
         kfree(open_dev[i]->name);
         kfree(open_dev[i]);
         open_dev[i] = NULL;
      }

   if (dlci.priv)
      kfree(dlci.priv);
}
#endif /* MODULE */
