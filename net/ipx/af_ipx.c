/*
 *	Implements an IPX socket layer (badly - but I'm working on it).
 *
 *	This code is derived from work by
 *		Ross Biro	: 	Writing the original IP stack
 *		Fred Van Kempen :	Tidying up the TCP/IP
 *
 *	Many thanks go to Keith Baker, Institute For Industrial Information
 *	Technology Ltd, Swansea University for allowing me to work on this
 *	in my own time even though it was in some ways related to commercial
 *	work I am currently employed to do there.
 *
 *	All the material in this file is subject to the Gnu license version 2.
 *	Neither Alan Cox nor the Swansea University Computer Society admit liability
 *	nor provide warranty for any of this software. This material is provided 
 *	as is and at no charge.		
 *
 *	Revision 0.21:	Uses the new generic socket option code.
 *	Revision 0.22:	Gcc clean ups and drop out device registration. Use the
 *			new multi-protocol edition of hard_header 
 *	Revision 0.23:  IPX /proc by Mark Evans.
 *     			Adding a route will overwrite any existing route to the same
 *			network.
 *	Revision 0.24:	Supports new /proc with no 4K limit
 *	Revision 0.25:	Add ephemeral sockets, passive local network 
 *			identification, support for local net 0 and
 *			multiple datalinks <Greg Page>
 *	Revision 0.26:  Device drop kills IPX routes via it. (needed for modules)
 *	Revision 0.27:  Autobind <Mark Evans>
 *	Revision 0.28:  Small fix for multiple local networks <Thomas Winder>
 *	Revision 0.29:  Assorted major errors removed <Mark Evans>
 *			Small correction to promisc mode error fix <Alan Cox>
 *			Asynchronous I/O support.
 *			Changed to use notifiers and the newer packet_type stuff.
 *			Assorted major fixes <Alejandro Liu>
 *	Revision 0.30:	Moved to net/ipx/...	<Alan Cox>
 *			Don't set address length on recvfrom that errors.
 *			Incorrect verify_area.
 *	Revision 0.31:	New sk_buffs. This still needs a lot of testing. <Alan Cox>
 *	Revision 0.32:  Using sock_alloc_send_skb, firewall hooks. <Alan Cox>
 *			Supports sendmsg/recvmsg
 *	Revision 0.33:	Internal network support, routing changes, uses a
 *			protocol private area for ipx data.
 *	Revision 0.34:	Module support. <Jim Freeman>
 *	Revision 0.35:  Checksum support. <Neil Turton>, hooked in by <Alan Cox>
 *			Handles WIN95 discovery packets <Volker Lendecke>
 *
 *	Protect the module by a MOD_INC_USE_COUNT/MOD_DEC_USE_COUNT
 *	pair. Also, now usage count is managed this way
 *	-Count one if the auto_interface mode is on
 *      -Count one per configured interface
 *
 *	Jacques Gelinas (jacques@solucorp.qc.ca)
 *
 *
 * 	Portions Copyright (c) 1995 Caldera, Inc. <greg@caldera.com>
 *	Neither Greg Page nor Caldera, Inc. admit liability nor provide 
 *	warranty for any of this software. This material is provided 
 *	"AS-IS" and at no charge.		
 */

#include <linux/module.h>

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <net/ipx.h>
#include <linux/inet.h>
#include <linux/route.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/termios.h>	/* For TIOCOUTQ/INQ */
#include <linux/interrupt.h>
#include <net/p8022.h>
#include <net/p8022tr.h>
#include <net/psnap.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/firewall.h>

#ifdef MODULE
static void ipx_proto_finito(void);
#endif /* def MODULE */

/* Configuration Variables */
static unsigned char	ipxcfg_max_hops = 16;
static char		ipxcfg_auto_select_primary = 0;
static char		ipxcfg_auto_create_interfaces = 0;

/* Global Variables */
static struct datalink_proto	*p8022_datalink = NULL;
static struct datalink_proto	*p8022tr_datalink = NULL;
static struct datalink_proto	*pEII_datalink = NULL;
static struct datalink_proto	*p8023_datalink = NULL;
static struct datalink_proto	*pSNAP_datalink = NULL;

static ipx_route 	*ipx_routes = NULL;
static ipx_interface	*ipx_interfaces = NULL;
static ipx_interface	*ipx_primary_net = NULL;
static ipx_interface	*ipx_internal_net = NULL;

static int
ipxcfg_set_auto_create(char val)
{
	if (ipxcfg_auto_create_interfaces != val){
		if (val){
			MOD_INC_USE_COUNT;
		}else{
			MOD_DEC_USE_COUNT;
		}
		ipxcfg_auto_create_interfaces = val;
	}
	return 0;
}
		
static int
ipxcfg_set_auto_select(char val)
{
	ipxcfg_auto_select_primary = val;
	if (val && (ipx_primary_net == NULL))
		ipx_primary_net = ipx_interfaces;
	return 0;
}

static int
ipxcfg_get_config_data(ipx_config_data *arg)
{
	ipx_config_data	vals;
	
	vals.ipxcfg_auto_create_interfaces = ipxcfg_auto_create_interfaces;
	vals.ipxcfg_auto_select_primary = ipxcfg_auto_select_primary;
	memcpy_tofs(arg, &vals, sizeof(vals));
	return 0;
}


/***********************************************************************************************************************\
*															*
*						Handlers for the socket list.						*
*															*
\***********************************************************************************************************************/

/*
 *	Note: Sockets may not be removed _during_ an interrupt or inet_bh
 *	handler using this technique. They can be added although we do not
 *	use this facility.
 */
 
static void 
ipx_remove_socket(ipx_socket *sk)
{
	ipx_socket	*s;
	ipx_interface	*intrfc;
	unsigned long	flags;

	save_flags(flags);
	cli();
	
	/* Determine interface with which socket is associated */
	intrfc = sk->protinfo.af_ipx.intrfc;
	if (intrfc == NULL) {
		restore_flags(flags);
		return;
	}

	s=intrfc->if_sklist;
	if(s==sk) {
		intrfc->if_sklist=s->next;
		restore_flags(flags);
		return;
	} 

	while(s && s->next) {
		if(s->next==sk) {
			s->next=sk->next;
			restore_flags(flags);
			return;
		}
		s=s->next;
	}
	restore_flags(flags);
}

/*
 *	This is only called from user mode. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */
 
static void 
ipx_destroy_socket(ipx_socket *sk)
{
	struct sk_buff	*skb;

	ipx_remove_socket(sk);
	while((skb=skb_dequeue(&sk->receive_queue))!=NULL) {
		kfree_skb(skb,FREE_READ);
	}
	
	sk_free(sk);
	MOD_DEC_USE_COUNT;
}
	
/* The following code is used to support IPX Interfaces (IPXITF).  An
 * IPX interface is defined by a physical device and a frame type.
 */

static ipx_route * ipxrtr_lookup(unsigned long);

static void
ipxitf_clear_primary_net(void)
{
	if (ipxcfg_auto_select_primary && (ipx_interfaces != NULL))
		ipx_primary_net = ipx_interfaces;
	else
		ipx_primary_net = NULL;
}

static ipx_interface *
ipxitf_find_using_phys(struct device *dev, unsigned short datalink)
{
	ipx_interface	*i;

	for (i=ipx_interfaces; 
		i && ((i->if_dev!=dev) || (i->if_dlink_type!=datalink)); 
		i=i->if_next)
		;
	return i;
}

static ipx_interface *
ipxitf_find_using_net(unsigned long net)
{
	ipx_interface	*i;

	if (net == 0L)
		return ipx_primary_net;

	for (i=ipx_interfaces; i && (i->if_netnum!=net); i=i->if_next)
		;

	return i;
}

/* Sockets are bound to a particular IPX interface. */
static void
ipxitf_insert_socket(ipx_interface *intrfc, ipx_socket *sk)
{
	ipx_socket	*s;

	sk->protinfo.af_ipx.intrfc = intrfc;
	sk->next = NULL;
	if (intrfc->if_sklist == NULL) {
		intrfc->if_sklist = sk;
	} else {
		for (s = intrfc->if_sklist; s->next != NULL; s = s->next)
			;
		s->next = sk;
	}
}

static ipx_socket *
ipxitf_find_socket(ipx_interface *intrfc, unsigned short port)
{
	ipx_socket	*s;

	for (s=intrfc->if_sklist; 
		(s != NULL) && (s->protinfo.af_ipx.port != port); 
		s=s->next)
		;

	return s;
}

#ifdef CONFIG_IPX_INTERN

static ipx_socket *
ipxitf_find_internal_socket(ipx_interface *intrfc,
			    unsigned char *node, unsigned short port)
{
	ipx_socket *s = intrfc->if_sklist;

	while (s != NULL)
	{
		if (   (s->protinfo.af_ipx.port == port)
		    && (memcmp(node, s->protinfo.af_ipx.node, IPX_NODE_LEN) == 0))
		{
			break;
		}
		s = s->next;
	}
	return s;
}	
#endif

static void ipxrtr_del_routes(ipx_interface *);

