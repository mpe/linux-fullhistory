/*
 *
 * 	Masquerading functionality
 *
 * 	Copyright (c) 1994 Pauline Middelink
 *
 *	See ip_fw.c for original log
 *
 * Fixes:
 *	Juan Jose Ciarlante	:	Modularized application masquerading (see ip_masq_app.c)
 *	Juan Jose Ciarlante	:	New struct ip_masq_seq that holds output/input delta seq.
 *	Juan Jose Ciarlante	:	Added hashed lookup by proto,maddr,mport and proto,saddr,sport
 *	Juan Jose Ciarlante	:	Fixed deadlock if free ports get exhausted
 *	Juan Jose Ciarlante	:	Added NO_ADDR status flag.
 *	Nigel Metheringham	:	Added ICMP handling for demasquerade
 *	Nigel Metheringham	:	Checksum checking of masqueraded data
 *	Nigel Metheringham	:	Better handling of timeouts of TCP conns
 *
 *	
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <asm/system.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/inet.h>
#include <net/protocol.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/checksum.h>
#include <net/ip_masq.h>

#define IP_MASQ_TAB_SIZE 256    /* must be power of 2 */

/*
 *	Implement IP packet masquerading
 */

static const char *strProt[] = {"UDP","TCP"};

static __inline__ const char * masq_proto_name(unsigned proto)
{
        return strProt[proto==IPPROTO_TCP];
}

/*
 *	Last masq_port number in use.
 *	Will cycle in MASQ_PORT boundaries.
 */
static __u16 masq_port = PORT_MASQ_BEGIN;

/*
 *	free ports counters (UDP & TCP)
 *
 *	Their value is _less_ or _equal_ to actual free ports:
 *	same masq port, diff masq addr (firewall iface address) allocated
 *	entries are accounted but their actually don't eat a more than 1 port.
 *
 *	Greater values could lower MASQ_EXPIRATION setting as a way to
 *	manage 'masq_entries resource'.
 *	
 */

int ip_masq_free_ports[2] = {
        PORT_MASQ_END - PORT_MASQ_BEGIN, 	/* UDP */
        PORT_MASQ_END - PORT_MASQ_BEGIN 	/* TCP */
};

static struct symbol_table ip_masq_syms = {
#include <linux/symtab_begin.h>
	X(ip_masq_new),
        X(ip_masq_set_expire),
        X(ip_masq_free_ports),
	X(ip_masq_expire),
	X(ip_masq_out_get_2),
#include <linux/symtab_end.h>
};

/*
 *	2 ip_masq hash tables: for input and output pkts lookups.
 */

struct ip_masq *ip_masq_m_tab[IP_MASQ_TAB_SIZE];
struct ip_masq *ip_masq_s_tab[IP_MASQ_TAB_SIZE];

/*
 * timeouts
 */

static struct ip_fw_masq ip_masq_dummy = {
	MASQUERADE_EXPIRE_TCP,
	MASQUERADE_EXPIRE_TCP_FIN,
	MASQUERADE_EXPIRE_UDP
};

struct ip_fw_masq *ip_masq_expire = &ip_masq_dummy;

/*
 *	Returns hash value
 */

static __inline__ unsigned

ip_masq_hash_key(unsigned proto, __u32 addr, __u16 port)
{
        return (proto^ntohl(addr)^ntohs(port)) & (IP_MASQ_TAB_SIZE-1);
}

/*
 *	Hashes ip_masq by its proto,addrs,ports.
 *	should be called with masked interrupts.
 *	returns bool success.
 */

static __inline__ int
ip_masq_hash(struct ip_masq *ms)
{
        unsigned hash;

        if (ms->flags & IP_MASQ_F_HASHED) {
                printk("ip_masq_hash(): request for already hashed\n");
                return 0;
        }
        /*
         *	Hash by proto,m{addr,port}
         */
        hash = ip_masq_hash_key(ms->protocol, ms->maddr, ms->mport);
        ms->m_link = ip_masq_m_tab[hash];
        ip_masq_m_tab[hash] = ms;

        /*
         *	Hash by proto,s{addr,port}
         */
        hash = ip_masq_hash_key(ms->protocol, ms->saddr, ms->sport);
        ms->s_link = ip_masq_s_tab[hash];
        ip_masq_s_tab[hash] = ms;


        ms->flags |= IP_MASQ_F_HASHED;
        return 1;
}

