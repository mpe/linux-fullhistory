/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The Internet Protocol (IP) output module.
 *
 * Version:	@(#)ip.c	1.0.16b	9/1/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald Becker, <becker@super.org>
 *		Alan Cox, <Alan.Cox@linux.org>
 *		Richard Underwood
 *		Stefan Becker, <stefanb@yello.ping.de>
 *		Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *
 *	See ip_input.c for original log
 *
 *	Fixes:
 *		Alan Cox	:	Missing nonblock feature in ip_build_xmit.
 *		Mike Kilburn	:	htons() missing in ip_build_xmit.
 *		Bradford Johnson:	Fix faulty handling of some frames when 
 *					no route is found.
 *		Alexander Demenshin:	Missing sk/skb free in ip_queue_xmit
 *					(in case if packet not accepted by
 *					output firewall rules)
 *		Alexey Kuznetsov:	use new route cache
 *		Andi Kleen:		Fix broken PMTU recovery and remove
 *					some redundant tests.
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/config.h>

#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>

#include <net/snmp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>
#include <net/icmp.h>
#include <net/raw.h>
#include <net/checksum.h>
#include <linux/igmp.h>
#include <linux/ip_fw.h>
#include <linux/firewall.h>
#include <linux/mroute.h>
#include <net/netlink.h>
#include <linux/ipsec.h>

static void __inline__ ip_ll_header_reserve(struct sk_buff *skb)
{
	struct rtable *rt = (struct rtable*)skb->dst;
	skb_reserve(skb, (rt->u.dst.dev->hard_header_len+15)&~15);
	ip_ll_header(skb);
}


int ip_id_count = 0;

int ip_build_pkt(struct sk_buff *skb, struct sock *sk, u32 saddr, u32 daddr,
		 struct ip_options *opt)
{
	struct rtable *rt;
	u32 final_daddr = daddr;
	struct iphdr *iph;
	int err;
	
	if (opt && opt->srr)
		daddr = opt->faddr;

	err = ip_route_output(&rt, daddr, saddr, RT_TOS(sk->ip_tos) |
			      (sk->localroute||0), NULL);
	if (err)
	{
		ip_statistics.IpOutNoRoutes++;
		return err;
	}

	if (opt && opt->is_strictroute && rt->rt_flags&RTF_GATEWAY) {
		ip_rt_put(rt);
		ip_statistics.IpOutNoRoutes++;
		return -ENETUNREACH;
	}

	skb->dst = dst_clone(&rt->u.dst);

	skb->dev = rt->u.dst.dev;
	skb->arp = 0;

	ip_ll_header_reserve(skb);
	
	/*
	 *	Now build the IP header.
	 */

	/*
	 *	Build the IP addresses
	 */
	 
	if (opt)
		iph=(struct iphdr *)skb_put(skb,sizeof(struct iphdr) + opt->optlen);
	else
		iph=(struct iphdr *)skb_put(skb,sizeof(struct iphdr));

	iph->version  = 4;
	iph->ihl      = 5;
	iph->tos      = sk->ip_tos;
	iph->frag_off = 0;
	if (sk->ip_pmtudisc == IP_PMTUDISC_WANT && 
		!(rt->rt_flags & RTF_NOPMTUDISC))
		iph->frag_off |= htons(IP_DF);
	iph->ttl      = sk->ip_ttl;
	iph->daddr    = rt->rt_dst;
	iph->saddr    = rt->rt_src;
	iph->protocol = sk->protocol;
	skb->nh.iph   = iph;
	skb->h.raw    = (unsigned char*)(iph+1);

	if (opt && opt->optlen)
	{
		iph->ihl += opt->optlen>>2;
		skb->h.raw += opt->optlen;
		ip_options_build(skb, opt, final_daddr,
				 rt->u.dst.dev->pa_addr, 0);
	}
	
	ip_rt_put(rt);
	return 0;
}

/*
 * This routine builds the appropriate hardware/IP headers for
 * the routine.
 */
int ip_build_header(struct sk_buff *skb, struct sock *sk)
{
	struct rtable *rt;
	struct ip_options *opt = sk->opt;
	u32 daddr = sk->daddr;
	u32 final_daddr = daddr;
	struct iphdr *iph;
	int err;

	if (opt && opt->srr)
		daddr = opt->faddr;

	rt = (struct rtable*)sk->dst_cache;

	if (!rt || rt->u.dst.obsolete) {
		ip_rt_put(rt);
		err = ip_route_output(&rt, daddr, sk->saddr, RT_TOS(sk->ip_tos) |
				      (sk->localroute||0), NULL);
		if (err)
			return err;
		sk->dst_cache = &rt->u.dst;
	}

	if (opt && opt->is_strictroute && rt->rt_flags&RTF_GATEWAY) {
		sk->dst_cache = NULL;
		ip_rt_put(rt);
		ip_statistics.IpOutNoRoutes++;
		return -ENETUNREACH;
	}

	skb->dst = dst_clone(sk->dst_cache);

	skb->dev = rt->u.dst.dev;
	skb->arp = 0;
	skb_reserve(skb, MAX_HEADER);
	skb->mac.raw = skb->data;
	
	/*
	 *	Now build the IP header.
	 */

	/*
	 *	Build the IP addresses
	 */
	 
	if (opt)
		iph=(struct iphdr *)skb_put(skb,sizeof(struct iphdr) + opt->optlen);
	else
		iph=(struct iphdr *)skb_put(skb,sizeof(struct iphdr));

	iph->version  = 4;
	iph->ihl      = 5;
	iph->tos      = sk->ip_tos;
	iph->frag_off = 0;
	if (sk->ip_pmtudisc == IP_PMTUDISC_WANT &&
		!(rt->rt_flags & RTF_NOPMTUDISC))
		iph->frag_off |= htons(IP_DF);
	iph->ttl      = sk->ip_ttl;
	iph->daddr    = rt->rt_dst;
	iph->saddr    = rt->rt_src;
	iph->protocol = sk->protocol;
	skb->nh.iph   = iph;
	skb->h.raw    = (unsigned char*)(iph+1);

	if (!opt || !opt->optlen)
		return 0;
	iph->ihl += opt->optlen>>2;
	skb->h.raw += opt->optlen;
	ip_options_build(skb, opt, final_daddr, rt->u.dst.dev->pa_addr, 0);

	return 0;
}

int ip_mc_output(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	struct rtable *rt = (struct rtable*)skb->dst;
	struct device *dev = rt->u.dst.dev;

	/*
	 *	If the indicated interface is up and running, send the packet.
	 */
	 
	ip_statistics.IpOutRequests++;
#ifdef CONFIG_IP_ACCT
	ip_fw_chk(skb->nh.iph, skb->dev,NULL,ip_acct_chain,0,IP_FW_MODE_ACCT_OUT);
#endif

	if (rt->rt_flags & RTCF_NAT)
		ip_do_nat(skb);

	/*
	 *	Multicasts are looped back for other local users
	 */
	 
	if (rt->rt_flags&RTF_MULTICAST && !(dev->flags&IFF_LOOPBACK)) {
		if (sk==NULL || sk->ip_mc_loop)
			dev_loopback_xmit(skb);

		/* Multicasts with ttl 0 must not go beyond the host */
		
		if (skb->nh.iph->ttl == 0) {
			kfree_skb(skb, FREE_WRITE);
			return 0;
		}
	}

	if ((rt->rt_flags&(RTF_LOCAL|RTF_BROADCAST)) == (RTF_LOCAL|RTF_BROADCAST) &&
	    !(dev->flags&IFF_LOOPBACK))
		dev_loopback_xmit(skb);

	if (dev->flags & IFF_UP) {
		dev_queue_xmit(skb);
		return 0;
	}
	ip_statistics.IpOutDiscards++;

	kfree_skb(skb, FREE_WRITE);
	return -ENETDOWN;
}

int ip_output(struct sk_buff *skb)
{
	struct rtable *rt = (struct rtable*)skb->dst;
	struct device *dev = rt->u.dst.dev;

	/*
	 *	If the indicated interface is up and running, send the packet.
	 */
	 
	ip_statistics.IpOutRequests++;

#ifdef CONFIG_IP_ACCT
	ip_fw_chk(skb->nh.iph, skb->dev,NULL,ip_acct_chain,0,IP_FW_MODE_ACCT_OUT);
#endif

	if (rt->rt_flags&RTCF_NAT)
		ip_do_nat(skb);

	if (dev->flags & IFF_UP) {
		dev_queue_xmit(skb);
		return 0;
	}
	ip_statistics.IpOutDiscards++;

	kfree_skb(skb, FREE_WRITE);
	return -ENETDOWN;
}

#ifdef CONFIG_IP_ACCT
int ip_acct_output(struct sk_buff *skb)
{
	/*
	 *	Count mapping we shortcut
	 */
			 
	ip_fw_chk(skb->nh.iph, skb->dev, NULL, ip_acct_chain, 0, IP_FW_MODE_ACCT_OUT);

	dev_queue_xmit(skb);

	return 0;
}
#endif			

/*
 *	Generate a checksum for an outgoing IP datagram.
 */

void ip_send_check(struct iphdr *iph)
{
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
}



/*
 * Queues a packet to be sent, and starts the transmitter
 * if necessary.  if free = 1 then we free the block after
 * transmit, otherwise we don't. If free==2 we not only
 * free the block but also don't assign a new ip seq number.
 * This routine also needs to put in the total length,
 * and compute the checksum
 */

void ip_queue_xmit(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	struct rtable *rt = (struct rtable*)skb->dst;
	struct device *dev = rt->u.dst.dev;
	unsigned int tot_len;
	struct iphdr *iph = skb->nh.iph;

	/*
	 *	Discard the surplus MAC header
	 */
		 
	skb_pull(skb, skb->nh.raw - skb->data);
	tot_len = skb->len;

	iph->tot_len = htons(tot_len);
	iph->id = htons(ip_id_count++);

	if (call_out_firewall(PF_INET, dev, iph, NULL,&skb) < FW_ACCEPT) {
		kfree_skb(skb, FREE_WRITE);
		return;
	}
	
#ifdef CONFIG_NET_SECURITY	
	/*
	 *	Add an IP checksum (must do this before SECurity because
	 *	of possible tunneling)
	 */

	ip_send_check(iph);

	if (call_out_firewall(PF_SECURITY, NULL, NULL, (void *) 4, &skb)<FW_ACCEPT)
	{
		kfree_skb(skb, FREE_WRITE);
		return;
	}
	
	iph = skb->nh.iph;
	/* don't update tot_len, as the dev->mtu is already decreased */	
#endif

	if (skb_headroom(skb) < dev->hard_header_len && dev->hard_header) {
		struct sk_buff *skb2;
		/* ANK: It is almost impossible, but
		 * if you loaded module device with hh_len > MAX_HEADER,
		 * and if a route changed to this device,
		 * and if (uh...) TCP had segments queued on this route...
		 */
		skb2 = skb_realloc_headroom(skb, (dev->hard_header_len+15)&~15);
		kfree_skb(skb, FREE_WRITE);
		if (skb2 == NULL)
			return;
		skb = skb2;
		iph = skb->nh.iph;
	}

	ip_ll_header(skb);


	/*
	 *	Do we need to fragment. Again this is inefficient.
	 *	We need to somehow lock the original buffer and use
	 *	bits of it.
	 */

	if (tot_len > rt->u.dst.pmtu)
		goto fragment;

	/*
	 *	Add an IP checksum
	 */

	ip_send_check(iph);

	if (sk)
		skb->priority = sk->priority;
	skb->dst->output(skb);
	return;

fragment:
	if ((iph->frag_off & htons(IP_DF)))
	{
		printk(KERN_DEBUG "sending pkt_too_big to self\n");
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED,
			  htonl(dev->mtu));
			  
		kfree_skb(skb, FREE_WRITE);
		return;
	}
	
	ip_fragment(skb, 1, skb->dst->output);
}



