/*
 * llc_mac.c - Manages interface between LLC and MAC
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/if_tr.h>
#include <linux/rtnetlink.h>
#include <net/llc_if.h>
#include <net/llc_mac.h>
#include <net/llc_pdu.h>
#include <net/llc_sap.h>
#include <net/llc_conn.h>
#include <net/sock.h>
#include <net/llc_main.h>
#include <net/llc_evnt.h>
#include <net/llc_c_ev.h>
#include <net/llc_s_ev.h>
#ifdef CONFIG_TR
extern void tr_source_route(struct sk_buff *skb, struct trh_hdr *trh,
			    struct net_device *dev);
#endif
/* function prototypes */
static void fix_up_incoming_skb(struct sk_buff *skb);

/**
 *	mac_send_pdu - Sends PDU to specific device.
 *	@skb: pdu which must be sent
 *
 *	If module is not initialized then returns failure, else figures out
 *	where to direct this PDU. Sends PDU to specific device, at this point a
 *	device must has been assigned to the PDU; If not, can't transmit the
 *	PDU. PDU sent to MAC layer, is free to re-send at a later time. Returns
 *	0 on success, 1 for failure.
 */
int mac_send_pdu(struct sk_buff *skb)
{
	struct sk_buff *skb2;
	int pri = GFP_ATOMIC, rc = -1;

	if (!skb->dev) {
		printk(KERN_ERR __FUNCTION__ ": skb->dev == NULL!");
		goto out;
	}
	if (skb->sk)
		pri = (int)skb->sk->priority;
	skb2 = skb_clone(skb, pri);
	if (!skb2)
		goto out;
	rc = 0;
	dev_queue_xmit(skb2);
out:
	return rc;
}

/**
 *	mac_indicate - 802.2 entry point from net lower layers
 *	@skb: received pdu
 *	@dev: device that receive pdu
 *	@pt: packet type
 *
 *	When the system receives a 802.2 frame this function is called. It
 *	checks SAP and connection of received pdu and passes frame to
 *	llc_pdu_router for sending to proper state machine. If frame is
 *	related to a busy connection (a connection is sending data now),
 *	function queues this frame in connection's backlog.
 */
int mac_indicate(struct sk_buff *skb, struct net_device *dev,
		 struct packet_type *pt)
{
	struct llc_sap *sap;
	llc_pdu_sn_t *pdu;
	u8 dest;

	/* When the interface is in promisc. mode, drop all the crap that it
	 * receives, do not try to analyse it.
	 */
	if (skb->pkt_type == PACKET_OTHERHOST) {
		printk(KERN_INFO __FUNCTION__ ": PACKET_OTHERHOST\n");
		goto drop;
	}
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		goto out;
	fix_up_incoming_skb(skb);
	pdu = (llc_pdu_sn_t *)skb->nh.raw;
	if (!pdu->dsap) { /* NULL DSAP, refer to station */
		llc_pdu_router(NULL, NULL, skb, 0);
		goto out;
	}
	sap = llc_sap_find(pdu->dsap);
	if (!sap) /* unknown SAP */
		goto drop;
	llc_decode_pdu_type(skb, &dest);
	if (dest == LLC_DEST_SAP) /* type 1 services */
		llc_pdu_router(sap, NULL, skb, LLC_TYPE_1);
	else if (dest == LLC_DEST_CONN) {
		struct llc_addr saddr, daddr;
		struct sock *sk;

		llc_pdu_decode_sa(skb, saddr.mac);
		llc_pdu_decode_ssap(skb, &saddr.lsap);
		llc_pdu_decode_da(skb, daddr.mac);
		llc_pdu_decode_dsap(skb, &daddr.lsap);

		sk = llc_find_sock(sap, &saddr, &daddr);
		if (!sk) { /* didn't find an active connection; allocate a
			    * connection to use; associate it with this SAP
			    */
			sk = llc_sock_alloc();
			if (!sk)
				goto drop;
			memcpy(&llc_sk(sk)->daddr, &saddr, sizeof(saddr));
			llc_sap_assign_sock(sap, sk);
			sock_hold(sk);
		}
		bh_lock_sock(sk);
		if (!sk->lock.users) {
			/* FIXME: Check this on SMP as it is now calling
			 * llc_pdu_router _with_ the lock held.
			 * Old comment:
			 * With the current code one can't call
			 * llc_pdu_router with the socket lock held, cause
			 * it'll route the pdu to the upper layers and it can
			 * reenter llc and in llc_req_prim will try to grab
			 * the same lock, maybe we should use spin_trylock_bh
			 * in the llc_req_prim (llc_data_req_handler, etc) and
			 * add the request to the backlog, well see...
			 */
			llc_pdu_router(llc_sk(sk)->sap, sk, skb, LLC_TYPE_2);
			bh_unlock_sock(sk);
		} else {
			skb->cb[0] = LLC_PACKET;
			sk_add_backlog(sk, skb);
			bh_unlock_sock(sk);
		}
		sock_put(sk);
	} else /* unknown or not supported pdu */
 		goto drop;
out:
	return 0;
drop:
	kfree_skb(skb);
	goto out;
}

/**
 *	fix_up_incoming_skb - initializes skb pointers
 *	@skb: This argument points to incoming skb
 *
 *	Initializes internal skb pointer to start of network layer by deriving
 *	length of LLC header; finds length of LLC control field in LLC header
 *	by looking at the two lowest-order bits of the first control field
 *	byte; field is either 3 or 4 bytes long.
 */
