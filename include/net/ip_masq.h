/*
 * 	IP masquerading functionality definitions
 */

#ifndef _IP_MASQ_H
#define _IP_MASQ_H

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

/*
 *	Linux ports don't normally get allocated above 32K.
 *	I used an extra 4K port-space
 */
 
#define PORT_MASQ_BEGIN	60000
#define PORT_MASQ_END	(PORT_MASQ_BEGIN+4096)

#define MASQUERADE_EXPIRE_TCP     15*60*HZ
#define MASQUERADE_EXPIRE_TCP_FIN  2*60*HZ
#define MASQUERADE_EXPIRE_UDP      5*60*HZ

#define IP_MASQ_F_OUT_SEQ              	0x01	/* must do output seq adjust */
#define IP_MASQ_F_IN_SEQ              	0x02	/* must do input seq adjust */
#define IP_MASQ_F_NO_DPORT    		0x04	/* no dport set yet */
#define IP_MASQ_F_NO_DADDR      	0x08 	/* no daddr yet */
#define IP_MASQ_F_HASHED		0x10 	/* hashed entry */
#define IP_MASQ_F_SAW_RST		0x20 	/* tcp rst pkt seen */
#define IP_MASQ_F_SAW_FIN_IN		0x40 	/* tcp fin pkt seen incoming */
#define IP_MASQ_F_SAW_FIN_OUT		0x80 	/* tcp fin pkt seen outgoing */
#define IP_MASQ_F_SAW_FIN		(IP_MASQ_F_SAW_FIN_IN | \
					 IP_MASQ_F_SAW_FIN_OUT)
						/* tcp fin pkts seen */

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
	struct timer_list timer;	/* Expiration timer */
	__u16 		protocol;	/* Which protocol are we talking? */
	__u16		sport, dport, mport;	/* src, dst & masq ports */
	__u32 		saddr, daddr, maddr;	/* src, dst & masq addresses */
        struct ip_masq_seq out_seq, in_seq;
	struct ip_masq_app *app;	/* bound ip_masq_app object */
	void		*app_data;	/* Application private data */
	unsigned  flags;        	/* status flags */
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
 */

extern int ip_masq_free_ports[2];

/*
 *	ip_masq initializer (registers symbols and /proc/net entries)
 */
extern int ip_masq_init(void);

/*
 *	functions called from ip layer
 */
extern int ip_fw_masquerade(struct sk_buff **, struct device *);
extern int ip_fw_masq_icmp(struct sk_buff **, struct device *);
extern int ip_fw_demasquerade(struct sk_buff **, struct device *);

/*
 *	ip_masq obj creation/deletion functions.
 */
extern struct ip_masq *ip_masq_new(struct device *dev, int proto, __u32 saddr, __u16 sport, __u32 daddr, __u16 dport, unsigned flags);
extern void ip_masq_set_expire(struct ip_masq *ms, unsigned long tout);


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
                (struct ip_masq_app *, struct ip_masq *, struct sk_buff **, struct device *);
        int (*pkt_in)           /* input (demasq) hook */
                (struct ip_masq_app *, struct ip_masq *, struct sk_buff **, struct device *);
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
extern int ip_masq_app_pkt_out(struct ip_masq *, struct sk_buff **skb_p, struct device *dev);
extern int ip_masq_app_pkt_in(struct ip_masq *, struct sk_buff **skb_p, struct device *dev);

/*
 *	service routine(s).
 */
extern struct ip_masq * ip_masq_out_get_2(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port);
extern struct ip_masq * ip_masq_in_get_2(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port);

/*
 *	/proc/net entry
 */
extern int ip_masq_app_getinfo(char *buffer, char **start, off_t offset, int length, int dummy);

/*
 *	skb_replace function used by "client" modules to replace
 *	a segment of skb.
 */
extern struct sk_buff * ip_masq_skb_replace(struct sk_buff *skb, int pri, char *o_buf, int o_len, char *n_buf, int n_len);

#endif /* __KERNEL__ */

#endif /* _IP_MASQ_H */
