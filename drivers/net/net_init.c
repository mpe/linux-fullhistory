/* netdrv_init.c: Initialization for network devices. */
/*
	Written 1993,1994,1995 by Donald Becker.

	The author may be reached as becker@cesdis.gsfc.nasa.gov or
	C/O Center of Excellence in Space Data and Information Sciences
		Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	This file contains the initialization for the "pl14+" style ethernet
	drivers.  It should eventually replace most of drivers/net/Space.c.
	It's primary advantage is that it's able to allocate low-memory buffers.
	A secondary advantage is that the dangerous NE*000 netcards can reserve
	their I/O port region before the SCSI probes start.

	Modifications/additions by Bjorn Ekwall <bj0rn@blox.se>:
		ethdev_index[MAX_ETH_CARDS]
		register_netdev() / unregister_netdev()
		
	Modifications by Wolfgang Walter
		Use dev_close cleanly so we always shut things down tidily.
		
	Changed 29/10/95, Alan Cox to pass sockaddr's around for mac addresses.
	
	14/06/96 - Paul Gortmaker:	Add generic eth_change_mtu() function. 
	24/09/96 - Paul Norton: Add token-ring variants of the netdev functions. 
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
#include <linux/fddidevice.h>
#include <linux/hippidevice.h>
#include <linux/trdevice.h>
#include <linux/if_arp.h>
#include <linux/if_ltalk.h>
#include <linux/rtnetlink.h>
#include <net/neighbour.h>

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
   
   [Removed all support for /dev network devices. When someone adds
    streams then by magic we get them, but otherwise they are un-needed
	and a space waste]
*/

/* The list of used and available "eth" slots (for "eth0", "eth1", etc.) */
#define MAX_ETH_CARDS 16
static struct device *ethdev_index[MAX_ETH_CARDS];


/* Fill in the fields of the device structure with ethernet-generic values.

   If no device structure is passed, a new one is constructed, complete with
   a SIZEOF_PRIVATE private data area.

   If an empty string area is passed as dev->name, or a new structure is made,
   a new name string is constructed.  The passed string area should be 8 bytes
   long.
 */

struct device *
init_etherdev(struct device *dev, int sizeof_priv)
{
	int new_device = 0;
	int i;

	/* Use an existing correctly named device in Space.c:dev_base. */
	if (dev == NULL) {
		int alloc_size = sizeof(struct device) + sizeof("eth%d  ")
			+ sizeof_priv + 3;
		struct device *cur_dev;
		char pname[8];		/* Putative name for the device.  */

		for (i = 0; i < MAX_ETH_CARDS; ++i)
			if (ethdev_index[i] == NULL) {
				sprintf(pname, "eth%d", i);
				for (cur_dev = dev_base; cur_dev; cur_dev = cur_dev->next)
					if (strcmp(pname, cur_dev->name) == 0) {
						dev = cur_dev;
						dev->init = NULL;
						sizeof_priv = (sizeof_priv + 3) & ~3;
						dev->priv = sizeof_priv
							  ? kmalloc(sizeof_priv, GFP_KERNEL)
							  :	NULL;
						if (dev->priv) memset(dev->priv, 0, sizeof_priv);
						goto found;
					}
			}

		alloc_size &= ~3;		/* Round to dword boundary. */

		dev = (struct device *)kmalloc(alloc_size, GFP_KERNEL);
		memset(dev, 0, alloc_size);
		if (sizeof_priv)
			dev->priv = (void *) (dev + 1);
		dev->name = sizeof_priv + (char *)(dev + 1);
		new_device = 1;
	}

found:						/* From the double loop above. */

	if (dev->name &&
		((dev->name[0] == '\0') || (dev->name[0] == ' '))) {
		for (i = 0; i < MAX_ETH_CARDS; ++i)
			if (ethdev_index[i] == NULL) {
				sprintf(dev->name, "eth%d", i);
				ethdev_index[i] = dev;
				break;
			}
	}

	ether_setup(dev); 	/* Hmmm, should this be called here? */
	
	if (new_device)
		register_netdevice(dev);

	return dev;
}


static int eth_mac_addr(struct device *dev, void *p)
{
	struct sockaddr *addr=p;
	if(dev->start)
		return -EBUSY;
	memcpy(dev->dev_addr, addr->sa_data,dev->addr_len);
	return 0;
}

static int eth_change_mtu(struct device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > 1500))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

#ifdef CONFIG_FDDI

static int fddi_change_mtu(struct device *dev, int new_mtu)
{
	if ((new_mtu < FDDI_K_SNAP_HLEN) || (new_mtu > FDDI_K_SNAP_DLEN))
		return(-EINVAL);
	dev->mtu = new_mtu;
	return(0);
}

