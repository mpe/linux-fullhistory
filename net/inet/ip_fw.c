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
#include "ip.h"
#include "protocol.h"
#include "route.h"
#include "tcp.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "icmp.h"
#include <linux/ip_fw.h>

/*
 *	Implement IP packet firewall
 */

#ifdef CONFIG_IP_FIREWALL
struct ip_fw *ip_fw_fwd_chain;
struct ip_fw *ip_fw_blk_chain;
int ip_fw_blk_policy=1;
int ip_fw_fwd_policy=1;
#endif
#ifdef CONFIG_IP_ACCT
struct ip_fw *ip_acct_chain;
#endif

#define IP_INFO_BLK	0
#define IP_INFO_FWD	1
#define IP_INFO_ACCT	2


extern inline void print_ip(unsigned long xaddr)
{
	unsigned long addr = ntohl(xaddr);
	printk("%ld.%ld.%ld.%ld",(addr>>24) & 0xff,
                         (addr>>16)&0xff,
                         (addr>>8)&0xff,
                         addr&0xFF);
}                  


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


/*
 *	Returns 0 if packet should be dropped, 1 or more if it should be accepted
 */

#ifdef CONFIG_IP_FIREWALL

int ip_fw_chk(struct iphdr *ip, struct ip_fw *chain, int policy)
{
	unsigned long src, dst;
	char got_proto=0;
	int frwl_proto, proto=0;
	struct ip_fw *f;
	unsigned short src_port=0, dst_port=0;
	unsigned short *portptr=(unsigned short *)&(((u_int *)ip)[ip->ihl]);

	if (!chain) 
		return(policy);  /* If no chain, use your policy. */

	src = ip->saddr;
	dst = ip->daddr;

#ifdef DEBUG_CONFIG_IP_FIREWALL
	{
		printk("packet ");
		switch(ip->protocol) 
		{
			case IPPROTO_TCP:
				printf("TCP ");
				break;
			case IPPROTO_UDP:
				printf("UDP ");
				break;
			case IPPROTO_ICMP:
				printf("ICMP:%d ",((char *)portptr)[0]&0xff);
				break;
			default:
				printf("p=%d ",ip->protocol);
				break;
		}
		print_ip(ip->saddr);
		if (ip->protocol==IPPROTO_TCP || ip->protocol==IPPROTO_UDP) 
		{
			printf(":%d ",ntohs(portptr[0]));
		}
		print_ip(ip->daddr);
		if ( ip->protocol==IPPROTO_TCP || ip->protocol==IPPROTO_UDP) 
		{
			printf(":%d ",ntohs(portptr[1]));
		}
		printf("\n");
	}
#endif

	for (f=chain;f;f=f->next) 
	{
		if ((src&f->src_mask.s_addr)==f->src.s_addr
			&&  (dst&f->dst_mask.s_addr)==f->dst.s_addr) 
		{
			frwl_proto=f->flags&IP_FW_F_KIND;
			if (frwl_proto==IP_FW_F_ALL) 
			{
				/* Universal frwl - we've got a match! */

#ifdef DEBUG_CONFIG_IP_FIREWALL
				printf("universal frwl match\n");
#endif
				f->p_cnt++;
				f->b_cnt+=ntohs(ip->tot_len);
#ifdef CONFIG_IP_FIREWALL_VERBOSE
				if (!(f->flags & IP_FW_F_ACCEPT))
					goto bad_packet;
				return 1;
#else
				return( f->flags & IP_FW_F_ACCEPT );
#endif
			}
			else
			{
				/*
				 *	Specific firewall - packet's protocol must match firewall's
				 */
				if (!got_proto) 
				{
					/*
	 				 * We still had not determined the protocol
					 * of this packet,now the time to do so.
					 */
					switch(ip->protocol) 
					{
						case IPPROTO_TCP:
							/*
							 * 	First two shorts in TCP are src/dst ports
							 */
							proto=IP_FW_F_TCP;
							src_port=ntohs(portptr[0]);
							dst_port=ntohs(portptr[1]);
							break;
						case IPPROTO_UDP:
							/*
							 *	First two shorts in UDP are src/dst ports
			 				 */
							proto = IP_FW_F_UDP;
							src_port = ntohs(portptr[0]);
							dst_port = ntohs(portptr[1]);
							break;
						case IPPROTO_ICMP:
							proto=IP_FW_F_ICMP;
							break;
						default:
							proto=IP_FW_F_ALL;
#ifdef DEBUG_CONFIG_IP_FIREWALL
							printf("non TCP/UDP packet\n");
#endif
					}
					got_proto=1;
				} 
				/*
				 * At this moment we surely know the protocol of this
				 * packet and we'll check if it matches,then proceed further..
				 */
				if (proto==frwl_proto) 
				{
	
					if (proto==IP_FW_F_ICMP || (port_match(&f->ports[0],f->n_src_p,src_port,
						f->flags&IP_FW_F_SRNG) &&
					        port_match(&f->ports[f->n_src_p],f->n_dst_p,dst_port,
						f->flags&IP_FW_F_DRNG))) 
					{
					/* We've got a match! */
					f->p_cnt++;
					f->b_cnt+=ntohs(ip->tot_len);
#ifdef CONFIG_IP_FIREWALL_VERBOSE
						if (!(f->flags & IP_FW_F_ACCEPT))
							goto bad_packet;
						return 1;
#else
						return( f->flags & IP_FW_F_ACCEPT);
#endif
					} /* Ports match */
				} /* Proto matches */
			}  /* ALL/Specific */
		} /* IP addr/mask matches */
	} /* Loop */
	
	/*
	 * If we get here then none of the firewalls matched.
	 * So now we relay on policy defined by user-unmatched packet can
	 * be ever accepted or rejected...
	 */

#ifdef CONFIG_IP_FIREWALL_VERBOSE
	if (!(policy))
		goto bad_packet;
	return 1;
#else
	return(policy);
#endif

#ifdef CONFIG_IP_FIREWALL_VERBOSE
bad_packet:
	/*
	 * VERY ugly piece of code which actually
	 * makes kernel printf for denied packets...
	 */
	if (f->flags&IP_FW_F_PRN) 
	{
		printf("ip_fw_chk says no to ");
		switch(ip->protocol) 
		{
			case IPPROTO_TCP:
				printf("TCP ");
				break;
			case IPPROTO_UDP:
				printf("UDP ");
				break;
			case IPPROTO_ICMP:
				printf("ICMP:%d ",((char *)portptr)[0]&0xff);
				break;
			default:
				printf("p=%d ",ip->protocol);
				break;
		}
		print_ip(ip->saddr);
		if (ip->protocol==IPPROTO_TCP || ip->protocol==IPPROTO_UDP) 
		{
			printf(":%d ",ntohs(portptr[0]));
		}
		else
		{
			printf("\n");
		}
		print_ip(ip->daddr);
		if ( ip->protocol == IPPROTO_TCP || ip->protocol == IPPROTO_UDP ) 
		{
			printf(":%d ",ntohs(portptr[1]));
		}
		printf("\n");
	}
	return(0);
#endif
}
#endif /* CONFIG_IP_FIREWALL */




