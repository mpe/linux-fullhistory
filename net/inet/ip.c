/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The Internet Protocol (IP) module.
 *
 * Version:	@(#)ip.c	1.0.16b	9/1/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald Becker, <becker@super.org>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *
 * Fixes:
 *		Alan Cox	:	Commented a couple of minor bits of surplus code
 *		Alan Cox	:	Undefining IP_FORWARD doesn't include the code
 *					(just stops a compiler warning).
 *		Alan Cox	:	Frames with >=MAX_ROUTE record routes, strict routes or loose routes
 *					are junked rather than corrupting things.
 *		Alan Cox	:	Frames to bad broadcast subnets are dumped
 *					We used to process them non broadcast and
 *					boy could that cause havoc.
 *		Alan Cox	:	ip_forward sets the free flag on the 
 *					new frame it queues. Still crap because
 *					it copies the frame but at least it 
 *					doesn't eat memory too.
 *		Alan Cox	:	Generic queue code and memory fixes.
 *		Fred Van Kempen :	IP fragment support (borrowed from NET2E)
 *		Gerhard Koerting:	Forward fragmented frames correctly.
 *		Gerhard Koerting: 	Fixes to my fix of the above 8-).
 *		Gerhard Koerting:	IP interface addressing fix.
 *		Linus Torvalds	:	More robustness checks
 *		Alan Cox	:	Even more checks: Still not as robust as it ought to be
 *		Alan Cox	:	Save IP header pointer for later
 *		Alan Cox	:	ip option setting
 *		Alan Cox	:	Use ip_tos/ip_ttl settings
 *		Alan Cox	:	Fragmentation bogosity removed
 *					(Thanks to Mark.Bush@prg.ox.ac.uk)
 *		Dmitry Gorodchanin :	Send of a raw packet crash fix.
 *		Alan Cox	:	Silly ip bug when an overlength
 *					fragment turns up. Now frees the
 *					queue.
 *		Linus Torvalds/ :	Memory leakage on fragmentation	
 *		Alan Cox	:	handling.
 *		Gerhard Koerting:	Forwarding uses IP priority hints
 *		Teemu Rantanen	:	Fragment problems.
 *		Alan Cox	:	General cleanup, comments and reformat
 *		Alan Cox	:	SNMP statistics
 *		Alan Cox	:	BSD address rule semantics. Also see
 *					UDP as there is a nasty checksum issue
 *					if you do things the wrong way.
 *
 * To Fix:
 *		IP option processing is mostly not needed. ip_forward needs to know about routing rules
 *		and time stamp but that's about all.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "snmp.h"
#include "ip.h"
#include "protocol.h"
#include "route.h"
#include "tcp.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "arp.h"
#include "icmp.h"

#define CONFIG_IP_FORWARD
#define CONFIG_IP_DEFRAG

extern int last_retran;
extern void sort_send(struct sock *sk);

#define min(a,b)	((a)<(b)?(a):(b))

/*
 *	SNMP management statistics
 */
 
struct ip_mib ip_statistics={1,64,};	/* Forwarding=Yes, Default TTL=64 */
 
/* 
 *	Print an IP packet for debugging purposes.
 *
 *	This function is exported for the IP
 *	upper layers to use also.
 */
 
void ip_print(const struct iphdr *ip)
{
	unsigned char buff[32];
	unsigned char *ptr;
	int addr;
	int len;
	int i;

	/* Are we debugging IP frames */
	
  	if (inet_debug != DBG_IP) 
  		return;

	/* Dump the IP header. */
	printk("IP: ihl=%d, version=%d, tos=%d, tot_len=%d\n",
		ip->ihl, ip->version, ip->tos, ntohs(ip->tot_len));
	printk("    id=%X, ttl=%d, prot=%d, check=%X\n",
		ip->id, ip->ttl, ip->protocol, ip->check);
	printk("    frag_off=%d\n", ip->frag_off);
	printk("    soucre=%s ", in_ntoa(ip->saddr));
	printk("dest=%s\n", in_ntoa(ip->daddr));
	printk("    ----\n");

	/* Dump the data. */
	ptr = (unsigned char *)(ip + 1);
	addr = 0;
	len = ntohs(ip->tot_len) - (4 * ip->ihl);

	while (len > 0) 
	{
		printk("    %04X: ", addr);
		for(i = 0; i < 16; i++) 
		{
			if (len > 0) 
			{
				printk("%02X ", (*ptr & 0xFF));
				buff[i] = *ptr++;
				if (buff[i] < 32 || buff[i] > 126) 
					buff[i] = '.';
			} 
			else 
			{
				printk("   ");
				buff[i] = ' ';
			}
			addr++;
			len--;
		};
		buff[i] = '\0';
		printk("  \"%s\"\n", buff);
  	}
  	printk("    ----\n\n");
}

/*
 *	Handle the issuing of an ioctl() request 
 *	for the ip device. This is scheduled to
 *	disappear
 */

int ip_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
  	switch(cmd) 
  	{
		case DDIOCSDBG:
			return(dbg_ioctl((void *) arg, DBG_IP));
		default:
			return(-EINVAL);
  	}
}


/* these two routines will do routing. */

static void
strict_route(struct iphdr *iph, struct options *opt)
{
}


static void
loose_route(struct iphdr *iph, struct options *opt)
{
}


static void
print_ipprot(struct inet_protocol *ipprot)
{
  DPRINTF((DBG_IP, "handler = %X, protocol = %d, copy=%d \n",
	   ipprot->handler, ipprot->protocol, ipprot->copy));
}


/* This routine will check to see if we have lost a gateway. */
void
ip_route_check(unsigned long daddr)
{
}


#if 0
/* this routine puts the options at the end of an ip header. */
static int
build_options(struct iphdr *iph, struct options *opt)
{
  unsigned char *ptr;
  /* currently we don't support any options. */
  ptr = (unsigned char *)(iph+1);
  *ptr = 0;
  return (4);
}
#endif


/*
 *	Take an skb, and fill in the MAC header. 
 */
 
static int ip_send(struct sk_buff *skb, unsigned long daddr, int len, struct device *dev, unsigned long saddr)
{
	int mac = 0;

	skb->dev = dev;
	skb->arp = 1;
	if (dev->hard_header) 
	{
  		/*
  		 *	Build a hardware header. Source address is our mac, destination unknown
  		 *  	(rebuild header will sort this out) 
  		 */
		mac = dev->hard_header(skb->data, dev, ETH_P_IP, NULL, NULL, len, skb);
		if (mac < 0) 
		{
			mac = -mac;
			skb->arp = 0;
			skb->raddr = daddr;	/* next routing address */
		}	
	}
	return mac;
}


/*
 * This routine builds the appropriate hardware/IP headers for
 * the routine.  It assumes that if *dev != NULL then the
 * protocol knows what it's doing, otherwise it uses the
 * routing/ARP tables to select a device struct.
 */
int ip_build_header(struct sk_buff *skb, unsigned long saddr, unsigned long daddr,
		struct device **dev, int type, struct options *opt, int len, int tos, int ttl)
{
  	static struct options optmem;
  	struct iphdr *iph;
  	struct rtable *rt;
  	unsigned char *buff;
  	unsigned long raddr;
  	static int count = 0;
  	int tmp;
  	unsigned long src;

	/*
	 *	If there is no 'from' address as yet, then make it our loopback
	 */
	 
  	if (saddr == 0) 
  		saddr = ip_my_addr();
 	
	DPRINTF((DBG_IP, "ip_build_header (skb=%X, saddr=%X, daddr=%X, *dev=%X,\n"
		   "                 type=%d, opt=%X, len = %d)\n",
		   skb, saddr, daddr, *dev, type, opt, len));
	   
	buff = skb->data;

	/* 
	 *	See if we need to look up the device. 
	 */
	 