/*
 *	Build and send a packet, with as little as one copy
 *
 *	Doesn't care much about ip options... option length can be
 *	different for fragment at 0 and other fragments.
 *
 *	Note that the fragment at the highest offset is sent first,
 *	so the getfrag routine can fill in the TCP/UDP checksum header
 *	field in the last fragment it sends... actually it also helps
 * 	the reassemblers, they can put most packets in at the head of
 *	the fragment queue, and they know the total size in advance. This
 *	last feature will measurable improve the Linux fragment handler.
 *
 *	The callback has five args, an arbitrary pointer (copy of frag),
 *	the source IP address (may depend on the routing table), the 
 *	destination address (char *), the offset to copy from, and the
 *	length to be copied.
 * 
 */

int ip_build_xmit(struct sock *sk,
		  int getfrag (const void *,
			       char *,
			       unsigned int,	
			       unsigned int),
		  const void *frag,
		  unsigned short length,
		  struct ipcm_cookie *ipc,
		  struct rtable *rt,
		  int flags)
{
	unsigned int fraglen, maxfraglen, fragheaderlen;
	int err;
	int offset, mf;
	unsigned short id;
	struct iphdr *iph;
	int hh_len = rt->u.dst.dev->hard_header_len;
	int nfrags=0;
	struct ip_options *opt = ipc->opt;
	struct device *dev = rt->u.dst.dev;
	int df = htons(IP_DF);
#ifdef CONFIG_NET_SECURITY
	int fw_res;
#endif	

	if (sk->ip_pmtudisc == IP_PMTUDISC_DONT ||
	     rt->rt_flags&RTF_NOPMTUDISC)
		df = 0;

	 
	/*
	 *	Try the simple case first. This leaves fragmented frames, and by
	 *	choice RAW frames within 20 bytes of maximum size(rare) to the long path
	 */

	if (!sk->ip_hdrincl)
		length += sizeof(struct iphdr);

	if (length <= rt->u.dst.pmtu && opt == NULL) {
		int error;
		struct sk_buff *skb=sock_alloc_send_skb(sk, length+15+hh_len,
							0, flags&MSG_DONTWAIT, &error);
		if(skb==NULL) {
			ip_statistics.IpOutDiscards++;
			return error;
		}

		skb->when=jiffies;
		skb->priority = sk->priority;
		skb->dst = dst_clone(&rt->u.dst);

		ip_ll_header_reserve(skb);

		skb->nh.iph = iph = (struct iphdr *)skb_put(skb, length);

		dev_lock_list();

		if(!sk->ip_hdrincl) {
			iph->version=4;
			iph->ihl=5;
			iph->tos=sk->ip_tos;
			iph->tot_len = htons(length);
			iph->id=htons(ip_id_count++);
			iph->frag_off = df;
			iph->ttl=sk->ip_mc_ttl;
			if (!(rt->rt_flags&RTF_MULTICAST))
				iph->ttl=sk->ip_ttl;
			iph->protocol=sk->protocol;
			iph->saddr=rt->rt_src;
			iph->daddr=rt->rt_dst;
			iph->check=0;
			iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
			err = getfrag(frag, ((char *)iph)+iph->ihl*4,0, length-iph->ihl*4);
		}
		else
			err = getfrag(frag, (void *)iph, 0, length);
		dev_unlock_list();

		if (err)
			err = -EFAULT;

		if(!err && call_out_firewall(PF_INET, skb->dev, iph, NULL, &skb) < FW_ACCEPT)
			err = -EPERM;
#ifdef CONFIG_NET_SECURITY
		if ((fw_res=call_out_firewall(PF_SECURITY, NULL, NULL, (void *) 5, &skb))<FW_ACCEPT)
		{
			kfree_skb(skb, FREE_WRITE);
			if (fw_res != FW_QUEUE)
				return -EPERM;
			else
				return 0;
		}
#endif

		if (err)
		{
			kfree_skb(skb, FREE_WRITE);
			return err;
		}

		return rt->u.dst.output(skb);
	}

	if (!sk->ip_hdrincl)
		length -= sizeof(struct iphdr);

	if (opt) {
		fragheaderlen = hh_len + sizeof(struct iphdr) + opt->optlen;
		maxfraglen = ((rt->u.dst.pmtu-sizeof(struct iphdr)-opt->optlen) & ~7) + fragheaderlen;
	} else {
		fragheaderlen = hh_len;
		if(!sk->ip_hdrincl)
			fragheaderlen += sizeof(struct iphdr);
		
		/*
		 *	Fragheaderlen is the size of 'overhead' on each buffer. Now work
		 *	out the size of the frames to send.
		 */
	 
		maxfraglen = ((rt->u.dst.pmtu-sizeof(struct iphdr)) & ~7) + fragheaderlen;
        }

	/*
	 *	Start at the end of the frame by handling the remainder.
	 */
	 
	offset = length - (length % (maxfraglen - fragheaderlen));
	
	/*
	 *	Amount of memory to allocate for final fragment.
	 */
	 
	fraglen = length - offset + fragheaderlen;
	
	if (length-offset==0) {
		fraglen = maxfraglen;
		offset -= maxfraglen-fragheaderlen;
	}
	
	
	/*
	 *	The last fragment will not have MF (more fragments) set.
	 */
	 
	mf = 0;

	/*
	 *	Can't fragment raw packets 
	 */
	 
	if (offset > 0 && df)
 		return(-EMSGSIZE);

	/*
	 *	Lock the device lists.
	 */

	dev_lock_list();
	
	/*
	 *	Get an identifier
	 */
	 
	id = htons(ip_id_count++);

	/*
	 *	Being outputting the bytes.
	 */
	 
	do {
		struct sk_buff * skb;
		int error;
		char *data;

		/*
		 *	Get the memory we require with some space left for alignment.
		 */

		skb = sock_alloc_send_skb(sk, fraglen+15, 0, flags&MSG_DONTWAIT, &error);
		if (skb == NULL) {
			ip_statistics.IpOutDiscards++;
			if(nfrags>1)
				ip_statistics.IpFragCreates++;			
			dev_unlock_list();
			return(error);
		}
		
		/*
		 *	Fill in the control structures
		 */
		 
		skb->when = jiffies;
		skb->priority = sk->priority;
		skb->dst = dst_clone(&rt->u.dst);

		ip_ll_header_reserve(skb);

		/*
		 *	Find where to start putting bytes.
		 */
		 
		data = skb_put(skb, fraglen-hh_len);
		skb->nh.iph = iph = (struct iphdr *)data;

		/*
		 *	Only write IP header onto non-raw packets 
		 */
		 
		if(!sk->ip_hdrincl) {
			iph->version = 4;
			iph->ihl = 5;
			if (opt) {
				iph->ihl += opt->optlen>>2;
				ip_options_build(skb, opt,
						 ipc->addr, dev->pa_addr, offset);
			}
			iph->tos = sk->ip_tos;
			iph->tot_len = htons(fraglen - fragheaderlen + iph->ihl*4);
			iph->id = id;
			iph->frag_off = htons(offset>>3);
			iph->frag_off |= mf|df;
			if (rt->rt_flags&RTF_MULTICAST)
				iph->ttl = sk->ip_mc_ttl;
			else
				iph->ttl = sk->ip_ttl;
			iph->protocol = sk->protocol;
			iph->check = 0;
			iph->saddr = rt->rt_src;
			iph->daddr = rt->rt_dst;
			iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
			data += iph->ihl*4;
			
			/*
			 *	Any further fragments will have MF set.
			 */
			 
			mf = htons(IP_MF);
		}
		
		/*
		 *	User data callback
		 */

		err = getfrag(frag, data, offset, fraglen-fragheaderlen);
		if (err)
			err = -EFAULT;
		
		/*
		 *	Account for the fragment.
		 */
		 
		if(!err && !offset && call_out_firewall(PF_INET, skb->dev, iph, NULL, &skb) < FW_ACCEPT)
			err = -EPERM;
#ifdef CONFIG_NET_SECURITY
		if ((fw_res=call_out_firewall(PF_SECURITY, NULL, NULL, (void *) 6, &skb))<FW_ACCEPT)
		{
			if (fw_res != FW_QUEUE)
				err= -EPERM;
		}
#endif		
		if (err)
 		{
			kfree_skb(skb, FREE_WRITE);
			dev_unlock_list();
			return err;
 		}
		offset -= (maxfraglen-fragheaderlen);
		fraglen = maxfraglen;


		nfrags++;
		
		if (rt->u.dst.output(skb)) {
			if (nfrags>1)
				ip_statistics.IpFragCreates += nfrags;
			dev_unlock_list();
			return -ENETDOWN;
		}
	} while (offset >= 0);

	if (nfrags>1)
		ip_statistics.IpFragCreates += nfrags;

	dev_unlock_list();
	return 0;
}