static void
ipxitf_down(ipx_interface *intrfc)
{
	ipx_interface	*i;
	ipx_socket	*s, *t;

	/* Delete all routes associated with this interface */
	ipxrtr_del_routes(intrfc);

	/* error sockets */
	for (s = intrfc->if_sklist; s != NULL; ) {
		s->err = ENOLINK;
		s->error_report(s);
		s->protinfo.af_ipx.intrfc = NULL;
		s->protinfo.af_ipx.port = 0;
		s->zapped=1;	/* Indicates it is no longer bound */
		t = s;
		s = s->next;
		t->next = NULL;
	}
	intrfc->if_sklist = NULL;

	/* remove this interface from list */
	if (intrfc == ipx_interfaces) {
		ipx_interfaces = intrfc->if_next;
	} else {
		for (i = ipx_interfaces; 
			(i != NULL) && (i->if_next != intrfc);
			i = i->if_next)
			;
		if ((i != NULL) && (i->if_next == intrfc)) 
			i->if_next = intrfc->if_next;
	}

	/* remove this interface from *special* networks */
	if (intrfc == ipx_primary_net)
		ipxitf_clear_primary_net();
	if (intrfc == ipx_internal_net)
		ipx_internal_net = NULL;

	kfree_s(intrfc, sizeof(*intrfc));
	/* sockets still dangling
	 * - must be closed from user space
	 */
	MOD_DEC_USE_COUNT;
	return;
}

static int 
ipxitf_device_event(struct notifier_block *notifier, unsigned long event, void *ptr)
{
	struct device *dev = ptr;
	ipx_interface *i, *tmp;

	if(event!=NETDEV_DOWN)
		return NOTIFY_DONE;

	for (i = ipx_interfaces; i != NULL; ) {
	
		tmp = i->if_next;
		if (i->if_dev == dev) 
			ipxitf_down(i);
		i = tmp;

	}

	return NOTIFY_DONE;
}

static int ipxitf_def_skb_handler(struct sock *sock, struct sk_buff *skb)
{
	int	retval;

	if((retval = sock_queue_rcv_skb(sock, skb))<0) 
	{
		/*
		 * skb->sk is NULL here, so FREE_WRITE does not hurt
		 * the sending socket.
	 	 */
		kfree_skb(skb,FREE_WRITE);
	}
	return retval;
}

/*
 * On input skb->sk is NULL. Nobody is charged for the memory.
 */

#ifdef CONFIG_IPX_INTERN
static int
ipxitf_demux_socket(ipx_interface *intrfc, struct sk_buff *skb, int copy) 
{
	ipx_packet	*ipx = (ipx_packet *)(skb->h.raw);
	ipx_socket	*s;

	int is_broadcast = (memcmp(ipx->ipx_dest.node, ipx_broadcast_node,
				   IPX_NODE_LEN) == 0);

	s = intrfc->if_sklist;

	while (s != NULL)
	{
		if (   (s->protinfo.af_ipx.port == ipx->ipx_dest.sock)
		    && (   is_broadcast
			|| (memcmp(ipx->ipx_dest.node, s->protinfo.af_ipx.node,
				   IPX_NODE_LEN) == 0)))
		{
			/* We found a socket to which to send */
			struct sk_buff *skb1;

			if (copy != 0)
			{
				skb1 = skb_clone(skb, GFP_ATOMIC);
				if (skb1 != NULL)
				{
					skb1->arp = skb1->free = 1;
				}
				else
				{
					return -ENOMEM;
				}
			}
			else
			{
				skb1 = skb;
				copy = 1; /* skb may only be used once */
			}
			ipxitf_def_skb_handler(s, skb1);

			if (intrfc != ipx_internal_net)
			{
				/* on an external interface, at most
                                 * one socket can listen.
				 */
				break;
			}
		}
		s = s->next;
	}

	if (copy == 0)
	{
		/* skb was solely for us, and we did not make a copy,
		 * so free it. FREE_WRITE does not hurt, because
		 * skb->sk is NULL here.
		 */
		kfree_skb(skb, FREE_WRITE);
	}
	return 0;
}

#else

static int
ipxitf_demux_socket(ipx_interface *intrfc, struct sk_buff *skb, int copy) 
{
	ipx_packet	*ipx = (ipx_packet *)(skb->h.raw);
	ipx_socket	*sock1 = NULL, *sock2 = NULL;
	struct sk_buff	*skb1 = NULL, *skb2 = NULL;

	sock1 = ipxitf_find_socket(intrfc, ipx->ipx_dest.sock);

	/*
	 *	We need to check if there is a primary net and if
	 *	this is addressed to one of the *SPECIAL* sockets because
	 *	these need to be propagated to the primary net.
	 *	The *SPECIAL* socket list contains: 0x452(SAP), 0x453(RIP) and
	 *	0x456(Diagnostic).
	 */
	 
	if (ipx_primary_net && (intrfc != ipx_primary_net)) 
	{
		switch (ntohs(ipx->ipx_dest.sock)) 
		{
			case 0x452:
			case 0x453:
			case 0x456:
				/*
				 *	The appropriate thing to do here is to
				 * 	dup the packet and route to the primary net
				 *	interface via ipxitf_send; however, we'll cheat
				 *	and just demux it here.
				 */
				sock2 = ipxitf_find_socket(ipx_primary_net, 
					ipx->ipx_dest.sock);
				break;
			default:
				break;
		}
	}

	/* 
	 *	if there is nothing to do, return. The kfree will
	 *	cancel any charging.
	 */
	 
	if (sock1 == NULL && sock2 == NULL) 
	{
		if (!copy) 
			kfree_skb(skb,FREE_WRITE);
		return 0;
	}

	/*
	 * This next segment of code is a little awkward, but it sets it up
	 * so that the appropriate number of copies of the SKB are made and 
	 * that skb1 and skb2 point to it (them) so that it (they) can be 
	 * demuxed to sock1 and/or sock2.  If we are unable to make enough
	 * copies, we do as much as is possible.
	 */
	 
	if (copy) 
	{
		skb1 = skb_clone(skb, GFP_ATOMIC);
		if (skb1 != NULL) 
			skb1->arp = skb1->free = 1;
	} 
	else 
	{
		skb1 = skb;
	}
	
	if (skb1 == NULL) 
		return -ENOMEM; 

	/*
	 *	Do we need 2 SKBs? 
	 */
	 
	if (sock1 && sock2) 
	{
		skb2 = skb_clone(skb1, GFP_ATOMIC);
		if (skb2 != NULL) 
			skb2->arp = skb2->free = 1;
	}
	else 
		skb2 = skb1;
		
	if (sock1)
		(void) ipxitf_def_skb_handler(sock1, skb1);

	if (skb2 == NULL) 
		return -ENOMEM;

	if (sock2)
		(void) ipxitf_def_skb_handler(sock2, skb2);

	return 0;
}
#endif

static struct sk_buff *
ipxitf_adjust_skbuff(ipx_interface *intrfc, struct sk_buff *skb)
{
	struct sk_buff	*skb2;
	int	in_offset = skb->h.raw - skb->head;
	int	out_offset = intrfc->if_ipx_offset;
	int	len;

	/* Hopefully, most cases */
	if (in_offset >= out_offset) {
		skb->arp = skb->free = 1;
		return skb;
	}

	/* Need new SKB */
	len = skb->len + out_offset;
	skb2 = alloc_skb(len, GFP_ATOMIC);
	if (skb2 != NULL) {
		skb_reserve(skb2,out_offset);
		skb2->h.raw=skb_put(skb2,skb->len);
		skb2->free=1;
		skb2->arp=1;
		memcpy(skb2->h.raw, skb->h.raw, skb->len);
	}
	kfree_skb(skb, FREE_WRITE);
	return skb2;
}

static int ipxitf_send(ipx_interface *intrfc, struct sk_buff *skb, char *node)
{
	ipx_packet	*ipx = (ipx_packet *)(skb->h.raw);
	struct device	*dev = intrfc->if_dev;
	struct datalink_proto	*dl = intrfc->if_dlink;
	char		dest_node[IPX_NODE_LEN];
	int		send_to_wire = 1;
	int		addr_len;
	
	/* 
	 *	We need to know how many skbuffs it will take to send out this
	 *	packet to avoid unnecessary copies.
	 */
	 
	if ((dl == NULL) || (dev == NULL) || (dev->flags & IFF_LOOPBACK)) 
		send_to_wire = 0;	/* No non looped */

	/*
	 *	See if this should be demuxed to sockets on this interface 
	 *
	 *	We want to ensure the original was eaten or that we only use
	 *	up clones.
	 */
	 
	if (ipx->ipx_dest.net == intrfc->if_netnum) 
	{
		/*
		 *	To our own node, loop and free the original.
		 */
		if (memcmp(intrfc->if_node, node, IPX_NODE_LEN) == 0) 
		{
			/*
			 *	Don't charge sender
			 */
			if(skb->sk)
			{
				atomic_sub(skb->truesize, &skb->sk->wmem_alloc);
				skb->sk=NULL;
			}
			/*
			 *	Will charge receiver
			 */
			return ipxitf_demux_socket(intrfc, skb, 0);
		}
		/*
		 *	Broadcast, loop and possibly keep to send on.
		 */
		if (memcmp(ipx_broadcast_node, node, IPX_NODE_LEN) == 0) 
		{
			if (!send_to_wire && skb->sk)
			{
				atomic_sub(skb->truesize, &skb->sk->wmem_alloc);
				skb->sk=NULL;
			}
			ipxitf_demux_socket(intrfc, skb, send_to_wire);
			if (!send_to_wire) 
				return 0;
		}
	}

	/*
	 *	If the originating net is not equal to our net; this is routed 
	 *	We are still charging the sender. Which is right - the driver
	 *	free will handle this fairly.
	 */
	 
	if (ipx->ipx_source.net != intrfc->if_netnum) 
	{
		if (++(ipx->ipx_tctrl) > ipxcfg_max_hops) 
			send_to_wire = 0;
	}

	if (!send_to_wire) 
	{
		/*
		 *	We do a FREE_WRITE here because this indicates how
		 *	to treat the socket with which the packet is 
	 	 *	associated.  If this packet is associated with a
		 *	socket at all, it must be the originator of the 
		 *	packet.   Routed packets will have no socket associated
		 *	with them.
		 */
		kfree_skb(skb,FREE_WRITE);
		return 0;
	}

	/*
	 *	Determine the appropriate hardware address 
	 */
	 
	addr_len = dev->addr_len;
	if (memcmp(ipx_broadcast_node, node, IPX_NODE_LEN) == 0) 
		memcpy(dest_node, dev->broadcast, addr_len);
	else
		memcpy(dest_node, &(node[IPX_NODE_LEN-addr_len]), addr_len);

	/*
	 *	Make any compensation for differing physical/data link size 
	 */
	 
	skb = ipxitf_adjust_skbuff(intrfc, skb);
	if (skb == NULL) 
		return 0;

	/* set up data link and physical headers */
	skb->dev = dev;
	skb->protocol = htons(ETH_P_IPX);
	dl->datalink_header(dl, skb, dest_node);
#if 0
	/* 
	 *	Now log the packet just before transmission 
	 */
	 
	dump_pkt("IPX snd:", (ipx_packet *)skb->h.raw);
	dump_data("ETH hdr:", skb->data, skb->h.raw - skb->data);
#endif

	/*
	 *	Send it out 
	 */
	 
	dev_queue_xmit(skb, dev, SOPRI_NORMAL);
	return 0;
}

