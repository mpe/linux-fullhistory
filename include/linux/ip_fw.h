/*
 *	IP firewalling code. This is taken from 4.4BSD. Please note the 
 *	copyright message below. As per the GPL it must be maintained
 *	and the licenses thus do not conflict. While this port is subject
 *	to the GPL I also place my modifications under the original 
 *	license in recognition of the original copyright. 
 *
 *	Ported from BSD to Linux,
 *		Alan Cox 22/Nov/1994.
 *	Merged and included the FreeBSD-Current changes at Ugen's request
 *	(but hey it's a lot cleaner now). Ugen would prefer in some ways
 *	we waited for his final product but since Linux 1.2.0 is about to
 *	appear it's not practical - Read: It works, it's not clean but please
 *	don't consider it to be his standard of finished work.
 *		Alan.
 *
 * Fixes:
 *	Pauline Middelink	:	Added masquerading.
 *	Jos Vos			:	Separate input  and output firewall
 *					chains, new "insert" and "append"
 *					commands to replace "add" commands,
 *					add ICMP header to struct ip_fwpkt.
 *	Jos Vos			:	Add support for matching device names.
 *
 *	All the real work was done by .....
 */

/*
 * Copyright (c) 1993 Daniel Boulet
 * Copyright (c) 1994 Ugen J.S.Antsilevich
 *
 * Redistribution and use in source forms, with and without modification,
 * are permitted provided that this entire comment appears intact.
 *
 * Redistribution in binary form may occur without any restrictions.
 * Obviously, it would be nice if you gave credit where credit is due
 * but requiring it would be too onerous.
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 */

/*
 * 	Format of an IP firewall descriptor
 *
 * 	src, dst, src_mask, dst_mask are always stored in network byte order.
 * 	flags and num_*_ports are stored in host byte order (of course).
 * 	Port numbers are stored in HOST byte order.
 */
 
#ifndef _IP_FW_H
#define _IP_FW_H

struct ip_fw 
{
	struct ip_fw  *fw_next;			/* Next firewall on chain */
	struct in_addr fw_src, fw_dst;		/* Source and destination IP addr */
	struct in_addr fw_smsk, fw_dmsk;	/* Mask for src and dest IP addr */
	struct in_addr fw_via;			/* IP address of interface "via" */
	struct device *fw_viadev;		/* device of interface "via" */
	unsigned short fw_flg;			/* Flags word */
	unsigned short fw_nsp, fw_ndp;          /* N'of src ports and # of dst ports */
						/* in ports array (dst ports follow */
    						/* src ports; max of 10 ports in all; */
    						/* count of 0 means match all ports) */
#define IP_FW_MAX_PORTS	10      		/* A reasonable maximum */
	unsigned short fw_pts[IP_FW_MAX_PORTS]; /* Array of port numbers to match */
	unsigned long  fw_pcnt,fw_bcnt;		/* Packet and byte counters */
	unsigned char  fw_tosand, fw_tosxor;	/* Revised packet priority */
	char           fw_vianame[IFNAMSIZ];	/* name of interface "via" */
};

/*
 *	Values for "flags" field .
 */

#define IP_FW_F_ALL	0x000	/* This is a universal packet firewall*/
#define IP_FW_F_TCP	0x001	/* This is a TCP packet firewall      */
#define IP_FW_F_UDP	0x002	/* This is a UDP packet firewall      */
#define IP_FW_F_ICMP	0x003	/* This is a ICMP packet firewall     */
#define IP_FW_F_KIND	0x003	/* Mask to isolate firewall kind      */
#define IP_FW_F_ACCEPT	0x004	/* This is an accept firewall (as     *
				 *         opposed to a deny firewall)*
				 *                                    */
#define IP_FW_F_SRNG	0x008	/* The first two src ports are a min  *
				 * and max range (stored in host byte *
				 * order).                            *
				 *                                    */
#define IP_FW_F_DRNG	0x010	/* The first two dst ports are a min  *
				 * and max range (stored in host byte *
				 * order).                            *
				 * (ports[0] <= port <= ports[1])     *
				 *                                    */