	if (*dev == NULL) 
	{
		if(skb->localroute)
			rt = ip_rt_local(daddr, &optmem, &src);
		else
			rt = ip_rt_route(daddr, &optmem, &src);
		if (rt == NULL) 
		{
			ip_statistics.IpOutNoRoutes++;
			return(-ENETUNREACH);
		}
	
		*dev = rt->rt_dev;
		/*
		 *	If the frame is from us and going off machine it MUST MUST MUST
		 *	have the output device ip address and never the loopback
		 */
		if (saddr == 0x0100007FL && daddr != 0x0100007FL) 
			saddr = src;/*rt->rt_dev->pa_addr;*/
		raddr = rt->rt_gateway;

		DPRINTF((DBG_IP, "ip_build_header: saddr set to %s\n", in_ntoa(saddr)));
		opt = &optmem;
	} 
	else 
	{
		/* 
		 *	We still need the address of the first hop. 
		 */
		if(skb->localroute)
			rt = ip_rt_local(daddr, &optmem, &src);
		else
			rt = ip_rt_route(daddr, &optmem, &src);
		/*
		 *	If the frame is from us and going off machine it MUST MUST MUST
		 *	have the output device ip address and never the loopback
		 */
		if (saddr == 0x0100007FL && daddr != 0x0100007FL) 
			saddr = src;/*rt->rt_dev->pa_addr;*/

		raddr = (rt == NULL) ? 0 : rt->rt_gateway;
	}
  
	  /*
	   *	No gateway so aim at the real destination
	   */
  	if (raddr == 0)
  		raddr = daddr;

  	/* 
  	 *	Now build the MAC header. 
  	 */
  	 
  	tmp = ip_send(skb, raddr, len, *dev, saddr);
  	buff += tmp;
	len -= tmp;

	/*
	 *	Book keeping
	 */

	skb->dev = *dev;
	skb->saddr = saddr;
	if (skb->sk) 
		skb->sk->saddr = saddr;

	/*
	 *	Now build the IP header. 
	 */

  	/* 
  	 *	If we are using IPPROTO_RAW, then we don't need an IP header, since
     	 *	one is being supplied to us by the user 
     	 */

  	if(type == IPPROTO_RAW) 
  		return (tmp);

	iph = (struct iphdr *)buff;
	iph->version  = 4;
	iph->tos      = tos;
	iph->frag_off = 0;
	iph->ttl      = ttl;
	iph->daddr    = daddr;
	iph->saddr    = saddr;
	iph->protocol = type;
	iph->ihl      = 5;
	iph->id       = htons(count++);

	/* Setup the IP options. */
#ifdef Not_Yet_Avail
	build_options(iph, opt);
#endif

	return(20 + tmp);	/* IP header plus MAC header size */
}


static int
do_options(struct iphdr *iph, struct options *opt)
{
  unsigned char *buff;
  int done = 0;
  int i, len = sizeof(struct iphdr);

  /* Zero out the options. */
  opt->record_route.route_size = 0;
  opt->loose_route.route_size  = 0;
  opt->strict_route.route_size = 0;
  opt->tstamp.ptr              = 0;
  opt->security                = 0;
  opt->compartment             = 0;
  opt->handling                = 0;
  opt->stream                  = 0;
  opt->tcc                     = 0;
  return(0);

  /* Advance the pointer to start at the options. */
  buff = (unsigned char *)(iph + 1);

  /* Now start the processing. */
  while (!done && len < iph->ihl*4) switch(*buff) {
	case IPOPT_END:
		done = 1;
		break;
	case IPOPT_NOOP:
		buff++;
		len++;
		break;
	case IPOPT_SEC:
		buff++;
		if (*buff != 11) return(1);
		buff++;
		opt->security = ntohs(*(unsigned short *)buff);
		buff += 2;
		opt->compartment = ntohs(*(unsigned short *)buff);
		buff += 2;
		opt->handling = ntohs(*(unsigned short *)buff);
		buff += 2;
	  	opt->tcc = ((*buff) << 16) + ntohs(*(unsigned short *)(buff+1));
	  	buff += 3;
	  	len += 11;
	  	break;
	case IPOPT_LSRR:
		buff++;
		if ((*buff - 3)% 4 != 0) return(1);
		len += *buff;
		opt->loose_route.route_size = (*buff -3)/4;
		buff++;
		if (*buff % 4 != 0) return(1);
		opt->loose_route.pointer = *buff/4 - 1;
		buff++;
		buff++;
		for (i = 0; i < opt->loose_route.route_size; i++) {
			if(i>=MAX_ROUTE)
				return(1);
			opt->loose_route.route[i] = *(unsigned long *)buff;
			buff += 4;
		}
		break;
	case IPOPT_SSRR:
		buff++;
		if ((*buff - 3)% 4 != 0) return(1);
		len += *buff;
		opt->strict_route.route_size = (*buff -3)/4;
		buff++;
		if (*buff % 4 != 0) return(1);
		opt->strict_route.pointer = *buff/4 - 1;
		buff++;
		buff++;
		for (i = 0; i < opt->strict_route.route_size; i++) {
			if(i>=MAX_ROUTE)
				return(1);
			opt->strict_route.route[i] = *(unsigned long *)buff;
			buff += 4;
		}
		break;
	case IPOPT_RR:
		buff++;
		if ((*buff - 3)% 4 != 0) return(1);
		len += *buff;
		opt->record_route.route_size = (*buff -3)/4;
		buff++;
		if (*buff % 4 != 0) return(1);
		opt->record_route.pointer = *buff/4 - 1;
		buff++;
		buff++;
		for (i = 0; i < opt->record_route.route_size; i++) {
			if(i>=MAX_ROUTE)
				return 1;
			opt->record_route.route[i] = *(unsigned long *)buff;
			buff += 4;
		}
		break;
	case IPOPT_SID:
		len += 4;
		buff +=2;
		opt->stream = *(unsigned short *)buff;
		buff += 2;
		break;
	case IPOPT_TIMESTAMP:
		buff++;
		len += *buff;
		if (*buff % 4 != 0) return(1);
		opt->tstamp.len = *buff / 4 - 1;
		buff++;
		if ((*buff - 1) % 4 != 0) return(1);
		opt->tstamp.ptr = (*buff-1)/4;
		buff++;
		opt->tstamp.x.full_char = *buff;
		buff++;
		for (i = 0; i < opt->tstamp.len; i++) {
			opt->tstamp.data[i] = *(unsigned long *)buff;
			buff += 4;
		}
		break;
	default:
		return(1);
  }

  if (opt->record_route.route_size == 0) {
	if (opt->strict_route.route_size != 0) {
		memcpy(&(opt->record_route), &(opt->strict_route),
					     sizeof(opt->record_route));
	} else if (opt->loose_route.route_size != 0) {
		memcpy(&(opt->record_route), &(opt->loose_route),
					     sizeof(opt->record_route));
	}
  }

  if (opt->strict_route.route_size != 0 &&
      opt->strict_route.route_size != opt->strict_route.pointer) {
	strict_route(iph, opt);
	return(0);
  }

  if (opt->loose_route.route_size != 0 &&
      opt->loose_route.route_size != opt->loose_route.pointer) {
	loose_route(iph, opt);
	return(0);
  }

  return(0);
}

/* 
 *	This is a version of ip_compute_csum() optimized for IP headers, which
 *	always checksum on 4 octet boundaries. 
 */
 
static inline unsigned short ip_fast_csum(unsigned char * buff, int wlen)
{
	unsigned long sum = 0;

    	if (wlen) 
    	{
    	unsigned long bogus;
	 __asm__("clc\n"
		"1:\t"
		"lodsl\n\t"
		"adcl %3, %0\n\t"
		"decl %2\n\t"
		"jne 1b\n\t"
		"adcl $0, %0\n\t"
		"movl %0, %3\n\t"
		"shrl $16, %3\n\t"
		"addw %w3, %w0\n\t"
		"adcw $0, %w0"
	    : "=r" (sum), "=S" (buff), "=r" (wlen), "=a" (bogus)
	    : "0"  (sum),  "1" (buff),  "2" (wlen));
    	}
	return (~sum) & 0xffff;
}