static int ipxrtr_add_route(unsigned long, ipx_interface *, unsigned char *);

static int ipxitf_add_local_route(ipx_interface *intrfc)
{
	return ipxrtr_add_route(intrfc->if_netnum, intrfc, NULL);
}

static const char * ipx_frame_name(unsigned short);
static const char * ipx_device_name(ipx_interface *);
static int ipxrtr_route_skb(struct sk_buff *);

static int ipxitf_rcv(ipx_interface *intrfc, struct sk_buff *skb)
{
	ipx_packet	*ipx = (ipx_packet *) (skb->h.raw);
	ipx_interface	*i;

#ifdef CONFIG_FIREWALL	
	/*
	 *	We firewall first, ask questions later.
	 */
	 
	if (call_in_firewall(PF_IPX, skb->dev, ipx, NULL)!=FW_ACCEPT)
	{
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	
#endif	

	/* See if we should update our network number */
	if ((intrfc->if_netnum == 0L) && 
		(ipx->ipx_source.net == ipx->ipx_dest.net) &&
		(ipx->ipx_source.net != 0L)) 
	{
		/* NB: NetWare servers lie about their hop count so we
		 * dropped the test based on it.  This is the best way
		 * to determine this is a 0 hop count packet.
		 */
		if ((i=ipxitf_find_using_net(ipx->ipx_source.net))==NULL) 
		{
			intrfc->if_netnum = ipx->ipx_source.net;
			(void) ipxitf_add_local_route(intrfc);
		} 
		else 
		{
			printk(KERN_WARNING "IPX: Network number collision %lx\n        %s %s and %s %s\n",
				htonl(ipx->ipx_source.net), 
				ipx_device_name(i),
				ipx_frame_name(i->if_dlink_type),
				ipx_device_name(intrfc),
				ipx_frame_name(intrfc->if_dlink_type));
		}
	}

	if (ipx->ipx_dest.net == 0L)
		ipx->ipx_dest.net = intrfc->if_netnum;
	if (ipx->ipx_source.net == 0L)
		ipx->ipx_source.net = intrfc->if_netnum;

	if (intrfc->if_netnum != ipx->ipx_dest.net) 
	{
#ifdef CONFIG_FIREWALL	
		/*
		 *	See if we are allowed to firewall forward
		 */
		if (call_fw_firewall(PF_IPX, skb->dev, ipx, NULL)!=FW_ACCEPT)
		{
			kfree_skb(skb, FREE_READ);
			return 0;
		}
#endif		
		/* We only route point-to-point packets. */
		if ((skb->pkt_type != PACKET_BROADCAST) &&
			(skb->pkt_type != PACKET_MULTICAST))
			return ipxrtr_route_skb(skb);
		
		kfree_skb(skb,FREE_READ);
		return 0;
	}

	/* see if we should keep it */
	if ((memcmp(ipx_broadcast_node, ipx->ipx_dest.node, IPX_NODE_LEN) == 0) 
		|| (memcmp(intrfc->if_node, ipx->ipx_dest.node, IPX_NODE_LEN) == 0)) 
	{
		return ipxitf_demux_socket(intrfc, skb, 0);
	}

	/* we couldn't pawn it off so unload it */
	kfree_skb(skb,FREE_READ);
	return 0;
}

static void
ipxitf_insert(ipx_interface *intrfc)
{
	ipx_interface	*i;

	intrfc->if_next = NULL;
	if (ipx_interfaces == NULL) {
		ipx_interfaces = intrfc;
	} else {
		for (i = ipx_interfaces; i->if_next != NULL; i = i->if_next)
			;
		i->if_next = intrfc;
	}

	if (ipxcfg_auto_select_primary && (ipx_primary_net == NULL))
		ipx_primary_net = intrfc;
	MOD_INC_USE_COUNT;
	return;
}

static int 
ipxitf_create_internal(ipx_interface_definition *idef)
{
	ipx_interface	*intrfc;

	/* Only one primary network allowed */
	if (ipx_primary_net != NULL) return -EEXIST;

	/* Must have a valid network number */
	if (idef->ipx_network == 0L) return -EADDRNOTAVAIL;
	if (ipxitf_find_using_net(idef->ipx_network) != NULL)
		return -EADDRINUSE;

	intrfc=(ipx_interface *)kmalloc(sizeof(ipx_interface),GFP_ATOMIC);
	if (intrfc==NULL)
		return -EAGAIN;
	intrfc->if_dev=NULL;
	intrfc->if_netnum=idef->ipx_network;
	intrfc->if_dlink_type = 0;
	intrfc->if_dlink = NULL;
	intrfc->if_sklist = NULL;
	intrfc->if_internal = 1;
	intrfc->if_ipx_offset = 0;
	intrfc->if_sknum = IPX_MIN_EPHEMERAL_SOCKET;
	memcpy((char *)&(intrfc->if_node), idef->ipx_node, IPX_NODE_LEN);
	ipx_internal_net = intrfc;
	ipx_primary_net = intrfc;
	ipxitf_insert(intrfc);
	return ipxitf_add_local_route(intrfc);
}

static int
ipx_map_frame_type(unsigned char type)
{
	switch (type) {
	case IPX_FRAME_ETHERII: return htons(ETH_P_IPX);
	case IPX_FRAME_8022: return htons(ETH_P_802_2);
	case IPX_FRAME_TR_8022: return htons(ETH_P_TR_802_2);
	case IPX_FRAME_SNAP: return htons(ETH_P_SNAP);
	case IPX_FRAME_8023: return htons(ETH_P_802_3);
	}
	return 0;
}

static int 
ipxitf_create(ipx_interface_definition *idef)
{
	struct device	*dev;
	unsigned short	dlink_type = 0;
	struct datalink_proto	*datalink = NULL;
	ipx_interface	*intrfc;

	if (idef->ipx_special == IPX_INTERNAL) 
		return ipxitf_create_internal(idef);

	if ((idef->ipx_special == IPX_PRIMARY) && (ipx_primary_net != NULL))
		return -EEXIST;

	if ((idef->ipx_network != 0L) &&
		(ipxitf_find_using_net(idef->ipx_network) != NULL))
		return -EADDRINUSE;

	switch (idef->ipx_dlink_type) {
	case IPX_FRAME_ETHERII: 
		dlink_type = htons(ETH_P_IPX);
		datalink = pEII_datalink;
		break;
	case IPX_FRAME_TR_8022:
		dlink_type = htons(ETH_P_TR_802_2);
		datalink = p8022tr_datalink;
		break;
	case IPX_FRAME_8022:
		dlink_type = htons(ETH_P_802_2);
		datalink = p8022_datalink;
		break;
	case IPX_FRAME_SNAP:
		dlink_type = htons(ETH_P_SNAP);
		datalink = pSNAP_datalink;
		break;
	case IPX_FRAME_8023:
		dlink_type = htons(ETH_P_802_3);
		datalink = p8023_datalink;
		break;
	case IPX_FRAME_NONE:
	default:
		break;
	}

	if (datalink == NULL) 
		return -EPROTONOSUPPORT;

	dev=dev_get(idef->ipx_device);
	if (dev==NULL) 
		return -ENODEV;

	if (!(dev->flags & IFF_UP))
		return -ENETDOWN;

	/* Check addresses are suitable */
	if(dev->addr_len>IPX_NODE_LEN)
		return -EINVAL;

	if ((intrfc = ipxitf_find_using_phys(dev, dlink_type)) == NULL) 
	{
		/* Ok now create */
		intrfc=(ipx_interface *)kmalloc(sizeof(ipx_interface),GFP_ATOMIC);
		if (intrfc==NULL)
			return -EAGAIN;
		intrfc->if_dev=dev;
		intrfc->if_netnum=idef->ipx_network;
		intrfc->if_dlink_type = dlink_type;
		intrfc->if_dlink = datalink;
		intrfc->if_sklist = NULL;
		intrfc->if_sknum = IPX_MIN_EPHEMERAL_SOCKET;
		/* Setup primary if necessary */
		if ((idef->ipx_special == IPX_PRIMARY)) 
			ipx_primary_net = intrfc;
		intrfc->if_internal = 0;
		intrfc->if_ipx_offset = dev->hard_header_len + datalink->header_length;
		if(memcmp(idef->ipx_node, "\000\000\000\000\000\000", IPX_NODE_LEN)==0)
		{
			memset(intrfc->if_node, 0, IPX_NODE_LEN);
			memcpy((char *)&(intrfc->if_node[IPX_NODE_LEN-dev->addr_len]), 
				dev->dev_addr, dev->addr_len);
		}
		else
			memcpy(intrfc->if_node, idef->ipx_node, IPX_NODE_LEN);
		ipxitf_insert(intrfc);
	}

	/* If the network number is known, add a route */
	if (intrfc->if_netnum == 0L) 
		return 0;

	return ipxitf_add_local_route(intrfc);
}

static int 
ipxitf_delete(ipx_interface_definition *idef)
{
	struct device	*dev = NULL;
	unsigned short	dlink_type = 0;
	ipx_interface	*intrfc;

	if (idef->ipx_special == IPX_INTERNAL) {
		if (ipx_internal_net != NULL) {
			ipxitf_down(ipx_internal_net);
			return 0;
		}
		return -ENOENT;
	}

	dlink_type = ipx_map_frame_type(idef->ipx_dlink_type);
	if (dlink_type == 0)
		return -EPROTONOSUPPORT;

	dev=dev_get(idef->ipx_device);
	if(dev==NULL) return -ENODEV;

	intrfc = ipxitf_find_using_phys(dev, dlink_type);
	if (intrfc != NULL) {
		ipxitf_down(intrfc);
		return 0;
	}
	return -EINVAL;
}

static ipx_interface *
ipxitf_auto_create(struct device *dev, unsigned short dlink_type)
{
	struct datalink_proto *datalink = NULL;
	ipx_interface	*intrfc;

	switch (htons(dlink_type)) {
	case ETH_P_IPX: datalink = pEII_datalink; break;
	case ETH_P_802_2: datalink = p8022_datalink; break;
	case ETH_P_TR_802_2: datalink = p8022tr_datalink; break;
	case ETH_P_SNAP: datalink = pSNAP_datalink; break;
	case ETH_P_802_3: datalink = p8023_datalink; break;
	default: return NULL;
	}
	
	if (dev == NULL)
		return NULL;

	/* Check addresses are suitable */
	if(dev->addr_len>IPX_NODE_LEN) return NULL;

	intrfc=(ipx_interface *)kmalloc(sizeof(ipx_interface),GFP_ATOMIC);
	if (intrfc!=NULL) {
		intrfc->if_dev=dev;
		intrfc->if_netnum=0L;
		intrfc->if_dlink_type = dlink_type;
		intrfc->if_dlink = datalink;
		intrfc->if_sklist = NULL;
		intrfc->if_internal = 0;
		intrfc->if_sknum = IPX_MIN_EPHEMERAL_SOCKET;
		intrfc->if_ipx_offset = dev->hard_header_len + 
			datalink->header_length;
		memset(intrfc->if_node, 0, IPX_NODE_LEN);
		memcpy((char *)&(intrfc->if_node[IPX_NODE_LEN-dev->addr_len]), 
			dev->dev_addr, dev->addr_len);
		ipxitf_insert(intrfc);
	}

	return intrfc;
}

static int 
ipxitf_ioctl_real(unsigned int cmd, void *arg)
{
	int err;
	switch(cmd)
	{
		case SIOCSIFADDR:
		{
			struct ifreq ifr;
			struct sockaddr_ipx *sipx;
			ipx_interface_definition f;
			err=verify_area(VERIFY_READ,arg,sizeof(ifr));
			if(err)
				return err;
			memcpy_fromfs(&ifr,arg,sizeof(ifr));
			sipx=(struct sockaddr_ipx *)&ifr.ifr_addr;
			if(sipx->sipx_family!=AF_IPX)
				return -EINVAL;
			f.ipx_network=sipx->sipx_network;
			memcpy(f.ipx_device, ifr.ifr_name, sizeof(f.ipx_device));
			memcpy(f.ipx_node, sipx->sipx_node, IPX_NODE_LEN);
			f.ipx_dlink_type=sipx->sipx_type;
			f.ipx_special=sipx->sipx_special;
			if(sipx->sipx_action==IPX_DLTITF)
				return ipxitf_delete(&f);
			else
				return ipxitf_create(&f);
		}
		case SIOCGIFADDR:
		{
			struct ifreq ifr;
			struct sockaddr_ipx *sipx;
			ipx_interface *ipxif;
			struct device *dev;
			err=verify_area(VERIFY_WRITE,arg,sizeof(ifr));
			if(err)
				return err;
			memcpy_fromfs(&ifr,arg,sizeof(ifr));
			sipx=(struct sockaddr_ipx *)&ifr.ifr_addr;
			dev=dev_get(ifr.ifr_name);
			if(!dev)
				return -ENODEV;
			ipxif=ipxitf_find_using_phys(dev, ipx_map_frame_type(sipx->sipx_type));
			if(ipxif==NULL)
				return -EADDRNOTAVAIL;
			sipx->sipx_family=AF_IPX;
			sipx->sipx_network=ipxif->if_netnum;
			memcpy(sipx->sipx_node, ipxif->if_node, sizeof(sipx->sipx_node));
			memcpy_tofs(arg,&ifr,sizeof(ifr));
			return 0;
		}
		case SIOCAIPXITFCRT:
			err=verify_area(VERIFY_READ,arg,sizeof(char));
			if(err)
				return err;
			return ipxcfg_set_auto_create(get_fs_byte(arg));
		case SIOCAIPXPRISLT:
			err=verify_area(VERIFY_READ,arg,sizeof(char));
			if(err)
				return err;
			return ipxcfg_set_auto_select(get_fs_byte(arg));
		default:
			return -EINVAL;
	}
}

static int 
ipxitf_ioctl(unsigned int cmd, void *arg)
{
	int ret;
	MOD_INC_USE_COUNT;
	ret = ipxitf_ioctl_real (cmd,arg);
	MOD_DEC_USE_COUNT;
	return ret;
}
/*******************************************************************************************************************\
*													            *
*	            			Routing tables for the IPX socket layer				            *
*														    *
\*******************************************************************************************************************/

static ipx_route *
ipxrtr_lookup(unsigned long net)
{
	ipx_route *r;

	for (r=ipx_routes; (r!=NULL) && (r->ir_net!=net); r=r->ir_next)
		;

	return r;
}

static int
ipxrtr_add_route(unsigned long network, ipx_interface *intrfc, unsigned char *node)
{
	ipx_route	*rt;

	/* Get a route structure; either existing or create */
	rt = ipxrtr_lookup(network);
	if (rt==NULL) {
		rt=(ipx_route *)kmalloc(sizeof(ipx_route),GFP_ATOMIC);
		if(rt==NULL)
			return -EAGAIN;
		rt->ir_next=ipx_routes;
		ipx_routes=rt;
	}
	else if (intrfc == ipx_internal_net)
		return(-EEXIST);

	rt->ir_net = network;
	rt->ir_intrfc = intrfc;
	if (node == NULL) {
		memset(rt->ir_router_node, '\0', IPX_NODE_LEN);
		rt->ir_routed = 0;
	} else {
		memcpy(rt->ir_router_node, node, IPX_NODE_LEN);
		rt->ir_routed=1;
	}
	return 0;
}

static void
ipxrtr_del_routes(ipx_interface *intrfc)
{
	ipx_route	**r, *tmp;

	for (r = &ipx_routes; (tmp = *r) != NULL; ) {
		if (tmp->ir_intrfc == intrfc) {
			*r = tmp->ir_next;
			kfree_s(tmp, sizeof(ipx_route));
		} else {
			r = &(tmp->ir_next);
		}
	}
}

static int 
ipxrtr_create(ipx_route_definition *rd)
{
	ipx_interface *intrfc;

	/* Find the appropriate interface */
	intrfc = ipxitf_find_using_net(rd->ipx_router_network);
	if (intrfc == NULL)
		return -ENETUNREACH;

	return ipxrtr_add_route(rd->ipx_network, intrfc, rd->ipx_router_node);
}


static int 
ipxrtr_delete(long net)
{
	ipx_route	**r;
	ipx_route	*tmp;

	for (r = &ipx_routes; (tmp = *r) != NULL; ) {
		if (tmp->ir_net == net) {
			if (!(tmp->ir_routed)) {
				/* Directly connected; can't lose route */
				return -EPERM;
			}
			*r = tmp->ir_next;
			kfree_s(tmp, sizeof(ipx_route));
			return 0;
		} 
		r = &(tmp->ir_next);
	}

	return -ENOENT;
}

/*
 *	Checksum routine for IPX
 */
 
/* Note: We assume ipx_tctrl==0 and htons(length)==ipx_pktsize */

static __u16 ipx_set_checksum(ipx_packet *packet,int length) 
{
	/* 
	 *	NOTE: sum is a net byte order quantity, which optimizes the 
	 *	loop. This only works on big and little endian machines. (I
	 *	don't know of a machine that isn't.)
	 */

	__u32 sum=0;

	/*
	 *	Pointer to second word - We skip the checksum field
	 */

	__u16 *p=(__u16 *)&packet->ipx_pktsize;

	/*
	 *	Number of complete words 
	 */

	__u32 i=length>>1;

	/*
	 *	Loop through all complete words except the checksum field 
	 */

	while(--i)
		sum+=*p++;

	/*
	 *	Add on the last part word if it exists 
	 */

	if(packet->ipx_pktsize&htons(1))
		sum+=ntohs(0xff00)&*p;

	/*
	 *	Do final fixup 
	 */
	 
	sum=(sum&0xffff)+(sum>>16);

	/*
	 *	It's a pity there's no concept of carry in C 
	 */

	if(sum>=0x10000)
		sum++;
		
	return ~sum;
};

 
/*
 *	Route an outgoing frame from a socket.
 */

static int ipxrtr_route_packet(ipx_socket *sk, struct sockaddr_ipx *usipx, struct iovec *iov, int len)
{
	struct sk_buff *skb;
	ipx_interface *intrfc;
	ipx_packet *ipx;
	int size;
	int ipx_offset;
	ipx_route *rt = NULL;
	int err;
	
	/* Find the appropriate interface on which to send packet */
	if ((usipx->sipx_network == 0L) && (ipx_primary_net != NULL)) 
	{
		usipx->sipx_network = ipx_primary_net->if_netnum;
		intrfc = ipx_primary_net;
	} 
	else 
	{
		rt = ipxrtr_lookup(usipx->sipx_network);
		if (rt==NULL) {
			return -ENETUNREACH;
		}
		intrfc = rt->ir_intrfc;
	}
	
	ipx_offset = intrfc->if_ipx_offset;
	size=sizeof(ipx_packet)+len;
	size += ipx_offset;

	skb=sock_alloc_send_skb(sk, size, 0, 0, &err);
	if(skb==NULL)
		return err;

	skb_reserve(skb,ipx_offset);
	skb->free=1;
	skb->arp=1;
	skb->sk=sk;

	/* Fill in IPX header */
	ipx=(ipx_packet *)skb_put(skb,sizeof(ipx_packet));
	ipx->ipx_pktsize=htons(len+sizeof(ipx_packet));
	ipx->ipx_tctrl=0;
	ipx->ipx_type=usipx->sipx_type;
	skb->h.raw = (unsigned char *)ipx;

	ipx->ipx_source.net = sk->protinfo.af_ipx.intrfc->if_netnum;
#ifdef CONFIG_IPX_INTERN
	memcpy(ipx->ipx_source.node, sk->protinfo.af_ipx.node, IPX_NODE_LEN);
#else
	if ((err = ntohs(sk->protinfo.af_ipx.port)) == 0x453 || err == 0x452)  
	{
		/* RIP/SAP special handling for mars_nwe */
		ipx->ipx_source.net = intrfc->if_netnum;
		memcpy(ipx->ipx_source.node, intrfc->if_node, IPX_NODE_LEN);
	}
	else
	{
		ipx->ipx_source.net = sk->protinfo.af_ipx.intrfc->if_netnum;
		memcpy(ipx->ipx_source.node, sk->protinfo.af_ipx.intrfc->if_node, IPX_NODE_LEN);
	}
#endif
	ipx->ipx_source.sock = sk->protinfo.af_ipx.port;
	ipx->ipx_dest.net=usipx->sipx_network;
	memcpy(ipx->ipx_dest.node,usipx->sipx_node,IPX_NODE_LEN);
	ipx->ipx_dest.sock=usipx->sipx_port;

	memcpy_fromiovec(skb_put(skb,len),iov,len);

	/*
	 *	Apply checksum. Not allowed on 802.3 links.
	 */
	 
	if(sk->no_check || intrfc->if_dlink_type==IPX_FRAME_8023)
		ipx->ipx_checksum=0xFFFF;
	else
		ipx->ipx_checksum=ipx_set_checksum(ipx, len+sizeof(ipx_packet));

#ifdef CONFIG_FIREWALL	
	if(call_out_firewall(PF_IPX, skb->dev, ipx, NULL)!=FW_ACCEPT)
	{
		kfree_skb(skb, FREE_WRITE);
		return -EPERM;
	}
#endif
	
	return ipxitf_send(intrfc, skb, (rt && rt->ir_routed) ? 
				rt->ir_router_node : ipx->ipx_dest.node);
}
	
static int
ipxrtr_route_skb(struct sk_buff *skb)
{
	ipx_packet	*ipx = (ipx_packet *) (skb->h.raw);
	ipx_route	*r;
	ipx_interface	*i;

	r = ipxrtr_lookup(ipx->ipx_dest.net);
	if (r == NULL) {
		/* no known route */
		kfree_skb(skb,FREE_READ);
		return 0;
	}
	i = r->ir_intrfc;
	(void)ipxitf_send(i, skb, (r->ir_routed) ? 
			r->ir_router_node : ipx->ipx_dest.node);
	return 0;
}

/*
 *	We use a normal struct rtentry for route handling
 */

static int ipxrtr_ioctl(unsigned int cmd, void *arg)
{
	int err;
	struct rtentry rt;	/* Use these to behave like 'other' stacks */
	struct sockaddr_ipx *sg,*st;

	err=verify_area(VERIFY_READ,arg,sizeof(rt));
	if(err)
		return err;
		
	memcpy_fromfs(&rt,arg,sizeof(rt));
	
	sg=(struct sockaddr_ipx *)&rt.rt_gateway;
	st=(struct sockaddr_ipx *)&rt.rt_dst;
	
	if(!(rt.rt_flags&RTF_GATEWAY))
		return -EINVAL;		/* Direct routes are fixed */
	if(sg->sipx_family!=AF_IPX)
		return -EINVAL;
	if(st->sipx_family!=AF_IPX)
		return -EINVAL;
		
	switch(cmd)
	{
		case SIOCDELRT:
			return ipxrtr_delete(st->sipx_network);
		case SIOCADDRT:
		{
			struct ipx_route_definition f;
			f.ipx_network=st->sipx_network;
			f.ipx_router_network=sg->sipx_network;
			memcpy(f.ipx_router_node, sg->sipx_node, IPX_NODE_LEN);
			return ipxrtr_create(&f);
		}
		default:
			return -EINVAL;
	}
}

static const char *
ipx_frame_name(unsigned short frame)
{
	switch (ntohs(frame)) {
	case ETH_P_IPX: return "EtherII";
	case ETH_P_802_2: return "802.2";
	case ETH_P_SNAP: return "SNAP";
	case ETH_P_802_3: return "802.3";
	case ETH_P_TR_802_2: return "802.2TR";
	default: return "None";
	}
}

static const char *
ipx_device_name(ipx_interface *intrfc)
{
	return (intrfc->if_internal ? "Internal" :
		(intrfc->if_dev ? intrfc->if_dev->name : "Unknown"));
}

/* Called from proc fs */
static int ipx_interface_get_info(char *buffer, char **start, off_t offset,
				  int length, int dummy)
{
	ipx_interface *i;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	/* Theory.. Keep printing in the same place until we pass offset */

	len += sprintf (buffer,"%-11s%-15s%-9s%-11s%s\n", "Network", 
		"Node_Address", "Primary", "Device", "Frame_Type");
	for (i = ipx_interfaces; i != NULL; i = i->if_next) {
		len += sprintf(buffer+len, "%08lX   ", ntohl(i->if_netnum));
		len += sprintf (buffer+len,"%02X%02X%02X%02X%02X%02X   ", 
				i->if_node[0], i->if_node[1], i->if_node[2],
				i->if_node[3], i->if_node[4], i->if_node[5]);
		len += sprintf(buffer+len, "%-9s", (i == ipx_primary_net) ?
			"Yes" : "No");
		len += sprintf (buffer+len, "%-11s", ipx_device_name(i));
		len += sprintf (buffer+len, "%s\n", 
			ipx_frame_name(i->if_dlink_type));

		/* Are we still dumping unwanted data then discard the record */
		pos=begin+len;
		
		if(pos<offset) {
			len=0;			/* Keep dumping into the buffer start */
			begin=pos;
		}
		if(pos>offset+length)		/* We have dumped enough */
			break;
	}
	
	/* The data in question runs from begin to begin+len */
	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Remove unwanted header data from length */
	if(len>length)
		len=length;		/* Remove unwanted tail data from length */
	
	return len;
}

static int ipx_get_info(char *buffer, char **start, off_t offset,
			int length, int dummy)
{
	ipx_socket *s;
	ipx_interface *i;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	/* Theory.. Keep printing in the same place until we pass offset */

#ifdef CONFIG_IPX_INTERN	
	len += sprintf (buffer,"%-28s%-28s%-10s%-10s%-7s%s\n", "Local_Address", 
#else
	len += sprintf (buffer,"%-15s%-28s%-10s%-10s%-7s%s\n", "Local_Address", 
#endif
			"Remote_Address", "Tx_Queue", "Rx_Queue", 
			"State", "Uid");
	for (i = ipx_interfaces; i != NULL; i = i->if_next) {
		for (s = i->if_sklist; s != NULL; s = s->next) {
#ifdef CONFIG_IPX_INTERN
			len += sprintf(buffer+len,
				       "%08lX:%02X%02X%02X%02X%02X%02X:%04X  ", 
				 htonl(s->protinfo.af_ipx.intrfc->if_netnum),
				       s->protinfo.af_ipx.node[0],
				       s->protinfo.af_ipx.node[1], 
				       s->protinfo.af_ipx.node[2], 
				       s->protinfo.af_ipx.node[3], 
				       s->protinfo.af_ipx.node[4], 
				       s->protinfo.af_ipx.node[5],
				       htons(s->protinfo.af_ipx.port));
#else
			len += sprintf(buffer+len,"%08lX:%04X  ", 
				       htonl(i->if_netnum),
				       htons(s->protinfo.af_ipx.port));
#endif
			if (s->state!=TCP_ESTABLISHED) {
				len += sprintf(buffer+len, "%-28s", "Not_Connected");
			} else {
				len += sprintf (buffer+len,
					"%08lX:%02X%02X%02X%02X%02X%02X:%04X  ", 
					htonl(s->protinfo.af_ipx.dest_addr.net),
					s->protinfo.af_ipx.dest_addr.node[0],
					s->protinfo.af_ipx.dest_addr.node[1], 
					s->protinfo.af_ipx.dest_addr.node[2],
					s->protinfo.af_ipx.dest_addr.node[3], 
					s->protinfo.af_ipx.dest_addr.node[4],
					s->protinfo.af_ipx.dest_addr.node[5],
					htons(s->protinfo.af_ipx.dest_addr.sock));
			}
			len += sprintf (buffer+len,"%08X  %08X  ", 
				s->wmem_alloc, s->rmem_alloc);
			len += sprintf (buffer+len,"%02X     %03d\n", 
				s->state, SOCK_INODE(s->socket)->i_uid);
		
			/* Are we still dumping unwanted data then discard the record */
			pos=begin+len;
		
			if(pos<offset)
			{
				len=0;			/* Keep dumping into the buffer start */
				begin=pos;
			}
			if(pos>offset+length)		/* We have dumped enough */
				break;
		}
	}
	
	/* The data in question runs from begin to begin+len */
	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Remove unwanted header data from length */
	if(len>length)
		len=length;		/* Remove unwanted tail data from length */
	
	return len;
}

static int ipx_rt_get_info(char *buffer, char **start, off_t offset,
			   int length, int dummy)
{
	ipx_route *rt;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	len += sprintf (buffer,"%-11s%-13s%s\n", 
			"Network", "Router_Net", "Router_Node");
	for (rt = ipx_routes; rt != NULL; rt = rt->ir_next)
	{
		len += sprintf (buffer+len,"%08lX   ", ntohl(rt->ir_net));
		if (rt->ir_routed) {
			len += sprintf (buffer+len,"%08lX     %02X%02X%02X%02X%02X%02X\n", 
				ntohl(rt->ir_intrfc->if_netnum), 
				rt->ir_router_node[0], rt->ir_router_node[1], 
				rt->ir_router_node[2], rt->ir_router_node[3], 
				rt->ir_router_node[4], rt->ir_router_node[5]);
		} else {
			len += sprintf (buffer+len, "%-13s%s\n",
					"Directly", "Connected");
		}
		pos=begin+len;
		if(pos<offset)
		{
			len=0;
			begin=pos;
		}
		if(pos>offset+length)
			break;
	}
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	return len;
}

/*******************************************************************************************************************\
*													            *
*	      Handling for system calls applied via the various interfaces to an IPX socket object		    *
*														    *
\*******************************************************************************************************************/

static int ipx_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
		default:
			return(-EINVAL);
	}
}

