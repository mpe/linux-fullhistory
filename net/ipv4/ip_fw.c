/*
 *	IP firewalling code. This is taken from 4.4BSD. Please note the 
 *	copyright message below. As per the GPL it must be maintained
 *	and the licenses thus do not conflict. While this port is subject
 *	to the GPL I also place my modifications under the original 
 *	license in recognition of the original copyright. 
 *				-- Alan Cox.
 *
 *	Ported from BSD to Linux,
 *		Alan Cox 22/Nov/1994.
 *	Zeroing /proc and other additions
 *		Jos Vos 4/Feb/1995.
 *	Merged and included the FreeBSD-Current changes at Ugen's request
 *	(but hey it's a lot cleaner now). Ugen would prefer in some ways
 *	we waited for his final product but since Linux 1.2.0 is about to
 *	appear it's not practical - Read: It works, it's not clean but please
 *	don't consider it to be his standard of finished work.
 *		Alan Cox 12/Feb/1995
 *	Porting bidirectional entries from BSD, fixing accounting issues,
 *	adding struct ip_fwpkt for checking packets with interface address
 *		Jos Vos 5/Mar/1995.
 *	Established connections (ACK check), ACK check on bidirectional rules,
 *	ICMP type check.
 *		Wilfred Mollenvanger 7/7/1995.
 *	TCP attack protection.
 *		Alan Cox 25/8/95, based on information from bugtraq.
 *	ICMP type printk, IP_FW_F_APPEND
 *		Bernd Eckenfels 1996-01-31
 *	Split blocking chain into input and output chains, add new "insert" and
 *	"append" commands to replace semi-intelligent "add" command, let "delete".
 *	only delete the first matching entry, use 0xFFFF (0xFF) as ports (ICMP
 *	types) when counting packets being 2nd and further fragments.
 *		Jos Vos <jos@xos.nl> 8/2/1996.
 *	Add support for matching on device names.
 *		Jos Vos <jos@xos.nl> 15/2/1996.
 *	Transparent proxying support.
 *		Willy Konynenberg <willy@xos.nl> 10/5/96.
 *	Make separate accounting on incoming and outgoing packets possible.
 *		Jos Vos <jos@xos.nl> 18/5/1996.
 *
 *
 * Masquerading functionality
 *
 * Copyright (c) 1994 Pauline Middelink
 *
 * The pieces which added masquerading functionality are totally
 * my responsibility and have nothing to with the original authors
 * copyright or doing.
 *
 * Parts distributed under GPL.
 *
 * Fixes:
 *	Pauline Middelink	:	Added masquerading.
 *	Alan Cox		:	Fixed an error in the merge.
 *	Thomas Quinot		:	Fixed port spoofing.
 *	Alan Cox		:	Cleaned up retransmits in spoofing.
 *	Alan Cox		:	Cleaned up length setting.
 *	Wouter Gadeyne		:	Fixed masquerading support of ftp PORT commands
 *
 *	Juan Jose Ciarlante	:	Masquerading code moved to ip_masq.c
 *
 *	All the real work was done by .....
 *
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

#include <linux/config.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/config.h>

#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/icmp.h>
#include <linux/udp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <linux/firewall.h>
#include <linux/ip_fw.h>

#ifdef CONFIG_IP_MASQUERADE
#include <net/ip_masq.h>
#endif

#include <net/checksum.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

/*
 *	Implement IP packet firewall
 */

#ifdef CONFIG_IP_FIREWALL_DEBUG 
#define dprintf1(a)		printk(a)
#define dprintf2(a1,a2)		printk(a1,a2)
#define dprintf3(a1,a2,a3)	printk(a1,a2,a3)
#define dprintf4(a1,a2,a3,a4)	printk(a1,a2,a3,a4)
#else
#define dprintf1(a)	
#define dprintf2(a1,a2)
#define dprintf3(a1,a2,a3)
#define dprintf4(a1,a2,a3,a4)
#endif

#define print_ip(a)	 printk("%ld.%ld.%ld.%ld",(ntohl(a)>>24)&0xFF,\
					      (ntohl(a)>>16)&0xFF,\
					      (ntohl(a)>>8)&0xFF,\
					      (ntohl(a))&0xFF);

#ifdef CONFIG_IP_FIREWALL_DEBUG
#define dprint_ip(a)	print_ip(a)
#else
#define dprint_ip(a)	
#endif

#if defined(CONFIG_IP_ACCT) || defined(CONFIG_IP_FIREWALL)

struct ip_fw *ip_fw_fwd_chain;
struct ip_fw *ip_fw_in_chain;
struct ip_fw *ip_fw_out_chain;
struct ip_fw *ip_acct_chain;

static struct ip_fw **chains[] =
	{&ip_fw_fwd_chain, &ip_fw_in_chain, &ip_fw_out_chain, &ip_acct_chain};

int ip_fw_fwd_policy=IP_FW_F_ACCEPT;
int ip_fw_in_policy=IP_FW_F_ACCEPT;
int ip_fw_out_policy=IP_FW_F_ACCEPT;

static int *policies[] =
	{&ip_fw_fwd_policy, &ip_fw_in_policy, &ip_fw_out_policy};

#endif

