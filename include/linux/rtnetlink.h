#ifndef __LINUX_RTNETLINK_H
#define __LINUX_RTNETLINK_H

#include <linux/config.h>
#include <linux/netlink.h>

#define RTNL_DEBUG 1


/****
 *		Routing/neighbour discovery messages.
 ****/

/* Types of messages */

#define RTM_BASE	0x10

#define	RTM_NEWLINK	(RTM_BASE+0)
#define	RTM_DELLINK	(RTM_BASE+1)
#define	RTM_GETLINK	(RTM_BASE+2)

#define	RTM_NEWADDR	(RTM_BASE+4)
#define	RTM_DELADDR	(RTM_BASE+5)
#define	RTM_GETADDR	(RTM_BASE+6)

#define	RTM_NEWROUTE	(RTM_BASE+8)
#define	RTM_DELROUTE	(RTM_BASE+9)
#define	RTM_GETROUTE	(RTM_BASE+10)

#define	RTM_NEWNEIGH	(RTM_BASE+12)
#define	RTM_DELNEIGH	(RTM_BASE+13)
#define	RTM_GETNEIGH	(RTM_BASE+14)

#define	RTM_NEWRULE	(RTM_BASE+16)
#define	RTM_DELRULE	(RTM_BASE+17)
#define	RTM_GETRULE	(RTM_BASE+18)

#define	RTM_MAX		(RTM_BASE+19)


/* Generic structure for encapsulation optional route
   information. It is reminiscent of sockaddr, but with sa_family
   replaced with attribute type.
   It would be good, if constructions of sort:
   struct something {
     struct rtattr rta;
     struct a_content a;
   }
   had correct alignment. It is true for x86, but I have no idea
   how to make it on 64bit architectures. Please, teach me. --ANK
 */

struct rtattr
{
	unsigned short	rta_len;
	unsigned short	rta_type;
/*
	unsigned char	rta_data[0];
 */
};

enum rtattr_type_t
{
	RTA_UNSPEC,
	RTA_DST,
	RTA_SRC,
	RTA_IIF,
	RTA_OIF,
	RTA_GATEWAY,
	RTA_PRIORITY,
	RTA_PREFSRC,
	RTA_WINDOW,
	RTA_RTT,
	RTA_MTU,
	RTA_IFNAME
};

#define RTA_MAX RTA_IFNAME

/* Macros to handle rtattributes */

#define RTA_ALIGNTO	4
#define RTA_ALIGN(len) ( ((len)+RTA_ALIGNTO-1) & ~(RTA_ALIGNTO-1) )
#define RTA_OK(rta,len) ((rta)->rta_len > sizeof(struct rtattr) && \
			 (rta)->rta_len <= (len))
#define RTA_NEXT(rta,attrlen)	((attrlen) -= RTA_ALIGN((rta)->rta_len), \
				 (struct rtattr*)(((char*)(rta)) + RTA_ALIGN((rta)->rta_len)))
#define RTA_LENGTH(len)	(RTA_ALIGN(sizeof(struct rtattr)) + (len))
#define RTA_SPACE(len)	RTA_ALIGN(RTA_LENGTH(len))
#define RTA_DATA(rta)   ((void*)(((char*)(rta)) + RTA_LENGTH(0)))


/*
 * "struct rtnexthop" describres all necessary nexthop information,
 * i.e. parameters of path to a destination via this nextop.
 *
 * At the moment it is impossible to set different prefsrc, mtu, window
 * and rtt for different paths from multipath.
 */

struct rtnexthop
{
	unsigned short		rtnh_len;
	unsigned char		rtnh_flags;
	unsigned char		rtnh_hops;
	int			rtnh_ifindex;
/*
	struct rtattr		rtnh_data[0];
 */
};

/* rtnh_flags */

#define RTNH_F_DEAD		1	/* Nexthop is dead (used by multipath)	*/
#define RTNH_F_PERVASIVE	2	/* Do recursive gateway lookup	*/
#define RTNH_F_ONLINK		4	/* Gateway is forced on link	*/

/* Macros to handle hexthops */

#define RTNH_ALIGNTO	4
#define RTNH_ALIGN(len) ( ((len)+RTNH_ALIGNTO-1) & ~(RTNH_ALIGNTO-1) )
#define RTNH_OK(rtnh,len) ((rtnh)->rtnh_len >= sizeof(struct rtnexthop) && \
			   (rtnh)->rtnh_len <= (len))