#define IP_FW_F_PRN	0x020	/* In verbose mode print this firewall*/
#define IP_FW_F_BIDIR	0x040	/* For bidirectional firewalls        */
#define IP_FW_F_TCPSYN	0x080	/* For tcp packets-check SYN only     */
#define IP_FW_F_ICMPRPL 0x100	/* Send back icmp unreachable packet  */
#define IP_FW_F_MASQ	0x200	/* Masquerading			      */
#define IP_FW_F_TCPACK	0x400	/* For tcp-packets match if ACK is set*/

#define IP_FW_F_MASK	0x7FF	/* All possible flag bits mask        */

/*    
 *	New IP firewall options for [gs]etsockopt at the RAW IP level.
 *	Unlike BSD Linux inherits IP options so you don't have to use
 *	a raw socket for this. Instead we check rights in the calls.
 */     

#define IP_FW_BASE_CTL  	64	/* base for firewall socket options */

#define IP_FW_COMMAND		0x00FF	/* mask for command without chain */
#define IP_FW_TYPE		0x0300	/* mask for type (chain) */
#define IP_FW_SHIFT		8	/* shift count for type (chain) */

#define IP_FW_FWD		0
#define IP_FW_IN		1
#define IP_FW_OUT		2
#define IP_FW_ACCT		3
#define IP_FW_CHAINS		4	/* total number of ip_fw chains */

#define IP_FW_INSERT		(IP_FW_BASE_CTL)
#define IP_FW_APPEND		(IP_FW_BASE_CTL+1)
#define IP_FW_DELETE		(IP_FW_BASE_CTL+2)
#define IP_FW_FLUSH		(IP_FW_BASE_CTL+3)
#define IP_FW_ZERO		(IP_FW_BASE_CTL+4)
#define IP_FW_POLICY		(IP_FW_BASE_CTL+5)
#define IP_FW_CHECK		(IP_FW_BASE_CTL+6)

#define IP_FW_INSERT_FWD	(IP_FW_INSERT | (IP_FW_FWD << IP_FW_SHIFT))
#define IP_FW_APPEND_FWD	(IP_FW_APPEND | (IP_FW_FWD << IP_FW_SHIFT))
#define IP_FW_DELETE_FWD	(IP_FW_DELETE | (IP_FW_FWD << IP_FW_SHIFT))
#define IP_FW_FLUSH_FWD		(IP_FW_FLUSH  | (IP_FW_FWD << IP_FW_SHIFT))
#define IP_FW_ZERO_FWD		(IP_FW_ZERO   | (IP_FW_FWD << IP_FW_SHIFT))
#define IP_FW_POLICY_FWD	(IP_FW_POLICY | (IP_FW_FWD << IP_FW_SHIFT))
#define IP_FW_CHECK_FWD		(IP_FW_CHECK  | (IP_FW_FWD << IP_FW_SHIFT))

#define IP_FW_INSERT_IN		(IP_FW_INSERT | (IP_FW_IN << IP_FW_SHIFT))
#define IP_FW_APPEND_IN		(IP_FW_APPEND | (IP_FW_IN << IP_FW_SHIFT))
#define IP_FW_DELETE_IN		(IP_FW_DELETE | (IP_FW_IN << IP_FW_SHIFT))
#define IP_FW_FLUSH_IN		(IP_FW_FLUSH  | (IP_FW_IN << IP_FW_SHIFT))
#define IP_FW_ZERO_IN		(IP_FW_ZERO   | (IP_FW_IN << IP_FW_SHIFT))
#define IP_FW_POLICY_IN		(IP_FW_POLICY | (IP_FW_IN << IP_FW_SHIFT))
#define IP_FW_CHECK_IN		(IP_FW_CHECK  | (IP_FW_IN << IP_FW_SHIFT))