/*
 *	Returns 1 if the port is matched by the vector, 0 otherwise
 */

extern inline int port_match(unsigned short *portptr,int nports,unsigned short port,int range_flag)
{
	if (!nports)
		return 1;
	if ( range_flag ) 
	{
		if ( portptr[0] <= port && port <= portptr[1] ) 
		{
			return( 1 );
		}
		nports -= 2;
		portptr += 2;
	}
	while ( nports-- > 0 ) 
	{
		if ( *portptr++ == port ) 
		{
			return( 1 );
		}
	}
	return(0);
}

#if defined(CONFIG_IP_ACCT) || defined(CONFIG_IP_FIREWALL)


/*
 *	Returns one of the generic firewall policies, like FW_ACCEPT.
 *	Also does accounting so you can feed it the accounting chain.
 *
 *	The modes is either IP_FW_MODE_FW (normal firewall mode),
 *	IP_FW_MODE_ACCT_IN or IP_FW_MODE_ACCT_OUT (accounting mode,
 *	steps through the entire chain and handles fragments
 *	differently), or IP_FW_MODE_CHK (handles user-level check,
 *	counters are not updated).
 */


int ip_fw_chk(struct iphdr *ip, struct device *rif, __u16 *redirport, struct ip_fw *chain, int policy, int mode)
{
	struct ip_fw *f;
	struct tcphdr		*tcp=(struct tcphdr *)((unsigned long *)ip+ip->ihl);
	struct udphdr		*udp=(struct udphdr *)((unsigned long *)ip+ip->ihl);
	struct icmphdr		*icmp=(struct icmphdr *)((unsigned long *)ip+ip->ihl);
	__u32			src, dst;
	__u16			src_port=0xFFFF, dst_port=0xFFFF, icmp_type=0xFF;
	unsigned short		f_prt=0, prt;
	char			notcpsyn=0, notcpack=0, match;
	unsigned short		offset;
	int			answer;
	unsigned char		tosand, tosxor;

	/*
	 *	If the chain is empty follow policy. The BSD one
	 *	accepts anything giving you a time window while
	 *	flushing and rebuilding the tables.
	 */
	 
	src = ip->saddr;
	dst = ip->daddr;

	/* 
	 *	This way we handle fragmented packets.
	 *	we ignore all fragments but the first one
	 *	so the whole packet can't be reassembled.
	 *	This way we relay on the full info which
	 *	stored only in first packet.
	 *
	 *	Note that this theoretically allows partial packet
	 *	spoofing. Not very dangerous but paranoid people may
	 *	wish to play with this. It also allows the so called
	 *	"fragment bomb" denial of service attack on some types
	 *	of system.
	 */

	offset = ntohs(ip->frag_off) & IP_OFFSET;
	
	/*
	 *	Don't allow a fragment of TCP 8 bytes in. Nobody
	 *	normal causes this. Its a cracker trying to break
	 *	in by doing a flag overwrite to pass the direction
	 *	checks.
	 */
	 
	if (offset == 1 && ip->protocol == IPPROTO_TCP)
		return FW_BLOCK;
		
	if (offset!=0 && !(mode & (IP_FW_MODE_ACCT_IN|IP_FW_MODE_ACCT_OUT)) &&
		(ip->protocol == IPPROTO_TCP || ip->protocol == IPPROTO_UDP ||
			ip->protocol == IPPROTO_ICMP))
		return FW_ACCEPT;
		
	/*
	 *	 Header fragment for TCP is too small to check the bits.
	 */
	 
	if(ip->protocol==IPPROTO_TCP && (ip->ihl<<2)+16 > ntohs(ip->tot_len))
		return FW_BLOCK;
	
	/*
	 *	Too short.
	 *
	 *	But only too short for a packet with ports...
	 */
	 
	else if((ntohs(ip->tot_len)<8+(ip->ihl<<2))&&(ip->protocol==IPPROTO_TCP || ip->protocol==IPPROTO_UDP))
		return FW_BLOCK;
		
	src = ip->saddr;
	dst = ip->daddr;

	/*
	 *	If we got interface from which packet came
	 *	we can use the address directly. This is unlike
	 *	4.4BSD derived systems that have an address chain
	 *	per device. We have a device per address with dummy
	 *	devices instead.
	 */
	 
	dprintf1("Packet ");
	switch(ip->protocol) 
	{
		case IPPROTO_TCP:
			dprintf1("TCP ");
			/* ports stay 0xFFFF if it is not the first fragment */
			if (!offset) {
				src_port=ntohs(tcp->source);
				dst_port=ntohs(tcp->dest);
				if(!tcp->ack)
					/* We do NOT have ACK, value TRUE */
					notcpack=1;
				if(!tcp->syn || !notcpack)
					/* We do NOT have SYN, value TRUE */
					notcpsyn=1;
			}
			prt=IP_FW_F_TCP;
			break;
		case IPPROTO_UDP:
			dprintf1("UDP ");
			/* ports stay 0xFFFF if it is not the first fragment */
			if (!offset) {
				src_port=ntohs(udp->source);
				dst_port=ntohs(udp->dest);
			}
			prt=IP_FW_F_UDP;
			break;
		case IPPROTO_ICMP:
			/* icmp_type stays 255 if it is not the first fragment */
			if (!offset)
				icmp_type=(__u16)(icmp->type);
			dprintf2("ICMP:%d ",icmp_type);
			prt=IP_FW_F_ICMP;
			break;
		default:
			dprintf2("p=%d ",ip->protocol);
			prt=IP_FW_F_ALL;
			break;
	}
#ifdef CONFIG_IP_FIREWALL_DEBUG
	dprint_ip(ip->saddr);
	
	if (ip->protocol==IPPROTO_TCP || ip->protocol==IPPROTO_UDP)
		/* This will print 65535 when it is not the first fragment! */
		dprintf2(":%d ", src_port);
	dprint_ip(ip->daddr);
	if (ip->protocol==IPPROTO_TCP || ip->protocol==IPPROTO_UDP)
		/* This will print 65535 when it is not the first fragment! */
		dprintf2(":%d ",dst_port);
	dprintf1("\n");
#endif	

	for (f=chain;f;f=f->fw_next) 
	{
		/*
		 *	This is a bit simpler as we don't have to walk
		 *	an interface chain as you do in BSD - same logic
		 *	however.
		 */

		/*
		 *	Match can become 0x01 (a "normal" match was found),
		 *	0x02 (a reverse match was found), and 0x03 (the
		 *	IP addresses match in both directions).
		 *	Now we know in which direction(s) we should look
		 *	for a match for the TCP/UDP ports.  Both directions
		 *	might match (e.g., when both addresses are on the
		 *	same network for which an address/mask is given), but
		 *	the ports might only match in one direction.
		 *	This was obviously wrong in the original BSD code.
		 */
		match = 0x00;

		if ((src&f->fw_smsk.s_addr)==f->fw_src.s_addr
		&&  (dst&f->fw_dmsk.s_addr)==f->fw_dst.s_addr)
			/* normal direction */
			match |= 0x01;

		if ((f->fw_flg & IP_FW_F_BIDIR) &&
		    (dst&f->fw_smsk.s_addr)==f->fw_src.s_addr
		&&  (src&f->fw_dmsk.s_addr)==f->fw_dst.s_addr)
			/* reverse direction */
			match |= 0x02;

		if (!match)
			continue;

		/*
		 *	Look for a VIA address match 
		 */
		if(f->fw_via.s_addr && rif)
		{
			if(rif->pa_addr!=f->fw_via.s_addr)
				continue;	/* Mismatch */
		}

		/*
		 *	Look for a VIA device match 
		 */
		if(f->fw_viadev)
		{
			if(rif!=f->fw_viadev)
				continue;	/* Mismatch */
		}

		/*
		 *	Ok the chain addresses match.
		 */

#ifdef CONFIG_IP_ACCT
		/*
		 *	See if we're in accounting mode and only want to
		 *	count incoming or outgoing packets.
		 */

		if (mode & (IP_FW_MODE_ACCT_IN|IP_FW_MODE_ACCT_OUT) &&
		   ((mode == IP_FW_MODE_ACCT_IN && f->fw_flg&IP_FW_F_ACCTOUT) ||
		    (mode == IP_FW_MODE_ACCT_OUT && f->fw_flg&IP_FW_F_ACCTIN)))
			continue;

#endif
		/*
		 * For all non-TCP packets and/or non-first fragments,
		 * notcpsyn and notcpack will always be FALSE,
		 * so the IP_FW_F_TCPSYN and IP_FW_F_TCPACK flags
		 * are actually ignored for these packets.
		 */
		 
		if((f->fw_flg&IP_FW_F_TCPSYN) && notcpsyn)
		 	continue;

		if((f->fw_flg&IP_FW_F_TCPACK) && notcpack)
		 	continue;

		f_prt=f->fw_flg&IP_FW_F_KIND;
		if (f_prt!=IP_FW_F_ALL) 
		{
			/*
			 *	Specific firewall - packet's protocol
			 *	must match firewall's.
			 */

			if(prt!=f_prt)
				continue;
				
			if((prt==IP_FW_F_ICMP &&
				! port_match(&f->fw_pts[0], f->fw_nsp,
					icmp_type,f->fw_flg&IP_FW_F_SRNG)) ||
			    !(prt==IP_FW_F_ICMP || ((match & 0x01) &&
				port_match(&f->fw_pts[0], f->fw_nsp, src_port,
					f->fw_flg&IP_FW_F_SRNG) &&
				port_match(&f->fw_pts[f->fw_nsp], f->fw_ndp, dst_port,
					f->fw_flg&IP_FW_F_DRNG)) || ((match & 0x02) &&
				port_match(&f->fw_pts[0], f->fw_nsp, dst_port,
					f->fw_flg&IP_FW_F_SRNG) &&
				port_match(&f->fw_pts[f->fw_nsp], f->fw_ndp, src_port,
					f->fw_flg&IP_FW_F_DRNG))))
			{
				continue;
			}
		}

#ifdef CONFIG_IP_FIREWALL_VERBOSE
		/*
		 * VERY ugly piece of code which actually
		 * makes kernel printf for matching packets...
		 */

		if (f->fw_flg & IP_FW_F_PRN)
		{
			__u32 *opt = (__u32 *) (ip + 1);
			int opti;

			if(mode == IP_FW_MODE_ACCT_IN)
				printk(KERN_INFO "IP acct in ");
			else if(mode == IP_FW_MODE_ACCT_OUT)
				printk(KERN_INFO "IP acct out ");
			else {
				if(chain == ip_fw_fwd_chain)
					printk(KERN_INFO "IP fw-fwd ");
				else if(chain == ip_fw_in_chain)
					printk(KERN_INFO "IP fw-in ");
				else
					printk(KERN_INFO "IP fw-out ");
				if(f->fw_flg&IP_FW_F_ACCEPT) {
					if(f->fw_flg&IP_FW_F_REDIR)
						printk("acc/r%d ", f->fw_pts[f->fw_nsp+f->fw_ndp]);
					else if(f->fw_flg&IP_FW_F_MASQ)
						printk("acc/masq ");
					else
						printk("acc ");
				} else if(f->fw_flg&IP_FW_F_ICMPRPL)
					printk("rej ");
				else
					printk("deny ");
			}
			printk(rif ? rif->name : "-");
			switch(ip->protocol)
			{
				case IPPROTO_TCP:
					printk(" TCP ");
					break;
				case IPPROTO_UDP:
					printk(" UDP ");
					break;
				case IPPROTO_ICMP:
					printk(" ICMP/%d ", icmp_type);
					break;
				default:
					printk(" PROTO=%d ", ip->protocol);
					break;
			}
			print_ip(ip->saddr);
			if(ip->protocol == IPPROTO_TCP || ip->protocol == IPPROTO_UDP)
				printk(":%hu", src_port);
			printk(" ");
			print_ip(ip->daddr);
			if(ip->protocol == IPPROTO_TCP || ip->protocol == IPPROTO_UDP)
				printk(":%hu", dst_port);
			printk(" L=%hu S=0x%2.2hX I=%hu F=0x%4.4hX T=%hu",
				ntohs(ip->tot_len), ip->tos, ntohs(ip->id),
				ip->frag_off, ip->ttl);
			for (opti = 0; opti < (ip->ihl - sizeof(struct iphdr) / 4); opti++)
				printk(" O=0x%8.8X", *opt++);
			printk("\n");
		}
#endif		
		if (mode != IP_FW_MODE_CHK) {
			f->fw_bcnt+=ntohs(ip->tot_len);
			f->fw_pcnt++;
		}
		if (!(mode & (IP_FW_MODE_ACCT_IN|IP_FW_MODE_ACCT_OUT)))
			break;
	} /* Loop */
	
	if (!(mode & (IP_FW_MODE_ACCT_IN|IP_FW_MODE_ACCT_OUT))) {

		/*
		 * We rely on policy defined in the rejecting entry or, if no match
		 * was found, we rely on the general policy variable for this type
		 * of firewall.
		 */

		if (f!=NULL) {
			policy=f->fw_flg;
			tosand=f->fw_tosand;
			tosxor=f->fw_tosxor;
		} else {
			tosand=0xFF;
			tosxor=0x00;
		}

		if (policy&IP_FW_F_ACCEPT) {
			/* Adjust priority and recompute checksum */
			__u8 old_tos = ip->tos;
			ip->tos = (old_tos & tosand) ^ tosxor;
			if (ip->tos != old_tos)
		 		ip_send_check(ip);
#ifdef CONFIG_IP_TRANSPARENT_PROXY
			if (policy&IP_FW_F_REDIR) {
				if (redirport)
					if ((*redirport = htons(f->fw_pts[f->fw_nsp+f->fw_ndp])) == 0) {
						/* Wildcard redirection.
						 * Note that redirport will become
						 * 0xFFFF for non-TCP/UDP packets.
						 */
						*redirport = htons(dst_port);
					}
				answer = FW_REDIRECT;
			} else
#endif
#ifdef CONFIG_IP_MASQUERADE
			if (policy&IP_FW_F_MASQ)
				answer = FW_MASQUERADE;
			else
#endif
				answer = FW_ACCEPT;
			
		} else if(policy&IP_FW_F_ICMPRPL)
			answer = FW_REJECT;
		else
			answer = FW_BLOCK;

		return answer;
	} else
		/* we're doing accounting, always ok */
		return 0;
}


