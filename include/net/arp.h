/* linux/net/inet/arp.h */
#ifndef _ARP_H
#define _ARP_H

extern void	arp_init(void);
extern void	arp_destroy(u32 paddr, int force);
extern void	arp_device_down(struct device *dev);
extern int	arp_rcv(struct sk_buff *skb, struct device *dev,
			struct packet_type *pt);
extern int	arp_query(unsigned char *haddr, u32 paddr, unsigned short type);
extern int	arp_find(unsigned char *haddr, u32 paddr,
		struct device *dev, u32 saddr, struct sk_buff *skb);
extern int	arp_get_info(char *buffer, char **start, off_t origin, int length);
extern int	arp_ioctl(unsigned int cmd, void *arg);
extern void     arp_send(int type, int ptype, u32 dest_ip, 
			 struct device *dev, u32 src_ip, 
			 unsigned char *dest_hw, unsigned char *src_hw);
extern int	arp_find_cache(unsigned char *dp, u32 daddr, struct device *dev);

extern unsigned long	arp_cache_stamp;
#endif	/* _ARP_H */