#define IP_FW_INSERT_OUT	(IP_FW_INSERT | (IP_FW_OUT << IP_FW_SHIFT))
#define IP_FW_APPEND_OUT	(IP_FW_APPEND | (IP_FW_OUT << IP_FW_SHIFT))
#define IP_FW_DELETE_OUT	(IP_FW_DELETE | (IP_FW_OUT << IP_FW_SHIFT))
#define IP_FW_FLUSH_OUT		(IP_FW_FLUSH  | (IP_FW_OUT << IP_FW_SHIFT))
#define IP_FW_ZERO_OUT		(IP_FW_ZERO   | (IP_FW_OUT << IP_FW_SHIFT))
#define IP_FW_POLICY_OUT	(IP_FW_POLICY | (IP_FW_OUT << IP_FW_SHIFT))
#define IP_FW_CHECK_OUT		(IP_FW_CHECK  | (IP_FW_OUT << IP_FW_SHIFT))

#define IP_ACCT_INSERT		(IP_FW_INSERT | (IP_FW_ACCT << IP_FW_SHIFT))
#define IP_ACCT_APPEND		(IP_FW_APPEND | (IP_FW_ACCT << IP_FW_SHIFT))
#define IP_ACCT_DELETE		(IP_FW_DELETE | (IP_FW_ACCT << IP_FW_SHIFT))
#define IP_ACCT_FLUSH		(IP_FW_FLUSH  | (IP_FW_ACCT << IP_FW_SHIFT))
#define IP_ACCT_ZERO		(IP_FW_ZERO   | (IP_FW_ACCT << IP_FW_SHIFT))

struct ip_fwpkt
{
	struct iphdr fwp_iph;			/* IP header */
	union {
		struct tcphdr fwp_tcph;		/* TCP header or */
		struct udphdr fwp_udph;		/* UDP header */
		struct icmphdr fwp_icmph;	/* ICMP header */
	} fwp_protoh;
	struct in_addr fwp_via;			/* interface address */
	char           fwp_vianame[IFNAMSIZ];	/* interface name */
};

/*
 *	Main firewall chains definitions and global var's definitions.
 */

#ifdef __KERNEL__

#include <linux/config.h>

#ifdef CONFIG_IP_MASQUERADE
struct ip_masq {
	struct ip_masq	*next;		/* next member in list */
	struct timer_list timer;	/* Expiration timer */
	__u16 		protocol;	/* Which protocol are we talking? */
	__u32 		src, dst;	/* Source and destination IP addresses */
	__u16		sport,dport;	/* Source and destination ports */
	__u16		mport;		/* Masquaraded port */
	__u32		init_seq;	/* Add delta from this seq. on */
	short		delta;		/* Delta in sequence numbers */
	short		previous_delta;	/* Delta in sequence numbers before last resized PORT command */
	char		sawfin;		/* Did we saw an FIN packet? */
};
extern struct ip_masq *ip_msq_hosts;
extern void ip_fw_masquerade(struct sk_buff **, struct device *);
extern int ip_fw_demasquerade(struct sk_buff *);
#endif
#ifdef CONFIG_IP_FIREWALL
extern struct ip_fw *ip_fw_in_chain;
extern struct ip_fw *ip_fw_out_chain;
extern struct ip_fw *ip_fw_fwd_chain;
extern int ip_fw_in_policy;
extern int ip_fw_out_policy;
extern int ip_fw_fwd_policy;
extern int ip_fw_ctl(int, void *, int);
#endif
#ifdef CONFIG_IP_ACCT
extern struct ip_fw *ip_acct_chain;
extern void ip_acct_cnt(struct iphdr *, struct device *, struct ip_fw *);
extern int ip_acct_ctl(int, void *, int);
#endif


extern int ip_fw_chk(struct iphdr *, struct device *rif,struct ip_fw *, int, int);
extern void ip_fw_init(void);
#endif /* KERNEL */

#ifdef CONFIG_IP_MASQUERADE

#undef DEBUG_MASQ

#define MASQUERADE_EXPIRE_TCP     15*60*HZ
#define MASQUERADE_EXPIRE_TCP_FIN  2*60*HZ
#define MASQUERADE_EXPIRE_UDP      5*60*HZ

/*
 *	Linux ports don't normally get allocated above 32K. I used an extra 4K port-space
 */
 
#define PORT_MASQ_BEGIN	60000
#define PORT_MASQ_END	(PORT_MASQ_BEGIN+4096)
#define FTP_DPORT_TBD (PORT_MASQ_END+1) /* Avoid using hardcoded port 20 for ftp data connection */
#endif

#endif /* _IP_FW_H */