static void zero_fw_chain(struct ip_fw *chainptr)
{
	struct ip_fw *ctmp=chainptr;
	while(ctmp) 
	{
		ctmp->fw_pcnt=0L;
		ctmp->fw_bcnt=0L;
		ctmp=ctmp->fw_next;
	}
}

static void free_fw_chain(struct ip_fw *volatile* chainptr)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	while ( *chainptr != NULL ) 
	{
		struct ip_fw *ftmp;
		ftmp = *chainptr;
		*chainptr = ftmp->fw_next;
		kfree_s(ftmp,sizeof(*ftmp));
	}
	restore_flags(flags);
}

/* Volatiles to keep some of the compiler versions amused */

static int insert_in_chain(struct ip_fw *volatile* chainptr, struct ip_fw *frwl,int len)
{
	struct ip_fw *ftmp;
	unsigned long flags;

	save_flags(flags);

	ftmp = kmalloc( sizeof(struct ip_fw), GFP_ATOMIC );
	if ( ftmp == NULL ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl:  malloc said no\n");
#endif
		return( ENOMEM );
	}

	memcpy(ftmp, frwl, len);
	/*
	 *	Allow the more recent "minimise cost" flag to be
	 *	set. [Rob van Nieuwkerk]
	 */
	ftmp->fw_tosand |= 0x01;
	ftmp->fw_tosxor &= 0xFE;
	ftmp->fw_pcnt=0L;
	ftmp->fw_bcnt=0L;

	cli();

	if ((ftmp->fw_vianame)[0]) {
		if (!(ftmp->fw_viadev = dev_get(ftmp->fw_vianame)))
			ftmp->fw_viadev = (struct device *) -1;
	} else
		ftmp->fw_viadev = NULL;

	ftmp->fw_next = *chainptr;
       	*chainptr=ftmp;
	restore_flags(flags);
	return(0);
}