/*
 *	UNhashes ip_masq from ip_masq_[ms]_tables.
 *	should be called with masked interrupts.
 *	returns bool success.
 */

static __inline__ int ip_masq_unhash(struct ip_masq *ms)
{
        unsigned hash;
        struct ip_masq ** ms_p;
        if (!(ms->flags & IP_MASQ_F_HASHED)) {
                printk("ip_masq_unhash(): request for unhash flagged\n");
                return 0;
        }
        /*
         *	UNhash by m{addr,port}
         */
        hash = ip_masq_hash_key(ms->protocol, ms->maddr, ms->mport);
        for (ms_p = &ip_masq_m_tab[hash]; *ms_p ; ms_p = &(*ms_p)->m_link)
                if (ms == (*ms_p))  {
                        *ms_p = ms->m_link;
                        break;
                }
        /*
         *	UNhash by s{addr,port}
         */
        hash = ip_masq_hash_key(ms->protocol, ms->saddr, ms->sport);
        for (ms_p = &ip_masq_s_tab[hash]; *ms_p ; ms_p = &(*ms_p)->s_link)
                if (ms == (*ms_p))  {
                        *ms_p = ms->s_link;
                        break;
                }

        ms->flags &= ~IP_MASQ_F_HASHED;
        return 1;
}

/*
 *	Returns ip_masq associated with addresses found in iph.
 *	called for pkts coming from outside-to-INside the firewall
 *
 * 	NB. Cannot check destination address, just for the incoming port.
 * 	reason: archie.doc.ac.uk has 6 interfaces, you send to
 * 	phoenix and get a reply from any other interface(==dst)!
 *
 * 	[Only for UDP] - AC
 */

struct ip_masq *
ip_masq_in_get(struct iphdr *iph)
{
 	__u16 *portptr;
        int protocol;
        __u32 s_addr, d_addr;
        __u16 s_port, d_port;

 	portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
        protocol = iph->protocol;
        s_addr = iph->saddr;
        s_port = portptr[0];
        d_addr = iph->daddr;
        d_port = portptr[1];

        return ip_masq_in_get_2(protocol, s_addr, s_port, d_addr, d_port);
}

/*
 *	Returns ip_masq associated with supplied parameters, either
 *	broken out of the ip/tcp headers or directly supplied for those
 *	pathological protocols with address/port in the data stream
 *	(ftp, irc).  addresses and ports are in network order.
 *	called for pkts coming from INside-to-outside the firewall.
 *
 * 	NB. Cannot check destination address, just for the incoming port.
 * 	reason: archie.doc.ac.uk has 6 interfaces, you send to
 * 	phoenix and get a reply from any other interface(==dst)!
 *
 * 	[Only for UDP] - AC
 */

struct ip_masq *
ip_masq_in_get_2(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
        unsigned hash;
        struct ip_masq *ms;

        hash = ip_masq_hash_key(protocol, d_addr, d_port);
        for(ms = ip_masq_m_tab[hash]; ms ; ms = ms->m_link) {
 		if ( protocol==ms->protocol &&
		    (s_addr==ms->daddr || ms->flags & IP_MASQ_F_NO_DADDR) &&
                    (s_port==ms->dport || ms->flags & IP_MASQ_F_NO_DPORT) &&
                    (d_addr==ms->maddr && d_port==ms->mport))
                        return ms;
        }
        return NULL;
}

/*
 *	Returns ip_masq associated with addresses found in iph.
 *	called for pkts coming from inside-to-OUTside the firewall.
 */

