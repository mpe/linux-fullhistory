#ifndef LLC_SAP_H
#define LLC_SAP_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/skbuff.h>
#include <net/llc_if.h>
/**
 * struct llc_sap - Defines the SAP component
 *
 * @station - station this sap belongs to
 * @state - sap state
 * @p_bit - only lowest-order bit used
 * @f_bit - only lowest-order bit used
 * @laddr - SAP value in this 'lsap'
 * @node - entry in station sap_list
 * @sk_list - LLC sockets this one manages
 */
struct llc_sap {
	struct llc_station *station;
	u8		    state;
	u8		    p_bit;
	u8		    f_bit;
	int		    (*rcv_func)(struct sk_buff *skb,
					struct net_device *dev,
					struct packet_type *pt);
	struct llc_addr	    laddr;
	struct list_head    node;
	struct {
		rwlock_t	  lock;
		struct hlist_head list;
	} sk_list;
};

extern void llc_sap_state_process(struct llc_sap *sap, struct sk_buff *skb);
extern void llc_sap_rtn_pdu(struct llc_sap *sap, struct sk_buff *skb);
extern void llc_save_primitive(struct sk_buff* skb, u8 prim);

extern struct llc_sap *llc_sap_open(u8 lsap,
				    int (*rcv)(struct sk_buff *skb,
					       struct net_device *dev,
					       struct packet_type *pt));
extern void llc_sap_close(struct llc_sap *sap);

extern void llc_build_and_send_ui_pkt(struct llc_sap *sap, struct sk_buff *skb,
				      u8 *dmac, u8 dsap);
extern void llc_build_and_send_xid_pkt(struct llc_sap *sap, struct sk_buff *skb,
				       u8 *dmac, u8 dsap);
extern void llc_build_and_send_test_pkt(struct llc_sap *sap,
					struct sk_buff *skb, u8 *dmac, u8 dsap);

#endif /* LLC_SAP_H */