static int append_to_chain(struct ip_fw *volatile* chainptr, struct ip_fw *frwl,int len)
{
	struct ip_fw *ftmp;
	struct ip_fw *chtmp=NULL;
	struct ip_fw *volatile chtmp_prev=NULL;
	unsigned long flags;

	save_flags(flags);

	ftmp = kmalloc( sizeof(struct ip_fw), GFP_ATOMIC );
	if ( ftmp == NULL ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl:  malloc said no\n");
#endif
		return( ENOMEM );
	}

	memcpy(ftmp, frwl, len);
	/*
	 *	Allow the more recent "minimise cost" flag to be
	 *	set. [Rob van Nieuwkerk]
	 */
	ftmp->fw_tosand |= 0x01;
	ftmp->fw_tosxor &= 0xFE;
	ftmp->fw_pcnt=0L;
	ftmp->fw_bcnt=0L;

	ftmp->fw_next = NULL;

	cli();

	if ((ftmp->fw_vianame)[0]) {
		if (!(ftmp->fw_viadev = dev_get(ftmp->fw_vianame)))
			ftmp->fw_viadev = (struct device *) -1;
	} else
		ftmp->fw_viadev = NULL;

	chtmp_prev=NULL;
	for (chtmp=*chainptr;chtmp!=NULL;chtmp=chtmp->fw_next) 
		chtmp_prev=chtmp;
	
	if (chtmp_prev)
		chtmp_prev->fw_next=ftmp;
	else
        	*chainptr=ftmp;
	restore_flags(flags);
	return(0);
}

