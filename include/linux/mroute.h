#ifndef __LINUX_MROUTE_H
#define __LINUX_MROUTE_H
/*
 *	Based on the MROUTING 3.5 defines primarily to keep
 *	source compatibility with BSD.
 *
 *	See the mrouted code for the original history.
 *
 */

#define MRT_BASE	200
#define MRT_INIT	(MRT_BASE)	/* Activate the kernel mroute code 	*/
#define MRT_DONE	(MRT_BASE+1)	/* Shutdown the kernel mroute		*/
#define MRT_ADD_VIF	(MRT_BASE+2)	/* Add a virtual interface		*/
#define MRT_DEL_VIF	(MRT_BASE+3)	/* Delete a virtual interface		*/
#define MRT_ADD_MFC	(MRT_BASE+4)	/* Add a multicast forwarding entry	*/
#define MRT_DEL_MFC	(MRT_BASE+5)	/* Delete a multicast forwarding entry	*/
#define MRT_VERSION	(MRT_BASE+6)	/* Get the kernel multicast version	*/
#define MRT_ASSERT	(MRT_BASE+7)	/* Activate PIM assert mode		*/

#define SIOCGETVIFCNT	SIOCPROTOPRIVATE	/* IP protocol privates */
#define SIOCGETSGCNT	(SIOCPROTOPRIVATE+1)

#define MAXVIFS		32	
typedef unsigned long vifbitmap_t;	/* User mode code depends on this lot */
typedef unsigned short vifi_t;
#define ALL_VIFS	((vifi_t)(-1))

/*
 *	Same idea as select
 */
 
#define VIFM_SET(n,m)	((m)|=(1<<(n)))
#define VIFM_CLR(n,m)	((m)&=~(1<<(n)))
#define VIFM_ISSET(n,m)	((m)&(1<<(n)))
#define VIFM_CLRALL(m)	((m)=0)
#define VIFM_COPY(mfrom,mto)	((mto)=(mfrom))
#define VIFM_SAME(m1,m2)	((m1)==(m2))

/*
 *	Passed by mrouted for an MRT_ADD_VIF - again we use the
 *	mrouted 3.6 structures for compatibility
 */
 
struct vifctl {
	vifi_t	vifc_vifi;		/* Index of VIF */
	unsigned char vifc_flags;	/* VIFF_ flags */
	unsigned char vifc_threshold;	/* ttl limit */
	unsigned int vifc_rate_limit;	/* Rate limiter values (NI) */
	struct in_addr vifc_lcl_addr;	/* Our address */
	struct in_addr vifc_rmt_addr;	/* IPIP tunnel addr */
};

#define VIFF_TUNNEL	0x1		/* IPIP tunnel */
#define VIFF_SRCRT	0x02		/* NI */

/*
 *	Cache manipulation structures for mrouted
 */
 
struct mfcctl
{
	struct in_addr mfcc_origin;		/* Origin of mcast	*/
	struct in_addr mfcc_mcastgrp;		/* Group in question	*/
	vifi_t	mfcc_parent;			/* Where it arrived	*/
	unsigned char mfcc_ttls[MAXVIFS];	/* Where it is going	*/
};

/* 
 *	Group count retrieval for mrouted
 */
 
struct sioc_sg_req
{
	struct in_addr src;
	struct in_addr grp;
	unsigned long pktcnt;
	unsigned long bytecnt;
	unsigned long wrong_if;
};

/*
 *	To get vif packet counts
 */

struct sioc_vif_req
{
	vifi_t	vifi;		/* Which iface */
	unsigned long icount;	/* In packets */
	unsigned long ocount;	/* Out packets */
	unsigned long ibytes;	/* In bytes */
	unsigned long obytes;	/* Out bytes */
};

/*
 *	This is the format the mroute daemon expects to see IGMP control
 *	data. Magically happens to be like an IP packet as per the original
 */
 
struct igmpmsg
{
	unsigned long unused1,unused2;
	unsigned char im_msgtype;		/* What is this */
	unsigned char im_mbz;			/* Must be zero */
	unsigned char im_vif;			/* Interface (this ought to be a vifi_t!) */
	unsigned char unused3;
	struct in_addr im_src,im_dst;
};

/*
 *	Thats all usermode folks
 */

#ifdef __KERNEL__
extern struct sock *mroute_socket;
extern int ip_mroute_setsockopt(struct sock *, int, char *, int);
extern int ip_mroute_getsockopt(struct sock *, int, char *, int *);
extern int ipmr_ioctl(struct sock *sk, int cmd, unsigned long arg);
extern void mroute_close(struct sock *sk);


struct vif_device
{
	struct device *dev;		/* Device we are using */
	struct route *rt_cache;		/* Tunnel route cache */
	unsigned long bytes_in,bytes_out;
	unsigned long pkt_in,pkt_out;	/* Statistics */
	unsigned long rate_limit;	/* Traffic shaping (NI) */
	unsigned char threshold;	/* TTL threshold */
	unsigned short flags;		/* Control flags */
	unsigned long local,remote;	/* Addresses (remote for tunnels) */
};

#endif
#endif