/*
 * This routine does all the checksum computations that don't
 * require anything special (like copying or special headers).
 */

unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	unsigned long sum = 0;

	/* Do the first multiple of 4 bytes and convert to 16 bits. */
	if (len > 3) 
	{
		__asm__("clc\n"
	        "1:\t"
	    	"lodsl\n\t"
	    	"adcl %%eax, %%ebx\n\t"
	    	"loop 1b\n\t"
	    	"adcl $0, %%ebx\n\t"
	    	"movl %%ebx, %%eax\n\t"
	    	"shrl $16, %%eax\n\t"
	    	"addw %%ax, %%bx\n\t"
	    	"adcw $0, %%bx"
	        : "=b" (sum) , "=S" (buff)
	        : "0" (sum), "c" (len >> 2) ,"1" (buff)
	        : "ax", "cx", "si", "bx" );
  	}
  	if (len & 2) 
  	{
		__asm__("lodsw\n\t"
	    	"addw %%ax, %%bx\n\t"
	    	"adcw $0, %%bx"
	        : "=b" (sum), "=S" (buff)
	        : "0" (sum), "1" (buff)
	        : "bx", "ax", "si");
  	}
  	if (len & 1) 
  	{
		__asm__("lodsb\n\t"
	    	"movb $0, %%ah\n\t"
	    	"addw %%ax, %%bx\n\t"
	    	"adcw $0, %%bx"
	        : "=b" (sum), "=S" (buff)
	        : "0" (sum), "1" (buff)
	        : "bx", "ax", "si");
  	}
  	sum =~sum;
  	return(sum & 0xffff);
}

/* 
 *	Check the header of an incoming IP datagram.  This version is still used in slhc.c. 
 */
 
int ip_csum(struct iphdr *iph)
{
  	return ip_fast_csum((unsigned char *)iph, iph->ihl);
}

/* 
 *	Generate a checksym for an outgoing IP datagram. 
 */

static void ip_send_check(struct iphdr *iph)
{
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
}

/************************ Fragment Handlers From NET2E not yet with tweaks to beat 4K **********************************/


/*
 *	This fragment handler is a bit of a heap. On the other hand it works quite
 *	happily and handles things quite well.
 */
 
static struct ipq *ipqueue = NULL;		/* IP fragment queue	*/

/*
 *	Create a new fragment entry. 
 */
 
static struct ipfrag *ip_frag_create(int offset, int end, struct sk_buff *skb, unsigned char *ptr)
{
   	struct ipfrag *fp;
 
   	fp = (struct ipfrag *) kmalloc(sizeof(struct ipfrag), GFP_ATOMIC);
   	if (fp == NULL) 
   	{
	 	printk("IP: frag_create: no memory left !\n");
	 	return(NULL);
   	}
  	memset(fp, 0, sizeof(struct ipfrag));

        /* Fill in the structure. */
	fp->offset = offset;
	fp->end = end;
	fp->len = end - offset;
	fp->skb = skb;
	fp->ptr = ptr;
 
	return(fp);
}
 
 
/*
 *	Find the correct entry in the "incomplete datagrams" queue for
 *	this IP datagram, and return the queue entry address if found.
 */
 
static struct ipq *ip_find(struct iphdr *iph)
{
	struct ipq *qp;
	struct ipq *qplast;
 
	cli();
	qplast = NULL;
	for(qp = ipqueue; qp != NULL; qplast = qp, qp = qp->next) 
	{
 		if (iph->id== qp->iph->id && iph->saddr == qp->iph->saddr &&
			iph->daddr == qp->iph->daddr && iph->protocol == qp->iph->protocol) 
		{
			del_timer(&qp->timer);	/* So it doesn't vanish on us. The timer will be reset anyway */
 			sti();
 			return(qp);
 		}
   	}
	sti();
	return(NULL);
}
 
 
/*
 *	Remove an entry from the "incomplete datagrams" queue, either
 *	because we completed, reassembled and processed it, or because
 *	it timed out.
 */

static void ip_free(struct ipq *qp)
{
	struct ipfrag *fp;
	struct ipfrag *xp;

	/*
	 * Stop the timer for this entry. 
	 */
	 
	del_timer(&qp->timer);

	/* Remove this entry from the "incomplete datagrams" queue. */
	cli();
	if (qp->prev == NULL) 
	{
	 	ipqueue = qp->next;
	 	if (ipqueue != NULL) 
	 		ipqueue->prev = NULL;
   	} 
   	else 
   	{
 		qp->prev->next = qp->next;
 		if (qp->next != NULL) 
 			qp->next->prev = qp->prev;
   	}
 
   	/* Release all fragment data. */

   	fp = qp->fragments;
   	while (fp != NULL) 
   	{
 		xp = fp->next;
 		IS_SKB(fp->skb);
 		kfree_skb(fp->skb,FREE_READ);
 		kfree_s(fp, sizeof(struct ipfrag));
 		fp = xp;
   	}
   	
   	/* Release the MAC header. */
   	kfree_s(qp->mac, qp->maclen);
 
   	/* Release the IP header. */
   	kfree_s(qp->iph, qp->ihlen + 8);
 
   	/* Finally, release the queue descriptor itself. */
   	kfree_s(qp, sizeof(struct ipq));
/*   	printk("ip_free:done\n");*/
   	sti();
 }
 
 
/*
 *	Oops- a fragment queue timed out.  Kill it and send an ICMP reply. 
 */
 
static void ip_expire(unsigned long arg)
{
   	struct ipq *qp;
 
   	qp = (struct ipq *)arg;
   	DPRINTF((DBG_IP, "IP: queue_expire: fragment queue 0x%X timed out!\n", qp));
 
   	/*
   	 *	Send an ICMP "Fragment Reassembly Timeout" message. 
   	 */

	ip_statistics.IpReasmTimeout++;
	ip_statistics.IpReasmFails++;   	 
   	/* This if is always true... shrug */
 	if(qp->fragments!=NULL)
 		icmp_send(qp->fragments->skb,ICMP_TIME_EXCEEDED,
 				ICMP_EXC_FRAGTIME, qp->dev);
 
   	/* 
   	 *	Nuke the fragment queue. 
   	 */
	ip_free(qp);
}
 
 
/*
 * 	Add an entry to the 'ipq' queue for a newly received IP datagram.
 * 	We will (hopefully :-) receive all other fragments of this datagram
 * 	in time, so we just create a queue for this datagram, in which we
 * 	will insert the received fragments at their respective positions.
 */

static struct ipq *ip_create(struct sk_buff *skb, struct iphdr *iph, struct device *dev)
{
  	struct ipq *qp;
  	int maclen;
  	int ihlen;

  	qp = (struct ipq *) kmalloc(sizeof(struct ipq), GFP_ATOMIC);
  	if (qp == NULL) 
  	{
		printk("IP: create: no memory left !\n");
		return(NULL);
   	skb->dev = qp->dev;
  	}
 	memset(qp, 0, sizeof(struct ipq));

  	/*
  	 *	Allocate memory for the MAC header. 
  	 *
  	 *	FIXME: We have a maximum MAC address size limit and define 
  	 *	elsewhere. We should use it here and avoid the 3 kmalloc() calls
  	 */
  	 
  	maclen = ((unsigned long) iph) - ((unsigned long) skb->data);
  	qp->mac = (unsigned char *) kmalloc(maclen, GFP_ATOMIC);
  	if (qp->mac == NULL) 
  	{
		printk("IP: create: no memory left !\n");
		kfree_s(qp, sizeof(struct ipq));
		return(NULL);
  	}