/*
 *	This IP datagram is too large to be sent in one piece.  Break it up into
 *	smaller pieces (each of size equal to the MAC header plus IP header plus
 *	a block of the data of the original IP data part) that will yet fit in a
 *	single device frame, and queue such a frame for sending.
 *
 *	Assumption: packet was ready for transmission, link layer header
 *	is already in.
 *
 *	Yes this is inefficient, feel free to submit a quicker one.
 */
 
void ip_fragment(struct sk_buff *skb, int local, int (*output)(struct sk_buff*))
{
	struct iphdr *iph;
	unsigned char *raw;
	unsigned char *ptr;
	struct device *dev;
	struct sk_buff *skb2;
	int left, mtu, hlen, len;
	int offset;
	int not_last_frag;
	u16 dont_fragment;
	struct rtable *rt = (struct rtable*)skb->dst;

	dev = skb->dev;

	/*
	 *	Point into the IP datagram header.
	 */

	raw = skb->data;
	iph = skb->nh.iph;

	/*
	 *	Setup starting values.
	 */

	hlen = iph->ihl * 4;
	left = ntohs(iph->tot_len) - hlen;	/* Space per frame */
	hlen += skb->nh.raw - raw;
	if (local)
		mtu = rt->u.dst.pmtu - hlen;	/* Size of data space */
	else
		mtu = dev->mtu - hlen;
	ptr = raw + hlen;			/* Where to start from */

	/*
	 *	The protocol doesn't seem to say what to do in the case that the
	 *	frame + options doesn't fit the mtu. As it used to fall down dead
	 *	in this case we were fortunate it didn't happen
	 */

	if (mtu<8) {
		ip_statistics.IpFragFails++;
		kfree_skb(skb, FREE_WRITE);
		return;
	}

	/*
	 *	Fragment the datagram.
	 */

	offset = (ntohs(iph->frag_off) & IP_OFFSET) << 3;
	not_last_frag = iph->frag_off & htons(IP_MF);

	/*
	 *	Nice moment: if DF is set and we are here,
	 *	it means that packet should be fragmented and
	 *	DF is set on fragments. If it works,
	 *	path MTU discovery can be done by ONE segment(!). --ANK
	 */
	dont_fragment = iph->frag_off & htons(IP_DF);

	/*
	 *	Keep copying data until we run out.
	 */

	while(left > 0)	{
		len = left;
		/* IF: it doesn't fit, use 'mtu' - the data space left */
		if (len > mtu)
			len = mtu;
		/* IF: we are not sending upto and including the packet end
		   then align the next start on an eight byte boundary */
		if (len < left)	{
			len/=8;
			len*=8;
		}
		/*
		 *	Allocate buffer.
		 */

		if ((skb2 = alloc_skb(len+hlen+15,GFP_ATOMIC)) == NULL) {
			NETDEBUG(printk(KERN_INFO "IP: frag: no memory for new fragment!\n"));
			ip_statistics.IpFragFails++;
			kfree_skb(skb, FREE_WRITE);
			return;
		}

		/*
		 *	Set up data on packet
		 */

		skb2->arp = skb->arp;
		skb2->dev = skb->dev;
		skb2->when = skb->when;
		skb2->pkt_type = skb->pkt_type;
		skb2->priority = skb->priority;
		skb_put(skb2, len + hlen);
		skb2->mac.raw = (char *) skb2->data;
		skb2->nh.raw = skb2->mac.raw + dev->hard_header_len;
		skb2->h.raw = skb2->mac.raw + hlen;

		/*
		 *	Charge the memory for the fragment to any owner
		 *	it might possess
		 */

		if (skb->sk)
			skb_set_owner_w(skb2, skb->sk);
		skb2->dst = dst_clone(skb->dst);

		/*
		 *	Copy the packet header into the new buffer.
		 */

		memcpy(skb2->mac.raw, raw, hlen);

		/*
		 *	Copy a block of the IP datagram.
		 */
		memcpy(skb2->h.raw, ptr, len);
		left -= len;

		/*
		 *	Fill in the new header fields.
		 */
		iph = skb2->nh.iph;
		iph->frag_off = htons((offset >> 3))|dont_fragment;

		/* ANK: dirty, but effective trick. Upgrade options only if
		 * the segment to be fragmented was THE FIRST (otherwise,
		 * options are already fixed) and make it ONCE
		 * on the initial skb, so that all the following fragments
		 * will inherit fixed options.
		 */
		if (offset == 0)
			ip_options_fragment(skb2);

		/*
		 *	Added AC : If we are fragmenting a fragment that's not the
		 *		   last fragment then keep MF on each bit
		 */
		if (left > 0 || not_last_frag)
			iph->frag_off |= htons(IP_MF);
		ptr += len;
		offset += len;

		/*
		 *	Put this fragment into the sending queue.
		 */

		ip_statistics.IpFragCreates++;

		iph->tot_len = htons(len + hlen - dev->hard_header_len);

		ip_send_check(iph);

		output(skb2);
	}
	kfree_skb(skb, FREE_WRITE);
	ip_statistics.IpFragOKs++;
}