#endif

#ifdef CONFIG_HIPPI
#define MAX_HIP_CARDS	4
static struct device *hipdev_index[MAX_HIP_CARDS];

static int hippi_change_mtu(struct device *dev, int new_mtu)
{
	/*
	 * HIPPI's got these nice large MTUs.
	 */
	if ((new_mtu < 68) || (new_mtu > 65280))
		return -EINVAL;
	dev->mtu = new_mtu;
	return(0);
}


/*
 * For HIPPI we will actually use the lower 4 bytes of the hardware
 * address as the I-FIELD rather than the actual hardware address.
 */
static int hippi_mac_addr(struct device *dev, void *p)
{
	struct sockaddr *addr = p;
	if(dev->start)
		return -EBUSY;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	return 0;
}


struct device *init_hippi_dev(struct device *dev, int sizeof_priv)
{
	int new_device = 0;
	int i;

	/* Use an existing correctly named device in Space.c:dev_base. */
	if (dev == NULL) {
		int alloc_size = sizeof(struct device) + sizeof("hip%d  ")
			+ sizeof_priv + 3;
		struct device *cur_dev;
		char pname[8];

		for (i = 0; i < MAX_HIP_CARDS; ++i)
			if (hipdev_index[i] == NULL) {
				sprintf(pname, "hip%d", i);
				for (cur_dev = dev_base; cur_dev; cur_dev = cur_dev->next)
					if (strcmp(pname, cur_dev->name) == 0) {
						dev = cur_dev;
						dev->init = NULL;
						sizeof_priv = (sizeof_priv + 3) & ~3;
						dev->priv = sizeof_priv
							  ? kmalloc(sizeof_priv, GFP_KERNEL)
							  :	NULL;
						if (dev->priv) memset(dev->priv, 0, sizeof_priv);
						goto hipfound;
					}
			}

		alloc_size &= ~3;		/* Round to dword boundary. */

		dev = (struct device *)kmalloc(alloc_size, GFP_KERNEL);
		memset(dev, 0, alloc_size);
		if (sizeof_priv)
			dev->priv = (void *) (dev + 1);
		dev->name = sizeof_priv + (char *)(dev + 1);
		new_device = 1;
	}

hipfound:				/* From the double loop above. */

	if (dev->name &&
		((dev->name[0] == '\0') || (dev->name[0] == ' '))) {
		for (i = 0; i < MAX_HIP_CARDS; ++i)
			if (hipdev_index[i] == NULL) {
				sprintf(dev->name, "hip%d", i);
				hipdev_index[i] = dev;
				break;
			}
	}

	hippi_setup(dev);
	
	if (new_device)
		register_netdevice(dev);

	return dev;
}

static int hippi_neigh_setup_dev(struct device *dev, struct neigh_parms *p)
{
	/* Never send broadcast/multicast ARP messages */
	p->mcast_probes = 0;
 
	/* In IPv6 unicast probes are valid even on NBMA,
	* because they are encapsulated in normal IPv6 protocol.
	* Should be a generic flag. 
	*/
	if (p->tbl->family != AF_INET6)
		p->ucast_probes = 0;
	return 0;
}

#endif

void ether_setup(struct device *dev)
{
	int i;
	/* Fill in the fields of the device structure with ethernet-generic values.
	   This should be in a common file instead of per-driver.  */
	
	/* register boot-defined "eth" devices */
	if (dev->name && (strncmp(dev->name, "eth", 3) == 0)) {
		i = simple_strtoul(dev->name + 3, NULL, 0);
		if (ethdev_index[i] == NULL) {
			ethdev_index[i] = dev;
		}
		else if (dev != ethdev_index[i]) {
			/* Really shouldn't happen! */
			printk("ether_setup: Ouch! Someone else took %s\n",
				dev->name);
		}
	}

	dev->change_mtu		= eth_change_mtu;
	dev->hard_header	= eth_header;
	dev->rebuild_header 	= eth_rebuild_header;
	dev->set_mac_address 	= eth_mac_addr;
	dev->hard_header_cache	= eth_header_cache;
	dev->header_cache_update= eth_header_cache_update;
	dev->hard_header_parse	= eth_header_parse;

	dev->type		= ARPHRD_ETHER;
	dev->hard_header_len 	= ETH_HLEN;
	dev->mtu		= 1500; /* eth_mtu */
	dev->addr_len		= ETH_ALEN;
	dev->tx_queue_len	= 100;	/* Ethernet wants good queues */	
	
	memset(dev->broadcast,0xFF, ETH_ALEN);

	/* New-style flags. */
	dev->flags		= IFF_BROADCAST|IFF_MULTICAST;

	dev_init_buffers(dev);
}

