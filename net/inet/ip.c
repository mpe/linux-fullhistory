/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The Internet Protocol (IP) module.
 *
 * Version:	@(#)ip.c	1.0.16	06/02/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
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
#include "inet.h"
#include "timer.h"
#include "dev.h"
#include "eth.h"
#include "ip.h"
#include "protocol.h"
#include "route.h"
#include "tcp.h"
#include "skbuff.h"
#include "sock.h"
#include "arp.h"
#include "icmp.h"

extern int last_retran;
extern void sort_send(struct sock *sk);

void
ip_print(struct iphdr *ip)
{
  unsigned char buff[32];
  unsigned char *ptr;
  int addr, len, i;

  if (inet_debug != DBG_IP) return;

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
  while (len > 0) {
	printk("    %04X: ", addr);
	for(i = 0; i < 16; i++) {
		if (len > 0) {
			printk("%02X ", (*ptr & 0xFF));
			buff[i] = *ptr++;
			if (buff[i] < 32 || buff[i] > 126) buff[i] = '.';
		} else {
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


int
ip_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
  switch(cmd) {
	case DDIOCSDBG:
		return(dbg_ioctl((void *) arg, DBG_IP));
	default:
		return(-EINVAL);
  }
}


/* these two routines will do routining. */
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


/* Take an skb, and fill in the MAC header. */
static int
ip_send(struct sk_buff *skb, unsigned long daddr, int len, struct device *dev,
	unsigned long saddr)
{
  unsigned char *ptr;
  int mac;

  ptr = (unsigned char *)(skb + 1);
  mac = 0;
  skb->arp = 1;
  if (dev->hard_header) {
	mac = dev->hard_header(ptr, dev, ETH_P_IP, daddr, saddr, len);
  }
  if (mac < 0) {
	mac = -mac;
	skb->arp = 0;
  }
  skb->dev = dev;
  return(mac);
}


/*
 * This routine builds the appropriate hardware/IP headers for
 * the routine.  It assumes that if *dev != NULL then the
 * protocol knows what it's doing, otherwise it uses the
 * routing/ARP tables to select a device struct.
 */
int
ip_build_header(struct sk_buff *skb, unsigned long saddr, unsigned long daddr,
		struct device **dev, int type, struct options *opt, int len)
{
  static struct options optmem;
  struct iphdr *iph;
  struct rtable *rt;
  unsigned char *buff;
  unsigned long raddr;
  static int count = 0;
  int tmp;

  if (saddr == 0) saddr = my_addr();
  DPRINTF((DBG_IP, "ip_build_header (skb=%X, saddr=%X, daddr=%X, *dev=%X,\n"
	   "                 type=%d, opt=%X, len = %d)\n",
	   skb, saddr, daddr, *dev, type, opt, len));
  buff = (unsigned char *)(skb + 1);

  /* See if we need to look up the device. */
  if (*dev == NULL) {
	rt = rt_route(daddr, &optmem);
	if (rt == NULL) return(-ENETUNREACH);

	*dev = rt->rt_dev;
	if (daddr != 0x0100007F) saddr = rt->rt_dev->pa_addr;
	raddr = rt->rt_gateway;

	DPRINTF((DBG_IP, "ip_build_header: saddr set to %s\n", in_ntoa(saddr)));
	opt = &optmem;
  } else {
	/* We still need the address of the first hop. */
	rt = rt_route(daddr, &optmem);
	raddr = (rt == NULL) ? 0 : rt->rt_gateway;
  }
  if (raddr == 0) raddr = daddr;

  /* Now build the MAC header. */
  tmp = ip_send(skb, raddr, len, *dev, saddr);
  buff += tmp;
  len -= tmp;

  skb->dev = *dev;
  skb->saddr = saddr;
  if (skb->sk) skb->sk->saddr = saddr;

  /* Now build the IP header. */

  /* If we are using IPPROTO_RAW, then we don't need an IP header, since
     one is being supplied to us by the user */

  if(type == IPPROTO_RAW) return (tmp);

  iph = (struct iphdr *)buff;
  iph->version  = 4;
  iph->tos      = 0;
  iph->frag_off = 0;
  iph->ttl      = 32;
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
 * This routine does all the checksum computations that don't
 * require anything special (like copying or special headers).
 */
unsigned short
ip_compute_csum(unsigned char * buff, int len)
{
  unsigned long sum = 0;

  /* Do the first multiple of 4 bytes and convert to 16 bits. */
  if (len > 3) {
	__asm__("\t clc\n"
	        "1:\n"
	        "\t lodsl\n"
	        "\t adcl %%eax, %%ebx\n"
	        "\t loop 1b\n"
	        "\t adcl $0, %%ebx\n"
	        "\t movl %%ebx, %%eax\n"
	        "\t shrl $16, %%eax\n"
	        "\t addw %%ax, %%bx\n"
	        "\t adcw $0, %%bx\n"
	        : "=b" (sum) , "=S" (buff)
	        : "0" (sum), "c" (len >> 2) ,"1" (buff)
	        : "ax", "cx", "si", "bx" );
  }
  if (len & 2) {
	__asm__("\t lodsw\n"
	        "\t addw %%ax, %%bx\n"
	        "\t adcw $0, %%bx\n"
	        : "=b" (sum), "=S" (buff)
	        : "0" (sum), "1" (buff)
	        : "bx", "ax", "si");
  }
  if (len & 1) {
	__asm__("\t lodsb\n"
	        "\t movb $0, %%ah\n"
	        "\t addw %%ax, %%bx\n"
	        "\t adcw $0, %%bx\n"
	        : "=b" (sum), "=S" (buff)
	        : "0" (sum), "1" (buff)
	        : "bx", "ax", "si");
  }
  sum =~sum;
  return(sum & 0xffff);
}


/* Check the header of an incoming IP datagram. */
int
ip_csum(struct iphdr *iph)
{
  if (iph->check == 0) return(0);
  if (ip_compute_csum((unsigned char *)iph, iph->ihl*4) == 0) return(0);
  return(1);
}


/* Generate a checksym for an outgoing IP datagram. */
static void
ip_send_check(struct iphdr *iph)
{
   iph->check = 0;
   iph->check = ip_compute_csum((unsigned char *)iph, iph->ihl*4);
}


/* Forward an IP datagram to its next destination. */
static void
ip_forward(struct sk_buff *skb, struct device *dev)
{
  struct device *dev2;
  struct iphdr *iph;
  struct sk_buff *skb2;
  struct rtable *rt;
  unsigned char *ptr;
  unsigned long raddr;

  /*
   * According to the RFC, we must first decrease the TTL field. If
   * that reaches zero, we must reply an ICMP control message telling
   * that the packet's lifetime expired.
   */
  iph = skb->h.iph;
  iph->ttl--;
  if (iph->ttl <= 0) {
	DPRINTF((DBG_IP, "\nIP: *** datagram expired: TTL=0 (ignored) ***\n"));
	DPRINTF((DBG_IP, "    SRC = %s   ", in_ntoa(iph->saddr)));
	DPRINTF((DBG_IP, "    DST = %s (ignored)\n", in_ntoa(iph->daddr)));

	/* Tell the sender its packet died... */
	icmp_send(skb, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL, dev);
	return;
  }

  /* Re-compute the IP header checksum. */
  ip_send_check(iph);

  /*
   * OK, the packet is still valid.  Fetch its destination address,
   * and give it to the IP sender for further processing.
   */
  rt = rt_route(iph->daddr, NULL);
  if (rt == NULL) {
	DPRINTF((DBG_IP, "\nIP: *** routing (phase I) failed ***\n"));

	/* Tell the sender its packet cannot be delivered... */
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
  if (raddr != 0) {
	rt = rt_route(raddr, NULL);
	if (rt == NULL) {
		DPRINTF((DBG_IP, "\nIP: *** routing (phase II) failed ***\n"));

		/* Tell the sender its packet cannot be delivered... */
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, dev);
		return;
	}
	if (rt->rt_gateway != 0) raddr = rt->rt_gateway;
  } else raddr = iph->daddr;
  dev2 = rt->rt_dev;

  /*
   * We now allocate a new buffer, and copy the datagram into it.
   * If the indicated interface is up and running, kick it.
   */
  DPRINTF((DBG_IP, "\nIP: *** fwd %s -> ", in_ntoa(iph->saddr)));
  DPRINTF((DBG_IP, "%s (via %s), LEN=%d\n",
			in_ntoa(raddr), dev2->name, skb->len));

  if (dev2->flags & IFF_UP) {
	skb2 = (struct sk_buff *) kmalloc(sizeof(struct sk_buff) +
		       dev2->hard_header_len + skb->len, GFP_ATOMIC);
	if (skb2 == NULL) {
		printk("\nIP: No memory available for IP forward\n");
		return;
	}
	ptr = (unsigned char *)(skb2 + 1);
	skb2->lock = 0;
	skb2->sk = NULL;
	skb2->len = skb->len + dev2->hard_header_len;
	skb2->mem_addr = skb2;
	skb2->mem_len = sizeof(struct sk_buff) + skb2->len;
	skb2->next = NULL;
	skb2->h.raw = ptr;

	/* Copy the packet data into the new buffer. */
	skb2->h.raw = ptr;
	memcpy(ptr + dev2->hard_header_len, skb->h.raw, skb->len);
		
	/* Now build the MAC header. */
	(void) ip_send(skb2, raddr, skb->len, dev2, dev2->pa_addr);

	dev2->queue_xmit(skb2, dev2, SOPRI_NORMAL);
  }
}


/* This function receives all incoming IP datagrams. */
int
ip_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
  struct iphdr *iph;
  unsigned char hash;
  unsigned char flag = 0;
  struct inet_protocol *ipprot;
  static struct options opt; /* since we don't use these yet, and they
				take up stack space. */
  int brd;

  iph = skb->h.iph;
  memset((char *) &opt, 0, sizeof(opt));
  DPRINTF((DBG_IP, "<<\n"));
  ip_print(iph);

  /* Is the datagram acceptable? */
  if (ip_csum(iph) || do_options(iph, &opt) || iph->version != 4) {
	DPRINTF((DBG_IP, "\nIP: *** datagram error ***\n"));
	DPRINTF((DBG_IP, "    SRC = %s   ", in_ntoa(iph->saddr)));
	DPRINTF((DBG_IP, "    DST = %s (ignored)\n", in_ntoa(iph->daddr)));
	skb->sk = NULL;
	kfree_skb(skb, FREE_WRITE);
	return(0);
  }

  /* Do any IP forwarding required. */
  if ((brd = chk_addr(iph->daddr)) == 0) {
	ip_forward(skb, dev);
	skb->sk = NULL;
	kfree_skb(skb, FREE_WRITE);
	return(0);
  }

  /*
   * Deal with fragments: not really...
   * Fragmentation is definitely a required part of IP (yeah, guys,
   * I read Linux-Activists.NET too :-), but the current "sk_buff"
   * allocation stuff doesn't make things simpler.  When we're all
   * done cleaning up the mess, we'll add Ross Biro's "mbuf" stuff
   * to the code, which will replace the sk_buff stuff completely.
   * That will (a) make the code even cleaner, (b) allow me to do
   * the DDI (Device Driver Interface) the way I want to, and (c),
   * it will allow for easy addition of fragging.  Any takers? -FvK
   */
  if ((iph->frag_off & 32) || (ntohs(iph->frag_off) & 0x1fff)) {
	printk("\nIP: *** datagram fragmentation not yet implemented ***\n");
	printk("    SRC = %s   ", in_ntoa(iph->saddr));
	printk("    DST = %s (ignored)\n", in_ntoa(iph->daddr));
	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PROT_UNREACH, dev);
	skb->sk = NULL;
	kfree_skb(skb, FREE_WRITE);
	return(0);
  }

  /* Point into the IP datagram, just past the header. */
  skb->h.raw += iph->ihl*4;
  hash = iph->protocol & (MAX_INET_PROTOS -1);
  for (ipprot = (struct inet_protocol *)inet_protos[hash];
       ipprot != NULL;
       ipprot=(struct inet_protocol *)ipprot->next)
    {
       struct sk_buff *skb2;

       if (ipprot->protocol != iph->protocol) continue;
       DPRINTF((DBG_IP, "Using protocol = %X:\n", ipprot));
       print_ipprot(ipprot);

       /*
	* See if we need to make a copy of it.  This will
	* only be set if more than one protpocol wants it. 
	* and then not for the last one.
	*/
       if (ipprot->copy) {
		skb2 = (struct sk_buff *) kmalloc (skb->mem_len, GFP_ATOMIC);
		if (skb2 == NULL) continue;
		memcpy(skb2, skb, skb->mem_len);
		skb2->mem_addr = skb2;
		skb2->lock = 0;
		skb2->h.raw = (unsigned char *)(
				(unsigned long)skb2 +
				(unsigned long) skb->h.raw -
				(unsigned long)skb);
	} else {
		skb2 = skb;
	}
	flag = 1;

       /*
	* Pass on the datagram to each protocol that wants it,
	* based on the datagram protocol.  We should really
	* check the protocol handler's return values here...
	*/
	ipprot->handler(skb2, dev, &opt, iph->daddr,
			(ntohs(iph->tot_len) - (iph->ihl * 4)),
			iph->saddr, 0, ipprot);

  }

  /*
   * All protocols checked.
   * If this packet was a broadcast, we may *not* reply to it, since that
   * causes (proven, grin) ARP storms and a leakage of memory (i.e. all
   * ICMP reply messages get queued up for transmission...)
   */
  if (!flag) {
	if (brd != IS_BROADCAST)
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PROT_UNREACH, dev);
	skb->sk = NULL;
	kfree_skb(skb, FREE_WRITE);
  }

  return(0);
}


/*
 * Queues a packet to be sent, and starts the transmitter
 * if necessary.  if free = 1 then we free the block after
 * transmit, otherwise we don't.
 * This routine also needs to put in the total length, and
 * compute the checksum.
 */
void
ip_queue_xmit(struct sock *sk, struct device *dev, 
	      struct sk_buff *skb, int free)
{
  struct iphdr *iph;
  unsigned char *ptr;

  if (sk == NULL) free = 1;
  if (dev == NULL) {
	printk("IP: ip_queue_xmit dev = NULL\n");
	return;
  }
  skb->free = free;
  skb->dev = dev;
  skb->when = jiffies;

  DPRINTF((DBG_IP, ">>\n"));
  ptr = (unsigned char *)(skb + 1);
  ptr += dev->hard_header_len;
  iph = (struct iphdr *)ptr;
  iph->tot_len = ntohs(skb->len - dev->hard_header_len);
  ip_send_check(iph);
  ip_print(iph);
  skb->next = NULL;

  /* See if this is the one trashing our queue. Ross? */
  skb->magic = 1;
  if (!free) {
	skb->link3 = NULL;
	sk->packets_out++;
	cli();
	if (sk->send_head == NULL) {
		sk->send_tail = skb;
		sk->send_head = skb;
	} else {
		/* See if we've got a problem. */
		if (sk->send_tail == NULL) {
			printk("IP: ***bug sk->send_tail == NULL != sk->send_head\n");
			sort_send(sk);
		} else {
			sk->send_tail->link3 = skb;
			sk->send_tail = skb;
		}
	}
	sti();
	sk->time_wait.len = backoff(sk->backoff) * (2 * sk->mdev + sk->rtt);
        sk->timeout = TIME_WRITE;
        reset_timer ((struct timer *)&sk->time_wait);
  } else {
	skb->sk = sk;
  }

  /* If the indicated interface is up and running, kick it. */
  if (dev->flags & IFF_UP) {
	if (sk != NULL) {
		dev->queue_xmit(skb, dev, sk->priority);
	} else {
		dev->queue_xmit(skb, dev, SOPRI_NORMAL);
	}
  } else {
	if (free) kfree_skb(skb, FREE_WRITE);
  }
}


void
ip_retransmit(struct sock *sk, int all)
{
  struct sk_buff * skb;
  struct proto *prot;
  struct device *dev;

  prot = sk->prot;
  skb = sk->send_head;
  while (skb != NULL) {
	dev = skb->dev;

	/*
	 * The rebuild_header function sees if the ARP is done.
	 * If not it sends a new ARP request, and if so it builds
	 * the header.
	 */
	if (!skb->arp) {
		if (dev->rebuild_header((struct enet_header *)(skb+1),dev)) {
			if (!all) break;
			skb = (struct sk_buff *)skb->link3;
			continue;
		}
	}
	skb->arp = 1;
	skb->when = jiffies;

	/* If the interface is (still) up and running, kick it. */
	if (dev->flags & IFF_UP) {
		if (sk) dev->queue_xmit(skb, dev, sk->priority);
		  else dev->queue_xmit(skb, dev, SOPRI_NORMAL );
	}

	sk->retransmits++;
	sk->prot->retransmits ++;
	if (!all) break;

	/* This should cut it off before we send too many packets. */
	if (sk->retransmits > sk->cong_window) break;
	skb = (struct sk_buff *)skb->link3;
  }

  /*
   * Double the RTT time every time we retransmit. 
   * This will cause exponential back off on how hard we try to
   * get through again.  Once we get through, the rtt will settle
   * back down reasonably quickly.
   */
  sk->backoff++;
  sk->time_wait.len = backoff(sk->backoff) * (2 * sk->mdev + sk->rtt);
  sk->timeout = TIME_WRITE;
  reset_timer((struct timer *)&sk->time_wait);

}

/* Backoff function - the subject of much research */
int backoff(int n)
{
	/* Use binary exponential up to retry #4, and quadratic after that
	 * This yields the sequence
	 * 1, 2, 4, 8, 16, 25, 36, 49, 64, 81, 100 ...
	 */

	if(n <= 4)
		return 1 << n;	/* Binary exponential back off */
	else
		return n * n;	/* Quadratic back off */
}