struct sk_buff * ip_reply(struct sk_buff *skb, int payload)
{
	struct {
		struct ip_options	opt;
		char			data[40];
	} replyopts;

	struct rtable *rt = (struct rtable*)skb->dst;
	struct sk_buff *reply;
	int    iphlen;
	struct iphdr *iph;

	struct ipcm_cookie ipc;
	u32 daddr;

	if (ip_options_echo(&replyopts.opt, skb))
		return NULL;

	daddr = ipc.addr = rt->rt_src;
	ipc.opt = &replyopts.opt;
	if (ipc.opt->srr)
		daddr = replyopts.opt.faddr;

	if (ip_route_output(&rt, daddr, rt->rt_spec_dst, RT_TOS(skb->nh.iph->tos), NULL))
		return NULL;

	iphlen = sizeof(struct iphdr) + replyopts.opt.optlen;
	reply = alloc_skb(rt->u.dst.dev->hard_header_len+15+iphlen+payload, GFP_ATOMIC);
	if (reply == NULL) {
		ip_rt_put(rt);
		return NULL;
	}

	reply->priority = skb->priority;
	reply->dst = &rt->u.dst;

	ip_ll_header_reserve(reply);

	/*
	 *	Now build the IP header.
	 */

	/*
	 *	Build the IP addresses
	 */
	 
