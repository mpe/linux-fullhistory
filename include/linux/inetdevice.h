#ifndef _LINUX_INETDEVICE_H
#define _LINUX_INETDEVICE_H

/* IPv4 specific flags. They are initialized from global sysctl variables,
   when IPv4 is initialized.
 */

#define IFF_IP_FORWARD		1
#define IFF_IP_PROXYARP		2
#define IFF_IP_RXREDIRECTS	4
#define IFF_IP_TXREDIRECTS	8
#define IFF_IP_SHAREDMEDIA	0x10
#define IFF_IP_MFORWARD		0x20
#define IFF_IP_RPFILTER		0x40

#ifdef __KERNEL__

struct in_device
{
	struct device		*dev;
	struct in_ifaddr	*ifa_list;	/* IP ifaddr chain		*/
	struct ip_mc_list	*mc_list;	/* IP multicast filter chain    */
	unsigned long		mr_v1_seen;
	unsigned		flags;
	struct neigh_parms	*arp_parms;
};


#define IN_DEV_RPFILTER(in_dev)	(ipv4_config.rfc1812_filter && ((in_dev)->flags&IFF_IP_RPFILTER))
#define IN_DEV_MFORWARD(in_dev)	(ipv4_config.multicast_route && ((in_dev)->flags&IFF_IP_MFORWARD))
#define IN_DEV_PROXY_ARP(in_dev)	(ipv4_config.proxy_arp || (in_dev)->flags&IFF_IP_PROXYARP)
#define IN_DEV_FORWARD(in_dev)		(IS_ROUTER || ((in_dev)->flags&IFF_IP_FORWARD))
#define IN_DEV_SHARED_MEDIA(in_dev)	(ipv4_config.rfc1620_redirects || (in_dev)->flags&IFF_IP_SHAREDMEDIA)
#define IN_DEV_RX_REDIRECTS(in_dev)	(ipv4_config.accept_redirects || (in_dev)->flags&IFF_IP_RXREDIRECTS)
#define IN_DEV_TX_REDIRECTS(in_dev)	(/*ipv4_config.send_redirects ||*/ (in_dev)->flags&IFF_IP_TXREDIRECTS)

struct in_ifaddr
{
	struct in_ifaddr	*ifa_next;
	struct in_device	*ifa_dev;
	u32			ifa_local;
	u32			ifa_address;
	u32			ifa_mask;
	u32			ifa_broadcast;
	u32			ifa_anycast;
	unsigned char		ifa_scope;
	unsigned char		ifa_flags;
	unsigned char		ifa_prefixlen;
	char			ifa_label[IFNAMSIZ];
};

extern int register_inetaddr_notifier(struct notifier_block *nb);
extern int unregister_inetaddr_notifier(struct notifier_block *nb);

extern struct device 	*ip_dev_find(u32 addr);
extern struct in_ifaddr	*inet_addr_onlink(struct in_device *in_dev, u32 a, u32 b);
extern int		devinet_ioctl(unsigned int cmd, void *);
extern void		devinet_init(void);
extern struct in_device *inetdev_init(struct device *dev);
extern struct in_device	*inetdev_by_index(int);
extern u32		inet_select_addr(struct device *dev, u32 dst, int scope);
extern struct in_ifaddr *inet_ifa_byprefix(struct in_device *in_dev, u32 prefix, u32 mask);
extern int		inet_add_bootp_addr(struct device *dev);
extern void		inet_del_bootp_addr(struct device *dev);

extern __inline__ int inet_ifa_match(u32 addr, struct in_ifaddr *ifa)
{
	return !((addr^ifa->ifa_address)&ifa->ifa_mask);
}

/*
 *	Check if a mask is acceptable.
 */
 
extern __inline__ int bad_mask(u32 mask, u32 addr)
{
	if (addr & (mask = ~mask))
		return 1;
	mask = ntohl(mask);
	if (mask & (mask+1))
		return 1;
	return 0;
}

#define for_primary_ifa(in_dev)	{ struct in_ifaddr *ifa; \
  for (ifa = (in_dev)->ifa_list; ifa && !(ifa->ifa_flags&IFA_F_SECONDARY); ifa = ifa->ifa_next)

#define for_ifa(in_dev)	{ struct in_ifaddr *ifa; \
  for (ifa = (in_dev)->ifa_list; ifa; ifa = ifa->ifa_next)


#define endfor_ifa(in_dev) }

#endif /* __KERNEL__ */

extern __inline__ __u32 inet_make_mask(int logmask)
{
	if (logmask)
		return htonl(~((1<<(32-logmask))-1));
	return 0;
}

extern __inline__ int inet_mask_len(__u32 mask)
{
	if (!(mask = ntohl(mask)))
		return 0;
	return 32 - ffz(~mask);
}


#endif /* _LINUX_INETDEVICE_H */