struct ip_masq *
ip_masq_out_get(struct iphdr *iph)
{
 	__u16 *portptr;
        int protocol;
        __u32 s_addr, d_addr;
        __u16 s_port, d_port;

 	portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
        protocol = iph->protocol;
        s_addr = iph->saddr;
        s_port = portptr[0];
        d_addr = iph->daddr;
        d_port = portptr[1];

        return ip_masq_out_get_2(protocol, s_addr, s_port, d_addr, d_port);
}

/*
 *	Returns ip_masq associated with supplied parameters, either
 *	broken out of the ip/tcp headers or directly supplied for those
 *	pathological protocols with address/port in the data stream
 *	(ftp, irc).  addresses and ports are in network order.
 *	called for pkts coming from inside-to-OUTside the firewall.
 */

struct ip_masq *
ip_masq_out_get_2(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
        unsigned hash;
        struct ip_masq *ms;

        hash = ip_masq_hash_key(protocol, s_addr, s_port);
        for(ms = ip_masq_s_tab[hash]; ms ; ms = ms->s_link) {
		if (protocol == ms->protocol &&
		    s_addr == ms->saddr && s_port == ms->sport &&
                    d_addr == ms->daddr && d_port == ms->dport )
                        return ms;
        }

        return NULL;
}

/*
 *	Returns ip_masq for given proto,m_addr,m_port.
 *      called by allocation routine to find an unused m_port.
 */

struct ip_masq *
ip_masq_getbym(int protocol, __u32 m_addr, __u16 m_port)
{
        unsigned hash;
        struct ip_masq *ms;

        hash = ip_masq_hash_key(protocol, m_addr, m_port);
        for(ms = ip_masq_m_tab[hash]; ms ; ms = ms->m_link) {
 		if ( protocol==ms->protocol &&
                    (m_addr==ms->maddr && m_port==ms->mport))
                        return ms;
        }
        return NULL;
}

static void masq_expire(unsigned long data)
{
	struct ip_masq *ms = (struct ip_masq *)data;
	unsigned long flags;

#ifdef DEBUG_CONFIG_IP_MASQUERADE
	printk("Masqueraded %s %lX:%X expired\n",
			masq_proto_name(ms->protocol),
			ntohl(ms->saddr),ntohs(ms->sport));
#endif
	
	save_flags(flags);
	cli();

        if (ip_masq_unhash(ms)) {
                ip_masq_free_ports[ms->protocol==IPPROTO_TCP]++;
                ip_masq_unbind_app(ms);
                kfree_s(ms,sizeof(*ms));
        }

	restore_flags(flags);
}

/*
 * 	Create a new masquerade list entry, also allocate an
 * 	unused mport, keeping the portnumber between the
 * 	given boundaries MASQ_BEGIN and MASQ_END.
 */

struct ip_masq * ip_masq_new(struct device *dev, int proto, __u32 saddr, __u16 sport, __u32 daddr, __u16 dport, unsigned mflags)
{
        struct ip_masq *ms, *mst;
        int ports_tried, *free_ports_p;
	unsigned long flags;
        static int n_fails = 0;

        free_ports_p = &ip_masq_free_ports[proto==IPPROTO_TCP];

        if (*free_ports_p == 0) {
                if (++n_fails < 5)
                        printk("ip_masq_new(proto=%s): no free ports.\n",
                               masq_proto_name(proto));
                return NULL;
        }
        ms = (struct ip_masq *) kmalloc(sizeof(struct ip_masq), GFP_ATOMIC);
        if (ms == NULL) {
                if (++n_fails < 5)
                        printk("ip_masq_new(proto=%s): no memory available.\n",
                               masq_proto_name(proto));
                return NULL;
        }
        memset(ms, 0, sizeof(*ms));
	init_timer(&ms->timer);
	ms->timer.data     = (unsigned long)ms;
	ms->timer.function = masq_expire;
        ms->protocol	   = proto;
        ms->saddr    	   = saddr;
        ms->sport	   = sport;
        ms->daddr	   = daddr;
        ms->dport	   = dport;
        ms->flags	   = mflags;
        ms->app_data	   = NULL;

