#ifndef __NET_NETLINK_H
#define __NET_NETLINK_H

#define NET_MAJOR 36		/* Major 18 is reserved for networking 						*/
/* so....                                ^^ is this 36? */
/* and the things below... */
#define MAX_LINKS 16		/* 18,0 for route updates, 18,1 for SKIP, 18,2 debug tap 18,3 PPP reserved 	*/
				/* 4-7 are psi0-psi3  8 is arpd 9 is ppp */
				/* 10 is for IPSEC <John Ioannidis> */
				/* 11 IPv6 route updates		*/
				/* 12 is for firewall trapout	*/

#define MAX_QBYTES 32768	/* Maximum bytes in the queue 							*/

#include <linux/netlink.h>

extern int netlink_attach(int unit, int (*function)(int,struct sk_buff *skb));
extern int netlink_donothing(int, struct sk_buff *skb);
extern void netlink_detach(int unit);
extern int netlink_post(int unit, struct sk_buff *skb);
extern int init_netlink(void);

/*
 *	skb should fit one page. This choice is good for headerless malloc.
 */
#define NLMSG_GOODSIZE (PAGE_SIZE - ((sizeof(struct sk_buff)+0xF)&~0xF))

#define NLMSG_RECOVERY_TIMEO	(HZ/2)		/* If deleivery was failed,
						   retry after */

struct nlmsg_ctl
{
	struct timer_list nlmsg_timer;
	struct sk_buff	*nlmsg_skb;		/* Partially built skb	*/
	int		nlmsg_unit;
	int		nlmsg_delay;		/* Time to delay skb send*/
	int		nlmsg_maxsize;		/* Maximal message size  */
	int		nlmsg_force;		/* post immediately	 */
	unsigned long	nlmsg_overrun_start;	/* seqno starting lossage*/
	unsigned long	nlmsg_overrun_end;	/* the last lost message */
	char		nlmsg_overrun;		/* overrun flag		 */
};

void* nlmsg_send(struct nlmsg_ctl*, unsigned long type, int len,
		 unsigned long seq, unsigned long pid);
void nlmsg_transmit(struct nlmsg_ctl*);

extern __inline__ void nlmsg_ack(struct nlmsg_ctl* ctl, unsigned long seq,
				 unsigned long pid, int err)
{
	int *r;

	start_bh_atomic();
	r = nlmsg_send(ctl, NLMSG_ACK, sizeof(r), seq, pid);
	if (r)
		*r = err;
	end_bh_atomic();
}


#define NETLINK_ROUTE		0	/* Routing/device hook				*/
#define NETLINK_SKIP		1	/* Reserved for ENskip  			*/
#define NETLINK_USERSOCK	2	/* Reserved for user mode socket protocols 	*/
#define NETLINK_FIREWALL	3	/* Firewalling hook				*/
#define NETLINK_PSI		4	/* PSI devices - 4 to 7 */
#define NETLINK_ARPD		8
#define NETLINK_IPSEC		10	/* IPSEC */
#define NETLINK_ROUTE6		11	/* af_inet6 route comm channel */
#define NETLINK_IP6_FW		13
/* Wouldn't this suffice instead of the confusion at the top of
   this file?  i.e. 3 is firewall or ppp... */
/* #define MAX_LINKS		16 */

#endif
