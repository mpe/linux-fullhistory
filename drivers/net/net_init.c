/* netdrv_init.c: Initialization for network devices. */
/*
	Written 1993 by Donald Becker.
	Copyright 1993 United States Government as represented by the Director,
	National Security Agency.  This software may only be used and distributed
	according to the terms of the GNU Public License as modified by SRC,
	incorported herein by reference.

	The author may be reached as becker@super.org or
	C/O Supercomputing Research Ctr., 17100 Science Dr., Bowie MD 20715

	This file contains the initialization for the "pl14+" style ethernet
	drivers.  It should eventually replace most of drivers/net/Space.c.
	It's primary advantage is that it's able to allocate low-memory buffers.
	A secondary advantage is that the dangerous NE*000 netcards can reserve
	their I/O port region before the SCSI probes start.

	register_netdev()/unregister_netdev() by Bjorn Ekwall <bj0rn@blox.se>
*/

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <linux/if_ether.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

/* The network devices currently exist only in the socket namespace, so these
   entries are unused.  The only ones that make sense are
    open	start the ethercard
    close	stop  the ethercard
    ioctl	To get statistics, perhaps set the interface port (AUI, BNC, etc.)
   One can also imagine getting raw packets using
    read & write
   but this is probably better handled by a raw packet socket.

   Given that almost all of these functions are handled in the current
   socket-based scheme, putting ethercard devices in /dev/ seems pointless.
   
   [Removed all support for /dev network devices. When someone adds streams then
    by magic we get them, but otherwise they are un-needed and a space waste]
*/

/* The next device number/name to assign: "eth0", "eth1", etc. */
static int next_ethdev_number = 0;

unsigned long lance_init(unsigned long mem_start, unsigned long mem_end);

/*
  net_dev_init() is our network device initialization routine.
  It's called from init/main.c with the start and end of free memory,
  and returns the new start of free memory.
  */

unsigned long net_dev_init (unsigned long mem_start, unsigned long mem_end)
{

#if defined(CONFIG_LANCE)			/* Note this is _not_ CONFIG_AT1500. */
	mem_start = lance_init(mem_start, mem_end);
#endif
#if defined(CONFIG_PI)
	mem_start = pi_init(mem_start, mem_end);
#endif	
	return mem_start;
}

/* Fill in the fields of the device structure with ethernet-generic values.

   If no device structure is passed, a new one is constructed, complete with
   a SIZEOF_PRIVATE private data area.

   If an empty string area is passed as dev->name, or a new structure is made,
   a new name string is constructed.  The passed string area should be 8 bytes
   long.
 */

struct device *init_etherdev(struct device *dev, int sizeof_private,
							 unsigned long *mem_startp)
{
	int i;
	int new_device = 0;

	if (dev == NULL) {
		int alloc_size = sizeof(struct device) + sizeof("eth%d ")
			+ sizeof_private;
		if (mem_startp && *mem_startp ) {
			dev = (struct device *)*mem_startp;
			*mem_startp += alloc_size;
		} else
			dev = (struct device *)kmalloc(alloc_size, GFP_KERNEL);
		memset(dev, 0, sizeof(alloc_size));
		dev->name = (char *)(dev + 1);
		if (sizeof_private)
			dev->priv = dev->name + sizeof("eth%d ");
		new_device = 1;
	}

	if (dev->name  &&  dev->name[0] == '\0')
		sprintf(dev->name, "eth%d", next_ethdev_number++);

	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);
	
	dev->hard_header	= eth_header;
	dev->rebuild_header	= eth_rebuild_header;
	dev->type_trans		= eth_type_trans;
	
	dev->type			= ARPHRD_ETHER;
	dev->hard_header_len = ETH_HLEN;
	dev->mtu			= 1500; /* eth_mtu */
	dev->addr_len		= ETH_ALEN;
	for (i = 0; i < ETH_ALEN; i++) {
		dev->broadcast[i]=0xff;
	}
	
	/* New-style flags. */
	dev->flags			= IFF_BROADCAST;
	dev->family			= AF_INET;
	dev->pa_addr		= 0;
	dev->pa_brdaddr		= 0;
	dev->pa_mask		= 0;
	dev->pa_alen		= sizeof(unsigned long);
	
	if (new_device) {
		/* Append the device to the device queue. */
		struct device **old_devp = &dev_base;
		while ((*old_devp)->next)
			old_devp = & (*old_devp)->next;
		(*old_devp)->next = dev;
		dev->next = 0;
	}
	return dev;
}

void ether_setup(struct device *dev)
{
	int i;
	/* Fill in the fields of the device structure with ethernet-generic values.
	   This should be in a common file instead of per-driver.  */
	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);

	dev->hard_header	= eth_header;
	dev->rebuild_header = eth_rebuild_header;
	dev->type_trans = eth_type_trans;

	dev->type		= ARPHRD_ETHER;
	dev->hard_header_len = ETH_HLEN;
	dev->mtu		= 1500; /* eth_mtu */
	dev->addr_len	= ETH_ALEN;
	for (i = 0; i < ETH_ALEN; i++) {
		dev->broadcast[i]=0xff;
	}

	/* New-style flags. */
	dev->flags		= IFF_BROADCAST;
	dev->family		= AF_INET;
	dev->pa_addr	= 0;
	dev->pa_brdaddr = 0;
	dev->pa_mask	= 0;
	dev->pa_alen	= sizeof(unsigned long);
}

int ether_config(struct device *dev, struct ifmap *map)
{
	if (map->mem_start != (u_long)(-1))
		dev->mem_start = map->mem_start;
	if (map->mem_end != (u_long)(-1))
		dev->mem_end = map->mem_end;
	if (map->base_addr != (u_short)(-1))
		dev->base_addr = map->base_addr;
	if (map->irq != (u_char)(-1))
		dev->irq = map->irq;
	if (map->dma != (u_char)(-1))
		dev->dma = map->dma;
	if (map->port != (u_char)(-1))
		dev->if_port = map->port;
	return 0;
}

int register_netdev(struct device *dev)
{
	struct device *d = dev_base;
	unsigned long flags;
	
	save_flags(flags);
	cli();

	if (dev && dev->init) 
	{
		if (dev->init(dev) != 0)
		{
			restore_flags(flags);
			return -EIO;
		}
		
		if (dev->name  &&  dev->name[0] == '\0')
			sprintf(dev->name, "eth%d", next_ethdev_number++);

		/* Add device to end of chain */
		if (dev_base) 
		{
			while (d->next)
				d = d->next;
			d->next = dev;
		}
		else
			dev_base = dev;
		dev->next = NULL;
	}
	restore_flags(flags);
	return 0;
}

void unregister_netdev(struct device *dev)
{
	struct device *d = dev_base;
	unsigned long flags;
	
	save_flags(flags);
	cli();

	printk("unregister_netdev: device ");
	if (dev) {
		if (dev->start)
			printk("'%s' busy", dev->name);
		else {
			if (dev_base == dev)
				dev_base = dev->next;
			else {
				while (d && (d->next != dev))
					d = d->next;

				if (d && (d->next == dev)) {
					d->next = dev->next;
					printk("'%s' unlinked", dev->name);
				}
				else
					printk("'%s' not found", dev->name);
			}
		}
	}
	else
		printk("was NULL");
	printk("\n");
	restore_flags(flags);
}


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c net_init.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 4
 * End:
 */
