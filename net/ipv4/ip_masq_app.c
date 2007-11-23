/*
 *		IP_MASQ_APP application masquerading module
 *
 *
 * Version:	@(#)ip_masq_app.c  0.03      03/96
 *
 * Author:	Juan Jose Ciarlante, <jjciarla@raiz.uncu.edu.ar>
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Fixes:
 *	JJC		: Implemented also input pkt hook
 *
 *
 * FIXME:
 *	- ip_masq_skb_replace(): use same skb if space available.
 *	
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <asm/system.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <net/ip_masq.h>

static const char *strProt[] = {"UDP","TCP"};

static __inline__ const char * masq_proto_name(unsigned proto)
{
        return strProt[proto==IPPROTO_TCP];
}

#define IP_MASQ_APP_TAB_SIZE  16 /* must be power of 2 */

#define IP_MASQ_APP_HASH(proto, port) ((port^proto) & (IP_MASQ_APP_TAB_SIZE-1))
#define IP_MASQ_APP_TYPE(proto, port) ( proto<<16 | port )
#define IP_MASQ_APP_PORT(type)        ( type & 0xffff )
#define IP_MASQ_APP_PROTO(type)       ( (type>>16) & 0x00ff )


static struct symbol_table ip_masq_app_syms = {
#include <linux/symtab_begin.h>
	X(register_ip_masq_app),
	X(unregister_ip_masq_app),
	X(ip_masq_skb_replace),
#include <linux/symtab_end.h>
};

/*
 * 	will hold masq app. hashed list heads
 */

struct ip_masq_app *ip_masq_app_base[IP_MASQ_APP_TAB_SIZE];

/*
 * 	ip_masq_app registration routine
 *	port: host byte order.
 */

int register_ip_masq_app(struct ip_masq_app *mapp, unsigned short proto, __u16 port)
{
        unsigned long flags;
        unsigned hash;
        if (!mapp) {
                printk("register_ip_masq_app(): NULL arg\n");
                return -EINVAL;
        }
        mapp->type = IP_MASQ_APP_TYPE(proto, port);
        mapp->n_attach = 0;
        hash = IP_MASQ_APP_HASH(proto, port);
        
        save_flags(flags);
        cli();
        mapp->next = ip_masq_app_base[hash];
        ip_masq_app_base[hash] = mapp; 
        restore_flags(flags);
        
        return 0;
}

/*
 * 	ip_masq_app unreg. routine.
 */

int unregister_ip_masq_app(struct ip_masq_app *mapp)
{
        struct ip_masq_app **mapp_p;
        unsigned hash;
        unsigned long flags;
        if (!mapp) {
                printk("unregister_ip_masq_app(): NULL arg\n");
                return -EINVAL;
        }
        /*
         * only allow unregistration if it has no attachments
         */
        if (mapp->n_attach)  {
                printk("unregister_ip_masq_app(): has %d attachments. failed\n",
                       mapp->n_attach);
                return -EINVAL;
        }
        hash = IP_MASQ_APP_HASH(IP_MASQ_APP_PROTO(mapp->type), IP_MASQ_APP_PORT(mapp->type));

        save_flags(flags);
        cli();
        for (mapp_p = &ip_masq_app_base[hash]; *mapp_p ; mapp_p = &(*mapp_p)->next)
                if (mapp == (*mapp_p))  {
                        *mapp_p = mapp->next;
                        restore_flags(flags);
                        return 0;
                }
        
        restore_flags(flags);
        printk("unregister_ip_masq_app(proto=%s,port=%u): not hashed!\n",
               masq_proto_name(IP_MASQ_APP_PROTO(mapp->type)), IP_MASQ_APP_PORT(mapp->type));
        return -EINVAL;
}

/*
 *	get ip_masq_app object by its proto and port (net byte order).
 */

struct ip_masq_app * ip_masq_app_get(unsigned short proto, __u16 port)
{
        struct ip_masq_app *mapp;
        unsigned hash;
        unsigned type;

        port = ntohs(port);
        type = IP_MASQ_APP_TYPE(proto,port);
        hash = IP_MASQ_APP_HASH(proto,port);
        for(mapp = ip_masq_app_base[hash]; mapp ; mapp = mapp->next) {
                if (type == mapp->type) return mapp;
        }
        return NULL;
}

/*
 *	ip_masq_app object binding related funcs.
 */

