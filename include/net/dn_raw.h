#ifndef _NET_DN_RAW_H
#define _NET_DN_RAW_H

#ifdef CONFIG_DECNET_RAW

extern struct proto_ops dn_raw_proto_ops;

extern void dn_raw_rx_nsp(struct sk_buff *skb);
extern void dn_raw_rx_routing(struct sk_buff *skb);

#ifdef CONFIG_DECNET_MOP
extern void dn_raw_rx_mop(struct sk_buff *skb);
#endif /* CONFIG_DECNET_MOP */

#endif /* CONFIG_DECNET_RAW */

#endif /* _NET_DN_RAW_H */