static int ipx_setsockopt(struct socket *sock, int level, int optname, char *optval, int optlen)
{
	ipx_socket *sk;
	int err,opt;
	
	sk=(ipx_socket *)sock->data;
	
	if(optval==NULL)
		return(-EINVAL);

	err=verify_area(VERIFY_READ,optval,sizeof(int));
	if(err)
		return err;
	opt=get_fs_long((unsigned long *)optval);
	
	switch(level)
	{
		case SOL_IPX:
			switch(optname)
			{
				case IPX_TYPE:
					sk->protinfo.af_ipx.type=opt;
					return 0;
				default:
					return -EOPNOTSUPP;
			}
			break;
			
		case SOL_SOCKET:
			return sock_setsockopt(sk,level,optname,optval,optlen);

		default:
			return -EOPNOTSUPP;
	}
}

static int ipx_getsockopt(struct socket *sock, int level, int optname,
	char *optval, int *optlen)
{
	ipx_socket *sk;
	int val=0;
	int err;
	
	sk=(ipx_socket *)sock->data;

	switch(level)
	{

		case SOL_IPX:
			switch(optname)
			{
				case IPX_TYPE:
					val=sk->protinfo.af_ipx.type;
					break;
				default:
					return -ENOPROTOOPT;
			}
			break;
			
		case SOL_SOCKET:
			return sock_getsockopt(sk,level,optname,optval,optlen);
			
		default:
			return -EOPNOTSUPP;
	}
	err=verify_area(VERIFY_WRITE,optlen,sizeof(int));
	if(err)
		return err;
	put_fs_long(sizeof(int),(unsigned long *)optlen);
	err=verify_area(VERIFY_WRITE,optval,sizeof(int));
	if (err) return err;
	put_fs_long(val,(unsigned long *)optval);
	return(0);
}