/*
 * 	change ip_masq_app object's number of bindings
 */

static __inline__ int ip_masq_app_bind_chg(struct ip_masq_app *mapp, int delta)
{
        unsigned long flags;
        int n_at;
        if (!mapp) return -1;
        save_flags(flags);
        cli();
        n_at = mapp->n_attach + delta;
        if (n_at < 0) {
                restore_flags(flags);
                printk("ip_masq_app: tried to set n_attach < 0 for (proto=%s,port==%d) ip_masq_app object.\n",
                       masq_proto_name(IP_MASQ_APP_PROTO(mapp->type)),
                       IP_MASQ_APP_PORT(mapp->type));
                return -1;
        }
        mapp->n_attach = n_at;
        restore_flags(flags);
        return 0;
}

/*
 *	Bind ip_masq to its ip_masq_app based on proto and dport ALREADY
 *	set in ip_masq struct. Also calls constructor.
 */

struct ip_masq_app * ip_masq_bind_app(struct ip_masq *ms)
{
        struct ip_masq_app * mapp;
        mapp = ip_masq_app_get(ms->protocol, ms->dport);
        if (mapp != NULL) {
                /*
                 *	don't allow binding if already bound
                 */
                
                if (ms->app != NULL) {
                        printk("ip_masq_bind_app() called for already bound object.\n");
                        return ms->app;
                }
                
                ms->app = mapp;
                if (mapp->masq_init_1) mapp->masq_init_1(mapp, ms);
                ip_masq_app_bind_chg(mapp, +1);
        }
        return mapp;
}

/*
 * 	Unbind ms from type object and call ms destructor (does not kfree()).
 */

int ip_masq_unbind_app(struct ip_masq *ms)
{
        struct ip_masq_app * mapp;
        mapp = ms->app;
        if (mapp != NULL) {
                if (mapp->masq_done_1) mapp->masq_done_1(mapp, ms);
                ms->app = NULL;
                ip_masq_app_bind_chg(mapp, -1);
        }
        return (mapp != NULL);
}

/*
 *	Fixes th->seq based on ip_masq_seq info.
 */

static __inline__ void masq_fix_seq(const struct ip_masq_seq *ms_seq, struct tcphdr *th)
{
        __u32 seq;
        
        seq = ntohl(th->seq);
        
	/*
	 * 	Adjust seq with delta-offset for all packets after
         * 	the most recent resized pkt seq and with previous_delta offset
         *	for all packets	before most recent resized pkt seq.
	 */

	if (ms_seq->delta || ms_seq->previous_delta) {
		if(after(seq,ms_seq->init_seq) ) {
			th->seq = htonl(seq + ms_seq->delta);
#if DEBUG_CONFIG_IP_MASQ_APP
			printk("masq_fix_seq() : added delta (%d) to seq\n",ms_seq->delta);
#endif
		} else {
			th->seq = htonl(seq + ms_seq->previous_delta);
#if DEBUG_CONFIG_IP_MASQ_APP
	 		printk("masq_fix_seq() : added previous_delta (%d) to seq\n",ms_seq->previous_delta);
#endif
		}
	}
        

}

/*
 *	Fixes th->ack_seq based on ip_masq_seq info.
 */

static __inline__ void masq_fix_ack_seq(const struct ip_masq_seq *ms_seq, struct tcphdr *th)
{
        __u32 ack_seq;
        
        ack_seq=ntohl(th->ack_seq);
        
        /*
         * Adjust ack_seq with delta-offset for
         * the packets AFTER most recent resized pkt has caused a shift
         * for packets before most recent resized pkt, use previous_delta
         */
        
        if (ms_seq->delta || ms_seq->previous_delta) {
                if(after(ack_seq,ms_seq->init_seq)) {
                        th->ack_seq = htonl(ack_seq-ms_seq->delta);
#if DEBUG_CONFIG_IP_MASQ_APP
                        printk("masq_fix_ack_seq() : subtracted delta (%d) from ack_seq\n",ms_seq->delta);
#endif
                } else {
                        th->ack_seq = htonl(ack_seq-ms_seq->previous_delta);
#if DEBUG_CONFIG_IP_MASQ_APP
                        printk("masq_fix_ack_seq() : subtracted previous_delta (%d) from ack_seq\n",ms_seq->previous_delta);
#endif
                }
        }
        
}