  	/* 
  	 *	Allocate memory for the IP header (plus 8 octects for ICMP). 
  	 */
  	 
  	ihlen = (iph->ihl * sizeof(unsigned long));
  	qp->iph = (struct iphdr *) kmalloc(ihlen + 8, GFP_ATOMIC);
  	if (qp->iph == NULL) 
  	{
		printk("IP: create: no memory left !\n");
		kfree_s(qp->mac, maclen);
		kfree_s(qp, sizeof(struct ipq));
		return(NULL);
  	}

  	/* Fill in the structure. */
  	memcpy(qp->mac, skb->data, maclen);
 	memcpy(qp->iph, iph, ihlen + 8);
  	qp->len = 0;
  	qp->ihlen = ihlen;
  	qp->maclen = maclen;
  	qp->fragments = NULL;
  	qp->dev = dev;
	
  	/* Start a timer for this entry. */
  	qp->timer.expires = IP_FRAG_TIME;		/* about 30 seconds	*/
  	qp->timer.data = (unsigned long) qp;		/* pointer to queue	*/
  	qp->timer.function = ip_expire;			/* expire function	*/
  	add_timer(&qp->timer);

  	/* Add this entry to the queue. */
  	qp->prev = NULL;
  	cli();
  	qp->next = ipqueue;
  	if (qp->next != NULL) 
  		qp->next->prev = qp;
  	ipqueue = qp;
  	sti();
  	return(qp);
}
 
 
/*
 *	See if a fragment queue is complete. 
 */
 
static int ip_done(struct ipq *qp)
{
	struct ipfrag *fp;
	int offset;
 
   	/* Only possible if we received the final fragment. */
   	if (qp->len == 0) 
   		return(0);
 
   	/* Check all fragment offsets to see if they connect. */
  	fp = qp->fragments;
   	offset = 0;
   	while (fp != NULL) 
   	{
 		if (fp->offset > offset) 
 			return(0);	/* fragment(s) missing */
 		offset = fp->end;
 		fp = fp->next;
   	}
 
   	/* All fragments are present. */
   	return(1);
 }
 
 
/* 
 *	Build a new IP datagram from all its fragments. 
 *
 *	FIXME: We copy here because we lack an effective way of handling lists
 *	of bits on input. Until the new skb data handling is in I'm not going
 *	to touch this with a bargepole. This also causes a 4Kish limit on
 *	packet sizes.
 */
 
static struct sk_buff *ip_glue(struct ipq *qp)
{
	struct sk_buff *skb;
   	struct iphdr *iph;
   	struct ipfrag *fp;
   	unsigned char *ptr;
   	int count, len;
 
   	/*
   	 *	Allocate a new buffer for the datagram. 
   	 */
   	 
   	len = qp->maclen + qp->ihlen + qp->len;

   	if ((skb = alloc_skb(len,GFP_ATOMIC)) == NULL) 
   	{
   		ip_statistics.IpReasmFails++;
 		printk("IP: queue_glue: no memory for glueing queue 0x%X\n", (int) qp);
 		ip_free(qp);
 		return(NULL);
   	}
 
   	/* Fill in the basic details. */
   	skb->len = (len - qp->maclen);
   	skb->h.raw = skb->data;
   	skb->free = 1;
 
   	/* Copy the original MAC and IP headers into the new buffer. */
   	ptr = (unsigned char *) skb->h.raw;
   	memcpy(ptr, ((unsigned char *) qp->mac), qp->maclen);
   	ptr += qp->maclen;
   	memcpy(ptr, ((unsigned char *) qp->iph), qp->ihlen);
   	ptr += qp->ihlen;
   	skb->h.raw += qp->maclen;
   	
   	count = 0;
 
   	/* Copy the data portions of all fragments into the new buffer. */
   	fp = qp->fragments;
   	while(fp != NULL) 
   	{
   		if(count+fp->len>skb->len)
   		{
   			printk("Invalid fragment list: Fragment over size.\n");
   			ip_free(qp);
   			kfree_skb(skb,FREE_WRITE);
   			ip_statistics.IpReasmFails++;
   			return NULL;
   		}
 		memcpy((ptr + fp->offset), fp->ptr, fp->len);
 		count += fp->len;
 		fp = fp->next;
   	}
 
   	/* We glued together all fragments, so remove the queue entry. */
   	ip_free(qp);
 
   	/* Done with all fragments. Fixup the new IP header. */
   	iph = skb->h.iph;
   	iph->frag_off = 0;
   	iph->tot_len = htons((iph->ihl * sizeof(unsigned long)) + count);
   	skb->ip_hdr = iph;
   	
   	ip_statistics.IpReasmOKs++;
   	return(skb);
}
 

/*
 *	Process an incoming IP datagram fragment. 
 */
 
static struct sk_buff *ip_defrag(struct iphdr *iph, struct sk_buff *skb, struct device *dev)
{
	struct ipfrag *prev, *next;
	struct ipfrag *tfp;
	struct ipq *qp;
	struct sk_buff *skb2;
	unsigned char *ptr;
	int flags, offset;
	int i, ihl, end;

	ip_statistics.IpReasmReqds++;
	
	/* Find the entry of this IP datagram in the "incomplete datagrams" queue. */
   	qp = ip_find(iph);
 
   	/* Is this a non-fragmented datagram? */
   	offset = ntohs(iph->frag_off);
   	flags = offset & ~IP_OFFSET;
   	offset &= IP_OFFSET;
   	if (((flags & IP_MF) == 0) && (offset == 0)) 
   	{
		if (qp != NULL)
 			ip_free(qp);	/* Huh? How could this exist?? */
 		return(skb);
   	}

   	offset <<= 3;		/* offset is in 8-byte chunks */
 
   	/*
    	 * If the queue already existed, keep restarting its timer as long
   	 * as we still are receiving fragments.  Otherwise, create a fresh
    	 * queue entry.
    	 */

	if (qp != NULL) 
	{
		del_timer(&qp->timer);
		qp->timer.expires = IP_FRAG_TIME;	/* about 30 seconds */
		qp->timer.data = (unsigned long) qp;	/* pointer to queue */
		qp->timer.function = ip_expire;		/* expire function */
		add_timer(&qp->timer);
	} 
	else 
	{
		/*
		 *	If we failed to create it, then discard the frame
		 */
		if ((qp = ip_create(skb, iph, dev)) == NULL) 
		{
			skb->sk = NULL;
			kfree_skb(skb, FREE_READ);
			ip_statistics.IpReasmFails++;
			return NULL;
		}
	}

   	/*
   	 *	Determine the position of this fragment. 
   	 */
   	 
   	ihl = (iph->ihl * sizeof(unsigned long));
   	end = offset + ntohs(iph->tot_len) - ihl;
 
   	/*
   	 *	Point into the IP datagram 'data' part. 
   	 */

   	ptr = skb->data + dev->hard_header_len + ihl;
 
   	/* 
   	 *	Is this the final fragment? 
   	 */

   	if ((flags & IP_MF) == 0) 
   		qp->len = end;
 
   	/*
   	 * 	Find out which fragments are in front and at the back of us
   	 * 	in the chain of fragments so far.  We must know where to put
   	 * 	this fragment, right?
   	 */
   	 
   	prev = NULL;
   	for(next = qp->fragments; next != NULL; next = next->next) 
   	{
 		if (next->offset > offset) 
 			break;	/* bingo! */
 		prev = next;
   	}	
 
   	/*
   	 * 	We found where to put this one.
   	 * 	Check for overlap with preceeding fragment, and, if needed,
   	 * 	align things so that any overlaps are eliminated.
   	 */
   	if (prev != NULL && offset < prev->end) 
   	{
 		i = prev->end - offset;
 		offset += i;	/* ptr into datagram */
 		ptr += i;	/* ptr into fragment data */
 		DPRINTF((DBG_IP, "IP: defrag: fixed low overlap %d bytes\n", i));
   	}	
 
