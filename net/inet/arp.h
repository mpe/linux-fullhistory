/* linux/net/inet/arp.h */
#ifndef _ARP_H
#define _ARP_H

extern void	arp_init(void);
extern void	arp_destroy(unsigned long paddr, int force);
extern int	arp_rcv(struct sk_buff *skb, struct device *dev,
			struct packet_type *pt);
extern int	arp_find(unsigned char *haddr, unsigned long paddr,
		struct device *dev, unsigned long saddr, struct sk_buff *skb);
extern int	arp_get_info(char *buffer, char **start, off_t origin, int length);
extern int	arp_ioctl(unsigned int cmd, void *arg);

#endif	/* _ARP_H */
