/*
 * 	IP masquerading functionality definitions
 */

#include <linux/config.h> /* for CONFIG_IP_MASQ_NDEBUG */
#ifndef _IP_MASQ_H
#define _IP_MASQ_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#endif /* __KERNEL__ */

/*
 * This define affects the number of ports that can be handled
 * by each of the protocol helper modules.
 */
#define MAX_MASQ_APP_PORTS 12

/*
 *	Linux ports don't normally get allocated above 32K.
 *	I used an extra 4K port-space
 */

#define PORT_MASQ_BEGIN	61000
#define PORT_MASQ_END	(PORT_MASQ_BEGIN+4096)

#define MASQUERADE_EXPIRE_TCP     15*60*HZ
#define MASQUERADE_EXPIRE_TCP_FIN  2*60*HZ
#define MASQUERADE_EXPIRE_UDP      5*60*HZ
/* 
 * ICMP can no longer be modified on the fly using an ioctl - this
 * define is the only way to change the timeouts 
 */
#define MASQUERADE_EXPIRE_ICMP      125*HZ

#define IP_MASQ_F_OUT_SEQ              	0x01	/* must do output seq adjust */
#define IP_MASQ_F_IN_SEQ              	0x02	/* must do input seq adjust */
#define IP_MASQ_F_NO_DPORT     	        0x04	/* no dport set yet */
#define IP_MASQ_F_NO_DADDR	        0x08 	/* no daddr yet */
#define IP_MASQ_F_HASHED	        0x10 	/* hashed entry */

#define IP_MASQ_F_NO_SPORT	       0x200	/* no sport set yet */
#define IP_MASQ_F_NO_REPLY	       0x800	/* no reply yet from outside */
#define IP_MASQ_F_MPORT		      0x1000 	/* own mport specified */

#ifdef __KERNEL__

/*
 *	Delta seq. info structure
 *	Each MASQ struct has 2 (output AND input seq. changes).
 */

struct ip_masq_seq {
        __u32		init_seq;	/* Add delta from this seq */
        short		delta;		/* Delta in sequence numbers */
        short		previous_delta;	/* Delta in sequence numbers before last resized pkt */
};

/*
 *	MASQ structure allocated for each masqueraded association
 */
struct ip_masq {
        struct ip_masq  *m_link, *s_link; /* hashed link ptrs */
	atomic_t refcnt;		/* reference count */
	struct timer_list timer;	/* Expiration timer */
	__u16 		protocol;	/* Which protocol are we talking? */
	__u16		sport, dport, mport;	/* src, dst & masq ports */
	__u32 		saddr, daddr, maddr;	/* src, dst & masq addresses */
        struct ip_masq_seq out_seq, in_seq;
	struct ip_masq_app *app;	/* bound ip_masq_app object */
	void		*app_data;	/* Application private data */
	struct ip_masq	*control;	/* Master control connection */
	atomic_t        n_control;	/* Number of "controlled" masqs */
	unsigned  	flags;        	/* status flags */
	unsigned	timeout;	/* timeout */
	unsigned	state;		/* state info */
	struct ip_masq_timeout_table *timeout_table;
};

/*
 *	timeout values
 */

struct ip_fw_masq {
        int tcp_timeout;
        int tcp_fin_timeout;
        int udp_timeout;
};

extern struct ip_fw_masq *ip_masq_expire;

/*
 *	[0]: UDP free_ports
 *	[1]: TCP free_ports
 *	[2]: ICMP free_ports
 */

extern atomic_t ip_masq_free_ports[3];

/*
 *	ip_masq initializer (registers symbols and /proc/net entries)
 */
extern int ip_masq_init(void);

/*
 *	functions called from ip layer
 */
extern int ip_fw_masquerade(struct sk_buff **, __u32 maddr);
extern int ip_fw_masq_icmp(struct sk_buff **, __u32 maddr);
extern int ip_fw_demasquerade(struct sk_buff **);