#ifdef CONFIG_FDDI

void fddi_setup(struct device *dev)
{
	/*
	 * Fill in the fields of the device structure with FDDI-generic values.
	 * This should be in a common file instead of per-driver.
	 */
	
	dev->change_mtu			= fddi_change_mtu;
	dev->hard_header		= fddi_header;
	dev->rebuild_header		= fddi_rebuild_header;

	dev->type				= ARPHRD_FDDI;
	dev->hard_header_len	= FDDI_K_SNAP_HLEN+3;	/* Assume 802.2 SNAP hdr len + 3 pad bytes */
	dev->mtu				= FDDI_K_SNAP_DLEN;		/* Assume max payload of 802.2 SNAP frame */
	dev->addr_len			= FDDI_K_ALEN;
	dev->tx_queue_len		= 100;	/* Long queues on FDDI */
	
	memset(dev->broadcast, 0xFF, FDDI_K_ALEN);

	/* New-style flags */
	dev->flags		= IFF_BROADCAST | IFF_MULTICAST;

	dev_init_buffers(dev);
	
	return;
}

#endif

#ifdef CONFIG_HIPPI
void hippi_setup(struct device *dev)
{
	int i;

	if (dev->name && (strncmp(dev->name, "hip", 3) == 0)) {
		i = simple_strtoul(dev->name + 3, NULL, 0);
		if (hipdev_index[i] == NULL) {
			hipdev_index[i] = dev;
		}
		else if (dev != hipdev_index[i]) {
			printk("hippi_setup: Ouch! Someone else took %s\n",
				dev->name);
		}
	}

	dev->set_multicast_list	= NULL;
	dev->change_mtu			= hippi_change_mtu;
	dev->hard_header		= hippi_header;
	dev->rebuild_header 		= hippi_rebuild_header;
	dev->set_mac_address 		= hippi_mac_addr;
	dev->hard_header_parse		= NULL;
	dev->hard_header_cache		= NULL;
	dev->header_cache_update	= NULL;
	dev->neigh_setup 		= hippi_neigh_setup_dev; 

	/*
	 * We don't support HIPPI `ARP' for the time being, and probably
	 * never will unless someone else implements it. However we
	 * still need a fake ARPHRD to make ifconfig and friends play ball.
	 */
	dev->type		= ARPHRD_HIPPI;
	dev->hard_header_len 	= HIPPI_HLEN;
	dev->mtu		= 65280;
	dev->addr_len		= HIPPI_ALEN;
	dev->tx_queue_len	= 25 /* 5 */;
	memset(dev->broadcast, 0xFF, HIPPI_ALEN);


	/*
	 * HIPPI doesn't support broadcast+multicast and we only use
	 * static ARP tables. ARP is disabled by hippi_neigh_setup_dev. 
	 */
	dev->flags = 0; 

	dev_init_buffers(dev);
}
#endif

#if defined(CONFIG_ATALK) || defined(CONFIG_ATALK_MODULE)

static int ltalk_change_mtu(struct device *dev, int mtu)
{
	return -EINVAL;
}

static int ltalk_mac_addr(struct device *dev, void *addr)
{	
	return -EINVAL;
}


void ltalk_setup(struct device *dev)
{
	/* Fill in the fields of the device structure with localtalk-generic values. */
	
	dev->change_mtu		= ltalk_change_mtu;
	dev->hard_header	= NULL;
	dev->rebuild_header 	= NULL;
	dev->set_mac_address 	= ltalk_mac_addr;
	dev->hard_header_cache	= NULL;
	dev->header_cache_update= NULL;

	dev->type		= ARPHRD_LOCALTLK;
	dev->hard_header_len 	= LTALK_HLEN;
	dev->mtu		= LTALK_MTU;
	dev->addr_len		= LTALK_ALEN;
	dev->tx_queue_len	= 10;	
	
	dev->broadcast[0]	= 0xFF;

	dev->flags		= IFF_BROADCAST|IFF_MULTICAST|IFF_NOARP;

	dev_init_buffers(dev);
}

#endif

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

static int etherdev_get_index(struct device *dev)
{
	int i=MAX_ETH_CARDS;

	for (i = 0; i < MAX_ETH_CARDS; ++i)	{
		if (ethdev_index[i] == NULL) {
			sprintf(dev->name, "eth%d", i);
/*			printk("loading device '%s'...\n", dev->name);*/
			ethdev_index[i] = dev;
			return i;
		}
	}
	return -1;
}