/*
 *	Updates ip_masq_seq if pkt has been resized
 *	Assumes already checked proto==IPPROTO_TCP and diff!=0.
 */

static __inline__ void masq_seq_update(struct ip_masq *ms, struct ip_masq_seq *ms_seq, unsigned mflag, __u32 seq, int diff)
{
        /* if (diff == 0) return; */
        
        if ( !(ms->flags & mflag) || after(seq, ms_seq->init_seq))
        {
                ms_seq->previous_delta=ms_seq->delta;
                ms_seq->delta+=diff;
                ms_seq->init_seq=seq;
                ms->flags |= mflag;
        }
}

/*
 *	Output pkt hook. Will call bound ip_masq_app specific function
 *	called by ip_fw_masquerade(), assumes previously checked ms!=NULL
 *	returns (new - old) skb->len diff.
 */

int ip_masq_app_pkt_out(struct ip_masq *ms, struct sk_buff **skb_p, struct device *dev)
{
        struct ip_masq_app * mapp;
        struct iphdr *iph;
	struct tcphdr *th;
        int diff;
        __u32 seq;
        
        /*
         *	check if application masquerading is bound to
         *	this ip_masq.
         *	assumes that once an ip_masq is bound,
         *	it will not be unbound during its life.
         */
        
        if ( (mapp = ms->app) == NULL)
                return 0;
        
        iph = (*skb_p)->h.iph;
        th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);
        
        /*
         *	Remember seq number in case this pkt gets resized
         */
        
        seq = ntohl(th->seq);
        
        /*
         *	Fix seq stuff if flagged as so.
         */
        
        if (ms->protocol == IPPROTO_TCP) {
                if (ms->flags & IP_MASQ_F_OUT_SEQ)
                        masq_fix_seq(&ms->out_seq, th);
                if (ms->flags & IP_MASQ_F_IN_SEQ)
                        masq_fix_ack_seq(&ms->in_seq, th);
        }
        
        /*
         *	Call private output hook function 
         */
        
        if ( mapp->pkt_out == NULL )
                return 0;
        
        diff = mapp->pkt_out(mapp, ms, skb_p, dev);
        
        /*
         *	Update ip_masq seq stuff if len has changed.
         */
        
        if (diff != 0 && ms->protocol == IPPROTO_TCP)
                masq_seq_update(ms, &ms->out_seq, IP_MASQ_F_OUT_SEQ, seq, diff);

        return diff;
}

/*
 *	Input pkt hook. Will call bound ip_masq_app specific function
 *	called by ip_fw_demasquerade(), assumes previously checked ms!=NULL.
 *	returns (new - old) skb->len diff.
 */

int ip_masq_app_pkt_in(struct ip_masq *ms, struct sk_buff **skb_p, struct device *dev)
{
        struct ip_masq_app * mapp;
        struct iphdr *iph;
	struct tcphdr *th;
        int diff;
        __u32 seq;
        
        /*
         *	check if application masquerading is bound to
         *	this ip_masq.
         *	assumes that once an ip_masq is bound,
         *	it will not be unbound during its life.
         */
        
        if ( (mapp = ms->app) == NULL)
                return 0;
        
        iph = (*skb_p)->h.iph;
        th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);
        
        /*
         *	Remember seq number in case this pkt gets resized
         */
        
        seq = ntohl(th->seq);
        
        /*
         *	Fix seq stuff if flagged as so.
         */
        
        if (ms->protocol == IPPROTO_TCP) {
                if (ms->flags & IP_MASQ_F_IN_SEQ)
                        masq_fix_seq(&ms->in_seq, th);
                if (ms->flags & IP_MASQ_F_OUT_SEQ)
                        masq_fix_ack_seq(&ms->out_seq, th);
        }
        
        /*
         *	Call private input hook function 
         */
        
        if ( mapp->pkt_in == NULL )
                return 0;
        
        diff = mapp->pkt_in(mapp, ms, skb_p, dev);

        /*
         *	Update ip_masq seq stuff if len has changed.
         */
        
        if (diff != 0 && ms->protocol == IPPROTO_TCP)
                masq_seq_update(ms, &ms->in_seq, IP_MASQ_F_IN_SEQ, seq, diff);

        return diff;
}

/*
 *	/proc/ip_masq_app entry function
 */

