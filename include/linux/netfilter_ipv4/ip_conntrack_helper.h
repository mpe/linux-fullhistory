/* IP connection tracking helpers. */
#ifndef _IP_CONNTRACK_HELPER_H
#define _IP_CONNTRACK_HELPER_H
#include <linux/netfilter_ipv4/ip_conntrack.h>

struct module;

struct ip_conntrack_helper
{	
	/* Internal use. */
	struct list_head list;

	/* Returns TRUE if it wants to help this connection (tuple is
           the tuple of REPLY packets from server). */
	int (*will_help)(const struct ip_conntrack_tuple *rtuple);

	/* Function to call when data passes; return verdict, or -1 to
           invalidate. */
	int (*help)(const struct iphdr *, size_t len,
		    struct ip_conntrack *ct,
		    enum ip_conntrack_info conntrackinfo);
};

extern int ip_conntrack_helper_register(struct ip_conntrack_helper *);
extern void ip_conntrack_helper_unregister(struct ip_conntrack_helper *);

/* Add an expected connection. */
extern int ip_conntrack_expect_related(struct ip_conntrack *related_to,
				       const struct ip_conntrack_tuple *tuple);
#endif /*_IP_CONNTRACK_HELPER_H*/