static int ipx_listen(struct socket *sock, int backlog)
{
	return -EOPNOTSUPP;
}

static void def_callback1(struct sock *sk)
{
	if(!sk->dead)
		wake_up_interruptible(sk->sleep);
}

static void def_callback2(struct sock *sk, int len)
{
	if(!sk->dead)
	{
		wake_up_interruptible(sk->sleep);
		sock_wake_async(sk->socket, 1);
	}
}

static int ipx_create(struct socket *sock, int protocol)
{
	ipx_socket *sk;
	sk=(ipx_socket *)sk_alloc(GFP_KERNEL);
	if(sk==NULL)
		return(-ENOMEM);
	switch(sock->type)
	{
		case SOCK_DGRAM:
			break;
		default:
			kfree_s((void *)sk,sizeof(*sk));
			return(-ESOCKTNOSUPPORT);
	}
	sk->rcvbuf=SK_RMEM_MAX;
	sk->sndbuf=SK_WMEM_MAX;
	sk->prot=NULL;	/* So we use default free mechanisms */
	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	sk->send_head=NULL;
	skb_queue_head_init(&sk->back_log);
	sk->state=TCP_CLOSE;
	sk->socket=sock;
	sk->type=sock->type;
	sk->mtu=IPX_MTU;
	sk->no_check = 1;		/* Checksum off by default */	
	if(sock!=NULL)
	{
		sock->data=(void *)sk;
		sk->sleep=sock->wait;
	}
	
	sk->state_change=def_callback1;
	sk->data_ready=def_callback2;
	sk->write_space=def_callback1;
	sk->error_report=def_callback1;

	sk->zapped=1;
	MOD_INC_USE_COUNT;
	return 0;
}