static void fix_up_incoming_skb(struct sk_buff *skb)
{
	u8 llc_len = 2;
	llc_pdu_sn_t *pdu = (llc_pdu_sn_t *)skb->data;

	if ((pdu->ctrl_1 & LLC_PDU_TYPE_MASK) == LLC_PDU_TYPE_U)
		llc_len = 1;
	llc_len += 2;
	skb_pull(skb, llc_len);
	if (skb->protocol == htons(ETH_P_802_2)) {
		u16 pdulen = ((struct ethhdr *)skb->mac.raw)->h_proto,
		    data_size = ntohs(pdulen) - llc_len;

		skb_trim(skb, data_size);
	}
}

/**
 *	llc_pdu_router - routes received pdus to the upper layers
 *	@sap: current sap component structure.
 *	@sk: current connection structure.
 *	@frame: received frame.
 *	@type: type of received frame, that is LLC_TYPE_1 or LLC_TYPE_2
 *
 *	Queues received PDUs from LLC_MAC PDU receive queue until queue is
 *	empty; examines LLC header to determine the destination of PDU, if DSAP
 *	is NULL then data unit destined for station else frame destined for SAP
 *	or connection; finds a matching open SAP, if one, forwards the packet
 *	to it; if no matching SAP, drops the packet. Returns 0 or the return of
 *	llc_conn_send_ev (that may well result in the connection being
 *	destroyed)
 */
int llc_pdu_router(struct llc_sap *sap, struct sock* sk,
		   struct sk_buff *skb, u8 type)
{
	llc_pdu_sn_t *pdu = (llc_pdu_sn_t *)skb->nh.raw;
	int rc = 0;

	if (!pdu->dsap) {
		struct llc_station *station = llc_station_get();
		struct llc_station_state_ev *stat_ev =
						  llc_station_alloc_ev(station);
		if (stat_ev) {
			stat_ev->type		 = LLC_STATION_EV_TYPE_PDU;
			stat_ev->data.pdu.skb	 = skb;
			stat_ev->data.pdu.reason = 0;
			llc_station_send_ev(station, stat_ev);
		}
	} else if (type == LLC_TYPE_1) {
		struct llc_sap_state_ev *sap_ev = llc_sap_alloc_ev(sap);

		if (sap_ev) {
			sap_ev->type		= LLC_SAP_EV_TYPE_PDU;
			sap_ev->data.pdu.skb	= skb;
			sap_ev->data.pdu.reason = 0;
			llc_sap_send_ev(sap, sap_ev);
		}
	} else if (type == LLC_TYPE_2) {
		struct llc_conn_state_ev *conn_ev = llc_conn_alloc_ev(sk);
		struct llc_opt *llc = llc_sk(sk);

		if (!llc->dev)
			llc->dev = skb->dev;
		if (conn_ev) {
			conn_ev->type		 = LLC_CONN_EV_TYPE_PDU;
			conn_ev->data.pdu.skb	 = skb;
			conn_ev->data.pdu.reason = 0;
			rc = llc_conn_send_ev(sk, conn_ev);
		}
	}
	return rc;
}

/**
 *	lan_hdrs_init - fills MAC header fields
 *	@skb: Address of the frame to initialize its MAC header
 *	@sa: The MAC source address
 *	@da: The MAC destination address
 *
 *	Fills MAC header fields, depending on MAC type. Returns 0, If MAC type
 *	is a valid type and initialization completes correctly 1, otherwise.
 */
u16 lan_hdrs_init(struct sk_buff *skb, u8 *sa, u8 *da)
{
	u8 *saddr;
	u8 *daddr;
	u16 rc = 0;

	switch (skb->dev->type) {
#ifdef CONFIG_TR
		case ARPHRD_IEEE802_TR: {
			struct trh_hdr *trh = (struct trh_hdr *)
						    skb_push(skb, sizeof(*trh));
			struct net_device *dev = skb->dev;

			trh->ac = AC;
			trh->fc = LLC_FRAME;
			if (sa)
				memcpy(trh->saddr, sa, dev->addr_len);
			else
				memset(trh->saddr, 0, dev->addr_len);
			if (da) {
				memcpy(trh->daddr, da, dev->addr_len);
				tr_source_route(skb, trh, dev);
			}
			skb->mac.raw = skb->data;
			break;
		}
#endif
		case ARPHRD_ETHER:
		case ARPHRD_LOOPBACK: {
			unsigned short len = skb->len;

			skb->mac.raw = skb_push(skb, sizeof(struct ethhdr));
			memset(skb->mac.raw, 0, sizeof(struct ethhdr));
			((struct ethhdr *)skb->mac.raw)->h_proto = htons(len);
			daddr = ((struct ethhdr *)skb->mac.raw)->h_dest;
			saddr = ((struct ethhdr *)skb->mac.raw)->h_source;
			memcpy(daddr, da, ETH_ALEN);
			memcpy(saddr, sa, ETH_ALEN);
			break;
		}
		default:
			printk(KERN_WARNING "Unknown DEVICE type : %d\n",
			       skb->dev->type);
			rc = 1;
	}
	return rc;
}

/**
 *	mac_dev_peer - search the appropriate dev to send packets to peer
 *	@current_dev - Current device suggested by upper layer
 *	@type - hardware type
 *	@mac - mac address
 *
 *	Check if the we should use loopback to send packets, i.e., if the
 *	dmac belongs to one of the local interfaces, returning the pointer
 *	to the loopback &net_device struct or the current_dev if it is not
 *	local.
 */
struct net_device *mac_dev_peer(struct net_device *current_dev, int type,
				u8 *mac)
{
	struct net_device *dev;

        rtnl_lock();
        dev = dev_getbyhwaddr(type, mac);
        if (dev)
                dev = __dev_get_by_name("lo");
        rtnl_unlock();
	return dev ? : current_dev;
}