#define RTNH_NEXT(rtnh)	((struct rtnexthop*)(((char*)(rtnh)) + RTNH_ALIGN((rtnh)->rtnh_len)))
#define RTNH_LENGTH(len) (RTNH_ALIGN(sizeof(struct rtnexthop)) + (len))
#define RTNH_SPACE(len)	RTNH_ALIGN(RTNH_LENGTH(len))
#define RTNH_DATA(rtnh)   ((struct rtattr*)(((char*)(rtnh)) + RTNH_LENGTH(0)))


struct rtmsg
{
	unsigned char		rtm_family;
	unsigned char		rtm_dst_len;
	unsigned char		rtm_src_len;
	unsigned char		rtm_tos;
	unsigned char		rtm_table;	/* Routing table id */
	unsigned char		rtm_protocol;	/* Routing protocol; see below	*/
	unsigned char		rtm_nhs;	/* Number of nexthops */
	unsigned char		rtm_type;	/* See below	*/
	unsigned short		rtm_optlen;	/* Byte length of rtm_opt */
	unsigned char		rtm_scope;	/* See below */	
	unsigned char		rtm_whatsit;	/* Unused byte */
	unsigned		rtm_flags;
/*
	struct rtattr		rtm_opt[0];
	struct rtnexthop	rtm_nh[0];
 */
};

#define RTM_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct rtmsg))))
#define RTM_RTNH(r) ((struct rtnexthop*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct rtmsg)) \
					   + NLMSG_ALIGN((r)->rtm_optlen)))
#define RTM_NHLEN(nlh,r) ((nlh)->nlmsg_len - NLMSG_SPACE(sizeof(struct rtmsg)) - NLMSG_ALIGN((r)->rtm_optlen))

/* rtm_type */

enum
{
	RTN_UNSPEC,
	RTN_UNICAST,		/* Gateway or direct route	*/
	RTN_LOCAL,		/* Accept locally		*/
	RTN_BROADCAST,		/* Accept locally as broadcast,
				   send as broadcast */
	RTN_ANYCAST,		/* Accept locally as broadcast,
				   but send as unicast */
	RTN_MULTICAST,		/* Multicast route		*/
	RTN_BLACKHOLE,		/* Drop				*/
	RTN_UNREACHABLE,	/* Destination is unreachable   */
	RTN_PROHIBIT,		/* Administratively prohibited	*/
	RTN_THROW,		/* Not in this table		*/
	RTN_NAT,		/* Translate this address	*/
	RTN_XRESOLVE,		/* Use external resolver	*/
};

#define RTN_MAX RTN_XRESOLVE

/* rtm_protocol */

#define RTPROT_UNSPEC	0
#define RTPROT_REDIRECT	1	/* Route installed by ICMP redirects;
				   not used by current IPv4 */
#define RTPROT_KERNEL	2	/* Route installed by kernel		*/
#define RTPROT_BOOT	3	/* Route installed during boot		*/
#define RTPROT_STATIC	4	/* Route installed by administrator	*/

/* Values of protocol >= RTPROT_STATIC are not interpreted by kernel;
   they just passed from user and back as is.
   It will be used by hypothetical multiple routing daemons.
   Note that protocol values should be standardized in order to
   avoid conflicts.
 */

#define RTPROT_GATED	8	/* Apparently, GateD */
#define RTPROT_RA	9	/* RDISC router advertisment */


/* rtm_scope

   Really it is not scope, but sort of distance to the destination.
   NOWHERE are reserved for not existing destinations, HOST is our
   local addresses, LINK are destinations, locate on directly attached
   link and UNIVERSE is everywhere in the Universe :-)

   Intermediate values are also possible f.e. interior routes
   could be assigned a value between UNIVERSE and LINK.
*/

enum rt_scope_t
{
	RT_SCOPE_UNIVERSE=0,
/* User defined values f.e. "site" */
	RT_SCOPE_LINK=253,
	RT_SCOPE_HOST=254,
	RT_SCOPE_NOWHERE=255
};

/* rtm_flags */

#define RTM_F_NOTIFY		0x100	/* Notify user of route change	*/
#define RTM_F_CLONED		0x200	/* This route is cloned		*/
#define RTM_F_NOPMTUDISC	0x400	/* Do not make PMTU discovery	*/
#define RTM_F_EQUALIZE		0x800	/* Multipath equalizer: NI	*/

/* Reserved table identifiers */

enum rt_class_t
{
	RT_TABLE_UNSPEC=0,
/* User defined values */
	RT_TABLE_DEFAULT=253,
	RT_TABLE_MAIN=254,
	RT_TABLE_LOCAL=255
};
#define RT_TABLE_MAX RT_TABLE_LOCAL


/*********************************************************
 *		Interface address.
 ****/

