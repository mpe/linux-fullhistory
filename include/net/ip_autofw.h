#include <linux/types.h>
#include <linux/kernel.h>

#ifndef _IP_AUTOFW_H
#define _IP_AUTOFW_H

#define IP_AUTOFW_EXPIRE	     15*HZ

#define IP_FWD_RANGE 		1
#define IP_FWD_PORT		2
#define IP_FWD_DIRECT		3

#define IP_AUTOFW_ACTIVE	1
#define IP_AUTOFW_USETIME	2
#define IP_AUTOFW_SECURE	4

struct ip_autofw {
	struct ip_autofw * next;
	__u16 type;
	__u16 low;
	__u16 hidden;
	__u16 high;
	__u16 visible;
	__u16 protocol;
	__u32 lastcontact;
	__u32 where;
	__u16 ctlproto;
	__u16 ctlport;
	__u16 flags;
	struct timer_list timer;
};
int ip_autofw_init(void);
#endif /* _IP_AUTOFW_H */