/*
 *	ip_masq obj creation/deletion functions.
 */
extern struct ip_masq *ip_masq_new(int proto, __u32 maddr, __u16 mport, __u32 saddr, __u16 sport, __u32 daddr, __u16 dport, unsigned flags);

extern void ip_masq_control_add(struct ip_masq *ms, struct ip_masq* ctl_ms);
extern void ip_masq_control_del(struct ip_masq *ms);
extern struct ip_masq * ip_masq_control_get(struct ip_masq *ms);


/*
 * 	
 *	IP_MASQ_APP: IP application masquerading definitions 
 *
 */

struct ip_masq_app
{
        struct ip_masq_app *next;
	char *name;		/* name of application proxy */
        unsigned type;          /* type = proto<<16 | port (host byte order)*/
        int n_attach;
        int (*masq_init_1)      /* ip_masq initializer */
                (struct ip_masq_app *, struct ip_masq *);
        int (*masq_done_1)      /* ip_masq fin. */
                (struct ip_masq_app *, struct ip_masq *);
        int (*pkt_out)          /* output (masquerading) hook */
                (struct ip_masq_app *, struct ip_masq *, struct sk_buff **, __u32);
        int (*pkt_in)           /* input (demasq) hook */
                (struct ip_masq_app *, struct ip_masq *, struct sk_buff **, __u32);
};

/*
 *	ip_masq_app initializer
 */
extern int ip_masq_app_init(void);

/*
 * 	ip_masq_app object registration functions (port: host byte order)
 */
extern int register_ip_masq_app(struct ip_masq_app *mapp, unsigned short proto, __u16 port);
extern int unregister_ip_masq_app(struct ip_masq_app *mapp);

/*
 *	get ip_masq_app obj by proto,port(net_byte_order)
 */
extern struct ip_masq_app * ip_masq_app_get(unsigned short proto, __u16 port);

/*
 *	ip_masq TO ip_masq_app (un)binding functions.
 */
extern struct ip_masq_app * ip_masq_bind_app(struct ip_masq *ms);
extern int ip_masq_unbind_app(struct ip_masq *ms);

/*
 *	output and input app. masquerading hooks.
 *	
 */
extern int ip_masq_app_pkt_out(struct ip_masq *, struct sk_buff **skb_p, __u32 maddr);
extern int ip_masq_app_pkt_in(struct ip_masq *, struct sk_buff **skb_p, __u32 maddr);

/*
 *	service routine(s).
 */

extern struct ip_masq * ip_masq_out_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port);
extern struct ip_masq * ip_masq_in_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port);

extern int ip_masq_listen(struct ip_masq *);

static __inline__ struct ip_masq * ip_masq_in_get_iph(const struct iphdr *iph)
{
 	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);
        return ip_masq_in_get(iph->protocol, 
				iph->saddr, portp[0], 
				iph->daddr, portp[1]);
}

static __inline__ struct ip_masq * ip_masq_out_get_iph(const struct iphdr *iph)
{
 	const __u16 *portp  = (__u16 *)&(((char *)iph)[iph->ihl*4]);
        return ip_masq_out_get(iph->protocol, 
				iph->saddr, portp[0], 
				iph->daddr, portp[1]);
}

extern void ip_masq_put(struct ip_masq *ms);


/* 
 *	Locking stuff
 */


static __inline__ void ip_masq_lock(atomic_t *lock, int rw)
{
#if 0
	if (rw) 
#endif
		start_bh_atomic();
	atomic_inc(lock);
}

static __inline__ void ip_masq_unlock(atomic_t *lock, int rw)
{
	atomic_dec(lock);
#if 0
	if (rw) 
#endif
		end_bh_atomic();
}

/*
 *	Sleep-able lockzzz...
 */
