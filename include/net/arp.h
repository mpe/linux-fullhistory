/* linux/net/inet/arp.h */
#ifndef _ARP_H
#define _ARP_H

#include <linux/if_arp.h>
#include <net/neighbour.h>
/*
 *	This structure defines the ARP mapping cache.
 */

struct arp_table
{
	union {
		struct neighbour	neigh;
		struct arp_table	*next;
	} u;

	u32				ip;

	unsigned long			last_updated;		/* For expiry 			*/
	unsigned int			flags;			/* Control status 		*/

	u32				mask;			/* netmask - used for generalised proxy arps (tridge) 		*/
	int				hatype;

	/*
	 *	The following entries are only used for unresolved hw addresses.
	 */
	struct timer_list		timer;			/* expire timer 		*/
	int				retries;		/* remaining retries	 	*/
};

extern void	arp_init(void);
extern int	arp_rcv(struct sk_buff *skb, struct device *dev,
			struct packet_type *pt);
extern int	arp_find(unsigned char *haddr, struct sk_buff *skb);
extern int	arp_find_1(unsigned char *haddr, struct dst_entry* dst,
			   struct neighbour *neigh);
extern int	arp_ioctl(unsigned int cmd, void *arg);
extern void     arp_send(int type, int ptype, u32 dest_ip, 
			 struct device *dev, u32 src_ip, 
			 unsigned char *dest_hw, unsigned char *src_hw, unsigned char *th);
extern int	arp_req_set(struct arpreq *r, struct device *dev);
extern int	arp_req_delete(struct arpreq *r, struct device *dev);
extern int	arp_bind_cache(struct hh_cache ** hhp, struct device *dev, unsigned short type, __u32 daddr);
extern int	arp_update_cache(struct hh_cache * hh);
extern struct neighbour *arp_find_neighbour(struct dst_entry *dst, int);
#endif	/* _ARP_H */