struct ifaddrmsg
{
	unsigned char	ifa_family;
	unsigned char	ifa_prefixlen;	/* The prefix length		*/
	unsigned char	ifa_flags;	/* Flags			*/
	unsigned char	ifa_scope;	/* See above			*/
	int		ifa_index;	/* Link index			*/
/*
	struct rtattr	ifa_data[0];
 */
};

enum
{
	IFA_UNSPEC,
	IFA_ADDRESS,
	IFA_LOCAL,
	IFA_LABEL,
	IFA_BROADCAST,
	IFA_ANYCAST
};

#define IFA_MAX IFA_ANYCAST

/* ifa_flags */

#define IFA_F_SECONDARY	1


#define IFA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ifaddrmsg))))

/*
   Important comment:
   IFA_ADDRESS is prefix address, rather than local interface address.
   It makes no difference for normally configured broadcast interfaces,
   but for point-to-point IFA_ADDRESS is DESTINATION address,
   local address is supplied in IFA_LOCAL attribute.
 */

/**************************************************************
 *		Neighbour discovery.
 ****/

struct ndmsg
{
	unsigned char	nd_family;
	int		nd_ifindex;	/* Link index			*/
	unsigned	nd_flags;
/*
	struct rtattr	nd_data[0];
 */
};

enum
{
	NDA_UNSPEC,
	NDA_DST,
	NDA_LLADDR,
};

#define NDA_MAX NDA_LLADDR

#define NDA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ndmsg))))

/****
 *		General form of address family dependent message.
 ****/

struct rtgenmsg
{
	unsigned char		rtgen_family;
};

/*****************************************************************
 *		Link layer specific messages.
 ****/

/* struct ifinfomsg
 * passes link level specific information, not dependent
 * on network protocol.
 */

struct ifinfomsg
{
	unsigned char	ifi_family;		/* Dummy	*/
	unsigned char	ifi_addrlen;		/* Length of HW address */
	unsigned short	ifi_pad__;
	int		ifi_index;		/* Link index	*/
	int		ifi_link;		/* Physical device */
	char		ifi_name[IFNAMSIZ];
	struct sockaddr	ifi_address;		/* HW address	*/
	struct sockaddr	ifi_broadcast;		/* HW broadcast	*/
	unsigned	ifi_flags;		/* IFF_* flags	*/
	int		ifi_mtu;		/* Link mtu	*/
	char		ifi_qdiscname[IFNAMSIZ];/* Id of packet scheduler */
	int		ifi_qdisc;		/* Packet scheduler handle */
};

/* ifi_flags.

   IFF_* flags.

   The only change is:
   IFF_LOOPBACK, IFF_BROADCAST and IFF_POINTOPOINT are
   more not changeable by user. They describe link media
   characteristics and set by device driver.

   Comments:
   - Combination IFF_BROADCAST|IFF_POINTOPOINT is invalid
   - If neiher of these three flags are set;
     the interface is NBMA.

   - IFF_MULTICAST does not mean anything special:
   multicasts can be used on all not-NBMA links.
   IFF_MULTICAST means that this media uses special encapsulation
   for multicast frames. Apparently, all IFF_POINTOPOINT and
   IFF_BROADCAST devices are able to use multicasts too.
 */

/* ifi_link.
   For usual devices it is equal ifi_index.
   If it is a "virtual interface" (f.e. tunnel), ifi_link
   can point to real physical interface (f.e. for bandwidth calculations),
   or maybe 0, what means, that real media is unknown (usual
   for IPIP tunnels, when route to endpoint is allowed to change)
 */

#define RTMGRP_LINK		1
#define RTMGRP_NOTIFY		2

#define RTMGRP_IPV4_IFADDR	0x10
#define RTMGRP_IPV4_NDISC	0x20
#define RTMGRP_IPV4_ROUTE	0x40
#define RTMGRP_IPV4_MROUTE	0x80

#define RTMGRP_IPV6_IFADDR	0x100
#define RTMGRP_IPV6_NDISC	0x200
#define RTMGRP_IPV6_ROUTE	0x400
#define RTMGRP_IPV6_MROUTE	0x800


#ifdef __KERNEL__

struct kern_rta
{
	void		*rta_dst;
	void		*rta_src;
	int		*rta_iif;
	int		*rta_oif;
	void		*rta_gw;
	u32		*rta_priority;
	void		*rta_prefsrc;
	unsigned	*rta_window;
	unsigned	*rta_rtt;
	unsigned	*rta_mtu;
	unsigned char	*rta_ifname;
};

struct kern_ifa
{
	void		*ifa_address;
	void		*ifa_local;
	unsigned char	*ifa_label;
	void		*ifa_broadcast;
	void		*ifa_anycast;
};