static int del_from_chain(struct ip_fw *volatile*chainptr, struct ip_fw *frwl)
{
	struct ip_fw 	*ftmp,*ltmp;
	unsigned short	tport1,tport2,tmpnum;
	char		matches,was_found;
	unsigned long 	flags;

	save_flags(flags);
	cli();

	ftmp=*chainptr;

	if ( ftmp == NULL ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl:  chain is empty\n");
#endif
		restore_flags(flags);
		return( EINVAL );
	}

	ltmp=NULL;
	was_found=0;

	while( !was_found && ftmp != NULL )
	{
		matches=1;
		if (ftmp->fw_src.s_addr!=frwl->fw_src.s_addr 
		     ||  ftmp->fw_dst.s_addr!=frwl->fw_dst.s_addr
		     ||  ftmp->fw_smsk.s_addr!=frwl->fw_smsk.s_addr
		     ||  ftmp->fw_dmsk.s_addr!=frwl->fw_dmsk.s_addr
		     ||  ftmp->fw_via.s_addr!=frwl->fw_via.s_addr
		     ||  ftmp->fw_flg!=frwl->fw_flg)
        		matches=0;

		tport1=ftmp->fw_nsp+ftmp->fw_ndp;
		tport2=frwl->fw_nsp+frwl->fw_ndp;
		if (tport1!=tport2)
		        matches=0;
		else if (tport1!=0)
		{
			for (tmpnum=0;tmpnum < tport1 && tmpnum < IP_FW_MAX_PORTS;tmpnum++)
        		if (ftmp->fw_pts[tmpnum]!=frwl->fw_pts[tmpnum])
				matches=0;
		}
		if (strncmp(ftmp->fw_vianame, frwl->fw_vianame, IFNAMSIZ))
		        matches=0;
		if(matches)
		{
			was_found=1;
			if (ltmp)
			{
				ltmp->fw_next=ftmp->fw_next;
				kfree_s(ftmp,sizeof(*ftmp));
				ftmp=ltmp->fw_next;
        		}
      			else
      			{
      				*chainptr=ftmp->fw_next; 
	 			kfree_s(ftmp,sizeof(*ftmp));
				ftmp=*chainptr;
			}       
		}
		else
		{
			ltmp = ftmp;
			ftmp = ftmp->fw_next;
		 }
	}
	restore_flags(flags);
	if (was_found)
		return 0;
	else
		return(EINVAL);
}

#endif  /* CONFIG_IP_ACCT || CONFIG_IP_FIREWALL */

struct ip_fw *check_ipfw_struct(struct ip_fw *frwl, int len)
{

	if ( len != sizeof(struct ip_fw) )
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: len=%d, want %d\n",len, sizeof(struct ip_fw));
#endif
		return(NULL);
	}

	if ( (frwl->fw_flg & ~IP_FW_F_MASK) != 0 )
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: undefined flag bits set (flags=%x)\n",
			frwl->fw_flg);