static void etherdev_put_index(struct device *dev)
{
	int i;
	for (i = 0; i < MAX_ETH_CARDS; ++i) {
		if (ethdev_index[i] == dev) {
			ethdev_index[i] = NULL;
			break;
		}
	}
}

int register_netdev(struct device *dev)
{
	int i=-1;

	rtnl_lock();

	if (dev->name &&
	    (dev->name[0] == '\0' || dev->name[0] == ' '))
		i = etherdev_get_index(dev);

	if (register_netdevice(dev)) {
		if (i >= 0)
			etherdev_put_index(dev);
		rtnl_unlock();
		return -EIO;
	}
	rtnl_unlock();
	return 0;
}

void unregister_netdev(struct device *dev)
{
	rtnl_lock();
	unregister_netdevice(dev);
	etherdev_put_index(dev);
	rtnl_unlock();
}


#ifdef CONFIG_TR
/* The list of used and available "tr" slots */
#define MAX_TR_CARDS 16
static struct device *trdev_index[MAX_TR_CARDS];

struct device *init_trdev(struct device *dev, int sizeof_priv)
{
	int new_device = 0;
	int i;

	/* Use an existing correctly named device in Space.c:dev_base. */
	if (dev == NULL) {
		int alloc_size = sizeof(struct device) + sizeof("tr%d  ")
			+ sizeof_priv + 3;
		struct device *cur_dev;
		char pname[8];		/* Putative name for the device.  */

		for (i = 0; i < MAX_TR_CARDS; ++i)
			if (trdev_index[i] == NULL) {
				sprintf(pname, "tr%d", i);
				for (cur_dev = dev_base; cur_dev; cur_dev = cur_dev->next)
					if (strcmp(pname, cur_dev->name) == 0) {
						dev = cur_dev;
						dev->init = NULL;
						sizeof_priv = (sizeof_priv + 3) & ~3;
						dev->priv = sizeof_priv
							  ? kmalloc(sizeof_priv, GFP_KERNEL)
							  :	NULL;
						if (dev->priv) memset(dev->priv, 0, sizeof_priv);
						goto trfound;
					}
			}

		alloc_size &= ~3;		/* Round to dword boundary. */
		dev = (struct device *)kmalloc(alloc_size, GFP_KERNEL);
		memset(dev, 0, alloc_size);
		if (sizeof_priv)
			dev->priv = (void *) (dev + 1);
		dev->name = sizeof_priv + (char *)(dev + 1);
		new_device = 1;
	}

trfound:						/* From the double loop above. */

	for (i = 0; i < MAX_TR_CARDS; ++i)
		if (trdev_index[i] == NULL) {
			sprintf(dev->name, "tr%d", i);
			trdev_index[i] = dev;
			break;
		}


	dev->hard_header	 = tr_header;
	dev->rebuild_header  = tr_rebuild_header;

	dev->type		     = ARPHRD_IEEE802;
	dev->hard_header_len = TR_HLEN;
	dev->mtu		     = 2000; /* bug in fragmenter...*/
	dev->addr_len		 = TR_ALEN;
	dev->tx_queue_len	 = 100;	/* Long queues on tr */
	
	memset(dev->broadcast,0xFF, TR_ALEN);

	/* New-style flags. */
	dev->flags		= IFF_BROADCAST;

	if (new_device)
		register_netdevice(dev);

	return dev;
}

void tr_setup(struct device *dev)
{
	int i;

	/* register boot-defined "tr" devices */
	if (dev->name && (strncmp(dev->name, "tr", 2) == 0)) {
		i = simple_strtoul(dev->name + 2, NULL, 0);
		if (trdev_index[i] == NULL) {
			trdev_index[i] = dev;
		}
		else if (dev != trdev_index[i]) {
			/* Really shouldn't happen! */
			printk("tr_setup: Ouch! Someone else took %s\n",
				dev->name);
		}
	}
}

void tr_freedev(struct device *dev)
{
	int i;
	for (i = 0; i < MAX_TR_CARDS; ++i) 
	{
		if (trdev_index[i] == dev) 
		{
			trdev_index[i] = NULL;
			break;
		}
	}
}

int register_trdev(struct device *dev)
{
	dev_init_buffers(dev);
	
	if (dev->init && dev->init(dev) != 0) {
		unregister_trdev(dev);
		return -EIO;
	}
	return 0;
}

void unregister_trdev(struct device *dev)
{
	rtnl_lock();
	unregister_netdevice(dev);
	rtnl_unlock();
	tr_freedev(dev);
}
#endif


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c net_init.c"
 *  version-control: t
 *  kept-new-versions: 5
 * End:
 */