#ifdef CONFIG_IP_ACCT
void ip_acct_cnt(struct iphdr *ip,struct ip_fw *chain)
{
	unsigned long src, dst;
	char got_proto=0,rev=0;
	int frwl_proto, proto=0;
	struct ip_fw *f;
	unsigned short src_port=0, dst_port=0;
	unsigned short *portptr=(unsigned short *)&(((u_int *)ip)[ip->ihl]);

	if (!chain) 
		return;     

	src = ip->saddr;
	dst = ip->daddr;

	for (f=chain;f;f=f->next) 
	{
		if ((src&f->src_mask.s_addr)==f->src.s_addr
			&&  (dst&f->dst_mask.s_addr)==f->dst.s_addr) 
		{
			rev=0;
			goto addr_match;
		}
	 	if  ((f->flags&IP_FW_F_BIDIR) &&
		    ((src&f->src_mask.s_addr)==f->dst.s_addr
		&&  (dst&f->dst_mask.s_addr)==f->src.s_addr)) 
		{ 
			rev=1;
			goto addr_match;
		}
		continue;
addr_match:
		frwl_proto=f->flags&IP_FW_F_KIND;
		if (frwl_proto==IP_FW_F_ALL) 
		{
			/*	Universal frwl - we've got a match! */
     			f->p_cnt++;	/*	Rise packet count */

			/*
			 *	Rise byte count, convert from host to network byte order.
		     	 */
		     	 
			f->b_cnt+=ntohs(ip->tot_len);
		}
		else
		{
			/*
			 *	Specific firewall - packet's protocol must match firewall's
			 */
			 
			if (!got_proto) 
			{
				/*
 				 *	We still had not determined the protocol
				 *	of this packet,now the time to do so.
				 */
				switch(ip->protocol) 
				{
				    	case IPPROTO_TCP:
						/*
						 *	First two shorts in TCP are src/dst ports
						 */
						proto=IP_FW_F_TCP;
						src_port=ntohs(portptr[0]);
						dst_port=ntohs(portptr[1]);
						break;
	    				case IPPROTO_UDP:
						/*
						 * First two shorts in UDP are src/dst ports
						 */
						proto = IP_FW_F_UDP;
						src_port = ntohs(portptr[0]);
						dst_port = ntohs(portptr[1]);
						break;
	    				case IPPROTO_ICMP:
						proto=IP_FW_F_ICMP;
						break;
					default:
						proto=IP_FW_F_ALL;
				}
				got_proto=1;
			} 
			/*
			 * At this moment we surely know the protocol of this
			 * packet and we'll check if it matches,then proceed further..
			 */
			if (proto==frwl_proto) 
			{

				if ((proto==IP_FW_F_ICMP ||
					(port_match(&f->ports[0],f->n_src_p,src_port,
					f->flags&IP_FW_F_SRNG) &&
					port_match(&f->ports[f->n_src_p],f->n_dst_p,dst_port,
					f->flags&IP_FW_F_DRNG)))
					|| ((rev)   
						&& (port_match(&f->ports[0],f->n_src_p,dst_port,
                        	                f->flags&IP_FW_F_SRNG)
						&& port_match(&f->ports[f->n_src_p],f->n_dst_p,src_port,
	                       	                f->flags&IP_FW_F_DRNG))))
				{
					f->p_cnt++;                   /* Rise packet count */
					/*
					 * Rise byte count, convert from host to network byte order.
					 */
					f->b_cnt+=ntohs(ip->tot_len);
				} /* Ports match */
			} /* Proto matches */
		}  /* ALL/Specific */
	} /* IP addr/mask matches */
} /* End of whole function */
#endif /* CONFIG_IP_ACCT */

