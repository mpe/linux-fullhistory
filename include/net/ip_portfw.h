#ifndef _IP_PORTFW_H
#define  _IP_PORTFW_H

#include <linux/types.h>

#define IP_PORTFW_PORT_MIN 1
#define IP_PORTFW_PORT_MAX 60999

#ifdef __KERNEL__
struct ip_portfw {
	struct 		list_head list;
	__u32           laddr, raddr;
	__u16           lport, rport;
	atomic_t	pref_cnt;	/* pref "counter" down to 0 */
	int 		pref;		/* user set pref */
};
extern int ip_portfw_init(void);

#endif /* __KERNEL__ */

struct ip_portfw_edits {
	__u16           protocol;       /* Which protocol are we talking? */
	__u32           laddr, raddr;   /* Remote address */
	__u16           lport, rport;   /* Local and remote port */
	__u16           dummy;          /* Make up to multiple of 4 */
	int 		pref;		/* Preference value */
};

#endif
