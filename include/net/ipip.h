extern int ipip_rcv(struct sk_buff *skb, struct device *dev, struct options *opt, 
		unsigned long daddr, unsigned short len, unsigned long saddr,
                                   int redo, struct inet_protocol *protocol);
                                   