   	/*
    	 * Look for overlap with succeeding segments.
    	 * If we can merge fragments, do it.
      	 */
   
   	for(; next != NULL; next = tfp) 
   	{
 		tfp = next->next;
 		if (next->offset >= end) 
 			break;		/* no overlaps at all */
 
 		i = end - next->offset;			/* overlap is 'i' bytes */
 		next->len -= i;				/* so reduce size of	*/
 		next->offset += i;			/* next fragment	*/
 		next->ptr += i;
 		
 		/* 
 		 *	If we get a frag size of <= 0, remove it and the packet
 		 *	that it goes with.
 		 */
 		if (next->len <= 0) 
 		{
 			DPRINTF((DBG_IP, "IP: defrag: removing frag 0x%X (len %d)\n",
 							next, next->len));
 			if (next->prev != NULL) 
 				next->prev->next = next->next;
 		  	else 
 		  		qp->fragments = next->next;
 		
 			if (tfp->next != NULL) 
 				next->next->prev = next->prev;
 				
			kfree_skb(next->skb,FREE_READ); 			
 			kfree_s(next, sizeof(struct ipfrag));
 		}
 		DPRINTF((DBG_IP, "IP: defrag: fixed high overlap %d bytes\n", i));
   	}
 
   	/* 
   	 *	Insert this fragment in the chain of fragments. 
   	 */
   	 
   	tfp = NULL;
   	tfp = ip_frag_create(offset, end, skb, ptr);
   	
   	/*
   	 *	No memory to save the fragment - so throw the lot
   	 */
   	
   	if (!tfp) 
   	{
   		skb->sk = NULL;
   		kfree_skb(skb, FREE_READ);
   		return NULL;
   	}
   	tfp->prev = prev;
   	tfp->next = next;
   	if (prev != NULL) 
   		prev->next = tfp;
     	else 
     		qp->fragments = tfp;
   
   	if (next != NULL) 
   		next->prev = tfp;
 
   	/*
    	 * 	OK, so we inserted this new fragment into the chain.
    	 * 	Check if we now have a full IP datagram which we can
    	 * 	bump up to the IP layer...
    	 */
   
   	if (ip_done(qp)) 
   	{
 		skb2 = ip_glue(qp);		/* glue together the fragments */
 		return(skb2);
   	}
   	return(NULL);
 }
 
 
 /*
  *	This IP datagram is too large to be sent in one piece.  Break it up into
  *	smaller pieces (each of size equal to the MAC header plus IP header plus
  *	a block of the data of the original IP data part) that will yet fit in a
  *	single device frame, and queue such a frame for sending by calling the
  *	ip_queue_xmit().  Note that this is recursion, and bad things will happen
  *	if this function causes a loop...
  *
  *	Yes this is inefficient, feel free to submit a quicker one.
  *
  *	**Protocol Violation**
  *	We copy all the options to each fragment. !FIXME!
  */
  
 void ip_fragment(struct sock *sk, struct sk_buff *skb, struct device *dev, int is_frag)
 {
   	struct iphdr *iph;
   	unsigned char *raw;
   	unsigned char *ptr;
   	struct sk_buff *skb2;
   	int left, mtu, hlen, len;
   	int offset;
 
   	/* 
   	 *	Point into the IP datagram header. 
   	 */
   	 
   	raw = skb->data;
   	iph = (struct iphdr *) (raw + dev->hard_header_len);

	skb->ip_hdr = iph;
	 	
   	/* 
   	 *	Setup starting values. 
   	 */
   	 
   	hlen = (iph->ihl * sizeof(unsigned long));
   	left = ntohs(iph->tot_len) - hlen;	/* Space per frame */
   	hlen += dev->hard_header_len;		/* Total header size */
   	mtu = (dev->mtu - hlen);		/* Size of data space */
   	ptr = (raw + hlen);			/* Where to start from */
 	
   	DPRINTF((DBG_IP, "IP: Fragmentation Desired\n"));
   	DPRINTF((DBG_IP, "    DEV=%s, MTU=%d, LEN=%d SRC=%s",
 		dev->name, dev->mtu, left, in_ntoa(iph->saddr)));
   	DPRINTF((DBG_IP, " DST=%s\n", in_ntoa(iph->daddr)));
 
   	/*
   	 *	Check for any "DF" flag. [DF means do not fragment]
   	 */
   	 
   	if (ntohs(iph->frag_off) & IP_DF) 
   	{
 		DPRINTF((DBG_IP, "IP: Fragmentation Desired, but DF set !\n"));
 		DPRINTF((DBG_IP, "    DEV=%s, MTU=%d, LEN=%d SRC=%s",
 			dev->name, dev->mtu, left, in_ntoa(iph->saddr)));
 		DPRINTF((DBG_IP, " DST=%s\n", in_ntoa(iph->daddr)));
 
 		ip_statistics.IpFragFails++;
 		icmp_send(skb,ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, dev); 
 		return;
   	}
 
 	/*
 	 *	The protocol doesn't seem to say what to do in the case that the
 	 *	frame + options doesn't fit the mtu. As it used to fall down dead
 	 *	in this case we were fortunate it didn't happen
 	 */
 	 
 	if(mtu<8)
 	{
 		/* It's wrong but its better than nothing */
 		icmp_send(skb,ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED,dev);
 		ip_statistics.IpFragFails++;
 		return;
 	}
 	
   	/* 
   	 *	Fragment the datagram. 
   	 */
   	 
   	/*
   	 *	The initial offset is 0 for a complete frame. When
   	 *	fragmenting fragments its wherever this one starts.
   	 */

	if (is_frag & 2)
		offset = (ntohs(iph->frag_off) & 0x1fff) << 3;
	else
   		offset = 0;


	/*
	 *	Keep copying data until we run out.
	 */
	    		
   	while(left > 0) 
   	{
 		len = left;
		/* IF: it doesn't fit, use 'mtu' - the data space left */
		if (len > mtu)
			len = mtu;
		/* IF: we are not sending upto and including the packet end
		   then align the next start on an eight byte boundary */
		if (len < left)
		{
			len/=8;
			len*=8;
		}
 		DPRINTF((DBG_IP,"IP: frag: creating fragment of %d bytes (%d total)\n",
 							len, len + hlen));
 
 		/*
 		 *	Allocate buffer. 
 		 */
 		 
 		if ((skb2 = alloc_skb(len + hlen,GFP_ATOMIC)) == NULL) 
 		{
 			printk("IP: frag: no memory for new fragment!\n");
 			ip_statistics.IpFragFails++;
 			return;
 		}
 		
 		/*
 		 *	Set up data on packet
 		 */

 		skb2->arp = skb->arp;
 		skb2->free = skb->free;
 		skb2->len = len + hlen;
 		skb2->h.raw=(char *) skb2->data;
 
		/*
		 *	Charge the memory for the fragment to any owner
		 *	it might posess
		 */
		 
 		if (sk) 
 			sk->wmem_alloc += skb2->mem_len;
 
 		/* 
 		 *	Copy the packet header into the new buffer. 
 		 */
 		 
 		memcpy(skb2->h.raw, raw, hlen);
 
 		/*
 		 *	Copy a block of the IP datagram. 
 		 */
 		memcpy(skb2->h.raw + hlen, ptr, len);
 		left -= len;

		skb2->h.raw+=dev->hard_header_len; 

 		/*
 		 *	Fill in the new header fields. 
 		 */
 		iph = (struct iphdr *)(skb2->h.raw/*+dev->hard_header_len*/);
 		iph->frag_off = htons((offset >> 3));
 		/* 
 		 *	Added AC : If we are fragmenting a fragment thats not the
 		 *		   last fragment then keep MF on each bit 
 		 */
 		if (left > 0 || (is_frag & 1)) 
 			iph->frag_off |= htons(IP_MF);
 		ptr += len;
 		offset += len;
 
 		/* 
 		 *	Put this fragment into the sending queue. 
 		 */
 		 
 		ip_statistics.IpFragCreates++;
 		
 		ip_queue_xmit(sk, dev, skb2, 1);
   	}
   	ip_statistics.IpFragOKs++;
}
 