static __inline__ void ip_masq_lockz(atomic_t *lock, struct wait_queue ** waitq, int rw)
{
	if (rw)
		while(atomic_read(lock)) sleep_on(waitq);
	ip_masq_lock(lock, rw);
}

static __inline__ void ip_masq_unlockz(atomic_t *lock, struct wait_queue ** waitq, int rw)
{
	ip_masq_unlock(lock, rw);
	if (rw)
		wake_up(waitq);
}
	
/*
 *	Perfect for winning races ... ;)
 */
static __inline__ int ip_masq_nlocks(atomic_t *lock)
{
	return atomic_read(lock);
}

extern atomic_t __ip_masq_lock;

/*
 *	Debugging stuff
 */

extern int ip_masq_get_debug_level(void);

#ifndef CONFIG_IP_MASQ_NDEBUG
#define IP_MASQ_DEBUG(level, msg...) \
	if (level <= ip_masq_get_debug_level()) \
		printk(KERN_DEBUG "IP_MASQ:" ## msg)
#else	/* NO DEBUGGING at ALL */
#define IP_MASQ_DEBUG(level, msg...) do { } while (0)
#endif

#define IP_MASQ_INFO(msg...) \
	printk(KERN_INFO "IP_MASQ:" ## msg)

#define IP_MASQ_ERR(msg...) \
	printk(KERN_ERR "IP_MASQ:" ## msg)

#define IP_MASQ_WARNING(msg...) \
	printk(KERN_WARNING "IP_MASQ:" ## msg)


/*
 *	/proc/net entry
 */
extern int ip_masq_app_getinfo(char *buffer, char **start, off_t offset, int length, int dummy);

/*
 *	skb_replace function used by "client" modules to replace
 *	a segment of skb.
 */
extern struct sk_buff * ip_masq_skb_replace(struct sk_buff *skb, int pri, char *o_buf, int o_len, char *n_buf, int n_len);

/*
 * masq_proto_num returns 0 for UDP, 1 for TCP, 2 for ICMP
 */

static __inline__ int masq_proto_num(unsigned proto)
{
   switch (proto)
   {
      case IPPROTO_UDP:  return (0); break;
      case IPPROTO_TCP:  return (1); break;
      case IPPROTO_ICMP: return (2); break;
      default:           return (-1); break;
   }
}

static __inline__ const char *masq_proto_name(unsigned proto)
{
	static char buf[20];
	static const char *strProt[] = {"UDP","TCP","ICMP"};
	int msproto = masq_proto_num(proto);

	if (msproto<0||msproto>2)  {
		sprintf(buf, "IP_%d", proto);
		return buf;
	}
        return strProt[msproto];
}

enum {
	IP_MASQ_S_NONE = 0,
	IP_MASQ_S_ESTABLISHED,
	IP_MASQ_S_SYN_SENT,
	IP_MASQ_S_SYN_RECV,
	IP_MASQ_S_FIN_WAIT,
	IP_MASQ_S_TIME_WAIT,
	IP_MASQ_S_CLOSE,
	IP_MASQ_S_CLOSE_WAIT,
	IP_MASQ_S_LAST_ACK,
	IP_MASQ_S_LISTEN,
	IP_MASQ_S_UDP,
	IP_MASQ_S_ICMP,
	IP_MASQ_S_LAST
};

struct ip_masq_timeout_table {
	atomic_t refcnt;
	int scale;
	int timeout[IP_MASQ_S_LAST+1];
};

static __inline__ void ip_masq_timeout_attach(struct ip_masq *ms, struct ip_masq_timeout_table *mstim)
{
	atomic_inc (&mstim->refcnt);
	ms->timeout_table=mstim;
}

static __inline__ void ip_masq_timeout_detach(struct ip_masq *ms)
{
	struct ip_masq_timeout_table *mstim = ms->timeout_table;

	if (!mstim)
		return;
	atomic_dec(&mstim->refcnt);
}

#endif /* __KERNEL__ */

#endif /* _IP_MASQ_H */