#endif
		return(NULL);
	}

#ifndef CONFIG_IP_TRANSPARENT_PROXY
	if (frwl->fw_flg & IP_FW_F_REDIR) {
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: unsupported flag IP_FW_F_REDIR\n");
#endif
		return(NULL);
	}
#endif

#ifndef CONFIG_IP_MASQUERADE
	if (frwl->fw_flg & IP_FW_F_MASQ) {
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: unsupported flag IP_FW_F_MASQ\n");
#endif
		return(NULL);
	}
#endif

	if ( (frwl->fw_flg & IP_FW_F_SRNG) && frwl->fw_nsp < 2 ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: src range set but fw_nsp=%d\n",
			frwl->fw_nsp);
#endif
		return(NULL);
	}

	if ( (frwl->fw_flg & IP_FW_F_DRNG) && frwl->fw_ndp < 2 ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: dst range set but fw_ndp=%d\n",
			frwl->fw_ndp);
#endif
		return(NULL);
	}

	if ( frwl->fw_nsp + frwl->fw_ndp > (frwl->fw_flg & IP_FW_F_REDIR ? IP_FW_MAX_PORTS - 1 : IP_FW_MAX_PORTS) ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: too many ports (%d+%d)\n",
			frwl->fw_nsp,frwl->fw_ndp);
#endif
		return(NULL);
	}

	return frwl;
}




#ifdef CONFIG_IP_ACCT

int ip_acct_ctl(int stage, void *m, int len)
{
	if ( stage == IP_ACCT_FLUSH )
	{
		free_fw_chain(&ip_acct_chain);
		return(0);
	}  
	if ( stage == IP_ACCT_ZERO )
	{
		zero_fw_chain(ip_acct_chain);
		return(0);
	}
	if ( stage == IP_ACCT_INSERT || stage == IP_ACCT_APPEND ||
	  				stage == IP_ACCT_DELETE )
	{
		struct ip_fw *frwl;

		if (!(frwl=check_ipfw_struct(m,len)))
			return (EINVAL);

		switch (stage) 
		{
			case IP_ACCT_INSERT:
				return( insert_in_chain(&ip_acct_chain,frwl,len));
			case IP_ACCT_APPEND:
				return( append_to_chain(&ip_acct_chain,frwl,len));
		    	case IP_ACCT_DELETE:
				return( del_from_chain(&ip_acct_chain,frwl));
			default:
				/*
 				 *	Should be panic but... (Why ??? - AC)
				 */
#ifdef DEBUG_CONFIG_IP_FIREWALL
				printk("ip_acct_ctl:  unknown request %d\n",stage);
#endif
				return(EINVAL);
		}
	}
#ifdef DEBUG_CONFIG_IP_FIREWALL
	printk("ip_acct_ctl:  unknown request %d\n",stage);
#endif
	return(EINVAL);
}
#endif