        if (proto == IPPROTO_UDP)
                ms->flags |= IP_MASQ_F_NO_DADDR;
        
        /* get masq address from rif */
        ms->maddr	   = dev->pa_addr;

        for (ports_tried = 0; ports_tried < *free_ports_p; ports_tried++){
                save_flags(flags);
                cli();
                
		/*
                 *	Try the next available port number
                 */
                
		ms->mport = htons(masq_port++);
		if (masq_port==PORT_MASQ_END) masq_port = PORT_MASQ_BEGIN;
                
                restore_flags(flags);
                
                /*
                 *	lookup to find out if this port is used.
                 */
                
                mst = ip_masq_getbym(proto, ms->maddr, ms->mport);
                if (mst == NULL) {
                        save_flags(flags);
                        cli();
                
                        if (*free_ports_p == 0) {
                                restore_flags(flags);
                                break;
                        }
                        (*free_ports_p)--;
                        ip_masq_hash(ms);
                        
                        restore_flags(flags);
                        
                        ip_masq_bind_app(ms);
                        n_fails = 0;
                        return ms;
                }
        }
        
        if (++n_fails < 5)
                printk("ip_masq_new(proto=%s): could not get free masq entry (free=%d).\n",
                       masq_proto_name(ms->protocol), *free_ports_p);
        kfree_s(ms, sizeof(*ms));
        return NULL;
}

/*
 * 	Set masq expiration (deletion) and adds timer,
 *	if timeout==0 cancel expiration.
 *	Warning: it does not check/delete previous timer!
 */

void ip_masq_set_expire(struct ip_masq *ms, unsigned long tout)
{
        if (tout) {
                ms->timer.expires = jiffies+tout;
                add_timer(&ms->timer);
        } else {
                del_timer(&ms->timer);
        }
}

static void recalc_check(struct udphdr *uh, __u32 saddr,
	__u32 daddr, int len)
{
	uh->check=0;
	uh->check=csum_tcpudp_magic(saddr,daddr,len,
		IPPROTO_UDP, csum_partial((char *)uh,len,0));
	if(uh->check==0)
		uh->check=0xFFFF;
}
	
int ip_fw_masquerade(struct sk_buff **skb_ptr, struct device *dev)
{
	struct sk_buff  *skb=*skb_ptr;
	struct iphdr	*iph = skb->h.iph;
	__u16	*portptr;
	struct ip_masq	*ms;
	int		size;
        unsigned long 	timeout;

	/*
	 * We can only masquerade protocols with ports...
	 * [TODO]
	 * We may need to consider masq-ing some ICMP related to masq-ed protocols
	 */

	if (iph->protocol!=IPPROTO_UDP && iph->protocol!=IPPROTO_TCP)
		return -1;

	/*
	 *	Now hunt the list to see if we have an old entry
	 */

	portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
#ifdef DEBUG_CONFIG_IP_MASQUERADE
	printk("Outgoing %s %lX:%X -> %lX:%X\n",
		masq_proto_name(iph->protocol),
		ntohl(iph->saddr), ntohs(portptr[0]),
		ntohl(iph->daddr), ntohs(portptr[1]));
#endif

        ms = ip_masq_out_get(iph);
        if (ms!=NULL)
                ip_masq_set_expire(ms,0);

	/*
	 *	Nope, not found, create a new entry for it
	 */
	
	if (ms==NULL)
	{
                ms = ip_masq_new(dev, iph->protocol,
                                 iph->saddr, portptr[0],
                                 iph->daddr, portptr[1],
                                 0);
                if (ms == NULL)
			return -1;
 	}

 	/*
 	 *	Change the fragments origin
 	 */
 	
 	size = skb->len - ((unsigned char *)portptr - skb->h.raw);
        /*
         *	Set iph addr and port from ip_masq obj.
         */
 	iph->saddr = ms->maddr;
 	portptr[0] = ms->mport;

 	/*
 	 *	Attempt ip_masq_app call.
         *	will fix ip_masq and iph seq stuff
 	 */
        if (ip_masq_app_pkt_out(ms, skb_ptr, dev) != 0)
	{
                /*
                 *	skb has possibly changed, update pointers.
                 */
                skb = *skb_ptr;
                iph = skb->h.iph;
                portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
                size = skb->len - ((unsigned char *)portptr-skb->h.raw);
        }

 	/*
 	 *	Adjust packet accordingly to protocol
 	 */
 	
 	if (iph->protocol==IPPROTO_UDP)
 	{
                timeout = ip_masq_expire->udp_timeout;
 		recalc_check((struct udphdr *)portptr,iph->saddr,iph->daddr,size);
 	}
 	else
 	{
 		struct tcphdr *th;
 		th = (struct tcphdr *)portptr;

		/* Set the flags up correctly... */
		if (th->fin)
		{
			ms->flags |= IP_MASQ_F_SAW_FIN_OUT;
		}

		if (th->rst)
		{
			ms->flags |= IP_MASQ_F_SAW_RST;
		}

 		/*
 		 *	Timeout depends if FIN packet has been seen
		 *	Very short timeout if RST packet seen.
 		 */
 		if (ms->flags & IP_MASQ_F_SAW_RST)
		{
                        timeout = 1;
		}
 		else if ((ms->flags & IP_MASQ_F_SAW_FIN) == IP_MASQ_F_SAW_FIN)
		{
                        timeout = ip_masq_expire->tcp_fin_timeout;
		}
 		else timeout = ip_masq_expire->tcp_timeout;

		skb->csum = csum_partial((void *)(th + 1), size - sizeof(*th), 0);
 		tcp_send_check(th,iph->saddr,iph->daddr,size,skb);
 	}
        ip_masq_set_expire(ms, timeout);
 	ip_send_check(iph);

 #ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("O-routed from %lX:%X over %s\n",ntohl(ms->maddr),ntohs(ms->mport),dev->name);
 #endif

	return 0;
 }