	reply->nh.iph = iph = (struct iphdr *)skb_put(reply, iphlen);

	iph->version  = 4;
	iph->ihl      = iphlen>>2;
	iph->tos      = skb->nh.iph->tos;
	iph->frag_off = 0;
	iph->ttl      = MAXTTL;
	iph->daddr    = rt->rt_dst;
	iph->saddr    = rt->rt_src;
	iph->protocol = skb->nh.iph->protocol;
	
	ip_options_build(reply, &replyopts.opt, daddr, rt->u.dst.dev->pa_addr, 0);

	return reply;
}

/*
 *	IP protocol layer initialiser
 */

static struct packet_type ip_packet_type =
{
	__constant_htons(ETH_P_IP),
	NULL,	/* All devices */
	ip_rcv,
	NULL,
	NULL,
};


/*
 *	Device notifier
 */
 
static int ip_netdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct device *dev=ptr;

	if (dev->family != AF_INET)
		return NOTIFY_DONE;

	if(event==NETDEV_UP) 
	{
		/*
		 *	Join the initial group if multicast.
		 */		
		ip_mc_allhost(dev);
	}
	if(event==NETDEV_DOWN)
		ip_mc_drop_device(dev);
		
	return ip_rt_event(event, dev);
}

struct notifier_block ip_netdev_notifier={
	ip_netdev_event,
	NULL,
	0
};

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry proc_net_igmp = {
	PROC_NET_IGMP, 4, "igmp",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	ip_mc_procinfo
};
#endif	

/*
 *	IP registers the packet type and then calls the subprotocol initialisers
 */

__initfunc(void ip_init(void))
{
	dev_add_pack(&ip_packet_type);

	ip_rt_init();

	/* So we flush routes and multicast lists when a device is downed */
	register_netdevice_notifier(&ip_netdev_notifier);

#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_net_igmp);
#endif	
}