#ifdef CONFIG_IP_FIREWALL
int ip_fw_ctl(int stage, void *m, int len)
{
	int cmd, fwtype;

	cmd = stage & IP_FW_COMMAND;
	fwtype = (stage & IP_FW_TYPE) >> IP_FW_SHIFT;

	if ( cmd == IP_FW_FLUSH )
	{
		free_fw_chain(chains[fwtype]);
		return(0);
	}  

	if ( cmd == IP_FW_ZERO )
	{
		zero_fw_chain(*chains[fwtype]);
		return(0);
	}  

	if ( cmd == IP_FW_POLICY )
	{
		int *tmp_policy_ptr;
		tmp_policy_ptr=(int *)m;
		*policies[fwtype] = *tmp_policy_ptr;
		return 0;
	}

	if ( cmd == IP_FW_CHECK )
	{
		struct device *viadev;
		struct ip_fwpkt *ipfwp;
		struct iphdr *ip;

		if ( len != sizeof(struct ip_fwpkt) )
		{
#ifdef DEBUG_CONFIG_IP_FIREWALL
			printk("ip_fw_ctl: length=%d, expected %d\n",
				len, sizeof(struct ip_fwpkt));
#endif
			return( EINVAL );
		}

	 	ipfwp = (struct ip_fwpkt *)m;
	 	ip = &(ipfwp->fwp_iph);

		if ( !(viadev = dev_get(ipfwp->fwp_vianame)) ) {
#ifdef DEBUG_CONFIG_IP_FIREWALL
			printk("ip_fw_ctl: invalid device \"%s\"\n", ipfwp->fwp_vianame);
#endif
			return(EINVAL);
		} else if ( viadev->pa_addr != ipfwp->fwp_via.s_addr ) {
#ifdef DEBUG_CONFIG_IP_FIREWALL
			printk("ip_fw_ctl: device \"%s\" has another IP address\n",
				ipfwp->fwp_vianame);
#endif
			return(EINVAL);
		} else if ( ip->ihl != sizeof(struct iphdr) / sizeof(int)) {
#ifdef DEBUG_CONFIG_IP_FIREWALL
			printk("ip_fw_ctl: ip->ihl=%d, want %d\n",ip->ihl,
					sizeof(struct iphdr)/sizeof(int));
#endif
			return(EINVAL);
		}

		switch (ip_fw_chk(ip, viadev, NULL, *chains[fwtype],
				*policies[fwtype], IP_FW_MODE_CHK))
		{
			case FW_ACCEPT:
				return(0);
	    		case FW_REDIRECT:
				return(ECONNABORTED);
	    		case FW_MASQUERADE:
				return(ECONNRESET);
	    		case FW_REJECT:
				return(ECONNREFUSED);
			default: /* FW_BLOCK */
				return(ETIMEDOUT);
		}
	}

	if ( cmd == IP_FW_MASQ_TIMEOUTS )
	{
#ifdef CONFIG_IP_MASQUERADE
		struct ip_fw_masq *masq;

		if ( len != sizeof(struct ip_fw_masq) )
		{
#ifdef DEBUG_CONFIG_IP_FIREWALL
			printk("ip_fw_ctl (masq): length %d, expected %d\n",
				len, sizeof(struct ip_fw_masq));

#endif
			return( EINVAL );
		}

		masq = (struct ip_fw_masq *) m;

		if (masq->tcp_timeout)
		{
			ip_masq_expire->tcp_timeout = masq->tcp_timeout;
		}

		if (masq->tcp_fin_timeout)
		{
			ip_masq_expire->tcp_fin_timeout = masq->tcp_fin_timeout;
		}

		if (masq->udp_timeout)
		{
			ip_masq_expire->udp_timeout = masq->udp_timeout;
		}

		return 0;
#else
		return( EINVAL );
#endif
	}

/*
 *	Here we really working hard-adding new elements
 *	to blocking/forwarding chains or deleting 'em
 */

	if ( cmd == IP_FW_INSERT || cmd == IP_FW_APPEND || cmd == IP_FW_DELETE )
	{
		struct ip_fw *frwl;
		int fwtype;

		frwl=check_ipfw_struct(m,len);
		if (frwl==NULL)
			return (EINVAL);
		fwtype = (stage & IP_FW_TYPE) >> IP_FW_SHIFT;
		
		switch (cmd) 
		{
			case IP_FW_INSERT:
				return(insert_in_chain(chains[fwtype],frwl,len));
			case IP_FW_APPEND:
				return(append_to_chain(chains[fwtype],frwl,len));
			case IP_FW_DELETE:
				return(del_from_chain(chains[fwtype],frwl));
			default:
			/*
	 		 *	Should be panic but... (Why are BSD people panic obsessed ??)
			 */
#ifdef DEBUG_CONFIG_IP_FIREWALL
				printk("ip_fw_ctl:  unknown request %d\n",stage);
#endif
				return(EINVAL);
		}
	} 

#ifdef DEBUG_CONFIG_IP_FIREWALL
	printk("ip_fw_ctl:  unknown request %d\n",stage);
#endif
	return(EINVAL);
}
#endif /* CONFIG_IP_FIREWALL */

#if defined(CONFIG_IP_FIREWALL) || defined(CONFIG_IP_ACCT)

static int ip_chain_procinfo(int stage, char *buffer, char **start,
			     off_t offset, int length, int reset)
{
	off_t pos=0, begin=0;
	struct ip_fw *i;
	unsigned long flags;
	int len, p;
	int last_len = 0;
	

	switch(stage)
	{
#ifdef CONFIG_IP_FIREWALL
		case IP_FW_IN:
			i = ip_fw_in_chain;
			len=sprintf(buffer, "IP firewall input rules, default %d\n",
				ip_fw_in_policy);
			break;
		case IP_FW_OUT:
			i = ip_fw_out_chain;
			len=sprintf(buffer, "IP firewall output rules, default %d\n",
				ip_fw_out_policy);
			break;
		case IP_FW_FWD:
			i = ip_fw_fwd_chain;
			len=sprintf(buffer, "IP firewall forward rules, default %d\n",
				ip_fw_fwd_policy);
			break;
#endif
#ifdef CONFIG_IP_ACCT
		case IP_FW_ACCT:
			i = ip_acct_chain;
			len=sprintf(buffer,"IP accounting rules\n");
			break;
#endif
		default:
			/* this should never be reached, but safety first... */
			i = NULL;
			len=0;
			break;
	}

	save_flags(flags);
	cli();
	
	while(i!=NULL)
	{
		len+=sprintf(buffer+len,"%08lX/%08lX->%08lX/%08lX %.16s %08lX %X ",
			ntohl(i->fw_src.s_addr),ntohl(i->fw_smsk.s_addr),
			ntohl(i->fw_dst.s_addr),ntohl(i->fw_dmsk.s_addr),
			(i->fw_vianame)[0] ? i->fw_vianame : "-",
			ntohl(i->fw_via.s_addr),i->fw_flg);
		len+=sprintf(buffer+len,"%u %u %-9lu %-9lu",
			i->fw_nsp,i->fw_ndp, i->fw_pcnt,i->fw_bcnt);
		for (p = 0; p < IP_FW_MAX_PORTS; p++)
			len+=sprintf(buffer+len, " %u", i->fw_pts[p]);
		len+=sprintf(buffer+len, " A%02X X%02X", i->fw_tosand, i->fw_tosxor);
		buffer[len++]='\n';
		buffer[len]='\0';
		pos=begin+len;
		if(pos<offset)
		{
			len=0;
			begin=pos;
		}
		else if(pos>offset+length)
		{
			len = last_len;
			break;		
		}
		else if(reset)
		{
			/* This needs to be done at this specific place! */
			i->fw_pcnt=0L;
			i->fw_bcnt=0L;
		}
		last_len = len;
		i=i->fw_next;
	}
	restore_flags(flags);
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;	
	return len;
}
#endif

