#define NET_MAJOR 18		/* Major 18 is reserved for networking 		*/
#define MAX_LINKS 3		/* 18,0 for route updates, 18,1 for SKIP 	*/
#define MAX_QBYTES 32768	/* Maximum bytes in the queue 			*/

extern int netlink_attach(int unit, int (*function)(struct sk_buff *skb));
extern void netlink_detach(int unit);
extern int netlink_post(int unit, struct sk_buff *skb);
extern void init_netlink(void);

#define NETLINK_ROUTE		0	/* Routing/device hook				*/
#define NETLINK_SKIP		1	/* Reserved for ENskip  			*/
#define NETLINK_USERSOCK	2	/* Reserved for user mode socket protocols 	*/

