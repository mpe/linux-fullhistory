/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Interfaces handler.
 *
 * Version:	@(#)dev.h	1.0.10	08/12/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Donald J. Becker, <becker@super.org>
 *		Alan Cox, <A.Cox@swansea.ac.uk>
 *		Bjorn Ekwall. <bj0rn@blox.se>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *		Moved to /usr/include/linux for NET3
 */
#ifndef _LINUX_NETDEVICE_H
#define _LINUX_NETDEVICE_H

#include <linux/config.h>
#include <linux/if.h>
#include <linux/if_ether.h>

/* for future expansion when we will have different priorities. */
#define DEV_NUMBUFFS	3
#define MAX_ADDR_LEN	7

#if !defined(CONFIG_AX25) && !defined(CONFIG_AX25_MODULE) && !defined(CONFIG_TR)
#define LL_MAX_HEADER	32
#else
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
#define LL_MAX_HEADER	96
#else
#define LL_MAX_HEADER	48
#endif
#endif

#if !defined(CONFIG_NET_IPIP) && \
    !defined(CONFIG_IPV6) && !defined(CONFIG_IPV6_MODULE)
#define MAX_HEADER LL_MAX_HEADER
#else
#define MAX_HEADER (LL_MAX_HEADER + 48)
#endif

#define IS_MYADDR	1		/* address is (one of) our own	*/
#define IS_LOOPBACK	2		/* address is for LOOPBACK	*/
#define IS_BROADCAST	3		/* address is a valid broadcast	*/
#define IS_INVBCAST	4		/* Wrong netmask bcast not for us (unused)*/
#define IS_MULTICAST	5		/* Multicast IP address */

/* NOTE: move to ipv4_device.h */

#define IFF_IP_ADDR_OK	1
#define IFF_IP_MASK_OK	2
#define IFF_IP_BRD_OK	4

#ifdef __KERNEL__

#include <linux/skbuff.h>

/*
 *	We tag multicasts with these structures.
 */
 
struct dev_mc_list
{	
	struct dev_mc_list *next;
	char dmi_addr[MAX_ADDR_LEN];
	unsigned short dmi_addrlen;
	unsigned short dmi_users;
};

struct hh_cache
{
	struct hh_cache *hh_next;
	int		hh_refcnt;	/* number of users */
	unsigned short  hh_type;	/* protocol identifier, f.e ETH_P_IP */
	char		hh_uptodate;	/* hh_data is valid */
	char		hh_data[16];    /* cached hardware header */
};

/*
 *	The DEVICE structure.
 *	Actually, this whole structure is a big mistake.  It mixes I/O
 *	data with strictly "high-level" data, and it has to know about
 *	almost every data structure used in the INET module.
 *
 *	FIXME: cleanup struct device such that network protocol info
 *	moves out.
 */

struct device
{

	/*
	 * This is the first field of the "visible" part of this structure
	 * (i.e. as seen by users in the "Space.c" file).  It is the name
	 * the interface.
	 */
	char			*name;

	/*
	 *	I/O specific fields
	 *	FIXME: Merge these and struct ifmap into one
	 */
	unsigned long		rmem_end;	/* shmem "recv" end	*/
	unsigned long		rmem_start;	/* shmem "recv" start	*/
	unsigned long		mem_end;	/* shared mem end	*/
	unsigned long		mem_start;	/* shared mem start	*/
	unsigned long		base_addr;	/* device I/O address	*/
	unsigned char		irq;		/* device IRQ number	*/
	
	/* Low-level status flags. */
	volatile unsigned char	start,		/* start an operation	*/
				interrupt;	/* interrupt arrived	*/
	unsigned long		tbusy;		/* transmitter busy must be
						   long for bitops	*/
	
	struct device		*next;
	
	/* The device initialization function. Called only once. */
	int			(*init)(struct device *dev);

	/* Interface index. Unique device identifier	*/
	int			ifindex;
	struct device		*next_up;

	/*
	 *	Some hardware also needs these fields, but they are not
	 *	part of the usual set specified in Space.c.
	 */
	unsigned char		if_port;	/* Selectable AUI, TP,..*/
	unsigned char		dma;		/* DMA channel		*/

	struct enet_statistics* (*get_stats)(struct device *dev);
#ifdef CONFIG_NET_RADIO
	struct iw_statistics*	(*get_wireless_stats)(struct device *dev);
#endif

	/*
	 * This marks the end of the "visible" part of the structure. All
	 * fields hereafter are internal to the system, and may change at
	 * will (read: may be cleaned up at will).
	 */

	/* These may be needed for future network-power-down code. */
	unsigned long		trans_start;	/* Time (in jiffies) of last Tx	*/
	unsigned long		last_rx;	/* Time of last Rx	*/
	
	unsigned short		flags;	/* interface flags (a la BSD)	*/
	unsigned short		family;	/* address family ID (AF_INET)	*/
	unsigned short		metric;	/* routing metric (not used)	*/
	unsigned short		mtu;	/* interface MTU value		*/
	unsigned short		type;	/* interface hardware type	*/
	unsigned short		hard_header_len;	/* hardware hdr length	*/
	void			*priv;	/* pointer to private data	*/
	
	/* Interface address info. */
	unsigned char		broadcast[MAX_ADDR_LEN];	/* hw bcast add	*/
	unsigned char		pad;		/* make dev_addr aligned to 8 bytes */
	unsigned char		dev_addr[MAX_ADDR_LEN];	/* hw address	*/
	unsigned char		addr_len;	/* hardware address length	*/
	unsigned long		pa_addr;	/* protocol address		*/

	unsigned long		pa_brdaddr;	/* protocol broadcast addr	*/
	unsigned long		pa_dstaddr;	/* protocol P-P other side addr	*/
	unsigned long		pa_mask;	/* protocol netmask		*/
	unsigned short		pa_alen;	/* protocol address length	*/

	struct dev_mc_list	*mc_list;	/* Multicast mac addresses	*/
	int			mc_count;	/* Number of installed mcasts	*/
  
	struct ip_mc_list	*ip_mc_list;	/* IP multicast filter chain    */
	unsigned		ip_flags;
	__u8			hash;	
	__u32			tx_queue_len;	/* Max frames per queue allowed */
    
	/* For load balancing driver pair support */
  
	unsigned long		pkt_queue;	/* Packets queued	*/
	struct device		*slave;		/* Slave device		*/
	struct net_alias_info	*alias_info;	/* main dev alias info	*/
	struct net_alias	*my_alias;	/* alias devs		*/
  
	/* Pointer to the interface buffers.	*/
	struct sk_buff_head	buffs[DEV_NUMBUFFS];

	/* Pointers to interface service routines.	*/
	int			(*open)(struct device *dev);
	int			(*stop)(struct device *dev);
	int			(*hard_start_xmit) (struct sk_buff *skb,
						    struct device *dev);
	int			(*hard_header) (struct sk_buff *skb,
						struct device *dev,
						unsigned short type,
						void *daddr,
						void *saddr,
						unsigned len);
	int			(*rebuild_header)(struct sk_buff *skb);
#define HAVE_MULTICAST			 
	void			(*set_multicast_list)(struct device *dev);
#define HAVE_SET_MAC_ADDR  		 
	int			(*set_mac_address)(struct device *dev,
						   void *addr);
#define HAVE_PRIVATE_IOCTL
	int			(*do_ioctl)(struct device *dev,
					    struct ifreq *ifr, int cmd);
#define HAVE_SET_CONFIG
	int			(*set_config)(struct device *dev,
					      struct ifmap *map);
#define HAVE_HEADER_CACHE
	int			(*hard_header_cache)(struct dst_entry *dst,
						     struct dst_entry *neigh,
						     struct hh_cache *hh);
	void			(*header_cache_update)(struct hh_cache *hh,
						       struct device *dev,
						       unsigned char *  haddr);
#define HAVE_CHANGE_MTU
	int			(*change_mtu)(struct device *dev, int new_mtu);

};


struct packet_type {
	unsigned short		type;	/* This is really htons(ether_type). */
	struct device		*dev;
	int			(*func) (struct sk_buff *, struct device *,
					 struct packet_type *);
	void			*data;
	struct packet_type	*next;
};


#include <linux/interrupt.h>
#include <linux/notifier.h>

/* Used by dev_rint */
#define IN_SKBUFF	1

extern struct device	loopback_dev;
extern struct device	*dev_base;
extern struct packet_type *ptype_base[16];

/* NOTE: move to INET specific header;
   __ip_chk_addr is deprecated, do not use if it's possible.
 */
extern int		__ip_chk_addr(unsigned long addr);
extern struct device 	*ip_dev_find(unsigned long addr, char *name);
/* This is the wrong place but it'll do for the moment */
extern void		ip_mc_allhost(struct device *dev);
extern int		devinet_ioctl(unsigned int cmd, void *);

extern struct device    *dev_getbyhwaddr(unsigned short type, char *hwaddr);
extern void		dev_add_pack(struct packet_type *pt);
extern void		dev_remove_pack(struct packet_type *pt);
extern struct device	*dev_get(const char *name);
extern int		dev_open(struct device *dev);
extern int		dev_close(struct device *dev);
extern int		dev_queue_xmit(struct sk_buff *skb);
extern void		dev_loopback_xmit(struct sk_buff *skb);
				      
#define HAVE_NETIF_RX 1
extern void		netif_rx(struct sk_buff *skb);
extern void		net_bh(void);
extern void		dev_tint(struct device *dev);
extern int		dev_get_info(char *buffer, char **start, off_t offset, int length, int dummy);
extern int		dev_ioctl(unsigned int cmd, void *);

extern void		dev_init(void);

/* Locking protection for page faults during outputs to devices unloaded during the fault */

extern atomic_t		dev_lockct;

/*
 *	These two don't currently need to be atomic
 *	but they may do soon. Do it properly anyway.
 */

extern __inline__ void  dev_lock_list(void)
{
	atomic_inc(&dev_lockct);
}

extern __inline__ void  dev_unlock_list(void)
{
	atomic_dec(&dev_lockct);
}

/*
 *	This almost never occurs, isn't in performance critical paths
 *	and we can thus be relaxed about it. 
 *
 *	FIXME: What if this is being run as a real time process ??
 *		Linus: We need a way to force a yield here ?
 */
 
extern __inline__ void dev_lock_wait(void)
{
	while(dev_lockct)
		schedule();
}

/* NOTE: about to be replaced with if_index */

static __inline__ __u8 dev_hash_name(char *name)
{
	__u8 hash = 0;
	__u8 *p;
	for (p=name; *p; p++)
		hash ^= *p;
	return hash;
}

static __inline__ __u8 dev_hash_mc_name(char *name)
{
	int i;
	__u8 hash = 0;
	unsigned *p = (unsigned*)name;
	for (i=0; i<MAX_ADDR_LEN/sizeof(unsigned); i++) {
		unsigned h = p[i];
		h ^= (h>>16);
		h ^= (h>>8);
		hash ^= h;
	}
	return hash;
}


/* These functions live elsewhere (drivers/net/net_init.c, but related) */

extern void		ether_setup(struct device *dev);
extern void		fddi_setup(struct device *dev);
extern void		tr_setup(struct device *dev);
extern void		tr_freedev(struct device *dev);
extern int		ether_config(struct device *dev, struct ifmap *map);
/* Support for loadable net-drivers */
extern int		register_netdev(struct device *dev);
extern void		unregister_netdev(struct device *dev);
extern int 		register_netdevice_notifier(struct notifier_block *nb);
extern int		unregister_netdevice_notifier(struct notifier_block *nb);
extern int		register_trdev(struct device *dev);
extern void		unregister_trdev(struct device *dev);
/* Functions used for multicast support */
extern void		dev_mc_upload(struct device *dev);
extern void 		dev_mc_delete(struct device *dev, void *addr, int alen, int all);
extern void		dev_mc_add(struct device *dev, void *addr, int alen, int newonly);
extern void		dev_mc_discard(struct device *dev);
/* Load a device via the kerneld */
extern void		dev_load(const char *name);

#endif /* __KERNEL__ */

#endif	/* _LINUX_DEV_H */