#if defined(CONFIG_IP_ACCT) || defined(CONFIG_IP_FIREWALL)

static void zero_fw_chain(struct ip_fw *chainptr)
{
	struct ip_fw *ctmp=chainptr;
	while(ctmp) 
	{
		ctmp->p_cnt=0l;
		ctmp->b_cnt=0l;
		ctmp=ctmp->next;
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
		*chainptr = ftmp->next;
		kfree_s(ftmp,sizeof(*ftmp));
	}
	restore_flags(flags);
}

/* Volatiles to keep some of the compiler versions amused */

static int add_to_chain(struct ip_fw *volatile* chainptr, struct ip_fw *frwl)
{
	struct ip_fw *ftmp;
	struct ip_fw *chtmp=NULL;
	struct ip_fw *volatile chtmp_prev=NULL;
	unsigned long flags;
	unsigned long m_src_mask,m_dst_mask;
	unsigned long n_sa,n_da,o_sa,o_da,o_sm,o_dm,n_sm,n_dm;
	unsigned short n_sr,n_dr,o_sr,o_dr; 
	unsigned short oldkind,newkind;
	int addb4=0;
	int n_o,n_n;
	
	save_flags(flags);
	
	ftmp = kmalloc( sizeof(struct ip_fw), GFP_ATOMIC );
	if ( ftmp == NULL ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printf("ip_fw_ctl:  malloc said no\n");
#endif
		return( ENOSPC );
	}

	memcpy(ftmp, frwl, sizeof( struct ip_fw ) );
	
	ftmp->p_cnt=0L;
	ftmp->b_cnt=0L;

	ftmp->next = NULL;

	cli();
	
	if (*chainptr==NULL)
	{
		*chainptr=ftmp;
	}
	else
	{
		chtmp_prev=NULL;
		for (chtmp=*chainptr;chtmp!=NULL;chtmp=chtmp->next) 
		{
			addb4=0;
			newkind=ftmp->flags & IP_FW_F_KIND;
			oldkind=chtmp->flags & IP_FW_F_KIND;
	
			if (newkind!=IP_FW_F_ALL 
				&&  oldkind!=IP_FW_F_ALL
				&&  oldkind!=newkind) 
			{
				chtmp_prev=chtmp;
				continue;
			}

			/*
			 *	Very very *UGLY* code...
			 *	Sorry,but i had to do this....
			 */

			n_sa=ntohl(ftmp->src.s_addr);
			n_da=ntohl(ftmp->dst.s_addr);
			n_sm=ntohl(ftmp->src_mask.s_addr);
			n_dm=ntohl(ftmp->dst_mask.s_addr);

			o_sa=ntohl(chtmp->src.s_addr);
			o_da=ntohl(chtmp->dst.s_addr);
			o_sm=ntohl(chtmp->src_mask.s_addr);
			o_dm=ntohl(chtmp->dst_mask.s_addr);

			m_src_mask = o_sm & n_sm;
			m_dst_mask = o_dm & n_dm;

			if ((o_sa & m_src_mask) == (n_sa & m_src_mask)) 
			{
				if (n_sm > o_sm) 
					addb4++;
				if (n_sm < o_sm) 
					addb4--;
			}
		
			if ((o_da & m_dst_mask) == (n_da & m_dst_mask)) 
			{
				if (n_dm > o_dm)
					addb4++;
				if (n_dm < o_dm)
					addb4--;
			}

			if (((o_da & o_dm) == (n_da & n_dm))
               			&&((o_sa & o_sm) == (n_sa & n_sm)))
			{
				if (newkind!=IP_FW_F_ALL &&
					oldkind==IP_FW_F_ALL)
					addb4++;
				if (newkind==oldkind && (oldkind==IP_FW_F_TCP
					||  oldkind==IP_FW_F_UDP)) 
				{
	
					/*
					 * 	Here the main idea is to check the size
					 * 	of port range which the frwl covers
					 * 	We actually don't check their values but
					 *	just the wideness of range they have
					 *	so that less wide ranges or single ports
					 *	go first and wide ranges go later. No ports
					 *	at all treated as a range of maximum number
					 *	of ports.
					 */

					if (ftmp->flags & IP_FW_F_SRNG) 
						n_sr=ftmp->ports[1]-ftmp->ports[0];
					else 
						n_sr=(ftmp->n_src_p)?ftmp->n_src_p : 0xFFFF;
						
					if (chtmp->flags & IP_FW_F_SRNG) 
						o_sr=chtmp->ports[1]-chtmp->ports[0];
					else 
						o_sr=(chtmp->n_src_p)?chtmp->n_src_p : 0xFFFF;

					if (n_sr<o_sr)
						addb4++;
					if (n_sr>o_sr)
						addb4--;
					
					n_n=ftmp->n_src_p;
					n_o=chtmp->n_src_p;
	
					/*
					 * Actually this cannot happen as the frwl control
					 * procedure checks for number of ports in source and
					 * destination range but we will try to be more safe.
					 */
					 
					if ((n_n>(IP_FW_MAX_PORTS-2)) ||
						(n_o>(IP_FW_MAX_PORTS-2)))
						goto skip_check;

					if (ftmp->flags & IP_FW_F_DRNG) 
					       n_dr=ftmp->ports[n_n+1]-ftmp->ports[n_n];
					else 
					       n_dr=(ftmp->n_dst_p)? ftmp->n_dst_p : 0xFFFF;

					if (chtmp->flags & IP_FW_F_DRNG) 
						o_dr=chtmp->ports[n_o+1]-chtmp->ports[n_o];
					else 
						o_dr=(chtmp->n_dst_p)? chtmp->n_dst_p : 0xFFFF;
					if (n_dr<o_dr)
						addb4++;
					if (n_dr>o_dr)
						addb4--;
skip_check:
				}
			}
			if (addb4>0) 
			{
				if (chtmp_prev) 
				{
					chtmp_prev->next=ftmp; 
					ftmp->next=chtmp;
				} 
				else 
				{
					*chainptr=ftmp;
					ftmp->next=chtmp;
				}
				restore_flags(flags);
				return 0;
			}
			chtmp_prev=chtmp;
		}
	}
	
	if (chtmp_prev)
		chtmp_prev->next=ftmp;
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
		printf("ip_fw_ctl:  chain is empty\n");
#endif
		restore_flags(flags);
		return( EINVAL );
	}

	ltmp=NULL;
	was_found=0;

	while( ftmp != NULL )
	{
		matches=1;
		if ((memcmp(&ftmp->src,&frwl->src,sizeof(struct in_addr))) 
     			|| (memcmp(&ftmp->src_mask,&frwl->src_mask,sizeof(struct in_addr)))
     			|| (memcmp(&ftmp->dst,&frwl->dst,sizeof(struct in_addr)))
     			|| (memcmp(&ftmp->dst_mask,&frwl->dst_mask,sizeof(struct in_addr)))
     			|| (ftmp->flags!=frwl->flags))
        		matches=0;

		tport1=ftmp->n_src_p+ftmp->n_dst_p;
		tport2=frwl->n_src_p+frwl->n_dst_p;
		if (tport1!=tport2)
		        matches=0;
		else if (tport1!=0)
		{
			for (tmpnum=0;tmpnum < tport1 && tmpnum < IP_FW_MAX_PORTS;tmpnum++)
        		if (ftmp->ports[tmpnum]!=frwl->ports[tmpnum])
        			matches=0;
		}
		if(matches)
		{
			was_found=1;
			if (ltmp)
			{
				ltmp->next=ftmp->next;
				kfree_s(ftmp,sizeof(*ftmp));
				ftmp=ltmp->next;
        		}
      			else
      			{
      				*chainptr=ftmp->next; 
	 			kfree_s(ftmp,sizeof(*ftmp));
				ftmp=*chainptr;
			}       
		}
		else
		{
			ltmp = ftmp;
			ftmp = ftmp->next;
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
		printf("ip_fw_ctl: len=%d, want %d\n",m->m_len,
					sizeof(struct ip_fw));
#endif
		return(NULL);
	}

	if ( (frwl->flags & ~IP_FW_F_MASK) != 0 )
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printf("ip_fw_ctl: undefined flag bits set (flags=%x)\n",
			frwl->flags);
#endif
		return(NULL);
	}

	if ( (frwl->flags & IP_FW_F_SRNG) && frwl->n_src_p < 2 ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printf("ip_fw_ctl: src range set but n_src_p=%d\n",
			frwl->n_src_p);
#endif
		return(NULL);
	}

	if ( (frwl->flags & IP_FW_F_DRNG) && frwl->n_dst_p < 2 ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printf("ip_fw_ctl: dst range set but n_dst_p=%d\n",
			frwl->n_dst_p);
#endif
		return(NULL);
	}

	if ( frwl->n_src_p + frwl->n_dst_p > IP_FW_MAX_PORTS ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printf("ip_fw_ctl: too many ports (%d+%d)\n",
			frwl->n_src_p,frwl->n_dst_p);
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
	if ( stage == IP_ACCT_ADD
	  || stage == IP_ACCT_DEL
	   )
	{
		struct ip_fw *frwl;

		if (!(frwl=check_ipfw_struct(m,len)))
			return (EINVAL);

		switch (stage) 
		{
			case IP_ACCT_ADD:
				return( add_to_chain(&ip_acct_chain,frwl));
		    	case IP_ACCT_DEL:
				return( del_from_chain(&ip_acct_chain,frwl));
			default:
				/*
 				 *	Should be panic but... (Why ??? - AC)
				 */
#ifdef DEBUG_CONFIG_IP_FIREWALL
				printf("ip_acct_ctl:  unknown request %d\n",stage);
#endif
				return(EINVAL);
		}
	}
#ifdef DEBUG_CONFIG_IP_FIREWALL
	printf("ip_acct_ctl:  unknown request %d\n",stage);
#endif
	return(EINVAL);
}
#endif

