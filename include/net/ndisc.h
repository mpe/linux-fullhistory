#ifndef _NDISC_H
#define _NDISC_H


/*
 *	Neighbor Cache Entry States (7.3.2.)
 */

/*
 *	The lsb is set for states that have a timer associated
 */

#define NUD_NONE	0x00
#define NUD_INCOMPLETE	0x11
#define NUD_REACHABLE	0x20
#define NUD_STALE	0x30
#define NUD_DELAY	0x41
#define NUD_PROBE	0x51
#define NUD_FAILED	0x60	/* neighbour discovery failed	*/

#define NUD_IN_TIMER	0x01

#define NDISC_QUEUE_LEN	3

#define NCF_NOARP		0x01	/* no ARP needed on this device */
#define NCF_SUBNET		0x02    /* NC entry for subnet		*/
#define NCF_INVALID		0x04
#define NCF_DELAY_EXPIRED	0x08	/* time to move to PROBE	*/
#define NCF_ROUTER		0x10	/* neighbour is a router	*/
#define NCF_HHVALID		0x20	/* Hardware header is valid	*/

/*
 *	ICMP codes for neighbour discovery messages
 */

#define NDISC_ROUTER_SOLICITATION	133
#define NDISC_ROUTER_ADVERTISEMENT	134
#define NDISC_NEIGHBOUR_SOLICITATION	135
#define NDISC_NEIGHBOUR_ADVERTISEMENT	136
#define NDISC_REDIRECT			137

/*
 *	ndisc options
 */

#define ND_OPT_SOURCE_LL_ADDR		1
#define ND_OPT_TARGET_LL_ADDR		2
#define ND_OPT_PREFIX_INFO		3
#define ND_OPT_REDIRECT_HDR		4
#define ND_OPT_MTU			5

#define MAX_RTR_SOLICITATION_DELAY	HZ

#define RECHABLE_TIME			(30*HZ)
#define RETRANS_TIMER			HZ

#define MIN_RANDOM_FACTOR		(1/2)
#define MAX_RANDOM_FACTOR		(3/2)

#define REACH_RANDOM_INTERVAL		(60*60*HZ)	/* 1 hour */

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/icmpv6.h>
#include <asm/atomic.h>

/*
 *	neighbour cache entry
 *	used by neighbour discovery module
 *	as similar functions of "struct hh_cache" used in ipv4
 */
struct neighbour {
	struct in6_addr		addr;		/* next hop addr */
	__u8			len;		/* prefix len	 */
	__u8			type;		/* {unicast, multicast} */
			
	struct device *		dev;

	__u8			flags;

	
	__u8			hh_data[MAX_ADDR_LEN];	/* cached hdr	*/
	__u8			*h_dest;		/* dest addr	*/

	struct sk_buff_head	arp_queue;	/* packets waiting for ND to
						   finish */
	atomic_t		refcnt;
	__u8			nud_state;
	__u8			probes;
	__u32			tstamp;		/* last reachable conf	*/

	unsigned long		expires;	/* timer expires at	*/

	struct neighbour	*next; 		/* for hash chaining	*/
	struct neighbour	*prev; 		/* for hash chaining	*/
};

struct nd_msg {
        struct icmpv6hdr icmph;
        struct in6_addr target;
        struct {
                __u8	opt_type;
                __u8	opt_len;
                __u8	link_addr[MAX_ADDR_LEN];
        } opt;
};

struct ra_msg {
        struct icmpv6hdr	icmph;
	__u32			reachable_time;
	__u32			retrans_timer;
};

struct ndisc_statistics {
	__u32	allocs;			/* allocated entries		*/
	__u32	free_delayed;		/* zombie entries		*/
	__u32	snt_probes_ucast;	/* ns probes sent (ucast)	*/
	__u32	snt_probes_mcast;	/* ns probes sent (mcast)	*/
	__u32	rcv_probes_ucast;	/* ns probes rcv  (ucast)	*/
	__u32	rcv_probes_mcast;	/* ns probes rcv  (mcast)	*/
	__u32	rcv_upper_conf;		/* confirmations from upper layers */
	__u32	res_failed;		/* address resolution failures	*/
};

extern struct neighbour *	ndisc_get_neigh(struct device *dev, 
						struct in6_addr *addr);

extern void			ndisc_validate(struct neighbour *neigh);

extern void			ndisc_init(struct proto_ops *ops);
extern void			ndisc_cleanup(void);

extern int			ndisc_eth_resolv(unsigned char *,
						 struct device *,
						 struct sk_buff *);

extern int			ndisc_rcv(struct sk_buff *skb,
					  struct device *dev,
					  struct in6_addr *saddr,
					  struct in6_addr *daddr,
					  struct ipv6_options *opt,
					  unsigned short len);

extern void			ndisc_event_send(struct neighbour *neigh,
						 struct sk_buff *skb);

extern void			ndisc_send_ns(struct device *dev,
					      struct neighbour *neigh,
					      struct in6_addr *solicit,
					      struct in6_addr *daddr,
					      struct in6_addr *saddr);

extern void			ndisc_send_rs(struct device *dev,
					      struct in6_addr *saddr,
					      struct in6_addr *daddr);

extern int			(*ndisc_eth_hook) (unsigned char *,
						   struct device *,
						   struct sk_buff *);

extern void			ndisc_forwarding_on(void);
extern void			ndisc_forwarding_off(void);

extern void			ndisc_send_redirect(struct sk_buff *skb,
						    struct neighbour *neigh,
						    struct in6_addr *target);

struct rt6_info *		dflt_rt_lookup(void);

extern unsigned long	nd_rand_seed;
extern int		ipv6_random(void);


static __inline__ void ndisc_dec_neigh(struct neighbour *neigh)
{
	atomic_dec(&neigh->refcnt);
}

#endif /* __KERNEL__ */


#endif