#ifdef CONFIG_IP_ACCT

static int ip_acct_procinfo(char *buffer, char **start, off_t offset,
			    int length, int reset)
{
	return ip_chain_procinfo(IP_FW_ACCT, buffer,start, offset,length,
				 reset);
}

#endif

#ifdef CONFIG_IP_FIREWALL

static int ip_fw_in_procinfo(char *buffer, char **start, off_t offset,
			      int length, int reset)
{
	return ip_chain_procinfo(IP_FW_IN, buffer,start,offset,length,
				 reset);
}

static int ip_fw_out_procinfo(char *buffer, char **start, off_t offset,
			      int length, int reset)
{
	return ip_chain_procinfo(IP_FW_OUT, buffer,start,offset,length,
				 reset);
}

static int ip_fw_fwd_procinfo(char *buffer, char **start, off_t offset,
			      int length, int reset)
{
	return ip_chain_procinfo(IP_FW_FWD, buffer,start,offset,length,
				 reset);
}
#endif


#ifdef CONFIG_IP_FIREWALL
/*
 *	Interface to the generic firewall chains.
 */
 
int ipfw_input_check(struct firewall_ops *this, int pf, struct device *dev, void *phdr, void *arg)
{
	return ip_fw_chk(phdr, dev, arg, ip_fw_in_chain, ip_fw_in_policy, IP_FW_MODE_FW);
}

int ipfw_output_check(struct firewall_ops *this, int pf, struct device *dev, void *phdr, void *arg)
{
	return ip_fw_chk(phdr, dev, arg, ip_fw_out_chain, ip_fw_out_policy, IP_FW_MODE_FW);
}

int ipfw_forward_check(struct firewall_ops *this, int pf, struct device *dev, void *phdr, void *arg)
{
	return ip_fw_chk(phdr, dev, arg, ip_fw_fwd_chain, ip_fw_fwd_policy, IP_FW_MODE_FW);
}
 
struct firewall_ops ipfw_ops=
{
	NULL,
	ipfw_forward_check,
	ipfw_input_check,
	ipfw_output_check,
	PF_INET,
	0	/* We don't even allow a fall through so we are last */
};

#endif

#if defined(CONFIG_IP_ACCT) || defined(CONFIG_IP_FIREWALL)

int ipfw_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct device *dev=ptr;
	char *devname = dev->name;
	unsigned long flags;
	struct ip_fw *fw;
	int chn;

	save_flags(flags);
	cli();
	
	if (event == NETDEV_UP) {
		for (chn = 0; chn < IP_FW_CHAINS; chn++)
			for (fw = *chains[chn]; fw; fw = fw->fw_next)
				if ((fw->fw_vianame)[0] && !strncmp(devname,
						fw->fw_vianame, IFNAMSIZ))
					fw->fw_viadev = dev;
	} else if (event == NETDEV_DOWN) {
		for (chn = 0; chn < IP_FW_CHAINS; chn++)
			for (fw = *chains[chn]; fw; fw = fw->fw_next)
				/* we could compare just the pointers ... */
				if ((fw->fw_vianame)[0] && !strncmp(devname,
						fw->fw_vianame, IFNAMSIZ))
					fw->fw_viadev = (struct device *) -1;
	}

	restore_flags(flags);
	return NOTIFY_DONE;
}

static struct notifier_block ipfw_dev_notifier={
	ipfw_device_event,
	NULL,
	0
};

#endif

void ip_fw_init(void)
{
#ifdef CONFIG_PROC_FS
#ifdef CONFIG_IP_ACCT
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPACCT, 7, "ip_acct",
		S_IFREG | S_IRUGO | S_IWUSR, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_acct_procinfo
	});
#endif
#endif
#ifdef CONFIG_IP_FIREWALL

	if(register_firewall(PF_INET,&ipfw_ops)<0)
		panic("Unable to register IP firewall.\n");

#ifdef CONFIG_PROC_FS		
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPFWIN, 8, "ip_input",
		S_IFREG | S_IRUGO | S_IWUSR, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_fw_in_procinfo
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPFWOUT, 9, "ip_output",
		S_IFREG | S_IRUGO | S_IWUSR, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_fw_out_procinfo
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPFWFWD, 10, "ip_forward",
		S_IFREG | S_IRUGO | S_IWUSR, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_fw_fwd_procinfo
	});
#endif
#endif
#ifdef CONFIG_IP_MASQUERADE
        
        /*
         *	Initialize masquerading. 
         */
        
        ip_masq_init();
#endif
        
#if defined(CONFIG_IP_ACCT) || defined(CONFIG_IP_FIREWALL)
	/* Register for device up/down reports */
	register_netdevice_notifier(&ipfw_dev_notifier);
#endif
}