static int ipx_release(struct socket *sock, struct socket *peer)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	if(sk==NULL)
		return(0);
	if(!sk->dead)
		sk->state_change(sk);
	sk->dead=1;
	sock->data=NULL;
	ipx_destroy_socket(sk);
	return(0);
}

static int ipx_dup(struct socket *newsock,struct socket *oldsock)
{
	return(ipx_create(newsock,SOCK_DGRAM));
}

static unsigned short 
ipx_first_free_socketnum(ipx_interface *intrfc)
{
	unsigned short	socketNum = intrfc->if_sknum;

	if (socketNum < IPX_MIN_EPHEMERAL_SOCKET)
		socketNum = IPX_MIN_EPHEMERAL_SOCKET;

	while (ipxitf_find_socket(intrfc, ntohs(socketNum)) != NULL)
		if (socketNum > IPX_MAX_EPHEMERAL_SOCKET)
			socketNum = IPX_MIN_EPHEMERAL_SOCKET;
		else
			socketNum++;

	intrfc->if_sknum = socketNum;
	return	ntohs(socketNum);
}
	
static int ipx_bind(struct socket *sock, struct sockaddr *uaddr,int addr_len)
{
	ipx_socket *sk;
	ipx_interface *intrfc;
	struct sockaddr_ipx *addr=(struct sockaddr_ipx *)uaddr;
	
	sk=(ipx_socket *)sock->data;
	
	if(sk->zapped==0)
		return -EIO;
		
	if(addr_len!=sizeof(struct sockaddr_ipx))
		return -EINVAL;
	
	intrfc = ipxitf_find_using_net(addr->sipx_network);
	if (intrfc == NULL)
		return -EADDRNOTAVAIL;

	if (addr->sipx_port == 0) {
		addr->sipx_port = ipx_first_free_socketnum(intrfc);
		if (addr->sipx_port == 0)
			return -EINVAL;
	}

	if(ntohs(addr->sipx_port)<IPX_MIN_EPHEMERAL_SOCKET && !suser())
		return -EPERM;	/* protect IPX system stuff like routing/sap */

	sk->protinfo.af_ipx.port=addr->sipx_port;

#ifdef CONFIG_IPX_INTERN
	if (intrfc == ipx_internal_net)
	{
		/* The source address is to be set explicitly if the
		 * socket is to be bound on the internal network. If a
		 * node number 0 was specified, the default is used.
		 */

		if (memcmp(addr->sipx_node, ipx_broadcast_node,
			   IPX_NODE_LEN) == 0)
		{
			return -EINVAL;
		}
		if (memcmp(addr->sipx_node, ipx_this_node, IPX_NODE_LEN) == 0)
		{
			memcpy(sk->protinfo.af_ipx.node, intrfc->if_node,
			       IPX_NODE_LEN);
		}
		else
		{
			memcpy(sk->protinfo.af_ipx.node, addr->sipx_node, IPX_NODE_LEN);
		}
		if (ipxitf_find_internal_socket(intrfc, 
			sk->protinfo.af_ipx.node, 
			sk->protinfo.af_ipx.port) != NULL)
		{
			if(sk->debug)
				printk("IPX: bind failed because port %X in"
				       " use.\n", (int)addr->sipx_port);
			return -EADDRINUSE;
		}
	}
	else
	{
		/* Source addresses are easy. It must be our
		 * network:node pair for an interface routed to IPX
		 * with the ipx routing ioctl()
		 */

		memcpy(sk->protinfo.af_ipx.node, intrfc->if_node, 
			IPX_NODE_LEN);
		
		if(ipxitf_find_socket(intrfc, addr->sipx_port)!=NULL) {
			if(sk->debug)
				printk("IPX: bind failed because port %X in"
				       " use.\n", (int)addr->sipx_port);
			return -EADDRINUSE;	   
		}
	}

#else

	/* Source addresses are easy. It must be our network:node pair for
	   an interface routed to IPX with the ipx routing ioctl() */

	if(ipxitf_find_socket(intrfc, addr->sipx_port)!=NULL) {
		if(sk->debug)
			printk("IPX: bind failed because port %X in use.\n",
				(int)addr->sipx_port);
		return -EADDRINUSE;	   
	}

#endif

	ipxitf_insert_socket(intrfc, sk);
	sk->zapped=0;
	if(sk->debug)
		printk("IPX: socket is bound.\n");
	return 0;
}

