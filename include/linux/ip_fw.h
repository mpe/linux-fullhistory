/*
 *	IP firewalling code. This is taken from 4.4BSD. Please note the 
 *	copyright message below. As per the GPL it must be maintained
 *	and the licenses thus do not conflict. While this port is subject
 *	to the GPL I also place my modifications under the original 
 *	license in recognition of the original copyright. 
 *
 *	Ported from BSD to Linux,
 *		Alan Cox 22/Nov/1994.
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
	struct ip_fw *next;			/* Next firewall on chain */
	struct in_addr src, dst;		/* Source and destination IP addr */
	struct in_addr src_mask, dst_mask;	/* Mask for src and dest IP addr */
	unsigned short flags;			/* Flags word */
	unsigned short n_src_p, n_dst_p;        /* # of src ports and # of dst ports */
						/* in ports array (dst ports follow */
    						/* src ports; max of 10 ports in all; */
    						/* count of 0 means match all ports) */
#define IP_FW_MAX_PORTS	10      		/* A reasonable maximum */
	unsigned short ports[IP_FW_MAX_PORTS];  /* Array of port numbers to match */
	unsigned long p_cnt,b_cnt;		/* Packet and byte counters */
};

/*
 *	Values for "flags" field .
 */

#define IP_FW_F_ALL	0x00	/* This is a universal packet firewall*/
#define IP_FW_F_TCP	0x01	/* This is a TCP packet firewall      */
#define IP_FW_F_UDP	0x02	/* This is a UDP packet firewall      */
#define IP_FW_F_ICMP	0x03	/* This is a ICMP packet firewall     */
#define IP_FW_F_KIND	0x03	/* Mask to isolate firewall kind      */
#define IP_FW_F_ACCEPT	0x04	/* This is an accept firewall (as     *
				 *         opposed to a deny firewall)*
				 *                                    */
#define IP_FW_F_SRNG	0x08	/* The first two src ports are a min  *
				 * and max range (stored in host byte *
				 * order).                            *
				 *                                    */
#define IP_FW_F_DRNG	0x10	/* The first two dst ports are a min  *
				 * and max range (stored in host byte *
				 * order).                            *
				 * (ports[0] <= port <= ports[1])     *
				 *                                    */
#define IP_FW_F_PRN	0x20	/* In verbose mode print this firewall*/
#define IP_FW_F_BIDIR	0x40	/* For accounting-count two way       */
#define IP_FW_F_MASK	0x7F	/* All possible flag bits mask        */

/*    
 *	New IP firewall options for [gs]etsockopt at the RAW IP level.
 *	Unlike BSD Linux inherits IP options so you don't have to use
 *	a raw socket for this. Instead we check rights in the calls.
 */     

#define IP_FW_BASE_CTL	64

#define IP_FW_ADD_BLK (IP_FW_BASE_CTL)
#define IP_FW_ADD_FWD (IP_FW_BASE_CTL+1)   
#define IP_FW_CHK_BLK (IP_FW_BASE_CTL+2)
#define IP_FW_CHK_FWD (IP_FW_BASE_CTL+3)
#define IP_FW_DEL_BLK (IP_FW_BASE_CTL+4)
#define IP_FW_DEL_FWD (IP_FW_BASE_CTL+5)
#define IP_FW_FLUSH   (IP_FW_BASE_CTL+6)
#define IP_FW_POLICY  (IP_FW_BASE_CTL+7) 

#define IP_ACCT_ADD   (IP_FW_BASE_CTL+10)
#define IP_ACCT_DEL   (IP_FW_BASE_CTL+11)
#define IP_ACCT_FLUSH (IP_FW_BASE_CTL+12)
#define IP_ACCT_ZERO  (IP_FW_BASE_CTL+13)


/*
 *	Main firewall chains definitions and global var's definitions.
 */

#ifdef __KERNEL__
#ifdef CONFIG_IP_FIREWALL
extern struct ip_fw *ip_fw_blk_chain;
extern struct ip_fw *ip_fw_fwd_chain;
extern int ip_fw_policy;
extern int ip_fw_chk(struct iphdr *, struct ip_fw *);
extern int ip_fw_ctl(int, void *, int);
#endif
#ifdef CONFIG_IP_ACCT
extern struct ip_fw *ip_acct_chain;
extern void ip_acct_cnt(struct iphdr *, struct ip_fw *, int);
extern int ip_acct_ctl(int, void *, int);
#endif
#endif /* KERNEL */

#endif /* _IP_FW_H */