int ip_masq_app_getinfo(char *buffer, char **start, off_t offset, int length, int dummy)
{
        off_t pos=0, begin=0;
        int len=0;
        struct ip_masq_app * mapp;
        unsigned idx;

	if (offset < 22)
		len=sprintf(buffer,"%-21s\n", "prot port    n_attach");
	pos = 22;

        for (idx=0 ; idx < IP_MASQ_APP_TAB_SIZE; idx++)
                for (mapp = ip_masq_app_base[idx]; mapp ; mapp = mapp->next) {
			/* 
			 * If you change the length of this sprintf, then all
			 * the length calculations need fixing too!
			 * Line length = 22 (3 + 2 + 7 + 1 + 7 + 1 + 1)
			 */
			pos += 22;
			if (pos < offset)
				continue;

                        len += sprintf(buffer+len, "%-3s  %-7u %-7d \n",
                                       masq_proto_name(IP_MASQ_APP_PROTO(mapp->type)),
                                       IP_MASQ_APP_PORT(mapp->type), mapp->n_attach);

                        if(len >= length)
                                goto done;
                }
done:
	begin = len - (pos - offset);
        *start = buffer + begin;
        len -= begin;
        if (len > length)
                len = length;	
        return len;
}


/*
 *	Initialization routine
 */

int ip_masq_app_init(void)
{
        
        register_symtab (&ip_masq_app_syms);
        
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IP_MASQ_APP, 11, "ip_masq_app",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_masq_app_getinfo
 	});
        
        return 0;
}

/*
 *	Replace a segment (of skb->data) with a new one.
 *	FIXME: Should re-use same skb if space available, this could
 *	       be done if n_len < o_len, unless some extra space
 *	       were already allocated at driver level :P .
 */

static struct sk_buff * skb_replace(struct sk_buff *skb, int pri, char *o_buf, int o_len, char *n_buf, int n_len)
{
        int maxsize, diff, o_offset;
        struct sk_buff *n_skb;

	maxsize = skb->truesize - sizeof(struct sk_buff);

        diff = n_len - o_len;
        o_offset = o_buf - (char*) skb->data;

	if (maxsize <= n_len) {
	    if (diff != 0) {
		memcpy(skb->data + o_offset + n_len,o_buf + o_len,
		       skb->len - (o_offset + o_len));
	    }

	    memcpy(skb->data + o_offset, n_buf, n_len);

	    n_skb    = skb;
	    skb->len = n_len;
	    skb->end = skb->head+n_len;
	} else {
                /*
                 * 	Sizes differ, make a copy
                 */
        
                n_skb = alloc_skb(MAX_HEADER + skb->len + diff, pri);
                if (n_skb == NULL) {
                        printk("skb_replace(): no room left (from %p)\n",
                               __builtin_return_address(0));
                        return skb;

                }
                n_skb->free = skb->free;
                skb_reserve(n_skb, MAX_HEADER);
                skb_put(n_skb, skb->len + diff);
                n_skb->h.raw = n_skb->data + (skb->h.raw - skb->data);
                
                /*
                 * Copy pkt in new buffer
                 */
                
                memcpy(n_skb->data, skb->data, o_offset);
                memcpy(n_skb->data + o_offset, n_buf, n_len);
                memcpy(n_skb->data + o_offset + n_len, o_buf + o_len,
                       skb->len - (o_offset + o_len) );
                
                /*
                 * Problem, how to replace the new skb with old one,
                 * preferably inplace
                 */
                
                kfree_skb(skb, FREE_WRITE);
        }
        return n_skb;
}

/*
 *	calls skb_replace() and update ip header if new skb was allocated
 */

struct sk_buff * ip_masq_skb_replace(struct sk_buff *skb, int pri, char *o_buf, int o_len, char *n_buf, int n_len)
{
        int diff;
        struct sk_buff *n_skb;
        unsigned skb_len;

        diff = n_len - o_len;
        n_skb = skb_replace(skb, pri, o_buf, o_len, n_buf, n_len);
        skb_len = skb->len;
                
        if (diff)
        {
                struct iphdr *iph;
#if DEBUG_CONFIG_IP_MASQ_APP
                printk("masq_skb_replace(): pkt resized for %d bytes (len=%ld)\n", diff, skb->len);
#endif
                /*
                 * 	update ip header
                 */
                iph = n_skb->h.iph;
                iph->check = 0;
                iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
                iph->tot_len = htons(skb_len + diff);
        }
        return n_skb;
}
