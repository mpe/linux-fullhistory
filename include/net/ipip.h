#ifndef __NET_IPIP_H
#define __NET_IPIP_H 1

extern void ipip_err(struct sk_buff *skb, unsigned char*);
extern int ipip_rcv(struct sk_buff *skb, unsigned short len);
                                   

#endif