/*
 *	Handle ICMP messages in forward direction.
 *	Find any that might be relevant, check against existing connections,
 *	forward to masqueraded host if relevant.
 *	Currently handles error types - unreachable, quench, ttl exceeded
 */

int ip_fw_masq_icmp(struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff 	*skb   = *skb_p;
 	struct iphdr	*iph   = skb->h.iph;
	struct icmphdr  *icmph = (struct icmphdr *)((char *)iph + (iph->ihl<<2));
	struct iphdr    *ciph;	/* The ip header contained within the ICMP */
	__u16	        *pptr;	/* port numbers from TCP/UDP contained header */
	struct ip_masq	*ms;
	unsigned short   len   = ntohs(iph->tot_len) - (iph->ihl * 4);

#ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("Incoming forward ICMP (%d) %lX -> %lX\n",
	        icmph->type,
 		ntohl(iph->saddr), ntohl(iph->daddr));
#endif

	/* 
	 * Work through seeing if this is for us.
	 * These checks are supposed to be in an order that
	 * means easy things are checked first to speed up
	 * processing.... however this means that some
	 * packets will manage to get a long way down this
	 * stack and then be rejected, but thats life
	 */
	if ((icmph->type != ICMP_DEST_UNREACH) &&
	    (icmph->type != ICMP_SOURCE_QUENCH) &&
	    (icmph->type != ICMP_TIME_EXCEEDED))
		return 0;

	/* Now find the contained IP header */
	ciph = (struct iphdr *) (icmph + 1);

	/* We are only interested ICMPs generated from TCP or UDP packets */
	if ((ciph->protocol != IPPROTO_UDP) && (ciph->protocol != IPPROTO_TCP))
		return 0;

	/* 
	 * Find the ports involved - this packet was 
	 * incoming so the ports are right way round
	 * (but reversed relative to outer IP header!)
	 */
	pptr = (__u16 *)&(((char *)ciph)[ciph->ihl*4]);
	if (ntohs(pptr[1]) < PORT_MASQ_BEGIN ||
 	    ntohs(pptr[1]) > PORT_MASQ_END)
 		return 0;

	/* Ensure the checksum is correct */
	if (ip_compute_csum((unsigned char *) icmph, len)) 
	{
		/* Failed checksum! */
		printk(KERN_INFO "MASQ: forward ICMP: failed checksum from %s!\n", 
		       in_ntoa(iph->saddr));
		return(-1);
	}

#ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("Handling forward ICMP for %lX:%X -> %lX:%X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));
#endif

	/* This is pretty much what ip_masq_in_get() does */
	ms = ip_masq_in_get_2(ciph->protocol, ciph->saddr, pptr[0], ciph->daddr, pptr[1]);

	if (ms == NULL)
		return 0;

	/* Now we do real damage to this packet...! */
	/* First change the source IP address, and recalc checksum */
	iph->saddr = ms->maddr;
	ip_send_check(iph);
	
	/* Now change the *dest* address in the contained IP */
	ciph->daddr = ms->maddr;
	ip_send_check(ciph);
	
	/* the TCP/UDP dest port - cannot redo check */
	pptr[1] = ms->mport;

	/* And finally the ICMP checksum */
	icmph->checksum = 0;
	icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);

#ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("Rewrote forward ICMP to %lX:%X -> %lX:%X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));
#endif

	return 1;
}

/*
 *	Handle ICMP messages in reverse (demasquerade) direction.
 *	Find any that might be relevant, check against existing connections,
 *	forward to masqueraded host if relevant.
 *	Currently handles error types - unreachable, quench, ttl exceeded
 */

int ip_fw_demasq_icmp(struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff 	*skb   = *skb_p;
 	struct iphdr	*iph   = skb->h.iph;
	struct icmphdr  *icmph = (struct icmphdr *)((char *)iph + (iph->ihl<<2));
	struct iphdr    *ciph;	/* The ip header contained within the ICMP */
	__u16	        *pptr;	/* port numbers from TCP/UDP contained header */
	struct ip_masq	*ms;
	unsigned short   len   = ntohs(iph->tot_len) - (iph->ihl * 4);

#ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("Incoming reverse ICMP (%d) %lX -> %lX\n",
	        icmph->type,
 		ntohl(iph->saddr), ntohl(iph->daddr));
#endif

	if ((icmph->type != ICMP_DEST_UNREACH) &&
	    (icmph->type != ICMP_SOURCE_QUENCH) &&
	    (icmph->type != ICMP_TIME_EXCEEDED))
		return 0;

	/* Now find the contained IP header */
	ciph = (struct iphdr *) (icmph + 1);

	/* We are only interested ICMPs generated from TCP or UDP packets */
	if ((ciph->protocol != IPPROTO_UDP) && (ciph->protocol != IPPROTO_TCP))
		return 0;

	/* 
	 * Find the ports involved - remember this packet was 
	 * *outgoing* so the ports are reversed (and addresses)
	 */
	pptr = (__u16 *)&(((char *)ciph)[ciph->ihl*4]);
	if (ntohs(pptr[0]) < PORT_MASQ_BEGIN ||
 	    ntohs(pptr[0]) > PORT_MASQ_END)
 		return 0;

	/* Ensure the checksum is correct */
	if (ip_compute_csum((unsigned char *) icmph, len)) 
	{
		/* Failed checksum! */
		printk(KERN_INFO "MASQ: reverse ICMP: failed checksum from %s!\n", 
		       in_ntoa(iph->saddr));
		return(-1);
	}

#ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("Handling reverse ICMP for %lX:%X -> %lX:%X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));
#endif

	/* This is pretty much what ip_masq_in_get() does, except params are wrong way round */
	ms = ip_masq_in_get_2(ciph->protocol, ciph->daddr, pptr[1], ciph->saddr, pptr[0]);

	if (ms == NULL)
		return 0;

	/* Now we do real damage to this packet...! */
	/* First change the dest IP address, and recalc checksum */
	iph->daddr = ms->saddr;
	ip_send_check(iph);
	
	/* Now change the *source* address in the contained IP */
	ciph->saddr = ms->saddr;
	ip_send_check(ciph);
	
	/* the TCP/UDP source port - cannot redo check */
	pptr[0] = ms->sport;

	/* And finally the ICMP checksum */
	icmph->checksum = 0;
	icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);

#ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("Rewrote reverse ICMP to %lX:%X -> %lX:%X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));
#endif

	return 1;
}


 /*
  *	Check if it's an masqueraded port, look it up,
  *	and send it on its way...
  *
  *	Better not have many hosts using the designated portrange
  *	as 'normal' ports, or you'll be spending many time in
  *	this function.
  */

int ip_fw_demasquerade(struct sk_buff **skb_p, struct device *dev)
{
        struct sk_buff 	*skb = *skb_p;
 	struct iphdr	*iph = skb->h.iph;
 	__u16	*portptr;
 	struct ip_masq	*ms;
	unsigned short len;
	unsigned long 	timeout;

	switch (iph->protocol) {
	case IPPROTO_ICMP:
		return(ip_fw_demasq_icmp(skb_p, dev));
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		/* Make sure packet is in the masq range */
		portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
		if (ntohs(portptr[1]) < PORT_MASQ_BEGIN ||
		    ntohs(portptr[1]) > PORT_MASQ_END)
			return 0;
		/* Check that the checksum is OK */
		len = ntohs(iph->tot_len) - (iph->ihl * 4);
		if ((iph->protocol == IPPROTO_UDP) && (portptr[3] == 0))
			/* No UDP checksum */
			break;

		switch (skb->ip_summed) 
		{
			case CHECKSUM_NONE:
				skb->csum = csum_partial((char *)portptr, len, 0);
			case CHECKSUM_HW:
				if (csum_tcpudp_magic(iph->saddr, iph->daddr, len,
						      iph->protocol, skb->csum))
				{
					printk(KERN_INFO "MASQ: failed TCP/UDP checksum from %s!\n", 
					       in_ntoa(iph->saddr));
					return -1;
				}
			default:
				/* CHECKSUM_UNNECESSARY */
		}
		break;
	default:
		return 0;
	}


#ifdef DEBUG_CONFIG_IP_MASQUERADE
 	printk("Incoming %s %lX:%X -> %lX:%X\n",
 		masq_proto_name(iph->protocol),
 		ntohl(iph->saddr), ntohs(portptr[0]),
 		ntohl(iph->daddr), ntohs(portptr[1]));
#endif
 	/*
 	 * reroute to original host:port if found...
         */

        ms = ip_masq_in_get(iph);

        if (ms != NULL)
        {
		/* Stop the timer ticking.... */
		ip_masq_set_expire(ms,0);

                /*
                 *	Set dport if not defined yet.
                 */

                if ( ms->flags & IP_MASQ_F_NO_DPORT && ms->protocol == IPPROTO_TCP ) {
                        ms->flags &= ~IP_MASQ_F_NO_DPORT;
                        ms->dport = portptr[0];
#if DEBUG_CONFIG_IP_MASQUERADE
                        printk("ip_fw_demasquerade(): filled dport=%d\n",
                               ntohs(ms->dport));
#endif
                }
                if (ms->flags & IP_MASQ_F_NO_DADDR && ms->protocol == IPPROTO_TCP)  {
                        ms->flags &= ~IP_MASQ_F_NO_DADDR;
                        ms->daddr = iph->saddr;
#if DEBUG_CONFIG_IP_MASQUERADE
                        printk("ip_fw_demasquerade(): filled daddr=%X\n",
                               ntohs(ms->daddr));
#endif
                }
                iph->daddr = ms->saddr;
                portptr[1] = ms->sport;

                /*
                 *	Attempt ip_masq_app call.
                 *	will fix ip_masq and iph ack_seq stuff
                 */

                if (ip_masq_app_pkt_in(ms, skb_p, dev) != 0)
                {
                        /*
                         *	skb has changed, update pointers.
                         */

                        skb = *skb_p;
                        iph = skb->h.iph;
                        portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
                        len = ntohs(iph->tot_len) - (iph->ihl * 4);
                }

                /*
                 * Yug! adjust UDP/TCP and IP checksums, also update
		 * timeouts.
		 * If a TCP RST is seen collapse the tunnel (by using short timeout)!
                 */
                if (iph->protocol==IPPROTO_UDP)
		{
                        recalc_check((struct udphdr *)portptr,iph->saddr,iph->daddr,len);
			timeout = ip_masq_expire->udp_timeout;
		}
                else
                {
			struct tcphdr *th;
                        skb->csum = csum_partial((void *)(((struct tcphdr *)portptr) + 1),
                                                 len - sizeof(struct tcphdr), 0);
                        tcp_send_check((struct tcphdr *)portptr,iph->saddr,iph->daddr,len,skb);

			/* Check if TCP FIN or RST */
			th = (struct tcphdr *)portptr;
			if (th->fin)
			{
				ms->flags |= IP_MASQ_F_SAW_FIN_IN;
			}
			if (th->rst)
			{
				ms->flags |= IP_MASQ_F_SAW_RST;
			}
			
			/* Now set the timeouts */
			if (ms->flags & IP_MASQ_F_SAW_RST)
			{
				timeout = 1;
			}
			else if ((ms->flags & IP_MASQ_F_SAW_FIN) == IP_MASQ_F_SAW_FIN)
			{
				timeout = ip_masq_expire->tcp_fin_timeout;
			}
			else timeout = ip_masq_expire->tcp_timeout;
                }
		ip_masq_set_expire(ms, timeout);
                ip_send_check(iph);
#ifdef DEBUG_CONFIG_IP_MASQUERADE
                printk("I-routed to %lX:%X\n",ntohl(iph->daddr),ntohs(portptr[1]));
#endif
                return 1;
 	}

 	/* sorry, all this trouble for a no-hit :) */
 	return 0;
}