#ifdef CONFIG_IP_FORWARD

/* 
 *	Forward an IP datagram to its next destination. 
 */

static void ip_forward(struct sk_buff *skb, struct device *dev, int is_frag)
{
	struct device *dev2;	/* Output device */
	struct iphdr *iph;	/* Our header */
	struct sk_buff *skb2;	/* Output packet */
	struct rtable *rt;	/* Route we use */
	unsigned char *ptr;	/* Data pointer */
	unsigned long raddr;	/* Router IP address */

	/*
	 * Only forward packets that were fired at us when we are in promiscuous
	 * mode. In standard mode we rely on the driver to filter for us.
	 */
   
	if(dev->flags&IFF_PROMISC)
	{
  		if(memcmp((char *)&skb[1],dev->dev_addr,dev->addr_len))
  			return;
	}
	

  
  	/*
  	 *	According to the RFC, we must first decrease the TTL field. If
  	 *	that reaches zero, we must reply an ICMP control message telling
 	 *	that the packet's lifetime expired.
 	 *
 	 *	Exception:
 	 *	We may not generate an ICMP for an ICMP. icmp_send does the
 	 *	enforcement of this so we can forget it here. It is however
 	 *	sometimes VERY important.
 	 */

	iph = skb->h.iph;
	iph->ttl--;
	if (iph->ttl <= 0) 
	{
		DPRINTF((DBG_IP, "\nIP: *** datagram expired: TTL=0 (ignored) ***\n"));
		DPRINTF((DBG_IP, "    SRC = %s   ", in_ntoa(iph->saddr)));
		DPRINTF((DBG_IP, "    DST = %s (ignored)\n", in_ntoa(iph->daddr)));
	
		/* Tell the sender its packet died... */
		icmp_send(skb, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL, dev);
		return;
	}

	/* 
	 *	Re-compute the IP header checksum. 
	 *	This is inefficient. We know what has happened to the header
	 *	and could thus adjust the checksum as Phil Karn does in KA9Q
	 */
	 
	ip_send_check(iph);

	/*
	 * OK, the packet is still valid.  Fetch its destination address,
   	 * and give it to the IP sender for further processing.
	 */

	rt = ip_rt_route(iph->daddr, NULL, NULL);
	if (rt == NULL) 
	{
		DPRINTF((DBG_IP, "\nIP: *** routing (phase I) failed ***\n"));

		/*
		 *	Tell the sender its packet cannot be delivered. Again
		 *	ICMP is screened later.
		 */
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_NET_UNREACH, dev);
		return;
	}


	/*
	 * Gosh.  Not only is the packet valid; we even know how to
	 * forward it onto its final destination.  Can we say this
	 * is being plain lucky?
	 * If the router told us that there is no GW, use the dest.
	 * IP address itself- we seem to be connected directly...
	 */

	raddr = rt->rt_gateway;

	if (raddr != 0) 
	{
		/*
		 *	There is a gateway so find the correct route for it.
		 *	Gateways cannot in turn be gatewayed.
		 */
		rt = ip_rt_route(raddr, NULL, NULL);
		if (rt == NULL) 
		{
			DPRINTF((DBG_IP, "\nIP: *** routing (phase II) failed ***\n"));

			/* 
			 *	Tell the sender its packet cannot be delivered... 
			 */
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, dev);
			return;
		}
		if (rt->rt_gateway != 0) 
			raddr = rt->rt_gateway;
  	} 
  	else 
  		raddr = iph->daddr;
  		
  	/*
  	 *	Having picked a route we can now send the frame out.
  	 */

	dev2 = rt->rt_dev;

	/*
	 *	In IP you never forward a frame on the interface that it arrived
	 *	upon. We should generate an ICMP HOST REDIRECT giving the route
	 *	we calculated.
	 *	For now just dropping the packet is an acceptable compromise.
	 */

	if (dev == dev2)
		return;

	/*
	 * We now allocate a new buffer, and copy the datagram into it.
	 * If the indicated interface is up and running, kick it.
	 */

	DPRINTF((DBG_IP, "\nIP: *** fwd %s -> ", in_ntoa(iph->saddr)));
	DPRINTF((DBG_IP, "%s (via %s), LEN=%d\n",
			in_ntoa(raddr), dev2->name, skb->len));

	if (dev2->flags & IFF_UP) 
	{
	
		/*
		 *	Current design decrees we copy the packet. For identical header
		 *	lengths we could avoid it. The new skb code will let us push
		 *	data so the problem goes away then.
		 */
		 
		skb2 = alloc_skb(dev2->hard_header_len + skb->len, GFP_ATOMIC);
		/*
		 *	This is rare and since IP is tolerant of network failures
		 *	quite harmless.
		 */
		if (skb2 == NULL) 
		{
			printk("\nIP: No memory available for IP forward\n");
			return;
		}
		ptr = skb2->data;
		skb2->free = 1;
		skb2->len = skb->len + dev2->hard_header_len;
		skb2->h.raw = ptr;

		/* 
		 *	Copy the packet data into the new buffer. 
		 */
		memcpy(ptr + dev2->hard_header_len, skb->h.raw, skb->len);
		
		/* Now build the MAC header. */
		(void) ip_send(skb2, raddr, skb->len, dev2, dev2->pa_addr);

		ip_statistics.IpForwDatagrams++;

		/*
		 *	See if it needs fragmenting. Note in ip_rcv we tagged
		 *	the fragment type. This must be right so that
		 *	the fragmenter does the right thing.
		 */
		 
		if(skb2->len > dev2->mtu)
		{
			ip_fragment(NULL,skb2,dev2, is_frag);
			kfree_skb(skb2,FREE_WRITE);
		}
		else
		{
			/*
			 *	Map service types to priority. We lie about
			 *	throughput being low priority, but its a good
			 *	choice to help improve general usage.
			 */
			if(iph->tos & IPTOS_LOWDELAY)
				dev_queue_xmit(skb2, dev2, SOPRI_INTERACTIVE);
			else if(iph->tos & IPTOS_THROUGHPUT)
				dev_queue_xmit(skb2, dev2, SOPRI_BACKGROUND);
			else
				dev_queue_xmit(skb2, dev2, SOPRI_NORMAL);
		}
	}
}


#endif

/*
 *	This function receives all incoming IP datagrams. 
 */
 
