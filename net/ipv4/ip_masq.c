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
 *
 *	
 */

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
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/udp.h>
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
        unsigned hash;
        struct ip_masq *ms;
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
        unsigned hash;
        struct ip_masq *ms;
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
			ntohl(ms->src),ntohs(ms->sport));
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
	
void ip_fw_masquerade(struct sk_buff **skb_ptr, struct device *dev)
{
	struct sk_buff  *skb=*skb_ptr;
	struct iphdr	*iph = skb->h.iph;
	__u16	*portptr;
	struct ip_masq	*ms;
	int		size;
        unsigned long 	timeout;

	/*
	 * We can only masquerade protocols with ports...
	 */

	if (iph->protocol!=IPPROTO_UDP && iph->protocol!=IPPROTO_TCP)
		return;

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
			return;
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
 
 		/*
 		 *	Timeout depends if FIN packet was seen
 		 */
 		if (ms->flags & IP_MASQ_F_SAW_FIN || th->fin)
                {
                        timeout = ip_masq_expire->tcp_fin_timeout;
 			ms->flags |= IP_MASQ_F_SAW_FIN;
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
 }
 
 /*
  *	Check if it's an masqueraded port, look it up,
  *	and send it on it's way...
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
	unsigned short  frag;
 
 	if (iph->protocol!=IPPROTO_UDP && iph->protocol!=IPPROTO_TCP)
 		return 0;
 
	/*
   	 * Toss fragments, since we handle them in ip_rcv()
	 */

	frag = ntohs(iph->frag_off);

	if ((frag & IP_MF) != 0 || (frag & IP_OFFSET) != 0)
	{
		return 0;
	}

 	portptr = (__u16 *)&(((char *)iph)[iph->ihl*4]);
 	if (ntohs(portptr[1]) < PORT_MASQ_BEGIN ||
 	    ntohs(portptr[1]) > PORT_MASQ_END)
 		return 0;
 
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
                int size;
        
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
                size = skb->len - ((unsigned char *)portptr - skb->h.raw);
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
                        size = skb->len - ((unsigned char *)portptr-skb->h.raw);
                }
                
                /*
                 * Yug! adjust UDP/TCP and IP checksums
                 */
                if (iph->protocol==IPPROTO_UDP)
                        recalc_check((struct udphdr *)portptr,iph->saddr,iph->daddr,size);
                else
                {
                        skb->csum = csum_partial((void *)(((struct tcphdr *)portptr) + 1),
                                                 size - sizeof(struct tcphdr), 0);
                        tcp_send_check((struct tcphdr *)portptr,iph->saddr,iph->daddr,size,skb);
                }
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
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPMSQHST, 13, "ip_masquerade",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_msqhst_procinfo
	});
        ip_masq_app_init();
        
        return 0;
}