/*
 *	/proc/net entry
 */

static int ip_msqhst_procinfo(char *buffer, char **start, off_t offset,
			      int length, int unused)
{
	off_t pos=0, begin;
	struct ip_masq *ms;
	unsigned long flags;
	char temp[129];
        int idx = 0;
	int len=0;
	
	if (offset < 128) 
	{
		sprintf(temp,
			"Prc FromIP   FPrt ToIP     TPrt Masq Init-seq  Delta PDelta Expires (free=%d,%d)",
			ip_masq_free_ports[0], ip_masq_free_ports[1]); 
		len = sprintf(buffer, "%-127s\n", temp);
	}
	pos = 128;
	save_flags(flags);
	cli();
        
        for(idx = 0; idx < IP_MASQ_TAB_SIZE; idx++)
        for(ms = ip_masq_m_tab[idx]; ms ; ms = ms->m_link)
	{
		int timer_active;
		pos += 128;
		if (pos <= offset)
			continue;

		timer_active = del_timer(&ms->timer);
		if (!timer_active)
			ms->timer.expires = jiffies;
		sprintf(temp,"%s %08lX:%04X %08lX:%04X %04X %08X %6d %6d %7lu",
			masq_proto_name(ms->protocol),
			ntohl(ms->saddr), ntohs(ms->sport),
			ntohl(ms->daddr), ntohs(ms->dport),
			ntohs(ms->mport),
			ms->out_seq.init_seq,
			ms->out_seq.delta,
			ms->out_seq.previous_delta,
			ms->timer.expires-jiffies);
		if (timer_active)
			add_timer(&ms->timer);
		len += sprintf(buffer+len, "%-127s\n", temp);

		if(len >= length)
			goto done;
        }
done:
	restore_flags(flags);
	begin = len - (pos - offset);
	*start = buffer + begin;
	len -= begin;
	if(len>length)
		len = length;
	return len;
}

/*
 *	Initialize ip masquerading
 */
int ip_masq_init(void)
{
        register_symtab (&ip_masq_syms);
#ifdef CONFIG_PROC_FS        
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPMSQHST, 13, "ip_masquerade",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_msqhst_procinfo
	});
#endif	
        ip_masq_app_init();

        return 0;
}