int ip_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	struct iphdr *iph = skb->h.iph;
	unsigned char hash;
	unsigned char flag = 0;
	unsigned char opts_p = 0;	/* Set iff the packet has options. */
	struct inet_protocol *ipprot;
	static struct options opt; /* since we don't use these yet, and they
				take up stack space. */
	int brd;
	int is_frag=0;


	ip_statistics.IpInReceives++;
	
	DPRINTF((DBG_IP, "<<\n"));

	/*
	 *	Tag the ip header of this packet so we can find it
	 */
	 
	skb->ip_hdr = iph;

	/*
	 *	Is the datagram acceptable? 
	 *
	 *	1.	Length at least the size of an ip header
	 *	2.	Version of 4
	 *	3.	Checksums correctly. [Speed optimisation for later, skip loopback checksums]
	 *	(4.	We ought to check for IP multicast addresses and undefined types.. does this matter ?)
	 */

  	if (skb->len<sizeof(struct iphdr) || iph->ihl<5 || iph->version != 4 || ip_fast_csum((unsigned char *)iph, iph->ihl) !=0) 
  	{
  		ip_statistics.IpInHdrErrors++;
		DPRINTF((DBG_IP, "\nIP: *** datagram error ***\n"));
		DPRINTF((DBG_IP, "    SRC = %s   ", in_ntoa(iph->saddr)));
		DPRINTF((DBG_IP, "    DST = %s (ignored)\n", in_ntoa(iph->daddr)));
		kfree_skb(skb, FREE_WRITE);
		return(0);
	}
	
	/*
	 *	Our transport medium may have padded the buffer out. Now we know it
	 *	is IP we can trim to the true length of the frame.
	 */
	 
	skb->len=ntohs(iph->tot_len);

	/*
	 *	Next anaylse the packet for options. Studies show under one packet in
	 *	a thousand have options....
	 */
	   
	if (iph->ihl != 5) 
	{  	/* Fast path for the typical optionless IP packet. */
      		ip_print(iph);		/* Bogus, only for debugging. */
      		memset((char *) &opt, 0, sizeof(opt));
      		if (do_options(iph, &opt) != 0)
	  		return 0;
      		opts_p = 1;
	}

	/*
	 *	Remember if the frame is fragmented.
	 */

	if (iph->frag_off & 0x0020)
  		is_frag|=1;
  	
  	/*
  	 *	Last fragment ?
  	 */
  	 
  	if (ntohs(iph->frag_off) & 0x1fff)
  		is_frag|=2;
  	
  	/* 
  	 *	Do any IP forwarding required.  chk_addr() is expensive -- avoid it someday. 
  	 *
  	 *	This is inefficient. While finding out if it is for us we could also compute
  	 *	the routing table entry. This is where the great unified cache theory comes
  	 *	in as and when someone impliments it
  	 */

	if ((brd = ip_chk_addr(iph->daddr)) == 0) 
	{
	
		/*
		 *	The packet is for another target. Forward the frame
		 */
		 
#ifdef CONFIG_IP_FORWARD
		ip_forward(skb, dev, is_frag);
#else
		printk("Machine %x tried to use us as a forwarder to %x but we have forwarding disabled!\n",
			iph->saddr,iph->daddr);
		ip_statistics.IpInAddrErrors++;
#endif			
		/*
		 *	The forwarder is inefficient and copies the packet. We 
		 *	free the original now.
		 */
		 
		kfree_skb(skb, FREE_WRITE);
		return(0);
  	}

  	/*
  	 * Reassemble IP fragments. 
  	 */

  	if(is_frag)
  	{
#ifdef CONFIG_IP_DEFRAG
		/* Defragment. Obtain the complete packet if there is one */
		skb=ip_defrag(iph,skb,dev);
        	if(skb==NULL)
        		return 0;
	        iph=skb->h.iph;
#else
		printk("\nIP: *** datagram fragmentation not yet implemented ***\n");
		printk("    SRC = %s   ", in_ntoa(iph->saddr));
		printk("    DST = %s (ignored)\n", in_ntoa(iph->daddr));
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PROT_UNREACH, dev);
		kfree_skb(skb, FREE_WRITE);
		return(0);
#endif
	}

	/*
	 *	Point into the IP datagram, just past the header. 
	 */

	skb->ip_hdr = iph;
	skb->h.raw += iph->ihl*4;
	
	/*
	 *	skb->h.raw now points at the protocol beyond the IP header.
	 */
	 
	hash = iph->protocol & (MAX_INET_PROTOS -1);
	for (ipprot = (struct inet_protocol *)inet_protos[hash];ipprot != NULL;ipprot=(struct inet_protocol *)ipprot->next)
	{
      		struct sk_buff *skb2;

       		if (ipprot->protocol != iph->protocol) 
       			continue;
		DPRINTF((DBG_IP, "Using protocol = %X:\n", ipprot));
		print_ipprot(ipprot);

       /*
	* 	See if we need to make a copy of it.  This will
	* 	only be set if more than one protocol wants it. 
	* 	and then not for the last one.
	*
	* 	This is an artifact of poor upper protocol design. 
	*	Because the upper protocols damage the actual packet
	*	we must do copying. In actual fact it's even worse
	*	than this as TCP may hold on to the buffer.
	*/
		if (ipprot->copy) 
		{
#if 0		
			skb2 = alloc_skb(skb->mem_len-sizeof(struct sk_buff), GFP_ATOMIC);
			if (skb2 == NULL) 
				continue;
			memcpy(skb2, skb, skb2->mem_len);
			skb2->ip_hdr = (struct iphdr *)(
					(unsigned long)skb2 +
					(unsigned long) skb->ip_hdr -
					(unsigned long)skb);
			skb2->h.raw = (unsigned char *)(
					(unsigned long)skb2 +
					(unsigned long) skb->h.raw -
					(unsigned long)skb);
			skb2->free=1;
#else
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if(skb2==NULL)
				continue;
#endif							
		} 
		else 
		{
			skb2 = skb;
		}
		flag = 1;

	       /*
		* Pass on the datagram to each protocol that wants it,
		* based on the datagram protocol.  We should really
		* check the protocol handler's return values here...
		*/
		ipprot->handler(skb2, dev, opts_p ? &opt : 0, iph->daddr,
				(ntohs(iph->tot_len) - (iph->ihl * 4)),
				iph->saddr, 0, ipprot);

	}

	/*
	 * All protocols checked.
	 * If this packet was a broadcast, we may *not* reply to it, since that
	 * causes (proven, grin) ARP storms and a leakage of memory (i.e. all
	 * ICMP reply messages get queued up for transmission...)
	 */

	if (!flag) 
	{
		if (brd != IS_BROADCAST)
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PROT_UNREACH, dev);
		kfree_skb(skb, FREE_WRITE);
	}

	return(0);
}


/*
 * Queues a packet to be sent, and starts the transmitter
 * if necessary.  if free = 1 then we free the block after
 * transmit, otherwise we don't.
 * This routine also needs to put in the total length,
 * and compute the checksum
 */
 
void ip_queue_xmit(struct sock *sk, struct device *dev, 
	      struct sk_buff *skb, int free)
{
  	struct iphdr *iph;
  	unsigned char *ptr;

	/* All buffers without an owner socket get freed */
  	if (sk == NULL) 
  		free = 1;
  	
  	/* Sanity check */
  	if (dev == NULL) 
  	{
		printk("IP: ip_queue_xmit dev = NULL\n");
		return;
  	}
  
  	IS_SKB(skb);
  	
  	/*
  	 *	Do some book-keeping in the packet for later
  	 */

  	skb->free = free;
  	skb->dev = dev;
  	skb->when = jiffies;
  
	DPRINTF((DBG_IP, ">>\n"));

	/*
	 *	Find the IP header and set the length. This is bad
	 *	but once we get the skb data handling code in the
	 *	hardware will push its header sensibly and we will
	 *	set skb->ip_hdr to avoid this mess and the fixed
	 *	header length problem
	 */
	 
	ptr = skb->data;
	ptr += dev->hard_header_len;
	iph = (struct iphdr *)ptr;
	skb->ip_hdr = iph;
	iph->tot_len = ntohs(skb->len-dev->hard_header_len);

	/*
	 *	Do we need to fragment. Again this is inefficient. 
	 *	We need to somehow lock the original buffer and use
	 *	bits of it.
	 */
	 
	if(skb->len > dev->mtu)
  	{
		ip_fragment(sk,skb,dev,0);
		IS_SKB(skb);
  		kfree_skb(skb,FREE_WRITE);
  		return;
	}
  
  	/*
  	 *	Add an IP checksum
  	 */
  	 
	ip_send_check(iph);
	
	/*
	 *	Print the frame when debugging
	 */
	ip_print(iph);

	/*
	 *	More debugging. You cannot queue a packet already on a list
	 *	Spot this and moan loudly.
	 */
	if (skb->next != NULL) 
	{
		printk("ip_queue_xmit: next != NULL\n");
		skb_unlink(skb);
	}