static int ipx_connect(struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	struct sockaddr_ipx *addr;
	
	sk->state = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;

	if(addr_len!=sizeof(*addr))
		return(-EINVAL);
	addr=(struct sockaddr_ipx *)uaddr;
	
	if(sk->protinfo.af_ipx.port==0)
	/* put the autobinding in */
	{
		struct sockaddr_ipx uaddr;
		int ret;
	
		uaddr.sipx_port = 0;
		uaddr.sipx_network = 0L;
#ifdef CONFIG_IPX_INTERN
		memcpy(uaddr.sipx_node, sk->protinfo.af_ipx.intrfc->if_node,
		       IPX_NODE_LEN);
#endif
		ret = ipx_bind (sock, (struct sockaddr *)&uaddr,
				sizeof(struct sockaddr_ipx));
		if (ret != 0) return (ret);
	}
	
	if(ipxrtr_lookup(addr->sipx_network)==NULL)
		return -ENETUNREACH;
	sk->protinfo.af_ipx.dest_addr.net=addr->sipx_network;
	sk->protinfo.af_ipx.dest_addr.sock=addr->sipx_port;
	memcpy(sk->protinfo.af_ipx.dest_addr.node,
		addr->sipx_node,IPX_NODE_LEN);
	sk->protinfo.af_ipx.type=addr->sipx_type;
	sock->state = SS_CONNECTED;
	sk->state=TCP_ESTABLISHED;
	return 0;
}

static int ipx_socketpair(struct socket *sock1, struct socket *sock2)
{
	return(-EOPNOTSUPP);
}

static int ipx_accept(struct socket *sock, struct socket *newsock, int flags)
{
	if(newsock->data) {
		kfree_s(newsock->data,sizeof(ipx_socket));
		MOD_DEC_USE_COUNT;
	}
	return -EOPNOTSUPP;
}

static int ipx_getname(struct socket *sock, struct sockaddr *uaddr,
	int *uaddr_len, int peer)
{
	ipx_address *addr;
	struct sockaddr_ipx sipx;
	ipx_socket *sk;
	
	sk=(ipx_socket *)sock->data;
	
	*uaddr_len = sizeof(struct sockaddr_ipx);
		
	if(peer) {
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
		addr=&sk->protinfo.af_ipx.dest_addr;
		sipx.sipx_network = addr->net;
		memcpy(sipx.sipx_node,addr->node,IPX_NODE_LEN);
		sipx.sipx_port = addr->sock;
	} else {
		if (sk->protinfo.af_ipx.intrfc != NULL) {
			sipx.sipx_network = sk->protinfo.af_ipx.intrfc->if_netnum;
#ifdef CONFIG_IPX_INTERN
			memcpy(sipx.sipx_node, sk->protinfo.af_ipx.node, IPX_NODE_LEN);
#else
			memcpy(sipx.sipx_node, 
				sk->protinfo.af_ipx.intrfc->if_node, IPX_NODE_LEN);
#endif

		} else {
			sipx.sipx_network = 0L;
			memset(sipx.sipx_node, '\0', IPX_NODE_LEN);
		}
		sipx.sipx_port = sk->protinfo.af_ipx.port;
	}
		
	sipx.sipx_family = AF_IPX;
	sipx.sipx_type = sk->protinfo.af_ipx.type;
	memcpy(uaddr,&sipx,sizeof(sipx));
	return 0;
}

#if 0
/*
 * User to dump IPX packets (debugging)
 */
void dump_data(char *str,unsigned char *d, int len) {
  static char h2c[] = "0123456789ABCDEF";
  int l,i;
  char *p, b[64];
  for (l=0;len > 0 && l<16;l++) {
    p = b;
    for (i=0; i < 8 ; i++, --len) {
	  if (len > 0) {
	      *(p++) = h2c[(d[i] >> 4) & 0x0f];
	      *(p++) = h2c[d[i] & 0x0f];
	  }
	  else {
	      *(p++) = ' ';
	      *(p++) = ' ';
	  }
      *(p++) = ' ';
    }
    *(p++) = '-';
    *(p++) = ' ';
	len += 8;
    for (i=0; i < 8 ; i++, --len)
		if (len > 0)
			*(p++) = ' '<= d[i] && d[i]<'\177' ? d[i] : '.';
		else
			*(p++) = ' ';
    *p = '\000';
    d += i;
    printk("%s-%04X: %s\n",str,l*8,b);
  }
}

void dump_addr(char *str,ipx_address *p) {
  printk("%s: %08X:%02X%02X%02X%02X%02X%02X:%04X\n",
   str,ntohl(p->net),p->node[0],p->node[1],p->node[2],
   p->node[3],p->node[4],p->node[5],ntohs(p->sock));
}

void dump_hdr(char *str,ipx_packet *p) {
  printk("%s: CHKSUM=%04X SIZE=%d (%04X) HOPS=%d (%02X) TYPE=%02X\n",
   str,p->ipx_checksum,ntohs(p->ipx_pktsize),ntohs(p->ipx_pktsize),
   p->ipx_tctrl,p->ipx_tctrl,p->ipx_type);
  dump_addr("  IPX-DST",&p->ipx_dest);
  dump_addr("  IPX-SRC",&p->ipx_source);
}

void dump_pkt(char *str,ipx_packet *p) {
  int len = ntohs(p->ipx_pktsize);
  dump_hdr(str,p);
  if (len > 30)
	  dump_data(str,(unsigned char *)p + 30, len - 30);
}
#endif

int ipx_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	/* NULL here for pt means the packet was looped back */
	ipx_interface	*intrfc;
	ipx_packet *ipx;
	
	
	ipx=(ipx_packet *)skb->h.raw;
	
	/* Too small */
	
	if(ntohs(ipx->ipx_pktsize)<sizeof(ipx_packet)) {
		kfree_skb(skb,FREE_READ);
		return 0;
	}
	
	if(ipx->ipx_checksum!=IPX_NO_CHECKSUM) 
	{
		if(ipx_set_checksum(ipx, ntohs(ipx->ipx_pktsize))!=ipx->ipx_checksum)
		{
			kfree_skb(skb,FREE_READ);
			return 0;
		}
	}
	
	/* Determine what local ipx endpoint this is */
	intrfc = ipxitf_find_using_phys(dev, pt->type);
	if (intrfc == NULL) 
	{
		if (ipxcfg_auto_create_interfaces &&
		    ntohl(ipx->ipx_dest.net)!=0L) 
		{
			intrfc = ipxitf_auto_create(dev, pt->type);
		}

		if (intrfc == NULL) {
			/* Not one of ours */
			kfree_skb(skb,FREE_READ);
			return 0;
		}
	}

	return ipxitf_rcv(intrfc, skb);
}

static int ipx_sendmsg(struct socket *sock, struct msghdr *msg, int len, int noblock,
	int flags)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	struct sockaddr_ipx *usipx=(struct sockaddr_ipx *)msg->msg_name;
	struct sockaddr_ipx local_sipx;
	int retval;

	if (sk->zapped) 
		return -EIO; /* Socket not bound */
	if(flags) 
		return -EINVAL;
		
	if(usipx) 
	{
		if(sk->protinfo.af_ipx.port == 0) 
		{
			struct sockaddr_ipx uaddr;
			int ret;

			uaddr.sipx_port = 0;
			uaddr.sipx_network = 0L; 
#ifdef CONFIG_IPX_INTERN
			memcpy(uaddr.sipx_node, sk->protinfo.af_ipx.intrfc
				->if_node, IPX_NODE_LEN);
#endif
			ret = ipx_bind (sock, (struct sockaddr *)&uaddr,
					sizeof(struct sockaddr_ipx));
			if (ret != 0) return ret;
		}

		if(msg->msg_namelen <sizeof(*usipx))
			return -EINVAL;
		if(usipx->sipx_family != AF_IPX)
			return -EINVAL;
	}
	else 
	{
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
		usipx=&local_sipx;
		usipx->sipx_family=AF_IPX;
		usipx->sipx_type=sk->protinfo.af_ipx.type;
		usipx->sipx_port=sk->protinfo.af_ipx.dest_addr.sock;
		usipx->sipx_network=sk->protinfo.af_ipx.dest_addr.net;
		memcpy(usipx->sipx_node,sk->protinfo.af_ipx.dest_addr.node,IPX_NODE_LEN);
	}
	
	retval = ipxrtr_route_packet(sk, usipx, msg->msg_iov, len);
	if (retval < 0) 
		return retval;

	return len;
}