extern atomic_t rtnl_rlockct;
extern struct wait_queue *rtnl_wait;

#ifdef CONFIG_RTNETLINK
extern struct sock *rtnl;

struct rtnetlink_link
{
	int (*doit)(struct sk_buff *, struct nlmsghdr*, void *attr);
	int (*dumpit)(struct sk_buff *, struct netlink_callback *cb);
};

extern struct rtnetlink_link * rtnetlink_links[NPROTO];
extern int rtnetlink_dump_ifinfo(struct sk_buff *skb, struct netlink_callback *cb);


extern void __rta_fill(struct sk_buff *skb, int attrtype, int attrlen, const void *data);

#define RTA_PUT(skb, attrtype, attrlen, data) \
({ if (skb_tailroom(skb) < RTA_SPACE(attrlen)) goto rtattr_failure; \
   __rta_fill(skb, attrtype, attrlen, data); })

extern unsigned long rtnl_wlockct;

/* NOTE: these locks are not interrupt safe, are not SMP safe,
 * they are even not atomic. 8)8)8) ... and it is not a bug.
 * Really, if these locks will be programmed correctly,
 * all the addressing/routing machine would become SMP safe,
 * but is absolutely useless at the moment, because all the kernel
 * is not reenterable in any case. --ANK
 *
 * Well, atomic_* and set_bit provide the only thing here:
 * gcc is confused not to overoptimize them, that's all.
 * I remember as gcc splitted ++ operation, but cannot reproduce
 * it with gcc-2.7.*. --ANK
 *
 * One more note: rwlock facility should be written and put
 * to a kernel wide location: f.e. current implementation of semaphores
 * (especially, for x86) looks like a wonder. It would be good
 * to have something similar for rwlock. Recursive lock could be also
 * useful thing. --ANK
 */

extern __inline__ int rtnl_shlock_nowait(void)
{
	atomic_inc(&rtnl_rlockct);
	if (test_bit(0, &rtnl_wlockct)) {
		atomic_dec(&rtnl_rlockct);
		return -EAGAIN;
	}
	return 0;
}

extern __inline__ void rtnl_shlock(void)
{
	while (rtnl_shlock_nowait())
		sleep_on(&rtnl_wait);
}

/* Check for possibility to PROMOTE shared lock to exclusive.
   Shared lock must be already grabbed with rtnl_shlock*().
 */

extern __inline__ int rtnl_exlock_nowait(void)
{
	if (atomic_read(&rtnl_rlockct) > 1)
		return -EAGAIN;
	if (test_and_set_bit(0, &rtnl_wlockct))
		return -EAGAIN;
	return 0;
}

extern __inline__ void rtnl_exlock(void)
{
	while (rtnl_exlock_nowait())
		sleep_on(&rtnl_wait);
}

#if 0
extern __inline__ void rtnl_shunlock(void)
{
	atomic_dec(&rtnl_rlockct);
	if (atomic_read(&rtnl_rlockct) <= 1) {
		wake_up(&rtnl_wait);
		if (rtnl->receive_queue.qlen)
			rtnl->data_ready(rtnl, 0);
	}
}
#else

/* The problem: inline requires to include <net/sock.h> and, hence,
   almost all of net includes :-(
 */

#define rtnl_shunlock() ({ \
	atomic_dec(&rtnl_rlockct); \
	if (atomic_read(&rtnl_rlockct) <= 1) { \
		wake_up(&rtnl_wait); \
		if (rtnl->receive_queue.qlen) \
			rtnl->data_ready(rtnl, 0); \
	} \
})
#endif

/* Release exclusive lock. Note, that we do not wake up rtnetlink socket,
 * it will be done later after releasing shared lock.
 */

extern __inline__ void rtnl_exunlock(void)
{
	clear_bit(0, &rtnl_wlockct);
	wake_up(&rtnl_wait);
}

#else

extern __inline__ void rtnl_shlock(void)
{
	while (atomic_read(&rtnl_rlockct))
		sleep_on(&rtnl_wait);
	atomic_inc(&rtnl_rlockct);
}

extern __inline__ void rtnl_shunlock(void)
{
	if (atomic_dec_and_test(&rtnl_rlockct))
		wake_up(&rtnl_wait);
}

extern __inline__ void rtnl_exlock(void)
{
}

extern __inline__ void rtnl_exunlock(void)
{
}

#endif

extern void rtnl_lock(void);
extern void rtnl_unlock(void);
extern void rtnetlink_init(void);

#endif /* __KERNEL__ */


#endif	/* __LINUX_RTNETLINK_H */