	/*
	 *	If a sender wishes the packet to remain unfreed
	 *	we add it to his send queue. This arguably belongs
	 *	in the TCP level since nobody elses uses it. BUT
	 *	remember IPng might change all the rules.
	 */
	 
	if (!free) 
	{
		unsigned long flags;
		/* The socket now has more outstanding blocks */
		
		sk->packets_out++;
		
		/* Protect the list for a moment */
		save_flags(flags);
		cli();
		
		if (skb->link3 != NULL) 
		{
			printk("ip.c: link3 != NULL\n");
			skb->link3 = NULL;
		}
		if (sk->send_head == NULL) 
		{
			sk->send_tail = skb;
			sk->send_head = skb;
		}
		else 
		{
			sk->send_tail->link3 = skb;
			sk->send_tail = skb;
		}
		/* skb->link3 is NULL */
		
		/* Interrupt restore */
		restore_flags(flags);
		/* Set the IP write timeout to the round trip time for the packet.
		   If an acknowledge has not arrived by then we may wish to act */	
		reset_timer(sk, TIME_WRITE, sk->rto);
	} 
	else 
		/* Remember who owns the buffer */
		skb->sk = sk;

	/*
	 *	If the indicated interface is up and running, send the packet. 
	 */
	ip_statistics.IpOutRequests++;
	 
	if (dev->flags & IFF_UP) 
	{
		/* 
		 *	If we have an owner use its priority setting,
		 *	otherwise use NORMAL
		 */
		
		if (sk != NULL) 
		{
			dev_queue_xmit(skb, dev, sk->priority);
		}
		else 
		{
			dev_queue_xmit(skb, dev, SOPRI_NORMAL);
		}
	} 
	else 
	{
		ip_statistics.IpOutDiscards++;
		if (free) 
			kfree_skb(skb, FREE_WRITE);
	}
}


/*
 *	A socket has timed out on its send queue and wants to do a
 *	little retransmitting. Currently this means TCP.
 */

void ip_do_retransmit(struct sock *sk, int all)
{
  	struct sk_buff * skb;
  	struct proto *prot;
  	struct device *dev;
  	int retransmits;

	prot = sk->prot;
	skb = sk->send_head;
	retransmits = sk->retransmits;
	
	while (skb != NULL) 
	{
		dev = skb->dev;
		IS_SKB(skb);
#if 0
	/********** THIS IS NOW DONE BY THE DEVICE LAYER **********/	
		/*
		 * 	The rebuild_header function sees if the ARP is done.
		 * 	If not it sends a new ARP request, and if so it builds
		 * 	the header. It isn't really needed here, and with the
		 *	new ARP pretty much will not happen.
		 */
		 
		if (!skb->arp) 
		{
			if (dev->rebuild_header(skb->data, dev, skb->raddr, NULL)) 
			{
				if (!all) 
					break;
				skb = skb->link3;
				continue;
			}
		}
#endif
		skb->when = jiffies;

		/* 
		 *	If the interface is (still) up and running, kick it. 
		 */
		
		if (dev->flags & IFF_UP) 
		{
			/*
			 *	If the packet is still being sent by the device/protocol
			 *	below then don't retransmit. This is both needed, and good -
			 *	especially with connected mode AX.25 where it stops resends
			 *	occuring of an as yet unsent anyway frame!
			 *	We still add up the counts as the round trip time wants
			 *	adjusting.
			 */
			if (sk && !skb_device_locked(skb))
			{
				/* Remove it from any existing driver queue first! */
				skb_unlink(skb);
				/* Now queue it */
				ip_statistics.IpOutRequests++;
				dev_queue_xmit(skb, dev, sk->priority);
			}
		}
		
		/*
		 *	Count retransmissions
		 */
		retransmits++;
		sk->prot->retransmits ++;
		
		/*
		 *	Only one retransmit requested.
		 */
		if (!all) 
			break;

		/*
		 *	This should cut it off before we send too many packets. 
		 */
		if (sk->retransmits > sk->cong_window) 
			break;
		skb = skb->link3;
	}
}

/*
 * 	This is the normal code called for timeouts.  It does the retransmission
 * 	and then does backoff.  ip_do_retransmit is separated out because
 * 	tcp_ack needs to send stuff from the retransmit queue without
 * 	initiating a backoff.
 */

void ip_retransmit(struct sock *sk, int all)
{
  	ip_do_retransmit(sk, all);

  	/*
  	 * Increase the timeout each time we retransmit.  Note that
  	 * we do not increase the rtt estimate.  rto is initialized
  	 * from rtt, but increases here.  Jacobson (SIGCOMM 88) suggests
  	 * that doubling rto each time is the least we can get away with.
  	 * In KA9Q, Karn uses this for the first few times, and then
  	 * goes to quadratic.  netBSD doubles, but only goes up to *64,
  	 * and clamps at 1 to 64 sec afterwards.  Note that 120 sec is
  	 * defined in the protocol as the maximum possible RTT.  I guess
  	 * we'll have to use something other than TCP to talk to the
   	 * University of Mars.
   	 */

	sk->retransmits++;
	sk->backoff++;
	sk->rto = min(sk->rto << 1, 120*HZ);
	reset_timer(sk, TIME_WRITE, sk->rto);
}

/*
 *	Socket option code for IP. This is the end of the line after any TCP,UDP etc options on
 *	an IP socket.
 *
 *	We impliment IP_TOS (type of service), IP_TTL (time to live).
 *
 *	Next release we will sort out IP_OPTIONS since for some people are kind of important.
 */
 
int ip_setsockopt(struct sock *sk, int level, int optname, char *optval, int optlen)
{
	int val,err;
	
  	if (optval == NULL) 
  		return(-EINVAL);

  	err=verify_area(VERIFY_READ, optval, sizeof(int));
  	if(err)
  		return err;
  	
  	val = get_fs_long((unsigned long *)optval);

	if(level!=SOL_IP)
		return -EOPNOTSUPP;

	switch(optname)
	{
		case IP_TOS:
			if(val<0||val>255)
				return -EINVAL;
			sk->ip_tos=val;
			return 0;
		case IP_TTL:
			if(val<1||val>255)
				return -EINVAL;
			sk->ip_ttl=val;
			return 0;
		/* IP_OPTIONS and friends go here eventually */
		default:
			return(-ENOPROTOOPT);
	}
}

/*
 *	Get the options. Note for future reference. The GET of IP options gets the
 *	_received_ ones. The set sets the _sent_ ones.
 */
 
int ip_getsockopt(struct sock *sk, int level, int optname, char *optval, int *optlen)
{
	int val,err;
	
	if(level!=SOL_IP)
		return -EOPNOTSUPP;
		
	switch(optname)
	{
		case IP_TOS:
			val=sk->ip_tos;
			break;
		case IP_TTL:
			val=sk->ip_ttl;
			break;
		default:
			return(-ENOPROTOOPT);
	}
	err=verify_area(VERIFY_WRITE, optlen, sizeof(int));
	if(err)
  		return err;
  	put_fs_long(sizeof(int),(unsigned long *) optlen);

  	err=verify_area(VERIFY_WRITE, optval, sizeof(int));
  	if(err)
  		return err;
  	put_fs_long(val,(unsigned long *)optval);

  	return(0);
}

/*
 *	IP protocol layer initialiser
 */
 
static struct packet_type ip_packet_type = 
{
	0,	/* MUTTER ntohs(ETH_P_IP),*/
	0,		/* copy */
	ip_rcv,
	NULL,
	NULL,
};
 
 
/*
 *	IP registers the packet type and then calls the subprotocol initialisers
 */
 
void ip_init(void)
{
	ip_packet_type.type=htons(ETH_P_IP);
	dev_add_pack(&ip_packet_type);
/*	ip_raw_init();
	ip_packet_init();
	ip_tcp_init();
	ip_udp_init();*/
}