#ifdef CONFIG_IP_FIREWALL
int ip_fw_ctl(int stage, void *m, int len)
{
	if ( stage == IP_FW_FLUSH_BLK )
	{
		free_fw_chain(&ip_fw_blk_chain);
		return(0);
	}  

	if ( stage == IP_FW_FLUSH_FWD )
	{
		free_fw_chain(&ip_fw_fwd_chain);
		return(0);
	}  

	if ( stage == IP_FW_ZERO_BLK )
	{
		zero_fw_chain(ip_fw_blk_chain);
		return(0);
	}  

	if ( stage == IP_FW_ZERO_FWD )
	{
		zero_fw_chain(ip_fw_fwd_chain);
		return(0);
	}  

	if ( stage == IP_FW_POLICY_BLK || stage == IP_FW_POLICY_FWD )
	{
		int *tmp_policy_ptr;
		tmp_policy_ptr=(int *)m;
		if ((*tmp_policy_ptr)!=1 && (*tmp_policy_ptr)!=0)
			return (EINVAL);
		if ( stage == IP_FW_POLICY_BLK )
			ip_fw_blk_policy=*tmp_policy_ptr;
		else
			ip_fw_fwd_policy=*tmp_policy_ptr;
		return 0;
	}

	if ( stage == IP_FW_CHK_BLK || stage == IP_FW_CHK_FWD )
	{
		struct iphdr *ip;

		if ( len < sizeof(struct iphdr) + 2 * sizeof(unsigned short) )
		{
#ifdef DEBUG_CONFIG_IP_FIREWALL
			printf("ip_fw_ctl: len=%d, want at least %d\n",
				len,sizeof(struct ip) + 2 * sizeof(unsigned short));
#endif
			return( EINVAL );
		}

	 	ip = (struct iphdr *)m;

		if ( ip->ihl != sizeof(struct iphdr) / sizeof(int))
		{
#ifdef DEBUG_CONFIG_IP_FIREWALL
			printf("ip_fw_ctl: ip->ihl=%d, want %d\n",ip->ihl,
					sizeof(struct ip)/sizeof(int));
#endif
			return(EINVAL);
		}

		if ( ip_fw_chk(ip,
			stage == IP_FW_CHK_BLK ?
	                ip_fw_blk_chain : ip_fw_fwd_chain,
			stage == IP_FW_CHK_BLK ?
	                ip_fw_blk_policy : ip_fw_fwd_policy )
		       ) 
			return(0);
	    	else	
			return(EACCES);
	}

/*
 *	Here we really working hard-adding new elements
 *	to blocking/forwarding chains or deleting 'em
 */

	if ( stage == IP_FW_ADD_BLK || stage == IP_FW_ADD_FWD
		|| stage == IP_FW_DEL_BLK || stage == IP_FW_DEL_FWD
		)
	{
		struct ip_fw *frwl;
		frwl=check_ipfw_struct(m,len);
		if (frwl==NULL)
			return (EINVAL);
		
		switch (stage) 
		{
			case IP_FW_ADD_BLK:
				return(add_to_chain(&ip_fw_blk_chain,frwl));
			case IP_FW_ADD_FWD:
				return(add_to_chain(&ip_fw_fwd_chain,frwl));
			case IP_FW_DEL_BLK:
				return(del_from_chain(&ip_fw_blk_chain,frwl));
			case IP_FW_DEL_FWD: 
				return(del_from_chain(&ip_fw_fwd_chain,frwl));
			default:
			/*
	 		 *	Should be panic but... (Why are BSD people panic obsessed ??)
			 */
#ifdef DEBUG_CONFIG_IP_FIREWALL
				printf("ip_fw_ctl:  unknown request %d\n",stage);
#endif
				return(EINVAL);
		}
	} 

#ifdef DEBUG_CONFIG_IP_FIREWALL
	printf("ip_fw_ctl:  unknown request %d\n",stage);
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
	int len;
	

	switch(stage)
	{
#ifdef CONFIG_IP_FIREWALL
		case IP_INFO_BLK:
			i = ip_fw_blk_chain;
			len=sprintf(buffer, "IP firewall block rules, policy = %d\n",
				ip_fw_blk_policy);
			break;
		case IP_INFO_FWD:
			i = ip_fw_fwd_chain;
			len=sprintf(buffer, "IP firewall forward rules, policy = %d\n",
				ip_fw_fwd_policy);
			break;
#endif
#ifdef CONFIG_IP_ACCT
		case IP_INFO_ACCT:
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
		len+=sprintf(buffer+len,"%08lX/%08lX->%08lX/%08lX %X ",
			ntohl(i->src.s_addr),ntohl(i->src_mask.s_addr),
			ntohl(i->dst.s_addr),ntohl(i->dst_mask.s_addr),
			i->flags);
		len+=sprintf(buffer+len,"%u %u %lu %lu ",
			i->n_src_p,i->n_dst_p, i->p_cnt,i->b_cnt);
		len+=sprintf(buffer+len,"%u %u %u %u %u %u %u %u %u %u\n",
			i->ports[0],i->ports[1],i->ports[2],i->ports[3],	
			i->ports[4],i->ports[5],i->ports[6],i->ports[7],	
			i->ports[8],i->ports[9]);	
		pos=begin+len;
		if(pos<offset)
		{
			len=0;
			begin=pos;
		}
		else if(reset)
		{
			/* This needs to be done at this specific place! */
			i->p_cnt=0L;
			i->b_cnt=0L;
		}
		if(pos>offset+length)
			break;
		i=i->next;
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

int ip_acct_procinfo(char *buffer, char **start, off_t offset, int length, int reset)
{
	return ip_chain_procinfo(IP_INFO_ACCT,buffer,start,offset,length,reset);
}

#endif

#ifdef CONFIG_IP_FIREWALL

int ip_fw_blk_procinfo(char *buffer, char **start, off_t offset, int length, int reset)
{
	return ip_chain_procinfo(IP_INFO_BLK,buffer,start,offset,length,reset);
}

int ip_fw_fwd_procinfo(char *buffer, char **start, off_t offset, int length, int reset)
{
	return ip_chain_procinfo(IP_INFO_FWD,buffer,start,offset,length,reset);
}

#endif