static int ipx_recvmsg(struct socket *sock, struct msghdr *msg, int size, int noblock,
		 int flags, int *addr_len)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	struct sockaddr_ipx *sipx=(struct sockaddr_ipx *)msg->msg_name;
	struct ipx_packet *ipx = NULL;
	int copied = 0;
	int truesize;
	struct sk_buff *skb;
	int er;
	
	if(sk->err)
		return sock_error(sk);
	
	if (sk->zapped)
		return -EIO;


	skb=skb_recv_datagram(sk,flags,noblock,&er);
	if(skb==NULL)
		return er;
	
	if(addr_len)
		*addr_len=sizeof(*sipx);

	ipx = (ipx_packet *)(skb->h.raw);
	truesize=ntohs(ipx->ipx_pktsize) - sizeof(ipx_packet);
	copied = (truesize > size) ? size : truesize;
	skb_copy_datagram_iovec(skb,sizeof(struct ipx_packet),msg->msg_iov,copied);
	
	if(sipx)
	{
		sipx->sipx_family=AF_IPX;
		sipx->sipx_port=ipx->ipx_source.sock;
		memcpy(sipx->sipx_node,ipx->ipx_source.node,IPX_NODE_LEN);
		sipx->sipx_network=ipx->ipx_source.net;
		sipx->sipx_type = ipx->ipx_type;
	}
	skb_free_datagram(sk, skb);
	return(truesize);
}		

static int ipx_shutdown(struct socket *sk,int how)
{
	return -EOPNOTSUPP;
}

static int ipx_select(struct socket *sock , int sel_type, select_table *wait)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	
	return datagram_select(sk,sel_type,wait);
}

static int ipx_ioctl(struct socket *sock,unsigned int cmd, unsigned long arg)
{
	int err;
	long amount=0;
	ipx_socket *sk=(ipx_socket *)sock->data;
	
	switch(cmd)
	{
		case TIOCOUTQ:
			err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(unsigned long));
			if(err)
				return err;
			amount=sk->sndbuf-sk->wmem_alloc;
			if(amount<0)
				amount=0;
			put_fs_long(amount,(unsigned long *)arg);
			return 0;
		case TIOCINQ:
		{
			struct sk_buff *skb;
			/* These two are safe on a single CPU system as only user tasks fiddle here */
			if((skb=skb_peek(&sk->receive_queue))!=NULL)
				amount=skb->len-sizeof(struct ipx_packet);
			err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(unsigned long));
			if(err)
				return err;
			put_fs_long(amount,(unsigned long *)arg);
			return 0;
		}
		case SIOCADDRT:
		case SIOCDELRT:
			if(!suser())
				return -EPERM;
			return(ipxrtr_ioctl(cmd,(void *)arg));
		case SIOCSIFADDR:
		case SIOCAIPXITFCRT:
		case SIOCAIPXPRISLT:
			if(!suser())
				return -EPERM;
		case SIOCGIFADDR:
			return(ipxitf_ioctl(cmd,(void *)arg));
		case SIOCIPXCFGDATA: 
		{
			err=verify_area(VERIFY_WRITE,(void *)arg,
				sizeof(ipx_config_data));
			if(err) return err;
			return(ipxcfg_get_config_data((void *)arg));
		}
		case SIOCGSTAMP:
			if (sk)
			{
				if(sk->stamp.tv_sec==0)
					return -ENOENT;
				err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(struct timeval));
				if(err)
					return err;
				memcpy_tofs((void *)arg,&sk->stamp,sizeof(struct timeval));
				return 0;
			}
			return -EINVAL;
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
			return -EINVAL;
		default:
			return(dev_ioctl(cmd,(void *) arg));
	}
	/*NOTREACHED*/
	return(0);
}

static struct proto_ops ipx_proto_ops = {
	AF_IPX,
	
	ipx_create,
	ipx_dup,
	ipx_release,
	ipx_bind,
	ipx_connect,
	ipx_socketpair,
	ipx_accept,
	ipx_getname,
	ipx_select,
	ipx_ioctl,
	ipx_listen,
	ipx_shutdown,
	ipx_setsockopt,
	ipx_getsockopt,
	ipx_fcntl,
	ipx_sendmsg,
	ipx_recvmsg
};

/* Called by protocol.c on kernel start up */

static struct packet_type ipx_8023_packet_type = 

{
	0,	/* MUTTER ntohs(ETH_P_8023),*/
	NULL,		/* All devices */
	ipx_rcv,
	NULL,
	NULL,
};

static struct packet_type ipx_dix_packet_type = 
{
	0,	/* MUTTER ntohs(ETH_P_IPX),*/
	NULL,		/* All devices */
	ipx_rcv,
	NULL,
	NULL,
};

static struct notifier_block ipx_dev_notifier={
	ipxitf_device_event,
	NULL,
	0
};


extern struct datalink_proto	*make_EII_client(void);
extern struct datalink_proto	*make_8023_client(void);
extern void	destroy_EII_client(struct datalink_proto *);
extern void	destroy_8023_client(struct datalink_proto *);

struct proc_dir_entry ipx_procinfo = {
	PROC_NET_IPX, 3, "ipx", S_IFREG | S_IRUGO,
	1, 0, 0, 0, &proc_net_inode_operations, ipx_get_info
};

struct proc_dir_entry ipx_if_procinfo = {
	PROC_NET_IPX_INTERFACE, 13, "ipx_interface", S_IFREG | S_IRUGO,
	1, 0, 0, 0, &proc_net_inode_operations, ipx_interface_get_info
};

struct proc_dir_entry ipx_rt_procinfo = {
	PROC_NET_IPX_ROUTE, 9, "ipx_route", S_IFREG | S_IRUGO,
	1, 0, 0, 0, &proc_net_inode_operations, ipx_rt_get_info
};

static unsigned char	ipx_8022_type = 0xE0;
static unsigned char	ipx_snap_id[5] =  { 0x0, 0x0, 0x0, 0x81, 0x37 };

void
ipx_proto_init(struct net_proto *pro)
{
	(void) sock_register(ipx_proto_ops.family, &ipx_proto_ops);

	pEII_datalink = make_EII_client();
	ipx_dix_packet_type.type=htons(ETH_P_IPX);
	dev_add_pack(&ipx_dix_packet_type);

	p8023_datalink = make_8023_client();
	ipx_8023_packet_type.type=htons(ETH_P_802_3);
	dev_add_pack(&ipx_8023_packet_type);
	
	if ((p8022_datalink = register_8022_client(ipx_8022_type, ipx_rcv)) == NULL)
		printk(KERN_CRIT "IPX: Unable to register with 802.2\n");

	if ((p8022tr_datalink = register_8022tr_client(ipx_8022_type, ipx_rcv)) == NULL)
		printk(KERN_CRIT "IPX: Unable to register with 802.2TR\n");
 
	if ((pSNAP_datalink = register_snap_client(ipx_snap_id, ipx_rcv)) == NULL)
		printk(KERN_CRIT "IPX: Unable to register with SNAP\n");
	
	register_netdevice_notifier(&ipx_dev_notifier);
#ifdef CONFIG_PROC_FS
	proc_net_register(&ipx_procinfo);
	proc_net_register(&ipx_if_procinfo);
	proc_net_register(&ipx_rt_procinfo);
#endif	
		
	printk(KERN_INFO "Swansea University Computer Society IPX 0.34 for NET3.035\n");
	printk(KERN_INFO "IPX Portions Copyright (c) 1995 Caldera, Inc.\n");
}

#ifdef MODULE
/* Note on MOD_{INC,DEC}_USE_COUNT:
 *
 * Use counts are incremented/decremented when
 * sockets are created/deleted.
 * 
 * Routes are always associated with an interface, and
 * allocs/frees will remain properly accounted for by
 * their associated interfaces.
 * 
 * Ergo, before the ipx module can be removed, all IPX
 * sockets be closed from user space. 
 */

static void
ipx_proto_finito(void)
{	ipx_interface	*ifc;

	while (ipx_interfaces) {
		ifc = ipx_interfaces;
		ipx_interfaces = ifc->if_next;
		ifc->if_next = NULL;
		ipxitf_down(ifc);
	}

#ifdef CONFIG_PROC_FS
	proc_net_unregister(PROC_NET_IPX_ROUTE);
	proc_net_unregister(PROC_NET_IPX_INTERFACE);
	proc_net_unregister(PROC_NET_IPX);
#endif	

	unregister_netdevice_notifier(&ipx_dev_notifier);

	unregister_snap_client(ipx_snap_id);
	pSNAP_datalink = NULL;

	unregister_8022tr_client(ipx_8022_type);
	p8022tr_datalink = NULL;

	unregister_8022_client(ipx_8022_type);
	p8022_datalink = NULL;

	dev_remove_pack(&ipx_8023_packet_type);
	destroy_8023_client(p8023_datalink);
	p8023_datalink = NULL;

	dev_remove_pack(&ipx_dix_packet_type);
	destroy_EII_client(pEII_datalink);
	pEII_datalink = NULL;

	(void) sock_unregister(ipx_proto_ops.family);

	return;
}

int init_module(void)
{
	ipx_proto_init(NULL);
	register_symtab(0);
	return 0;
}

void cleanup_module(void)
{
	ipx_proto_finito();
	return;
}
#endif /* def MODULE */
